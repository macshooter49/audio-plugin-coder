// ════════════════════════════════════════════════════════════════════════════
//  geode_cut_cert.cpp — RESYNTH LOW + HIGH certification (Pattern A — NOT in CMakeLists).
//
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Tests/geode_cut_cert.cpp -o /tmp/gcc_ && /tmp/gcc_
//
//  rs2-cut. Max: "take away the low pass and the high pass filter and put it in the back ... this IS
//  spectral so they need a low and a high pass filter ... the same DSP for the Resynth LP and HP, but on
//  the back ... these two need to coexist and sculpt the sound."
//
//  THE PROPERTY: the back-row Low (SYN_OSC_x_SPECTRAL_LO) is a HIGH-PASS and High (SYN_OSC_x_SPECTRAL_HI)
//  is a LOW-PASS on the partial bank, BOTH LIVE AT ONCE, using the single Cut knob's exact DSP.
//
//  Modelled on Tests/geode_shape_cert.cpp (makeA/B/C stores, renderSteady, the BH-windowed 65536-pt
//  analyser, the FNV fingerprint recipes of G7 / G7b). Compiles against the SHIPPED header AND the
//  new one: the knob setters below are SFINAE'd on whether GeodeParams has `lo`/`hi` (new) or
//  `cut`/`cutMode` (shipped), so on the shipped header the Low/High bars read "knob does nothing" → RED.
//
//  METRIC: per-harmonic ABSOLUTE dB (peak within ±6 FFT bins of n·f0, n = 1..64) of the steady state,
//  as a DELTA against the neutral render of the same store. A cut is "there" when the partials past
//  its corner drop by the ~24 dB/oct law and the partials inside it move < 0.5 dB.
//
//  THE BARS (each has a mutation that turns it red — see the header of GeodeEngine.h's LOW/HIGH block):
//    C1 NEUTRAL       Low=0 / High=1 is BIT-IDENTICAL to the shipped engine: three G7 fingerprints
//    C2 HIGH          High at 0.7 removes the TOP: n ≥ 24 down ≥ 15 dB, n ≤ 12 within 0.5 dB   (shipped header: RED)
//    C3 LOW           Low at 0.3 removes the BOTTOM: fundamental down ≥ 20 dB, n ≥ 3 within 0.5 dB (shipped header: RED)
//    C4 BAND          Low 0.3 + High 0.7 TOGETHER = a band: C2 and C3 at once                    (shipped header: RED · -DGEODE_MUT_EXCLUSIVE: RED)
//    C5 SAME DSP      High at x == the shipped Cut LP at x, Low at x == the shipped Cut HP at 1-x, BIT-FOR-BIT
    // fb598 — this pinned case carries BLOOM 0.9: re-baked to the fb598 engine (Bloom now spawns children where it spawned none),
    //         the same value Tests/geode_shape_cert.cpp G7b 'HIGH(LP).4 BLOOM cap20' pins. The three Bloom-free cases are still 922aec6.
//                     (the G7b "CUT LP.4 BLOOM cap20" fingerprint 0x3b5f919a10e7bfd7 is reproduced by High=0.4)  (-DGEODE_MUT_LO_CURVE: RED)
//    C6 AMP-ONLY      the cuts never move a partial's RATIO (nothing detunes): the bank's ratio array is bit-equal with and without them
//  + the DECIDING PROPERTY, printed (shipped header only): no single Cut setting can make the band.
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
#include <type_traits>
#define private public
#include "GeodeEngine.h"
#undef private
using namespace tw;

// ── SFINAE knob setters: compile on BOTH headers ────────────────────────────────────────────
template <class T> auto setLo  (T& p, float v, int)  -> decltype (p.lo, void())      { p.lo = v; }
template <class T> void setLo  (T&,   float,   long) {}
template <class T> auto setHi  (T& p, float v, int)  -> decltype (p.hi, void())      { p.hi = v; }
template <class T> void setHi  (T&,   float,   long) {}
template <class T> auto setCut (T& p, float v, int m, int) -> decltype (p.cut, p.cutMode, void()) { p.cut = v; p.cutMode = m; }
template <class T> void setCut (T&,   float,   int,   long) {}
template <class T> auto hasLoHi (int) -> decltype (std::declval<T>().lo, std::declval<T>().hi, std::true_type()) { return {}; }
template <class T> std::false_type hasLoHi (long) { return {}; }
static constexpr bool kNewHeader = decltype (hasLoHi<GeodeParams> (0))::value;

