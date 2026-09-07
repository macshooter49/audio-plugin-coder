// ════════════════════════════════════════════════════════════════════════════
//  geode_shape_cert.cpp — RESYNTH SHAPE certification (Pattern A — NOT in CMakeLists).
//
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Tests/geode_shape_cert.cpp -o /tmp/gsc && /tmp/gsc
//
//  THE PROPERTY: every SHAPE target must render a spectrum that is CLOSE to its own ideal
//  (shapeWeight(target,n), GeodeEngine.h:1021) and FAR from the other targets, on every kind of
//  source — not just the default 48-harmonic saw store.
//
//  Modelled on Source/GeodeEngine_test.cpp (makeStore / renderFund). Three synthetic sources:
//    A  default store — 48 harmonics at 1/n via GeodeAnalyzer::buildFromWave, exactly what
//       PluginProcessor.cpp:1760 buildDefaultGeodeStore builds when no sample is loaded
//    B  realistic sparse INHARMONIC pluck — 10 partials at 1, 2.01, 3.04, 4.1, 5.2, 6.35, 7.6,
//       9.1, 11.3, 13.8 with a 1/n^1.3 rolloff (what a real analysed sample looks like)
//    C  dull voice-like source — 12 harmonics at 1/n²
//
//  METRIC (a hearing metric — sample-difference RMS is BANNED, phase-only changes are inaudible):
//    amplitude-weighted magnitude-spectrum distance in dB. Both spectra are measured the SAME way
//    (Blackman-Harris window, 65536-pt FFT of the steady state after a 100 ms skip), converted to dB
//    re their own fundamental and clamped at a -90 dB floor. Per bin, the |dB error| is weighted by
//    the LINEAR amplitude of the LOUDER of the two spectra at that bin, so bins at the noise floor
//    cannot dominate. dist = Σ w·|ΔdB| / Σ w.
//      dH = harmonic metric: 64 bins = peak magnitude within ±6 FFT bins of n·f0, n = 1..64
//      dF = FULL-spectrum metric: 258 quarter-harmonic bands (centres 0.5·f0 … 64.75·f0, width f0/4,
//           band energy) — sees INHARMONIC partials sitting between harmonics, which dH cannot.
//    The IDEAL for each target is synthesised (Σ W(n)·sin(2π n f0 t), n=1..64) and measured through
//    the identical analyser, so window/leakage factors cancel.
//
//  GATE (red today, green after the fix): Saw on source B at SHAPE=1 must be ≤ 6 dB (dF) from an
//  ideal saw at both quality=0.80 (default) and quality=1.0.
// ════════════════════════════════════════════════════════════════════════════
// GeodeEngine::shapeWeight (GeodeEngine.h:1021) is private. The IDEAL must come from the SHIPPED
// table, not a copy that can drift — so the std headers the engine uses are pulled in first, then
// the class is opened for this one include (test-harness idiom; Source/ is untouched).
#include <cstdint>
#include <cmath>
#include <complex>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#define private public
#include "GeodeEngine.h"
#undef private
using namespace tw;

static const char* SHN[11] = { "Sine", "Square", "Saw", "Triangle", "Pulse", "Hollow", "Organ", "Half", "Vowel", "Bright", "Metal" };

static constexpr int    kH       = 64;              // harmonics compared (shapeWeight n = 1..64)
static constexpr int    kSub     = 4;               // full-spectrum bands per harmonic
static constexpr int    kBands   = kH * kSub + 2;   // centres 0.5·f0 + b/4 → b=0..257, harmonic n at b = 4n-2
static constexpr double kFloorDb = -90.0;
static constexpr int    kFFT     = 65536;
static constexpr double kAudFloor = 3.0;            // pairs closer than this are "the same shape"
static constexpr double kDead     = 6.0;            // SHAPE=1 vs SHAPE=0 closer than this → the knob does nothing
static constexpr double kMissDb   = -40.0;          // an ideal harmonic ≥ -40 dB that renders < -40 dB is MISSING

struct Spec
{
    std::array<double, kH + 1> h {};      // dB re fundamental, index 1..64
    std::array<double, kBands> band {};   // dB re fundamental band
};

