// ════════════════════════════════════════════════════════════════════════════
//  geode_drive_cert.cpp — RESYNTH DRIVE-MODE certification (Pattern A — NOT in CMakeLists).
//
//    shipped:   c++ -std=c++17 -O2 -Wall -Wextra -ISource Tests/geode_drive_cert.cpp -o /tmp/gdc && /tmp/gdc
//    prototype: c++ -std=c++17 -O2 -Wall -Wextra -I<scratch>/rs2-drive -ISource Tests/geode_drive_cert.cpp -o /tmp/gdc && /tmp/gdc
//
//  THE PROPERTY: every DRIVE mode the menu offers must CHANGE THE SOUND (≥ 6 dB dF, drive=1 vs 0)
//  on every kind of source — including a DENSE ANALYSED SAMPLE (the thing Max actually drops in) —
//  and no two offered modes may sound the same (< 3 dB dF pairwise at drive=1).
//
//  Modelled on Tests/geode_shape_cert.cpp (same sources A/B/C, same renderSteady, same dF metric).
//  Two more sources go through the REAL analyser (GeodeAnalyzer::analyzeSample):
//    D  a clean analysed pluck (≈ 60 tracked partials)             — the tracker leaves free slots
//    E  a DENSE analysed sample: a 3-note chord + -22 dB noise      — every one of the 96 tracker
//       slots is busy in every frame (nPartials == 96)               — what a real drum/vocal/chord does
//  Unlike the shape cert, renderSteady here calls postProcess() after each block — exactly what
//  SynthVoice.h:7404 does — because Saturate/Foldback/Ember live there.
// ════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <complex>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#define private public
#include "GeodeEngine.h"
#undef private
using namespace tw;

static const char* DMN[6] = { "Saturate", "Bloom", "Glint", "Moire", "Foldback", "Ember" };
static constexpr int    kModes   = 6;
static constexpr int    kH       = 64;
static constexpr int    kSub     = 4;
static constexpr int    kBands   = kH * kSub + 2;
static constexpr double kFloorDb = -90.0;
static constexpr int    kFFT     = 65536;
static constexpr double kAudFloor = 3.0;   // pairs closer than this are "the same mode"
static constexpr double kDead     = 6.0;   // drive=1 vs drive=0 closer than this → the knob does nothing

struct Spec { std::array<double, kH + 1> h {}; std::array<double, kBands> band {}; };

// ── sources (A/B/C == geode_shape_cert.cpp) ─────────────────────────────────────────────────
static GeodeFrameStore makeA()
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
static GeodeFrameStore makeB (float f0)
{
    const std::vector<float> R = { 1.f, 2.01f, 3.04f, 4.1f, 5.2f, 6.35f, 7.6f, 9.1f, 11.3f, 13.8f };
    std::vector<float> A; for (size_t i = 0; i < R.size(); ++i) A.push_back (std::pow ((float) (i + 1), -1.3f));
    return makeFrom (R, A, f0);
}
static GeodeFrameStore makeC (float f0)
{
    std::vector<float> R, A; for (int n = 1; n <= 12; ++n) { R.push_back ((float) n); A.push_back (1.f / (float) (n * n)); }
    return makeFrom (R, A, f0);
}
// ── D / E go through the REAL analyser ──────────────────────────────────────────────────────
static std::uint32_t rngS = 0x1234567u;
static float urand() { rngS ^= rngS << 13; rngS ^= rngS >> 17; rngS ^= rngS << 5; return (float) (rngS & 0xFFFFFF) / 16777215.f - 0.5f; }
static GeodeFrameStore makeAnalysed (bool dense, double sr, float f0Hz)
{
    const int n = (int) (sr * 1.2);
    std::vector<float> x ((size_t) n, 0.f);
    auto tone = [&] (double f0, double gain, double B, int nH, double roll)
    {
        for (int h = 1; h <= nH; ++h)
        {
            const double r = h * std::sqrt (1.0 + B * h * h);
            const double f = f0 * r; if (f >= sr * 0.45) break;
            const double a = gain / std::pow ((double) h, roll);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / sr;
                x[(size_t) i] += (float) (a * std::exp (-t * (0.4 + 0.02 * h)) * std::sin (2.0 * M_PI * f * t + 0.7 * h));
            }
        }
    };
    tone (f0Hz, 0.5, 0.0002, 60, 0.8);
    if (dense)
    {
        tone (f0Hz * 1.4983, 0.35, 0.0004, 50, 0.9);   // a fifth (a "chord" sample)
        tone (f0Hz * 1.2599, 0.30, 0.0003, 50, 0.9);   // a major third
        for (int i = 0; i < n; ++i) x[(size_t) i] += 0.08f * urand();    // ≈ -22 dB broadband (a real recording's floor)
    }
    else
        for (int i = 0; i < n; ++i) x[(size_t) i] += 0.004f * urand();   // ≈ -48 dB
    GeodeFrameStore s; GeodeAnalyzer::analyzeSample (x.data(), n, sr, s);
    return s;
}
static void storeStats (const GeodeFrameStore& s, int& mn, int& mx, float& f0)
{
    mn = 999; mx = 0; f0 = s.f0;
    for (const auto& f : s.frames) { mn = std::min (mn, f.nPartials); mx = std::max (mx, f.nPartials); }
}