static constexpr int    kH    = 64;
static constexpr int    kFFT  = 65536;
static constexpr double kFloorDb = -120.0;

// ── sources (== geode_shape_cert.cpp) ──────────────────────────────────────────────────────
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
static GeodeFrameStore makeFull64 (float f0)   // 64 × 1/n at quality 1: every harmonic 1..64 is rendered → the cut law is measurable up to 64
{
    std::vector<float> R, A; for (int n = 1; n <= 64; ++n) { R.push_back ((float) n); A.push_back (1.f / (float) n); }
    return makeFrom (R, A, f0);
}

// ── render (== geode_shape_cert.cpp renderSteady) ───────────────────────────────────────────
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

// ── per-harmonic ABSOLUTE dB (not normalised: a cut fundamental must read as a drop, not as a new reference) ──
static std::array<double, kH + 1> harmDb (const std::vector<float>& x, double sr, double f0)
{
    std::vector<std::complex<float>> bf ((size_t) kFFT);
    for (int i = 0; i < kFFT; ++i)
    {
        const double t = 2.0 * M_PI * i / (kFFT - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        bf[(size_t) i] = { (float) (x[(size_t) i] * w), 0.f };
    }
    geodedsp::fft (bf.data(), kFFT, false);
    const double binHz = sr / kFFT;
    std::array<double, kH + 1> h {};
    for (int n = 1; n <= kH; ++n)
    {
        const int c = (int) std::lround (n * f0 / binHz);
        double m = 0; for (int b = std::max (1, c - 6); b <= std::min (kFFT / 2 - 1, c + 6); ++b) m = std::max (m, (double) std::abs (bf[(size_t) b]));
        h[(size_t) n] = std::max (kFloorDb, 20.0 * std::log10 (std::max (m, 1e-12)));
    }
    return h;
}

static std::uint64_t fnv (const std::vector<float>& x, size_t n0, size_t n)
{
    std::uint64_t h = 1469598103934665603ull;
    for (size_t i = n0; i < n0 + n; ++i) { std::uint32_t b; std::memcpy (&b, &x[i], 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8*k)) & 0xff; h *= 1099511628211ull; } }
    return h;
}
// G7 recipe (geode_shape_cert.cpp): q=0.8, scan 0, 8192 samples in 256-blocks with postProcess, hash of the 2nd half
static std::uint64_t fpG7 (GeodeParams p, GeodeFrameStore& st, double playHz, double sr)
{
    GeodeEngine e; e.prepare (sr); e.setFrameStore (&st);
    p.scan = 0.f; p.quality = 0.8f; p.shape = 0.f; e.setParams (p); e.noteOn (playHz, 999);
    const int Nn = 8192; std::vector<float> L ((size_t) Nn, 0.f), R ((size_t) Nn, 0.f);
    for (int off = 0; off < Nn; off += 256) { e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], 256); e.postProcess (&L[(size_t) off], &R[(size_t) off], 256); }
    return fnv (L, 4096, 4096);
}
// G7b recipe (geode_shape_cert.cpp "CUT LP.4 BLOOM cap20"): st12, cap 20, drive .9 BLOOM, q 1, hash of all 8192
static std::uint64_t fpG7b (GeodeParams p, GeodeFrameStore& st, int cap, double playHz, double sr)
{
    GeodeEngine e; e.prepare (sr); e.setFrameStore (&st); int live = 0; if (cap > 0) e.setPartialBudget (&live, cap);
    e.setParams (p); e.noteOn (playHz, 999);
    const int Nn = 8192; std::vector<float> L ((size_t) Nn, 0.f), R ((size_t) Nn, 0.f);
    for (int off = 0; off < Nn; off += 256) { live = 0; e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], 256); e.postProcess (&L[(size_t) off], &R[(size_t) off], 256); }
    return fnv (L, 0, (size_t) Nn);
}