// ── sources ─────────────────────────────────────────────────────────────────────────────────
static GeodeFrameStore makeA()      // == PluginProcessor.cpp:1760 buildDefaultGeodeStore (WF=4, WP=48, 1/n)
{
    constexpr int WF = 4, WP = 48;
    std::vector<float> wr ((size_t) WF * WP, 0.f), wa ((size_t) WF * WP, 0.f);
    for (int f = 0; f < WF; ++f)
        for (int h = 1; h <= WP; ++h)
        { wr[(size_t) f * WP + (h - 1)] = (float) h; wa[(size_t) f * WP + (h - 1)] = 1.f / (float) h; }
    GeodeFrameStore s; GeodeAnalyzer::buildFromWave (wr.data(), wa.data(), WF, WP, s);
    s.naturalSec = 2.0f;
    return s;
}
static GeodeFrameStore makeFrom (const std::vector<float>& R, const std::vector<float>& A, float f0)
{
    GeodeFrameStore s; s.frames.resize (4); s.naturalSec = 2.0f; s.valid = true; s.f0 = f0;
    for (auto& f : s.frames)
    {
        for (size_t i = 0; i < R.size(); ++i) { f.ratio[i] = R[i]; f.amp[i] = A[i]; }
        f.nPartials = (int) R.size();
    }
    return s;
}
static GeodeFrameStore makeB (float f0)   // sparse inharmonic pluck
{
    const std::vector<float> R = { 1.f, 2.01f, 3.04f, 4.1f, 5.2f, 6.35f, 7.6f, 9.1f, 11.3f, 13.8f };
    std::vector<float> A; for (size_t i = 0; i < R.size(); ++i) A.push_back (std::pow ((float) (i + 1), -1.3f));
    return makeFrom (R, A, f0);
}
static GeodeFrameStore makeC (float f0)   // dull voice-like 12 × 1/n²
{
    std::vector<float> R, A; for (int n = 1; n <= 12; ++n) { R.push_back ((float) n); A.push_back (1.f / (float) (n * n)); }
    return makeFrom (R, A, f0);
}

// ── render (as GeodeEngine_test.cpp renderFund) → steady-state buffer after a 100 ms skip ────
static std::vector<float> renderSteady (GeodeParams p, GeodeFrameStore& st, double playHz, double sr)
{
    const int skip = (int) (sr * 0.1);
    const int N = skip + kFFT;
    std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
    GeodeEngine e; e.prepare (sr); e.setFrameStore (&st); e.setParams (p); e.noteOn (playHz, 999);
    const int blk = 256;
    for (int off = 0; off < N; off += blk)
    { const int m = std::min (blk, N - off); e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m); }
    return std::vector<float> (L.begin() + skip, L.end());
}

// ── the ideal: Σ W(n) sin(2π n f0 t), n=1..64 — measured through the SAME analyser ───────────
static std::vector<float> synthIdeal (int target, double playHz, double sr)
{
    std::vector<float> x ((size_t) kFFT, 0.f);
    for (int n = 1; n <= kH; ++n)
    {
        const float w = GeodeEngine::shapeWeight (target, n);
        if (w <= 0.f) continue;
        const double inc = n * playHz / sr;
        for (int i = 0; i < kFFT; ++i) x[(size_t) i] += w * (float) std::sin (2.0 * M_PI * inc * i);
    }
    return x;
}

static Spec analyse (const std::vector<float>& x, double sr, double f0)
{
    std::vector<std::complex<float>> bf ((size_t) kFFT);
    for (int i = 0; i < kFFT; ++i)
    {
        const double t = 2.0 * M_PI * i / (kFFT - 1);   // 4-term Blackman-Harris (-92 dB sidelobes)
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        bf[(size_t) i] = { (float) (x[(size_t) i] * w), 0.f };
    }
    geodedsp::fft (bf.data(), kFFT, false);
    std::vector<double> mag ((size_t) kFFT / 2, 0.0);
    for (int b = 0; b < kFFT / 2; ++b) mag[(size_t) b] = std::abs (bf[(size_t) b]);
    const double binHz = sr / kFFT;

    Spec s;
    // harmonic peaks
    std::array<double, kH + 1> lin {};
    for (int n = 1; n <= kH; ++n)
    {
        const int c = (int) std::lround (n * f0 / binHz);
        double m = 0; for (int b = std::max (1, c - 6); b <= std::min (kFFT / 2 - 1, c + 6); ++b) m = std::max (m, mag[(size_t) b]);
        lin[(size_t) n] = m;
    }
    const double ref = std::max (lin[1], 1e-12);
    for (int n = 1; n <= kH; ++n) s.h[(size_t) n] = std::max (kFloorDb, 20.0 * std::log10 (std::max (lin[(size_t) n], 1e-12) / ref));
    // quarter-harmonic band energies
    std::array<double, kBands> be {};
    for (int b = 0; b < kBands; ++b)
    {
        const double cR = 0.5 + (double) b / kSub;
        const int lo = (int) std::lround ((cR - 0.125) * f0 / binHz), hi = (int) std::lround ((cR + 0.125) * f0 / binHz);
        double e = 0; for (int k = std::max (1, lo); k < std::min (kFFT / 2, hi); ++k) e += mag[(size_t) k] * mag[(size_t) k];
        be[(size_t) b] = std::sqrt (e);
    }
    const double bref = std::max (be[(size_t) (kSub - 2)], 1e-12);   // band centred on the fundamental (b = 4·1-2)
    for (int b = 0; b < kBands; ++b) s.band[(size_t) b] = std::max (kFloorDb, 20.0 * std::log10 (std::max (be[(size_t) b], 1e-12) / bref));
    return s;
}