// ── render exactly as SynthVoice::renderGeodeOsc: prepare+render, then postProcess (7404) ───
struct RenderOut { std::vector<float> x; int live0 = 0; int liveN = 0; };
static RenderOut renderSteady (GeodeParams p, GeodeFrameStore& st, double playHz, double sr, int cap = 0)
{
    const int skip = (int) (sr * 0.1);
    const int N = skip + kFFT;
    std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
    GeodeEngine e; e.prepare (sr); e.setFrameStore (&st); e.setParams (p); e.noteOn (playHz, 999);
    int used = 0; if (cap > 0) e.setPartialBudget (&used, cap);
    const int blk = 256; RenderOut o; bool first = true;
    for (int off = 0; off < N; off += blk)
    {
        const int m = std::min (blk, N - off); used = 0; e.setParams (p);
        e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m);
        e.postProcess (&L[(size_t) off], &R[(size_t) off], m);
        if (first) { o.live0 = e.preparedActive(); first = false; }
        o.liveN = e.preparedActive();
    }
    o.x = std::vector<float> (L.begin() + skip, L.end());
    return o;
}

static Spec analyse (const std::vector<float>& x, double sr, double f0)
{
    std::vector<std::complex<float>> bf ((size_t) kFFT);
    for (int i = 0; i < kFFT; ++i)
    {
        const double t = 2.0 * M_PI * i / (kFFT - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        bf[(size_t) i] = { (float) (x[(size_t) i] * w), 0.f };
    }
    geodedsp::fft (bf.data(), kFFT, false);
    std::vector<double> mag ((size_t) kFFT / 2, 0.0);
    for (int b = 0; b < kFFT / 2; ++b) mag[(size_t) b] = std::abs (bf[(size_t) b]);
    const double binHz = sr / kFFT;
    Spec s;
    std::array<double, kH + 1> lin {};
    for (int n = 1; n <= kH; ++n)
    {
        const int c = (int) std::lround (n * f0 / binHz);
        double m = 0; for (int b = std::max (1, c - 6); b <= std::min (kFFT / 2 - 1, c + 6); ++b) m = std::max (m, mag[(size_t) b]);
        lin[(size_t) n] = m;
    }
    const double ref = std::max (lin[1], 1e-12);
    for (int n = 1; n <= kH; ++n) s.h[(size_t) n] = std::max (kFloorDb, 20.0 * std::log10 (std::max (lin[(size_t) n], 1e-12) / ref));
    std::array<double, kBands> be {};
    for (int b = 0; b < kBands; ++b)
    {
        const double cR = 0.5 + (double) b / kSub;
        const int lo = (int) std::lround ((cR - 0.125) * f0 / binHz), hi = (int) std::lround ((cR + 0.125) * f0 / binHz);
        double e = 0; for (int k = std::max (1, lo); k < std::min (kFFT / 2, hi); ++k) e += mag[(size_t) k] * mag[(size_t) k];
        be[(size_t) b] = std::sqrt (e);
    }
    const double bref = std::max (be[(size_t) (kSub - 2)], 1e-12);
    for (int b = 0; b < kBands; ++b) s.band[(size_t) b] = std::max (kFloorDb, 20.0 * std::log10 (std::max (be[(size_t) b], 1e-12) / bref));
    return s;
}
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
static double dF (const Spec& a, const Spec& b) { return wdist (a.band, b.band, 0); }
static double rmsOf (const std::vector<float>& x) { double e = 0; for (float v : x) e += (double) v * v; return std::sqrt (e / (double) x.size()); }
static double peakOf (const std::vector<float>& x) { double m = 0; for (float v : x) m = std::max (m, (double) std::fabs (v)); return m; }
static double db (double r) { return 20.0 * std::log10 (std::max (r, 1e-12)); }

struct Row { double knob1, knobHalf, rmsDb, rmsHalfDb, peak, nearest; int kids; int nearestM; };
struct SrcResult { std::array<Row, kModes> rows; double M[kModes][kModes]; };

static SrcResult runSource (const char* name, GeodeFrameStore& st, double playHz, double sr, float quality, int cap)
{
    GeodeParams base; base.scan = 0.f; base.quality = quality;
    GeodeParams p0 = base; p0.drive = 0.f;
    const RenderOut r0 = renderSteady (p0, st, playHz, sr, cap);
    const Spec s0 = analyse (r0.x, sr, playHz);
    const double rms0 = rmsOf (r0.x), pk0 = peakOf (r0.x);
    SrcResult res {};
    std::array<Spec, kModes> s1;
    for (int m = 0; m < kModes; ++m)
    {
        GeodeParams p = base; p.driveMode = m;
        p.drive = 1.f;  const RenderOut r1 = renderSteady (p, st, playHz, sr, cap); s1[(size_t) m] = analyse (r1.x, sr, playHz);
        p.drive = 0.5f; const RenderOut rh = renderSteady (p, st, playHz, sr, cap); const Spec sh = analyse (rh.x, sr, playHz);
        Row& r = res.rows[(size_t) m];
        r.knob1 = dF (s1[(size_t) m], s0); r.knobHalf = dF (sh, s0);
        r.rmsDb = db (rmsOf (r1.x) / rms0); r.rmsHalfDb = db (rmsOf (rh.x) / rms0);
        r.peak = peakOf (r1.x) / pk0; r.kids = r1.liveN - r0.liveN;
    }
    for (int a = 0; a < kModes; ++a) for (int b = 0; b < kModes; ++b) res.M[a][b] = (a == b) ? 0.0 : dF (s1[(size_t) a], s1[(size_t) b]);
    for (int a = 0; a < kModes; ++a)
    { res.rows[(size_t) a].nearest = 1e9; res.rows[(size_t) a].nearestM = -1;
      for (int b = 0; b < kModes; ++b) if (b != a && res.M[a][b] < res.rows[(size_t) a].nearest) { res.rows[(size_t) a].nearest = res.M[a][b]; res.rows[(size_t) a].nearestM = b; } }

    std::printf ("\n=== SOURCE %s | quality=%.2f | cap=%d | drive=0: live partials %d, rms %.4f, peak %.3f ===\n", name, quality, cap, r0.liveN, rms0, pk0);
    std::printf ("%-9s %9s %9s | %8s %8s %7s | %5s | %14s | %s\n", "mode", "dF(1vs0)", "dF(.5vs0)", "rms@1", "rms@.5", "peak@1", "kids", "nearest-other", "flags");
    for (int m = 0; m < kModes; ++m)
    {
        const Row& r = res.rows[(size_t) m];
        std::string fl;
        if (r.knob1 < kDead) fl += " DEAD";
        if (r.nearest < kAudFloor) fl += " DUP";
        std::printf ("%-9s %9.1f %9.1f | %+7.1fdB %+7.1fdB %6.2fx | %5d | %6.1f (%-8s) |%s\n",
                     DMN[m], r.knob1, r.knobHalf, r.rmsDb, r.rmsHalfDb, r.peak, r.kids, r.nearest, DMN[r.nearestM], fl.c_str());
    }
    std::printf ("pairwise dF at drive=1 (dB):\n         ");
    for (int b = 0; b < kModes; ++b) std::printf ("%9.9s", DMN[b]); std::printf ("\n");
    for (int a = 0; a < kModes; ++a) { std::printf ("%-8s ", DMN[a]); for (int b = 0; b < kModes; ++b) std::printf ("%9.1f", res.M[a][b]); std::printf ("\n"); }
    return res;
}

int main()
{
    const double sr = 48000.0, playHz = 261.63;
    std::printf ("=== RESYNTH DRIVE CERT (played C4 = %.2f Hz, %d-pt FFT, BH window; renderBlockAdd + postProcess per block as SynthVoice.h:7388-7404) ===\n", playHz, kFFT);
    std::printf ("flags: DEAD = drive=1 within %.0f dB (dF) of drive=0 — the mode does nothing; DUP = another mode within %.0f dB at drive=1\n", kDead, kAudFloor);

    auto A = makeA(); auto B = makeB ((float) playHz); auto C = makeC ((float) playHz);
    auto D = makeAnalysed (false, sr, 220.f); auto E = makeAnalysed (true, sr, 220.f);
    { int mn, mx; float f0; storeStats (D, mn, mx, f0); std::printf ("\nsource D (clean analysed pluck): %d frames, f0=%.1f Hz, nPartials per frame %d..%d\n", D.numFrames(), f0, mn, mx);
      storeStats (E, mn, mx, f0);                   std::printf ("source E (dense analysed chord+noise): %d frames, f0=%.1f Hz, nPartials per frame %d..%d  (kMaxPartials=%d)\n", E.numFrames(), f0, mn, mx, geode::kMaxPartials); }

    GeodeFrameStore* SRC[5] = { &A, &B, &C, &D, &E }; const char* SN[5] = { "A default-48@1/n", "B sparse-inharm-10", "C dull-12@1/n2", "D analysed-pluck", "E analysed-DENSE" };
    std::array<SrcResult, 5> R;
    for (int s = 0; s < 5; ++s) R[(size_t) s] = runSource (SN[s], *SRC[s], playHz, sr, 0.80f, 0);

    // ═══ THE BARS ═══
    int pass = 0, fail = 0; char ln[512];
    auto bar = [&] (bool good, const char* name, const std::string& detail)
    { good ? ++pass : ++fail; std::printf ("  %s  %s\n        %s\n", good ? "PASS" : "FAIL", name, detail.c_str()); };
    std::printf ("\n");
    // D1 — every offered mode is AUDIBLE on every source (incl. the dense analysed sample)
    {
        double worst = 1e9; std::string w;
        for (int s = 0; s < 5; ++s) for (int m = 0; m < kModes; ++m)
        { const double d = R[(size_t) s].rows[(size_t) m].knob1; if (d < worst) { worst = d; std::snprintf (ln, sizeof ln, "%s/%s %.1f dB", SN[s], DMN[m], d); w = ln; } }
        bar (worst >= kDead, "[D1] EVERY OFFERED DRIVE MODE CHANGES THE SOUND — dF(drive=1 vs 0) >= 6 dB on every source, dense analysed sample included", "worst " + w);
    }
    // D2 — no two offered modes sound the same
    {
        double minSep = 1e9; std::string w;
        // fb598 — SATURATE (0) ~ EMBER (5) is a DOCUMENTED exception: the same softClip at two gains, 1.8-2.4 dB apart on
        // real samples. Max: "Saturate sounds great, Foldback sounds great, and the one under Foldback" — Ember stays EXACTLY
        // as shipped (the post-clip warmth that would separate them is behind -DGEODE_EMBER_WARMTH). Every other pair must clear.
        for (int s = 0; s < 5; ++s) for (int a = 0; a < kModes; ++a) for (int b = a + 1; b < kModes; ++b)
        { if (a == 0 && b == 5) continue;   // Saturate~Ember, see above
          const double d = R[(size_t) s].M[a][b]; if (d < minSep) { minSep = d; std::snprintf (ln, sizeof ln, "%s/%s~%s %.1f dB", SN[s], DMN[a], DMN[b], d); w = ln; } }
        bar (minSep >= kAudFloor, "[D2] NO TWO DRIVE MODES SOUND THE SAME — pairwise dF >= 3 dB at drive=1, every source", "closest pair " + w);
    }
    // D3 — the spectral modes actually GROW children on the dense sample (the slot-starvation probe)
    {
        std::string det; bool ok = true;
        for (int m = 1; m <= 3; ++m) { const int k = R[4].rows[(size_t) m].kids; ok = ok && k > 0; std::snprintf (ln, sizeof ln, "%s +%d · ", DMN[m], k); det += ln; }
        bar (ok, "[D3] BLOOM/GLINT/MOIRE GROW CHILDREN ON A FULL 96-SLOT BANK — live partials at drive=1 minus drive=0 on source E", det);
    }
    // D4 — the shared budget still bounds the children: cap = live0 + 5 → at most 5 children
    {
        GeodeParams p; p.scan = 0.f; p.quality = 0.8f; p.drive = 1.f; p.driveMode = 1;
        GeodeParams q0 = p; q0.drive = 0.f;
        const RenderOut r0 = renderSteady (q0, A, playHz, sr, 0);
        const int cap = r0.liveN + 5;
        const RenderOut r1 = renderSteady (p, A, playHz, sr, cap);
        std::snprintf (ln, sizeof ln, "Bloom on A: %d live at drive=0, cap %d → %d live at drive=1 (want <= %d)", r0.liveN, cap, r1.liveN, cap);
        bar (r1.liveN <= cap && r1.liveN > r0.liveN, "[D4] CHILDREN NEVER BUST THE SHARED BUDGET — a cap 5 above the parent count admits at most 5 children", ln);
    }
    // D5 — drive=0 is bit-identical to the SHIPPED engine on every source (the fix touches only drive>0)
    {
        auto fnv = [] (const std::vector<float>& x, size_t n0, size_t n) { std::uint64_t h = 1469598103934665603ull;
            for (size_t i = n0; i < n0 + n; ++i) { std::uint32_t b; std::memcpy (&b, &x[i], 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8*k)) & 0xff; h *= 1099511628211ull; } } return h; };
        std::string det; bool same = true;
        // taken from the SHIPPED header (d677a1c) through THESE stores and this exact recipe
        const std::uint64_t SHIPPED[5] = { 0xd8da956225908510ull, 0x886e76aaa4c57e11ull, 0xfc3d22d244448803ull, 0xe62c9727037a228aull, 0xa96e89783e6a2f91ull };
        for (int s = 0; s < 5; ++s)
        {
            GeodeParams p; p.scan = 0.f; p.quality = 0.8f; p.drive = 0.f; p.driveMode = 1;
            const RenderOut r = renderSteady (p, *SRC[s], playHz, sr, 0);
            const std::uint64_t h = fnv (r.x, 0, 8192);
            std::snprintf (ln, sizeof ln, "%c %016llx%s ", "ABCDE"[s], (unsigned long long) h, h == SHIPPED[s] ? "" : "≠SHIPPED"); det += ln;
            same = same && (h == SHIPPED[s]);
        }
        bar (same, "[D5] DRIVE=0 IS BIT-IDENTICAL TO THE SHIPPED ENGINE — five fingerprints (mode set to Bloom, knob at 0)", det);
    }
    // D6 — no click: drive stepped 0→1 and 1→0 mid-note, every mode (children ramp through the declick loop; audio modes ramp driveSm_)
    {
        auto maxDiff = [] (const std::vector<float>& x, size_t a, size_t b) { double m = 0; for (size_t i = std::max<size_t> (a, 1); i < std::min (b, x.size()); ++i) m = std::max (m, (double) std::fabs (x[i] - x[i-1])); return m; };
        double worstRatio = 0; std::string w;
        for (int m = 0; m < kModes; ++m)
        {
            const int blk = 128, nb = 200; std::vector<float> L ((size_t) blk * nb, 0.f), Rr ((size_t) blk * nb, 0.f);
            GeodeEngine e; e.prepare (sr); e.setFrameStore (&E);
            GeodeParams p; p.scan = 0.f; p.quality = 0.8f; p.drive = 0.f; p.driveMode = m; e.setParams (p); e.noteOn (playHz, 999);
            for (int b = 0; b < nb; ++b) { p.drive = (b >= 60 && b < 120) ? 1.f : 0.f; e.setParams (p); e.renderBlockAdd (&L[(size_t) b * blk], &Rr[(size_t) b * blk], blk); e.postProcess (&L[(size_t) b * blk], &Rr[(size_t) b * blk], blk); }
            const double steady0 = maxDiff (L, 40 * blk, 58 * blk), steady1 = maxDiff (L, 90 * blk, 118 * blk);
            const double up = maxDiff (L, 60 * blk - 8, 64 * blk), down = maxDiff (L, 120 * blk - 8, 124 * blk);
            const double ref = std::max (steady0, steady1);
            const double ratio = std::max (up, down) / ref;
            if (ratio > worstRatio) { worstRatio = ratio; std::snprintf (ln, sizeof ln, "%s: steady %.4f/%.4f, step up %.4f down %.4f (%.2fx)", DMN[m], steady0, steady1, up, down, ratio); w = ln; }
        }
        bar (worstRatio <= 2.0, "[D6] NO CLICK — DRIVE stepped 0→1 and 1→0 mid-note on the dense sample, every mode: no first-difference spike beyond 2x steady (a full-scale step ramps over ONE block by design, fb204 — Saturate's own ramp reads 1.50x)", "worst " + w);
    }
    // D7 — continuity: a MOVING head over source E with Bloom at 1: live slots never change frequency (children keep their slot)
    {
        GeodeEngine e2; e2.prepare (sr); e2.setFrameStore (&E);
        GeodeParams q; q.scan = 0.5f; q.quality = 0.8f; q.drive = 1.f; q.driveMode = 1; e2.setParams (q); e2.noteOn (playHz, 999);
        const int blk = 128, nb = 200;
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
        std::snprintf (ln, sizeof ln, "live-slot frequency jumps over %d blocks of a moving head with Bloom=1 on E: %d (want 0) · slot births after the attack: %d", nb - 21, jumps, births);
        bar (jumps == 0 && births <= 100, "[D7] CONTINUITY — a moving head over the dense sample with Bloom at 1: no live slot ever changes frequency, and children KEEP their slots (births <= 100; a child re-born every block = 2787 on the -DGEODE_DRIVE_MUT_NOKEY mutant)", ln);
    }
    // D8 — the fb596 interaction: SHAPE > 0 fills the home slots to 95 → the shipped children had no slot on ANY source
    {
        std::string det; bool ok = true;
        for (int m = 1; m <= 3; ++m)
        {
            GeodeParams p; p.scan = 0.f; p.quality = 0.8f; p.shape = 0.5f; p.shapeTarget = 2; p.driveMode = m;
            p.drive = 0.f; const RenderOut r0 = renderSteady (p, B, playHz, sr, 0);
            p.drive = 1.f; const RenderOut r1 = renderSteady (p, B, playHz, sr, 0);
            const double d = dF (analyse (r1.x, sr, playHz), analyse (r0.x, sr, playHz));
            ok = ok && (r1.liveN - r0.liveN) > 0 && d >= kDead;
            std::snprintf (ln, sizeof ln, "%s +%d kids, dF %.1f dB · ", DMN[m], r1.liveN - r0.liveN, d); det += ln;
        }
        bar (ok, "[D8] DRIVE CHILDREN SURVIVE SHAPE > 0 — Saw at SHAPE=0.5 on source B: each spectral mode still grows children and moves the sound >= 6 dB", det);
    }
    std::printf ("\n  %s %d passed, %d failed\n", fail ? "❌" : "✅", pass, fail);
    return fail == 0 ? 0 : 1;
}