int main()
{
    const double sr = 48000.0, playHz = 261.63;   // C4, as GeodeEngine_test.cpp / geode_shape_cert.cpp
    int pass = 0, fail = 0; char ln[512];
    auto bar = [&] (bool good, const char* name, const std::string& detail)
    { good ? ++pass : ++fail; std::printf ("  %s  %s\n        %s\n", good ? "PASS" : "FAIL", name, detail.c_str()); };

    std::printf ("=== RESYNTH LOW + HIGH CUT CERT (played C4 = %.2f Hz, %d-pt FFT, BH window) — header: %s ===\n",
                 playHz, kFFT, kNewHeader ? "NEW (GeodeParams::lo/hi)" : "SHIPPED (GeodeParams::cut/cutMode)");

    auto A = makeA(); auto B = makeB ((float) playHz); auto C = makeC ((float) playHz); auto F = makeFull64 ((float) playHz);
    GeodeFrameStore* SRC[3] = { &A, &B, &C }; const char* SN[3] = { "A", "B", "C" };

    // ── C1 NEUTRAL: bit-identical to the shipped engine ──
    {
        // fingerprints taken from the SHIPPED header (d677a1c) through THIS recipe: identical to G7's, cut at its default 1.0
        const std::uint64_t SHIPPED[3] = { 0x99ad5041369bebaaull, 0x3c3fd0c462ea60bfull, 0x808ddbd9a0211a21ull };
        bool same = true; std::string det;
        for (int s = 0; s < 3; ++s)
        {
            GeodeParams p; setLo (p, 0.f, 0); setHi (p, 1.f, 0);
            const std::uint64_t h = fpG7 (p, *SRC[s], playHz, sr);
            std::snprintf (ln, sizeof ln, "%s %016llx%s ", SN[s], (unsigned long long) h, h == SHIPPED[s] ? "" : "≠SHIPPED"); det += ln;
            same = same && (h == SHIPPED[s]);
        }
        bar (same, "[C1] NEUTRAL — Low=0 / High=1 is BIT-IDENTICAL to the shipped engine (three G7 fingerprints)", det);
    }

    // ── the neutral reference spectrum on the 64-harmonic store at quality 1 ──
    GeodeParams base; base.scan = 0.f; base.quality = 1.f;
    const auto h0 = harmDb (renderSteady (base, F, playHz, sr), sr, playHz);
    auto delta = [&] (const GeodeParams& p) { auto h = harmDb (renderSteady (p, F, playHz, sr), sr, playHz);
                                              std::array<double, kH + 1> d {}; for (int n = 1; n <= kH; ++n) d[(size_t) n] = h[(size_t) n] - h0[(size_t) n]; return d; };
    auto stats = [&] (const std::array<double, kH + 1>& d, int a, int b, double& mean, double& worstAbs)
    { mean = 0; worstAbs = 0; for (int n = a; n <= b; ++n) { mean += d[(size_t) n]; worstAbs = std::max (worstAbs, std::fabs (d[(size_t) n])); } mean /= (b - a + 1); };

    // the law: amp /= distance^4 → past the corner, -80·log10(distance) dB = -24.1 dB/oct
    const double loCorner = 0.5 * std::pow (90.0, 0.3);   // Low 0.3 → 1.93×
    const double hiCorner = 0.6 * std::pow (96.0, 0.7);   // High 0.7 → 14.6×
    std::printf ("corners: Low 0.3 → %.2f× (fundamental expected %.1f dB) · High 0.7 → %.2f× (n=32 expected %.1f dB)\n",
                 loCorner, -80.0 * std::log10 (loCorner), hiCorner, -80.0 * std::log10 (32.0 / hiCorner));

    // ── C2 HIGH removes the top ──
    {
        GeodeParams p = base; setHi (p, 0.7f, 0);
        const auto d = delta (p); double mTop, wTop, mIn, wIn; stats (d, 24, 64, mTop, wTop); stats (d, 1, 12, mIn, wIn);
        std::snprintf (ln, sizeof ln, "n≥24 mean %+.1f dB (want ≤ -15) · n≤12 worst |Δ| %.2f dB (want ≤ 0.5) · n=16 %+.1f n=24 %+.1f n=32 %+.1f n=48 %+.1f",
                       mTop, wIn, d[16], d[24], d[32], d[48]);
        bar (mTop <= -15.0 && wIn <= 0.5, "[C2] HIGH AT 0.7 REMOVES THE TOP — partials above the corner fall ~24 dB/oct, the ones below do not move", ln);
    }
    // ── C3 LOW removes the bottom ──
    {
        GeodeParams p = base; setLo (p, 0.3f, 0);
        const auto d = delta (p); double mAbove, wAbove; stats (d, 3, 64, mAbove, wAbove);
        std::snprintf (ln, sizeof ln, "fundamental %+.1f dB (want ≤ -20, law %.1f) · n=2 %+.1f · n≥3 worst |Δ| %.2f dB (want ≤ 0.5)",
                       d[1], -80.0 * std::log10 (loCorner), d[2], wAbove);
        bar (d[1] <= -20.0 && wAbove <= 0.5, "[C3] LOW AT 0.3 REMOVES THE BOTTOM — the fundamental falls by the law, everything above the corner stays", ln);
    }
    // ── C4 BOTH = a band ──
    {
        GeodeParams p = base; setLo (p, 0.3f, 0); setHi (p, 0.7f, 0);
        const auto d = delta (p); double mTop, wTop, mMid, wMid; stats (d, 24, 64, mTop, wTop); stats (d, 3, 12, mMid, wMid);
        std::snprintf (ln, sizeof ln, "fundamental %+.1f (want ≤ -20) · n=3..12 worst |Δ| %.2f (want ≤ 0.5) · n≥24 mean %+.1f (want ≤ -15)", d[1], wMid, mTop);
        bar (d[1] <= -20.0 && wMid <= 0.5 && mTop <= -15.0, "[C4] LOW + HIGH TOGETHER = A BAND — both cuts live at once, the middle untouched (the thing one Cut knob could never do)", ln);
    }
    // ── C5 SAME DSP: fingerprints against the shipped Cut ──
    {
        // (a) G7b's "CUT LP.4 BLOOM cap20" — the number in Tests/geode_shape_cert.cpp, reproduced by High = 0.4
        auto mk = [&] (int Hn, float expo) { std::vector<float> R, Aa; for (int n = 1; n <= Hn; ++n) { R.push_back ((float) n); Aa.push_back (1.f / std::pow ((float) n, expo)); } return makeFrom (R, Aa, (float) playHz); };
        auto st12 = mk (12, 1.f);
        GeodeParams pa; pa.scan = 0.f; pa.shape = 0.f; pa.quality = 1.f; pa.drive = 0.9f; pa.driveMode = 1;
        setCut (pa, 0.4f, 0, 0); setHi (pa, 0.4f, 0);
        const std::uint64_t ha = fpG7b (pa, st12, 20, playHz, sr);
        // (b) High 0.7 on store A, G7 recipe, vs the shipped Cut LP 0.7
        GeodeParams pb; setCut (pb, 0.7f, 0, 0); setHi (pb, 0.7f, 0);
        const std::uint64_t hb = fpG7 (pb, A, playHz, sr);
        // (c) Low 0.3 on store A vs the shipped Cut HP at knob 0.7 (1-0.3 is exact in float, so the exponent is bit-equal)
        GeodeParams pc; setCut (pc, 0.7f, 1, 0); setLo (pc, 0.3f, 0);
        const std::uint64_t hc = fpG7 (pc, A, playHz, sr);
        // (d) Low 0.5 on store B vs Cut HP at 0.5
        GeodeParams pd; setCut (pd, 0.5f, 1, 0); setLo (pd, 0.5f, 0);
        const std::uint64_t hd = fpG7 (pd, B, playHz, sr);
        // shipped-header values (d677a1c); (a) is the one already pinned in geode_shape_cert.cpp G7b
        const std::uint64_t SA = 0x3b5f919a10e7bfd7ull, SB = 0xf0ff0791ea58b720ull, SC = 0x17cd53b39f1313a3ull, SD = 0xa85bf631695f7161ull;
        std::snprintf (ln, sizeof ln, "G7b LP.4 %016llx%s · A LP.7 %016llx%s · A HP(Low .3) %016llx%s · B HP(Low .5) %016llx%s",
                       (unsigned long long) ha, ha == SA ? "" : "≠SHIPPED", (unsigned long long) hb, hb == SB ? "" : (SB ? "≠SHIPPED" : "(capture)"),
                       (unsigned long long) hc, hc == SC ? "" : (SC ? "≠SHIPPED" : "(capture)"), (unsigned long long) hd, hd == SD ? "" : (SD ? "≠SHIPPED" : "(capture)"));
        const bool ok = (ha == SA) && (SB == 0 || hb == SB) && (SC == 0 || hc == SC) && (SD == 0 || hd == SD);
        bar (ok, "[C5] SAME DSP — High at x is the shipped Cut LP at x, Low at x the shipped Cut HP at 1-x, bit-for-bit", ln);
    }
    // ── C6 AMP-ONLY: ratios never move ──
    {
        bool same = true; std::string det;
        for (int s = 0; s < 3; ++s)
        {
            GeodeParams p0; p0.scan = 0.f; p0.quality = 1.f;
            GeodeParams p1 = p0; setLo (p1, 0.3f, 0); setHi (p1, 0.7f, 0);
            GeodeEngine e0, e1; e0.prepare (sr); e1.prepare (sr); e0.setFrameStore (SRC[s]); e1.setFrameStore (SRC[s]);
            e0.setParams (p0); e1.setParams (p1); e0.noteOn (playHz, 999); e1.noteOn (playHz, 999);
            std::vector<float> L (256, 0.f), R (256, 0.f);
            for (int b = 0; b < 4; ++b) { e0.setParams (p0); e1.setParams (p1); e0.renderBlockAdd (L.data(), R.data(), 256); e1.renderBlockAdd (L.data(), R.data(), 256); }
            const float* r0 = e0.wrRatioForTesting(); const float* r1 = e1.wrRatioForTesting();
            int diff = 0, cutN = 0; for (int j = 0; j < geode::kMaxPartials; ++j) { if (std::memcmp (&r0[j], &r1[j], 4) != 0) ++diff; }
            const float* a0 = e0.ampZForTesting(); const float* a1 = e1.ampZForTesting();
            for (int j = 0; j < geode::kMaxPartials; ++j) if (a0[j] > 1e-7f && a1[j] < a0[j] * 0.5f) ++cutN;
            std::snprintf (ln, sizeof ln, "%s ratio diffs %d, partials cut >6 dB %d · ", SN[s], diff, cutN); det += ln;
            same = same && diff == 0;
        }
        bar (same, "[C6] AMPLITUDE ONLY — the two cuts never move a partial's ratio (nothing detunes)", det);
    }

    // ── THE DECIDING PROPERTY (shipped header): can the one Cut knob already make the band? ──
    if (! kNewHeader)
    {
        // the band target by the law: -80·log10(max(1, 1.93/n)) - 80·log10(max(1, n/14.6))
        std::array<double, kH + 1> tgt {};
        for (int n = 1; n <= kH; ++n) tgt[(size_t) n] = -80.0 * std::log10 (std::max (1.0, loCorner / n)) - 80.0 * std::log10 (std::max (1.0, n / hiCorner));
        double best = 1e9; float bestK = 0; int bestM = 0;
        for (int m = 0; m < 2; ++m) for (int k = 0; k <= 20; ++k)
        {
            GeodeParams p = base; setCut (p, (float) k / 20.f, m, 0);
            const auto d = delta (p);
            double err = 0; for (int n = 1; n <= 32; ++n) err = std::max (err, std::fabs (d[(size_t) n] - tgt[(size_t) n]));
            if (err < best) { best = err; bestK = (float) k / 20.f; bestM = m; }
        }
        std::printf ("  DECIDING PROPERTY — closest single-Cut setting to the Low.3+High.7 band on the shipped code: %s at %.2f, worst harmonic error %.1f dB (n=1..32). The band needs BOTH.\n",
                     bestM ? "HP" : "LP", bestK, best);
    }

    std::printf ("\n  %s %d passed, %d failed\n", fail ? "❌" : "✅", pass, fail);
    return fail ? 1 : 0;
}