// ── amplitude-weighted dB distance ──────────────────────────────────────────────────────────
template <size_t N>
static double wdist (const std::array<double, N>& a, const std::array<double, N>& b, size_t from)
{
    double num = 0, den = 0;
    for (size_t i = from; i < N; ++i)
    {
        const double w = std::max (std::pow (10.0, a[i] / 20.0), std::pow (10.0, b[i] / 20.0));
        num += w * std::fabs (a[i] - b[i]); den += w;
    }
    return den > 0 ? num / den : 0.0;
}
static double dH (const Spec& a, const Spec& b) { return wdist (a.h, b.h, 1); }
static double dF (const Spec& a, const Spec& b) { return wdist (a.band, b.band, 0); }

// ideal harmonics ≥ -40 dB that the render lacks (< -40 dB re fundamental), within the first 32
static void missingCount (const Spec& ideal, const Spec& got, int& missing, int& expected)
{
    missing = 0; expected = 0;
    for (int n = 1; n <= 32; ++n)
        if (ideal.h[(size_t) n] >= kMissDb) { ++expected; if (got.h[(size_t) n] < kMissDb) ++missing; }
}

struct Row { double d1F, d1H, dHalfF, knob1, knobHalf, nearest; int missing, expected; int nearestT; };

static bool runSource (const char* name, GeodeFrameStore& st, double playHz, double sr, float quality,
                       const std::array<Spec, 11>& ideal, std::string& tableOut, double* sawDistOut)
{
    GeodeParams base; base.scan = 0.f; base.quality = quality;   // HOLD read-head → a stable frame
    GeodeParams p0 = base; p0.shape = 0.f;
    const Spec s0 = analyse (renderSteady (p0, st, playHz, sr), sr, playHz);

    std::array<Spec, 11> s1, sHalf;
    std::array<Row, 11> rows {};
    for (int t = 0; t < 11; ++t)
    {
        GeodeParams p = base; p.shapeTarget = t;
        p.shape = 1.0f; s1[(size_t) t]    = analyse (renderSteady (p, st, playHz, sr), sr, playHz);
        p.shape = 0.5f; sHalf[(size_t) t] = analyse (renderSteady (p, st, playHz, sr), sr, playHz);
        Row& r = rows[(size_t) t];
        r.d1F = dF (s1[(size_t) t], ideal[(size_t) t]);   r.d1H = dH (s1[(size_t) t], ideal[(size_t) t]);
        r.dHalfF = dF (sHalf[(size_t) t], ideal[(size_t) t]);
        r.knob1 = dF (s1[(size_t) t], s0);                r.knobHalf = dF (sHalf[(size_t) t], s0);
        missingCount (ideal[(size_t) t], s1[(size_t) t], r.missing, r.expected);
    }
    // pairwise matrix at SHAPE=1
    double M[11][11];
    for (int a = 0; a < 11; ++a) for (int b = 0; b < 11; ++b) M[a][b] = (a == b) ? 0.0 : dF (s1[(size_t) a], s1[(size_t) b]);
    for (int a = 0; a < 11; ++a)
    { rows[(size_t) a].nearest = 1e9; rows[(size_t) a].nearestT = -1;
      for (int b = 0; b < 11; ++b) if (b != a && M[a][b] < rows[(size_t) a].nearest) { rows[(size_t) a].nearest = M[a][b]; rows[(size_t) a].nearestT = b; } }

    char ln[512];
    auto emit = [&] (const char* s) { std::fputs (s, stdout); tableOut += s; };
    std::snprintf (ln, sizeof ln, "\n=== SOURCE %s | quality=%.2f | SHAPE=0 baseline: dF(src,idealSaw)=%.1f dB ===\n", name, quality, dF (s0, ideal[2])); emit (ln);
    std::snprintf (ln, sizeof ln, "%-9s %8s %8s %9s | %9s %9s | %8s %14s | %s\n", "target", "dF1→ideal", "dH1→ideal", "dF.5→ideal", "dF(1vs0)", "dF(.5vs0)", "miss/exp", "nearest-other", "flags"); emit (ln);
    for (int t = 0; t < 11; ++t)
    {
        const Row& r = rows[(size_t) t];
        std::string fl;
        if (r.d1F > kDead) fl += " FAR";
        if (r.knob1 < kDead) fl += " DEAD";
        if (r.nearest < kAudFloor) fl += " DUP";
        if (r.missing > 0) fl += " HOLES";
        std::snprintf (ln, sizeof ln, "%-9s %8.1f %8.1f %9.1f | %9.1f %9.1f | %4d/%-4d %6.1f (%-8s) |%s\n",
                       SHN[t], r.d1F, r.d1H, r.dHalfF, r.knob1, r.knobHalf, r.missing, r.expected, r.nearest, SHN[r.nearestT], fl.c_str());
        emit (ln);
    }
    emit ("pairwise dF at SHAPE=1 (dB):\n         ");
    for (int b = 0; b < 11; ++b) { std::snprintf (ln, sizeof ln, "%6.6s", SHN[b]); emit (ln); } emit ("\n");
    for (int a = 0; a < 11; ++a)
    {
        std::snprintf (ln, sizeof ln, "%-8s ", SHN[a]); emit (ln);
        for (int b = 0; b < 11; ++b) { std::snprintf (ln, sizeof ln, "%6.1f", M[a][b]); emit (ln); }
        emit ("\n");
    }
    int dup = 0;
    for (int a = 0; a < 11; ++a) for (int b = a + 1; b < 11; ++b)
        if (M[a][b] < kAudFloor) { std::snprintf (ln, sizeof ln, "  DUP  %s ~ %s : %.1f dB\n", SHN[a], SHN[b], M[a][b]); emit (ln); ++dup; }
    if (dup == 0) emit ("  (no pairs under the 3 dB audibility floor)\n");
    // per-harmonic detail for Saw (the one Max named): first 16 harmonics, render vs ideal
    emit ("Saw @SHAPE=1, harmonics 1..16 dB re fund (render | ideal): ");
    for (int n = 1; n <= 16; ++n) { std::snprintf (ln, sizeof ln, "%d:%.0f|%.0f ", n, s1[2].h[(size_t) n], ideal[2].h[(size_t) n]); emit (ln); }
    emit ("\n");
    if (sawDistOut) *sawDistOut = rows[2].d1F;
    return true;
}

int main()
{
    const double sr = 48000.0, playHz = 261.63;   // C4, as GeodeEngine_test.cpp
    std::array<Spec, 11> ideal;
    for (int t = 0; t < 11; ++t) ideal[(size_t) t] = analyse (synthIdeal (t, playHz, sr), sr, playHz);

    std::printf ("=== RESYNTH SHAPE CERT (played C4 = %.2f Hz, %d-pt FFT, BH window, floor %.0f dB) ===\n", playHz, kFFT, kFloorDb);
    std::printf ("metric: amplitude-weighted |dB| distance; dF = full 258-band spectrum, dH = 64 harmonic peaks\n");
    std::printf ("flags: FAR = >%.0f dB from own ideal at SHAPE=1; DEAD = SHAPE=1 within %.0f dB of SHAPE=0; DUP = another target within %.0f dB; HOLES = ideal harmonics (<=32, >=-40 dB) missing in the render\n",
                 kDead, kDead, kAudFloor);
    // ideal-vs-ideal separation (what the targets COULD sound like if rendered perfectly)
    std::printf ("\nideal-vs-ideal dF (dB) — the separation the recipes themselves offer:\n         ");
    for (int b = 0; b < 11; ++b) std::printf ("%6.6s", SHN[b]); std::printf ("\n");
    for (int a = 0; a < 11; ++a) { std::printf ("%-8s ", SHN[a]); for (int b = 0; b < 11; ++b) std::printf ("%6.1f", a == b ? 0.0 : dF (ideal[(size_t) a], ideal[(size_t) b])); std::printf ("\n"); }

    // ── metric self-check: the analyser must read ~0 for a perfect render and >0 for a perturbed one.
    // A 64-harmonic 1/n store rendered by the ENGINE at SHAPE=0 vs the synthesised ideal saw goes
    // through window, LUT sine, declick ramps and band alignment — everything the real measurements
    // go through — so this is the calibration line for every dF/dH below.
    {
        std::vector<float> R, Aa; for (int n = 1; n <= 64; ++n) { R.push_back ((float) n); Aa.push_back (1.f / (float) n); }
        auto full = makeFrom (R, Aa, (float) playHz);
        GeodeParams p; p.scan = 0.f; p.quality = 1.f; p.shape = 0.f;
        const Spec s = analyse (renderSteady (p, full, playHz, sr), sr, playHz);
        Aa[1] *= 0.5f;                                   // harmonic 2 down 6 dB → a clearly audible change
        auto pert = makeFrom (R, Aa, (float) playHz);
        const Spec sp = analyse (renderSteady (p, pert, playHz, sr), sr, playHz);
        std::printf ("\nmetric self-check: engine-rendered 64×1/n vs ideal saw: dF=%.2f dH=%.2f (expect ~0);  same with harmonic 2 at -6 dB: dF=%.2f (expect >0)\n",
                     dF (s, ideal[2]), dH (s, ideal[2]), dF (sp, ideal[2]));
    }

    auto A = makeA(); auto B = makeB ((float) playHz); auto C = makeC ((float) playHz);
    std::string table; double sawB80 = 0, sawB100 = 0, dummy = 0;
    runSource ("A default-48@1/n",   A, playHz, sr, 0.80f, ideal, table, &dummy);
    runSource ("A default-48@1/n",   A, playHz, sr, 1.00f, ideal, table, &dummy);
    runSource ("B sparse-inharm-10", B, playHz, sr, 0.80f, ideal, table, &sawB80);
    runSource ("B sparse-inharm-10", B, playHz, sr, 1.00f, ideal, table, &sawB100);
    runSource ("C dull-12@1/n2",     C, playHz, sr, 0.80f, ideal, table, &dummy);
    runSource ("C dull-12@1/n2",     C, playHz, sr, 1.00f, ideal, table, &dummy);
    // H2 probe: thin-before-sculpt. quality=0.5 → active = 4+30 = 34 < 48 → the default store is
    // thinned BEFORE the sculpt (GeodeEngine.h:518-529) and the sculpt never revives (1084).
    runSource ("A default-48 (H2 probe)", A, playHz, sr, 0.50f, ideal, table, &dummy);

    bool ok = (sawB80 <= kDead) && (sawB100 <= kDead);
    std::printf ("\n=== GATE: Saw on source B at SHAPE=1 → ideal saw: q=0.80 %.1f dB, q=1.00 %.1f dB (limit %.0f)  %s ===\n",
                 sawB80, sawB100, kDead, ok ? "PASS" : "FAIL");

    // ═══════════════════════════════════════════════════════════════════════════════════════════
    //  fb596 — THE SEVEN BARS. Each one has a mutation that turns it red (see the header):
    //    G1 fidelity      every KEPT target on every source at q=1: ≤ 1 dB from its ideal, 0 holes   (shipped · -DGEODE_NO_SPAWN)
    //    G2 separation    every pair of KEPT targets ≥ 6 dB apart at SHAPE=1 on every source         (shipped: Saw~Pulse 2.6 on C)
    //    G3 quota         SHAPE can never cost more than QUALITY allows: q=.5→34 · unison 16→13 · budget room 6→6   (-DGEODE_NO_POSTTHIN)
    //    G4 level         SHAPE changes timbre, not loudness: RMS(1)/RMS(0) within ±1 dB; Metal −3.1±1 (its peak trim)   (-DGEODE_NO_LEVELMATCH)
    //    G5 no click      SHAPE stepped 0→1 and 1→0 mid-note: no first-difference spike beyond 1.5× steady
    //    G6 continuity    a MOVING read-head over a churning store: no live slot changes frequency (home slots)   (-DGEODE_SPAWN_APPEND_ONLY → 43 jumps)
    //    G7 identity      SHAPE=0 is bit-identical to the SHIPPED engine: three fingerprints from the pre-fb596 header   (-DGEODE_MUT_SHAPE0)
    //  KEPT = the nine that survive the audit — Hollow (5) and Bright (9) are TILT positions, hidden in the menu.
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    {
        int pass = 0, fail = 0;
        auto bar = [&] (bool good, const char* name, const std::string& detail)
        { good ? ++pass : ++fail; std::printf ("  %s  %s\n        %s\n", good ? "PASS" : "FAIL", name, detail.c_str()); };
        const int KEPT[9] = { 0, 1, 2, 3, 4, 6, 7, 8, 10 };
        GeodeFrameStore* SRC[3] = { &A, &B, &C }; const char* SN[3] = { "A", "B", "C" };
        char ln[256];

        // ── G1 + G2 ──
        {
            double worstFid = 0; int worstHoles = 0; std::string wf; double minSep = 1e9; std::string ws;
            for (int s = 0; s < 3; ++s)
            {
                std::array<Spec, 11> r1;
                for (int k = 0; k < 9; ++k)
                {
                    const int t = KEPT[k];
                    GeodeParams p; p.scan = 0.f; p.quality = 1.f; p.shape = 1.f; p.shapeTarget = t;
                    r1[(size_t) t] = analyse (renderSteady (p, *SRC[s], playHz, sr), sr, playHz);
                    const double d = dF (r1[(size_t) t], ideal[(size_t) t]);
                    int miss = 0, exp = 0; missingCount (ideal[(size_t) t], r1[(size_t) t], miss, exp);
                    if (d > worstFid) { worstFid = d; std::snprintf (ln, sizeof ln, "%s/%s %.2f dB", SN[s], SHN[t], d); wf = ln; }
                    if (miss > worstHoles) worstHoles = miss;
                }
                for (int a = 0; a < 9; ++a) for (int b = a + 1; b < 9; ++b)
                {
                    const double d = dF (r1[(size_t) KEPT[a]], r1[(size_t) KEPT[b]]);
                    if (d < minSep) { minSep = d; std::snprintf (ln, sizeof ln, "%s/%s~%s %.1f dB", SN[s], SHN[KEPT[a]], SHN[KEPT[b]], d); ws = ln; }
                }
            }
            bar (worstFid <= 1.0 && worstHoles == 0, "[G1] EVERY KEPT SHAPE IS ITS OWN IDEAL — ≤ 1 dB, no missing harmonic, every source",
                 "worst " + wf + ", worst holes " + std::to_string (worstHoles) + " (9 targets × 3 sources at q=1.0)");
            bar (minSep >= 6.0, "[G2] NO TWO KEPT SHAPES SOUND THE SAME — ≥ 6 dB apart at SHAPE=1, every source",
                 "closest pair " + ws);
        }

        // ── G3 quota ──
        {
            auto liveAfter = [&] (GeodeFrameStore& st, float q, int target, int unison, int* used, int cap)
            {
                GeodeEngine e; e.prepare (sr); e.setFrameStore (&st);
                if (unison > 1) e.setUnisonScale (unison);
                if (used != nullptr) e.setPartialBudget (used, cap);
                GeodeParams p; p.scan = 0.f; p.quality = q; p.shape = 1.f; p.shapeTarget = target; e.setParams (p); e.noteOn (playHz, 999);
                std::vector<float> L (256, 0.f), R (256, 0.f);
                for (int i = 0; i < 8; ++i) { if (used) *used = 0; e.setParams (p); e.renderBlockAdd (L.data(), R.data(), 256); }
                return e.preparedActive();
            };
            int used = 0;
            const int a1 = liveAfter (A, 0.5f, 2, 1, nullptr, 0);          // quota 4 + 0.5·60 = 34
            const int a2 = liveAfter (A, 0.8f, 1, 16, nullptr, 0);         // 52 / ⌈√16⌉ = 13
            const int a3 = liveAfter (C, 0.8f, 1, 1, &used, 6);            // shared room 6
            std::snprintf (ln, sizeof ln, "q=0.5 Saw on A: %d live (want 34) · unison 16 Square on A: %d (want 13) · budget room 6 Square on C: %d (want 6)", a1, a2, a3);
            bar (a1 == 34 && a2 == 13 && a3 == 6, "[G3] SHAPE NEVER COSTS MORE THAN QUALITY ALLOWS — the quota holds under thin, unison and the shared budget", ln);
        }

        // ── G4 level ──
        {
            auto rmsOf = [] (const std::vector<float>& x) { double e = 0; for (float v : x) e += (double) v * v; return std::sqrt (e / (double) x.size()); };
            double worst = 0; std::string ww; double metalMin = 0, metalMax = -99;
            for (int s = 0; s < 3; ++s)
            {
                GeodeParams p0; p0.scan = 0.f; p0.quality = 0.8f; p0.shape = 0.f;
                const double r0 = rmsOf (renderSteady (p0, *SRC[s], playHz, sr));
                for (int k = 0; k < 9; ++k)
                {
                    GeodeParams p = p0; p.shape = 1.f; p.shapeTarget = KEPT[k];
                    const double db = 20.0 * std::log10 (rmsOf (renderSteady (p, *SRC[s], playHz, sr)) / r0);
                    if (KEPT[k] == 10) { metalMin = std::min (metalMin, db); metalMax = std::max (metalMax, db); continue; }
                    if (std::fabs (db) > worst) { worst = std::fabs (db); std::snprintf (ln, sizeof ln, "%s/%s %+.2f dB", SN[s], SHN[KEPT[k]], db); ww = ln; }
                }
            }
            std::snprintf (ln, sizeof ln, "worst non-Metal %s (limit ±1) · Metal %+.2f..%+.2f dB (its documented −3.1 dB peak trim, window −4.1..−2.1)", ww.c_str(), metalMin, metalMax);
            bar (worst <= 1.0 && metalMin >= -4.1 && metalMax <= -2.1, "[G4] SHAPE CHANGES TIMBRE, NOT LOUDNESS — RMS at SHAPE=1 within ±1 dB of SHAPE=0, every kept target, every source", ln);
        }

        // ── G5 no click  +  G6 continuity ──
        {
            auto maxDiff = [] (const std::vector<float>& x, size_t a, size_t b) { double m = 0; for (size_t i = std::max<size_t> (a, 1); i < std::min (b, x.size()); ++i) m = std::max (m, (double) std::fabs (x[i] - x[i-1])); return m; };
            const int blk = 128, nb = 200; std::vector<float> L ((size_t) blk * nb, 0.f), R ((size_t) blk * nb, 0.f);
            GeodeEngine e; e.prepare (sr); e.setFrameStore (&B);
            GeodeParams p; p.scan = 0.f; p.quality = 0.8f; p.shape = 0.f; p.shapeTarget = 2; e.setParams (p); e.noteOn (playHz, 999);
            for (int b = 0; b < nb; ++b) { p.shape = (b >= 60 && b < 120) ? 1.f : 0.f; e.setParams (p); e.renderBlockAdd (&L[(size_t) b * blk], &R[(size_t) b * blk], blk); }
            const double steady0 = maxDiff (L, 40 * blk, 58 * blk), steady1 = maxDiff (L, 90 * blk, 118 * blk);
            const double up = maxDiff (L, 60 * blk - 8, 64 * blk), down = maxDiff (L, 120 * blk - 8, 124 * blk);
            const double ref = std::max (steady0, steady1);
            std::snprintf (ln, sizeof ln, "steady max|Δ| SHAPE=0 %.4f, SHAPE=1 %.4f · at 0→1 step %.4f (%.2f×) · at 1→0 step %.4f (%.2f×)", steady0, steady1, up, up / ref, down, down / ref);
            bar (up <= 1.5 * ref && down <= 1.5 * ref, "[G5] NO CLICK — SHAPE stepped 0→1 and 1→0 mid-note without a first-difference spike", ln);

            // G6: moving head over a CHURNING store (a partial dies, another is born). Every spawned
            // harmonic has a HOME slot (kMaxPartials-kMaxH-1+n) so it keeps its phase_/ampZ_ block after
            // block while the source set changes; an append-only spawn hops slots and a live slot's
            // frequency JUMPS — measured 43 jumps on the -DGEODE_SPAWN_APPEND_ONLY mutant, 0 here.
            // (The output-domain first-difference cannot see a hop: it is a per-block crossfade, not a click.)
            GeodeFrameStore churn = B; churn.naturalSec = 0.4f;   // 4 frames in 0.4 s: the 0.53 s render at natural speed crosses every frame boundary
            for (size_t f = 2; f < churn.frames.size(); ++f)
            {   // frames 2/3: the 7.6 partial DIES (its slot empties) and a 12.1 partial is BORN in a NEW slot — nP 10 → 11,
                // so an append-only spawn re-packs every spawned harmonic one slot up (= a frequency jump in each live slot)
                churn.frames[f].amp[6] = 0.f;
                churn.frames[f].ratio[10] = 12.1f; churn.frames[f].amp[10] = 0.15f; churn.frames[f].nPartials = 11;
            }
            GeodeEngine e2; e2.prepare (sr); e2.setFrameStore (&churn);
            GeodeParams q; q.scan = 0.5f; q.quality = 0.8f; q.shape = 1.f; q.shapeTarget = 2; e2.setParams (q); e2.noteOn (playHz, 999);
            std::vector<float> M ((size_t) blk, 0.f), MR ((size_t) blk, 0.f);
            std::vector<float> prevR (96, 0.f), prevZ (96, 0.f);
            int jumps = 0, births = 0;
            for (int b = 0; b < nb; ++b)
            {
                e2.setParams (q); std::fill (M.begin(), M.end(), 0.f); std::fill (MR.begin(), MR.end(), 0.f);
                e2.renderBlockAdd (M.data(), MR.data(), blk);
                const float* r = e2.wrRatioForTesting(); const float* z = e2.ampZForTesting();
                if (b > 20)
                    for (int j = 0; j < 96; ++j)
                    {
                        if (prevZ[(size_t) j] > 1e-7f && z[j] > 1e-7f && std::fabs (r[j] - prevR[(size_t) j]) > 0.5f) ++jumps;
                        if (prevZ[(size_t) j] <= 1e-7f && z[j] > 1e-7f) ++births;
                    }
                for (int j = 0; j < 96; ++j) { prevR[(size_t) j] = r[j]; prevZ[(size_t) j] = z[j]; }
            }
            std::snprintf (ln, sizeof ln, "live-slot frequency jumps over %d blocks of a moving head: %d (want 0) · slot births after the attack: %d", nb - 21, jumps, births);
            bar (jumps == 0, "[G6] CONTINUITY — a moving head over a churning store: no live slot ever changes frequency (home slots hold)", ln);
        }

        // ── G7 identity ──
        {
            auto fnv = [] (const std::vector<float>& x, size_t n0, size_t n) { std::uint64_t h = 1469598103934665603ull;
                for (size_t i = n0; i < n0 + n; ++i) { std::uint32_t b; std::memcpy (&b, &x[i], 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8*k)) & 0xff; h *= 1099511628211ull; } } return h; };
            const std::uint64_t SHIPPED[3] = { 0x99ad5041369bebaaull, 0x3c3fd0c462ea60bfull, 0x808ddbd9a0211a21ull };   // taken from the PRE-fb596 header (922aec6) through THESE stores and this exact recipe
            bool same = true; std::string det;
            for (int s = 0; s < 3; ++s)
            {
                GeodeEngine e; e.prepare (sr); e.setFrameStore (SRC[s]);
                GeodeParams p; p.scan = 0.f; p.quality = 0.8f; p.shape = 0.f; e.setParams (p); e.noteOn (playHz, 999);
                const int Nn = 8192; std::vector<float> L ((size_t) Nn, 0.f), R ((size_t) Nn, 0.f);
                for (int off = 0; off < Nn; off += 256) { e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], 256); e.postProcess (&L[(size_t) off], &R[(size_t) off], 256); }
                const std::uint64_t h = fnv (L, 4096, 4096);
                std::snprintf (ln, sizeof ln, "%s %016llx%s ", SN[s], (unsigned long long) h, h == SHIPPED[s] ? "" : "≠SHIPPED"); det += ln;
                same = same && (h == SHIPPED[s]);
            }
            bar (same, "[G7] SHAPE=0 IS BIT-IDENTICAL TO THE SHIPPED ENGINE — three fingerprints", det);
        }

        std::printf ("\n  %s %d passed, %d failed\n", fail ? "❌" : "✅", pass, fail);
        ok = ok && fail == 0;
    }
    return ok ? 0 : 1;
}
