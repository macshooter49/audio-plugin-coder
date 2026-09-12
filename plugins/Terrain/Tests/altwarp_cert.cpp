// ══════════════════════════════════════════════════════════════════════════════════════════════
//  altwarp_cert.cpp — fb636 ALT WARP (39-46): SERUM 2's MEASURED LAWS, DRY WHERE THEY SAY, SMOOTH, EXACT WHERE
//  CLAIMED, BAND-LIMITED WHERE IT MATTERS — AND MODES 0-38 DID NOT MOVE A BIT.
//
//    python3 Tests/extract_altwarp.py Source/SynthVoice.h /tmp/aw_slice.h [--mutate M]
//    clang++ -std=c++17 -O2 -I Tests/shim -I Source -include /tmp/aw_slice.h Tests/altwarp_cert.cpp \
//            -framework Accelerate -o /tmp/aw_cert && /tmp/aw_cert
//    (from plugins/Terrain. Tests/altwarp_gates.sh runs it healthy and as every control.)
//
//  It drives the SHIPPED statics (sliced verbatim by extract_altwarp.py into awx::NEW; the reference file's
//  into awx::OLD) and reads a REAL mip-mapped tw::Wavetable (Source/Wavetable.h, a 1023-harmonic saw built by
//  buildFromSpec, the mip picked by mipLevelForPhaseIncrement exactly as the voice asks). The per-sample chain
//  below (slot 1 -> slot 2 -> read -> Odd/Even's second tap -> window -> amp 1 -> amp 2) is the voice's order.
//
//  BARS
//   [1]  DRY, STATIC: 39/40/42/43 at 0, 41/44 at 50 %, 45 at 0 and 100 % return p bit-exact with the window
//        and skip untouched; 46's phase stage is the identity at every amount and its pair is dry at 50 %.
//   [2]  DRY IN A PLAYING VOICE: the float amount glide (lvlSmCoef_, 44.1-192 kHz, from 0 and from 1) stalls
//        short of 50 % — and the stall is still EXACTLY dry for 41, 44 and 46 (the dead band). Just outside the
//        band the mode is live again and the step is < 1e-4 of a cycle.
//   [3]  SERUM 2, AS MEASURED (calibrator + independent checker numbers, typed from their reports):
//        [3a] Bend + log2 r at 10/25/50/75/100 % and at the checker's off-grid amounts
//        [3b] Bend − and Bend +/- (the "+" mode on the LOW half)   [3c] Asym knees, slopes and direction,
//        Asym − and Asym +/-   [3d] Flip's polarity law, 0 of N bins   [3e] Odd/Even's two gains
//        [3f] the read rate: r on Bend's "+" side, 1 everywhere else (Serum's own mip choice)
//   [4]  SHAPE LAWS: [4a] Bend/Asym monotone with fixed points  [4b] Bend point-symmetric  [4c] Bend − is the
//        exact inverse of Bend +  [4d] Asym − is Asym +'s point-mirror (and NOT its inverse)  [4e] the +/- mode
//        at 50 ∓ a/2 % IS the + / − mode at a, bit for bit, at dyadic amounts
//   [5]  CONTINUITY: a 1e-4 knob step moves no phase map more than 1e-3 of a cycle (circular), Flip's window
//        by at most 4 of 4952 bins, Odd/Even's gains by at most 2.1e-4; every map is continuous across the wrap
//   [6]  ROBUSTNESS: 10 M random calls (p in [−3, 3], subnormal and edge amounts, random histories): every
//        output finite, maps in [0, 1), |window| <= 1, no subnormal out
//   [7]  ALIASING AT THE EXTREMES (OOHR = off-harmonic energy / total, in dB; a prime harmonic spacing so no
//        alias can land on the grid; three tables through the shipping bake — SINE, SAW with its jump at 0,
//        SQUARE with jumps at 0 and ½):
//        [7a]  Flip on the SINE (every edge a real ±2y jump), C6 and C7: band-limited edges beat hard ones by
//              >= 12 dB (the design's bar)
//        [7a2] Flip on the SAW and the SQUARE, where the table's OWN jump sits on a flip edge and the product is
//              a FOLD, not a jump: nothing for an edge residual to band-limit, and it never costs > 1 dB
//        [7b]  Flip after Sync (R = 3.25) and on an FM carrier that runs backwards: never > 1 dB worse than hard
//              edges on any table, |window| <= 1
//        [7c]  Flip after Mirror 100 % (the input phase runs BACKWARDS half the cycle): the mirrored residual
//              beats hard edges by >= 6 dB on the sine
//        [7d]  Bend + with its read rate (r) against the base mip on the SQUARE — content exactly where Bend +
//              reads fastest — at 50/100 %, C6/C7: >= 6 dB cleaner
//        [7e]  Asym ± at full strength on the base mip (Serum's measured choice) alias no more than the SHIPPED
//              Skew (5) at 100 % + 3 dB, on all three tables, C6 and C7
//        [7f]  REPORTED, NOT GATED — Bend − on the base mip (Serum's choice, Max's "match Serum") next to Skew
//              and next to the max-slope mip it declined; and Bend + on the SAW, where r is conservative
//   [8]  ODD/EVEN PARITY (the narrowed guarantee, tested rather than assumed): at 0 % every even harmonic
//        <= −100 dBr, at 100 % every odd one (h1 too), in slot 1 and in slot 2; odd-only survives Tube at VAR 0,
//        even-only survives Tube at VAR 0.5 and Rectify; odd-only LEAKS (> −60 dBr) behind Tube VAR 0.5 and
//        Rectify — expected, and asserted, so the guarantee's edge is measured; both slots on 46 cascade
//   [9]  DC: Flip asks for the blocker exactly while it is live (and makes −A/2-class DC on a saw without it);
//        39-44 and 46 never do; 47 keeps its default
//   [10] MODES 0-38 (+47) UNTOUCHED: applyPhaseWarp (with and without a history pointer), applyAmpWarp,
//        warpReadRate and warpAmpNeedsDc agree BIT FOR BIT with the reference file over a dense grid
//   [11] EVERY NEW MODE IS LIVE (the fb470 rule: a case label that slides makes a silent mode)
//
//  CONTROLS: extract_altwarp.py --mutate <m> (the table of which bar each must redden is in its header).
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "Wavetable.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <complex>
#include <random>
#include <string>
#include <vector>

using NV = awx::NEW;
using OV = awx::OLD;

static int gPass = 0, gFail = 0;
static std::vector<std::string> gRed;
static void bar (bool ok, const char* id, const char* fmt, ...)
{
    char msg[1024]; va_list ap; va_start (ap, fmt); std::vsnprintf (msg, sizeof msg, fmt, ap); va_end (ap);
    std::printf ("  %s  [%s] %s\n", ok ? "ok  " : "FAIL", id, msg);
    if (ok) ++gPass; else { ++gFail; gRed.push_back (id); }
}
static bool same (double a, double b) { return std::memcmp (&a, &b, sizeof a) == 0; }
static bool samef (float a, float b)  { return std::memcmp (&a, &b, sizeof a) == 0; }
static double frac (double v) { return v - std::floor (v); }
static double circ (double a, double b) { double d = std::fabs (frac (a) - frac (b)); return std::min (d, 1.0 - d); }

// one slot, no history (the curve card / waterfall form)
static double W (int mode, double a, double p, float* winOut = nullptr, double* hist = nullptr, float var = 0.0f)
{
    float w = 1.0f; bool sk = false;
    const double r = NV::applyPhaseWarp (mode, (float) a, p, w, sk, var, nullptr, hist);
    if (winOut) *winOut = w;
    return r;
}

// ── FFT (radix-2, double) ───────────────────────────────────────────────────────────────────────
static void fft (std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) std::swap (x[i], x[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        { std::complex<double> w (1.0, 0.0);
          for (size_t k = 0; k < len / 2; ++k) { auto u = x[i + k], v = x[i + k + len / 2] * w; x[i + k] = u + v; x[i + k + len / 2] = u - v; w *= wl; } }
    }
}
static const int NF = 65536;
struct Spec { std::vector<double> p; int k; };
static Spec spectrum (const std::vector<float>& y, int k)
{
    std::vector<std::complex<double>> x ((size_t) NF);
    double mean = 0; for (int i = 0; i < NF; ++i) mean += y[(size_t) i]; mean /= NF;
    for (int i = 0; i < NF; ++i) x[(size_t) i] = (double) y[(size_t) i] - mean;
    fft (x);
    Spec s; s.k = k; s.p.resize (NF / 2);
    for (int i = 0; i < NF / 2; ++i) s.p[(size_t) i] = std::norm (x[(size_t) i]);
    return s;
}
static double oohrDb (const Spec& s)
{
    double on = 0, off = 0;
    for (int i = 1; i < NF / 2; ++i) ((i % s.k) == 0 ? on : off) += s.p[(size_t) i];
    return 10.0 * std::log10 (std::max (1e-300, off / std::max (1e-300, on + off)));
}
// worst harmonic of one parity relative to the strongest harmonic, dB
static double parityDbr (const Spec& s, bool even)
{
    double mx = 0, worst = 0;
    for (int h = 1; h * s.k < NF / 2; ++h) mx = std::max (mx, s.p[(size_t) (h * s.k)]);
    for (int h = even ? 2 : 1; h * s.k < NF / 2; h += 2) worst = std::max (worst, s.p[(size_t) (h * s.k)]);
    return 10.0 * std::log10 (std::max (1e-300, worst / std::max (1e-300, mx)));
}

// ── the tables, all 16 frames identical, through the SHIPPING bake (buildFromSpec) ──────────────────
//   SINE   one harmonic: smooth everywhere, so EVERY Flip edge is a real ±2y jump — the clean edge test
//   SAW    1023 harmonics, 1/h, jump at phase 0 (y(½) = 0): the wrap edge sits ON the table's own jump
//   SQUARE odd harmonics, 1/h, jumps at 0 and ½: content exactly where Bend + reads fastest (mid-cycle)
static tw::Wavetable gSine, gSaw, gSquare;
static tw::Wavetable* gWt = &gSaw;
static void buildTables()
{
    static tw::WavetableSpec spec;
    auto bake = [&] (tw::Wavetable& wt, int kind)
    {
        for (auto& f : spec.frames)
        {   f.numHarmonics = (kind == 0) ? 1 : 1023;
            for (int h = 1; h <= 1023; ++h)
            {   float a = 0.0f;
                if (kind == 0) a = (h == 1) ? 1.0f : 0.0f;
                if (kind == 1) a = 1.0f / (float) h;
                if (kind == 2) a = (h & 1) ? 1.0f / (float) h : 0.0f;
                f.amplitudes[(size_t) (h - 1)] = a; f.phases[(size_t) (h - 1)] = 0.0f; } }
        wt.buildFromSpec (spec);
    };
    bake (gSine, 0); bake (gSaw, 1); bake (gSquare, 2);
}

// ── the voice's per-sample chain for one sine (SynthVoice's WT branch, minus blend/feedback/unison) ─
struct Slot { int mode = 0; float amt = 0.0f; float var = 0.0f; };
struct RenderOpt { bool blep = true; double fmIndex = 0.0; double fmRatio = 0.0; bool readRate = true; double forceRate = 0.0; };
static std::vector<float> render (Slot s1, Slot s2, int k, RenderOpt o, float* maxAbsWin = nullptr)
{
    const double inc = (double) k / (double) NF;
    const double rate = (o.forceRate > 0.0) ? o.forceRate
                      : o.readRate ? NV::warpReadRate (s1.mode, s1.amt) * NV::warpReadRate (s2.mode, s2.amt) : 1.0;
    const int mip = tw::Wavetable::mipLevelForPhaseIncrement (inc * rate);
    std::vector<float> y ((size_t) NF);
    double h1 = -1.0, h2 = -1.0, ph = 0.0, mph = 0.0;
    float mw = 0.0f;
    for (int n = 0; n < NF + 4096; ++n)   // 4096 samples of settle so the histories are warm
    {
        double p = ph;
        if (o.fmIndex != 0.0) p = ph + o.fmIndex * std::sin (2.0 * M_PI * mph);
        float win = 1.0f; bool skip = false;
        double w = NV::applyPhaseWarp (s1.mode, s1.amt, p, win, skip, s1.var, nullptr, o.blep ? &h1 : nullptr);
        if (! skip && s2.mode != 0) w = NV::applyPhaseWarp (s2.mode, s2.amt, w, win, skip, s2.var, nullptr, o.blep ? &h2 : nullptr);
        float g0 = 1.0f, g1 = 0.0f;
        const bool oe = NV::altOddEvenGains (s1.mode, s1.amt, s2.mode, s2.amt, g0, g1);
        float v = 0.0f;
        if (! skip)
        {
            v = gWt->lookup (mip, 0.0f, (float) frac (w));
            if (oe) v = g0 * v + g1 * gWt->lookup (mip, 0.0f, (float) frac ((s1.mode == 46 ? p : w) + 0.5));
            v *= win;
            v = NV::applyAmpWarp (s1.mode, s1.amt, v, s1.var);
            v = NV::applyAmpWarp (s2.mode, s2.amt, v, s2.var);
        }
        mw = std::max (mw, std::fabs (win));
        if (n >= 4096) y[(size_t) (n - 4096)] = v;
        ph += inc; if (ph >= 1.0) ph -= 1.0;
        mph += inc * o.fmRatio; if (mph >= 1.0) mph -= 1.0;
    }
    if (maxAbsWin) *maxAbsWin = mw;
    return y;
}

int main()
{
    std::printf ("altwarp_cert — fb636 ALT WARP 39-46%s%s\n\n", *AW_MUTATION ? "   [MUTATION: " : "", *AW_MUTATION ? AW_MUTATION "]" : "");
    const double PS[] = { 0.0, 1e-12, 0.25, 0.5, 0.7331, std::nextafter (1.0, 0.0), 0.123456789, 0.9999 };

    // ── [1] DRY, STATIC ────────────────────────────────────────────────────────────────────────────
    {
        struct D { int m; float a; } dry[] = { {39, 0.0f}, {40, 0.0f}, {42, 0.0f}, {43, 0.0f}, {41, 0.5f}, {44, 0.5f},
                                             {45, 0.0f}, {45, 1.0f}, {46, 0.0f}, {46, 0.5f}, {46, 1.0f}, {46, 0.37f} };
        int bad = 0;
        for (auto d : dry) for (double p : PS)
        {
            float w = 0.8125f; bool sk = false; double h = 0.3;
            const double r = NV::applyPhaseWarp (d.m, d.a, p, w, sk, 0.0f, nullptr, &h);
            if (! same (r, p) || ! samef (w, 0.8125f) || sk) ++bad;
        }
        float g0 = 0, g1 = 0;
        const bool oeDry = ! NV::altOddEvenGains (46, 0.5f, 0, 0.0f, g0, g1) && g0 == 1.0f && g1 == 0.0f
                        && ! NV::altOddEvenGains (46, 0.5f, 46, 0.5f, g0, g1);
        bar (bad == 0 && oeDry, "1", "%d of %zu dry-point calls moved p, the window or skip; Odd/Even at 50 %% %s",
             bad, sizeof dry / sizeof dry[0] * (sizeof PS / sizeof PS[0]), oeDry ? "is dry (no second read)" : "READS A SECOND TAP");
    }

    // ── [2] DRY IN A PLAYING VOICE — the glide stall ───────────────────────────────────────────────
    {
        int bad = 0; float worstStall = 0.5f; char where[256] = "";
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (float start : { 0.0f, 1.0f })
            {
                const float coef = 1.0f - std::exp (-1.0f / (0.0025f * (float) sr));   // SynthVoice lvlSmCoef_, verbatim
                float v = start;
                for (int n = 0; n < 400000; ++n) v += (0.5f - v) * coef;           // warpAmount_ += (T − warpAmount_) * lvlSmCoef_
                if (std::fabs (v - 0.5f) > std::fabs (worstStall - 0.5f)) { worstStall = v; std::snprintf (where, sizeof where, "%.0f Hz from %.0f", sr, start); }
                for (int m : { 41, 44 }) for (double p : PS) if (! same (W (m, v, p), p)) ++bad;
                float g0, g1; if (NV::altOddEvenGains (46, v, 0, 0.0f, g0, g1)) ++bad;
            }
        // just outside the band: live again, and a small step
        const float out = 0.5f - 0.51e-4f;
        double step = 0; for (double p : PS) { step = std::max (step, circ (W (41, out, p), p)); step = std::max (step, circ (W (44, out, p), p)); }
        float g0, g1; const bool liveOut = NV::altOddEvenGains (46, 0.5f + 0.51e-4f, 0, 0.0f, g0, g1) && step > 0.0;
        bar (bad == 0 && liveOut && step < 1e-4, "2", "the glide parks at %.8f (worst, %s; |s| = %.2e) and 41/44/46 are EXACTLY dry there "
             "(%d misses); at |s| = 1.02e-4 they are live again, step %.2e of a cycle", worstStall, where, std::fabs (2.0 * worstStall - 1.0), bad, step);
    }

    // ── [3] SERUM 2, AS MEASURED ───────────────────────────────────────────────────────────────────
    auto midLog2 = [] (int m, float a) { const double h = 1e-8; return std::log2 ((W (m, a, 0.5 + h) - W (m, a, 0.5)) / h); };
    auto knee = [] (int m, float a) { double lo = 0, hi = 1; for (int i = 0; i < 80; ++i) { const double mid = 0.5 * (lo + hi); (W (m, a, mid) < 0.5 ? lo : hi) = mid; } return 0.5 * (lo + hi); };
    {
        struct R { float a; double v; double tol; } bp[] = {
            {0.10f, 0.2313, 1.5e-3}, {0.25f, 0.5850, 1.5e-3}, {0.50f, 1.2224, 1.5e-3}, {0.75f, 2.0001, 1.5e-3}, {1.00f, 3.1700, 1.5e-3},   // calibrator
            {0.13f, 0.30118, 2e-4}, {0.37f, 0.88043, 2e-4}, {0.62f, 1.56962, 2e-4}, {0.88f, 2.52531, 2e-4}, {1.0f, 3.16994, 2e-4} };      // checker
        double worst = 0; for (auto r : bp) worst = std::max (worst, std::fabs (midLog2 (39, r.a) - r.v) / r.tol);
        bar (worst <= 1.0, "3a", "Bend + log2 r against Serum at 10 amounts: worst error %.2f of tolerance (e.g. 100 %% -> %.5f, Serum 3.16994)", worst, midLog2 (39, 1.0f));
    }
    {
        struct R { int m; float a; double v; } bm[] = { {40, 0.37f, -0.88041}, {40, 1.0f, -3.16991},
            {41, 0.0f, 3.16993}, {41, 0.07f, 2.43572}, {41, 0.31f, 0.90579}, {41, 0.69f, -0.90578}, {41, 0.93f, -2.43569}, {41, 1.0f, -3.16991} };
        double worst = 0; for (auto r : bm) worst = std::max (worst, std::fabs (midLog2 (r.m, r.a) - r.v));
        bar (worst <= 2e-4, "3b", "Bend − and Bend +/- (low half = Bend +) against Serum at 8 amounts: worst log2 r error %.1e (tol 2e-4); 41 @ 7 %% -> %.5f",
             worst, midLog2 (41, 0.07f));
    }
    {
        struct K { int m; float a; double v; double tol; } kn[] = {
            {42, 0.13f, 0.448, 2e-4}, {42, 0.37f, 0.352, 2e-4}, {42, 0.62f, 0.252, 2e-4}, {42, 0.88f, 0.148, 2e-4}, {42, 1.0f, 0.100, 2e-4},
            {43, 0.37f, 0.6478, 5e-4}, {43, 1.0f, 0.8997, 5e-4},
            {44, 0.0f, 0.1, 1e-6}, {44, 0.25f, 0.3, 1e-6}, {44, 0.75f, 0.7, 1e-6}, {44, 1.0f, 0.9, 1e-6} };
        double worst = 0; for (auto r : kn) worst = std::max (worst, std::fabs (knee (r.m, r.a) - r.v) / r.tol);
        auto slope = [] (int m, float a, double x) { const double h = 1e-7; return (W (m, a, x + h) - W (m, a, x - h)) / (2 * h); };
        struct S2 { float a; double pre, post; } sl[] = { {0.62f, 1.9848, 0.6682}, {0.88f, 3.3784, 0.5870}, {1.0f, 5.0000, 0.5558} };
        double sworst = 0; for (auto s : sl) { const double m = knee (42, s.a);
            sworst = std::max ({ sworst, std::fabs (slope (42, s.a, 0.5 * m) - s.pre), std::fabs (slope (42, s.a, 0.5 * (1 + m)) - s.post) }); }
        bar (worst <= 1.0 && sworst <= 2e-3 && knee (42, 0.5f) < 0.5, "3c", "Asym knees against Serum at 11 amounts: worst %.2f of tolerance; slopes worst %.1e; "
             "Asym + moves mid-table EARLIER (knee %.3f at 50 %%)", worst, sworst, knee (42, 0.5f));
    }
    {
        int mism = 0, bins = 0;
        for (float a : { 0.13f, 0.25f, 0.37f, 0.5f, 0.62f, 0.75f, 0.88f, 1.0f, 0.07f, 0.93f })
            for (int b = 0; b < 4952; ++b, ++bins)
            {
                const double x = (b + 0.5) / 4952.0;
                float w; W (45, a, x, &w);
                const bool inv = (x >= 2.0 * (double) a - 1.0 && x < 2.0 * (double) a) && a < 1.0f;
                if ((w < 0.0f) != inv) ++mism;
            }
        bar (mism == 0, "3d", "Flip is inverted on [2a − 1, 2a) ∩ [0, 1): %d of %d bins off (Serum: 0 of 13,056)", mism, bins);
    }
    {
        struct G { float a; double odd, even; } gg[] = { {0.0f, 2.0, 0.0}, {0.17f, 1.66, 0.34}, {0.5f, 1.0, 1.0}, {0.73f, 0.54, 1.46}, {1.0f, 0.0, 2.0} };
        double worst = 0;
        for (auto g : gg) { float g0 = 1, g1 = 0; NV::altOddEvenGains (46, g.a, 0, 0.0f, g0, g1);
            worst = std::max ({ worst, std::fabs ((g0 - g1) - g.odd), std::fabs ((g0 + g1) - g.even) }); }
        bar (worst <= 1e-5, "3e", "Odd/Even odd/even gains = 2 − 2a / 2a at 5 amounts (Serum's measured 2.000/0.000 … 0.000/2.000): worst %.1e", worst);
    }
    {
        double worst = 0; int bad = 0;
        for (int i = 0; i <= 100; ++i)
        {
            const float a = (float) i / 100.0f;
            const double s41 = 1.0 - 2.0 * (double) a;
            const double law39 = (a >= 1e-4f) ? (0.5 + 0.4 * a) / (0.5 - 0.4 * a) : 1.0;
            const double law41 = (s41 >= 1e-4) ? (0.5 + 0.4 * s41) / (0.5 - 0.4 * s41) : 1.0;
            worst = std::max ({ worst, std::fabs (NV::warpReadRate (39, a) / law39 - 1.0), std::fabs (NV::warpReadRate (41, a) / law41 - 1.0) });
            for (int m : { 40, 42, 43, 44, 45, 46 }) if (NV::warpReadRate (m, a) != 1.0) ++bad;
        }
        const double slope9 = std::exp2 (midLog2 (39, 1.0f));
        bar (worst < 1e-12 && bad == 0 && std::fabs (NV::warpReadRate (39, 1.0f) - slope9) < 1e-5, "3f",
             "read rate = r on Bend's + side (100 %% asks %.4fx = the kernel's own mid slope %.4f), 1x for Bend −, Asym, Flip, Odd/Even (%d off)",
             NV::warpReadRate (39, 1.0f), slope9, bad);
    }

    // ── [4] SHAPE LAWS ─────────────────────────────────────────────────────────────────────────────
    {
        int back = 0, ends = 0;
        for (int m = 39; m <= 44; ++m) for (int ia = 0; ia <= 40; ++ia)
        {
            const float a = (float) ia / 40.0f; double prev = -1.0;
            for (int i = 0; i <= 20000; ++i)
            {
                const double x = (double) i / 20001.0; double w = W (m, a, x);
                if (i == 20000 && w < 0.5) w += 1.0;              // the last point may land on the wrap (w = 1 -> 0)
                if (w < prev) ++back; prev = w;
            }
            if (W (m, a, 0.0) != 0.0) ++ends;
            if (m <= 41 && W (m, a, 0.5) != 0.5) ++ends;
        }
        bar (back == 0 && ends == 0, "4a", "Bend/Asym monotone over 6 modes x 41 amounts x 20001 phases: %d backward steps, %d fixed points moved", back, ends);
    }
    {
        double worst = 0;
        for (int m = 39; m <= 41; ++m) for (int ia = 0; ia <= 20; ++ia) for (int i = 1; i < 4000; ++i)
        { const double x = i / 4000.0; const float a = ia / 20.0f; worst = std::max (worst, std::fabs (W (m, a, 1.0 - x) - (1.0 - W (m, a, x)))); }
        bar (worst <= 1e-14, "4b", "Bend is point-symmetric, w(1 − x) = 1 − w(x): worst %.1e", worst);
    }
    {
        double worst = 0, worstDy = 0;
        for (int ia = 0; ia <= 100; ++ia) for (int i = 0; i < 4001; ++i)
        {
            const float a = ia / 100.0f; const double x = i / 4001.0;
            worst = std::max ({ worst, circ (W (39, a, W (40, a, x)), x), circ (W (40, a, W (39, a, x)), x) });
        }
        for (float a : { 0.25f, 0.5f, 0.75f, 1.0f }) for (int i = 0; i < 4001; ++i) { const double x = i / 4001.0; worstDy = std::max (worstDy, circ (W (39, a, W (40, a, x)), x)); }
        bar (worst <= 1e-12, "4c", "Bend − is the exact inverse of Bend + at every amount (closed-form 1/r): worst %.1e over 101 amounts, %.1e at the dyadic ones", worst, worstDy);
    }
    {
        double mirror = 0, invErr = 0;
        for (int ia = 0; ia <= 100; ++ia) for (int i = 1; i < 4001; ++i)
        { const float a = ia / 100.0f; const double x = i / 4001.0; mirror = std::max (mirror, std::fabs (W (43, a, x) - (1.0 - W (42, a, 1.0 - x)))); }
        for (int i = 0; i < 4001; ++i) { const double x = i / 4001.0; invErr = std::max (invErr, circ (W (42, 1.0f, W (43, 1.0f, x)), x)); }
        bar (mirror <= 1e-12 && invErr > 0.05, "4d", "Asym − = 1 − Asym +(1 − x) (Serum's point-mirror): worst %.1e; and it is NOT the inverse (composition off by %.3f)", mirror, invErr);
    }
    {
        int bad = 0;
        for (float a : { 0.25f, 0.5f, 0.75f, 1.0f }) for (double p : PS)
        {
            if (! same (W (41, 0.5f - a / 2, p), W (39, a, p))) ++bad;
            if (! same (W (41, 0.5f + a / 2, p), W (40, a, p))) ++bad;
            if (! same (W (44, 0.5f - a / 2, p), W (42, a, p))) ++bad;
            if (! same (W (44, 0.5f + a / 2, p), W (43, a, p))) ++bad;
        }
        bar (bad == 0, "4e", "the +/- mode at 50 ∓ a/2 %% IS the + / − mode at a, bit for bit, at the dyadic amounts: %d differ", bad);
    }

    // ── [5] CONTINUITY ─────────────────────────────────────────────────────────────────────────────
    {
        double worst = 0, wrap = 0; int worstMode = 0;
        for (int m = 39; m <= 44; ++m)
        {
            for (int i = 0; i < 10000; ++i)
            {
                const float a0 = i / 10000.0f, a1 = (i + 1) / 10000.0f;
                for (int xi = 0; xi < 257; ++xi) { const double x = xi / 257.0; const double d = circ (W (m, a0, x), W (m, a1, x)); if (d > worst) { worst = d; worstMode = m; } }
            }
            for (int ia = 0; ia <= 20; ++ia) wrap = std::max (wrap, circ (W (m, ia / 20.0f, std::nextafter (1.0, 0.0)), W (m, ia / 20.0f, 0.0)));
        }
        int flipWorst = 0;
        for (int i = 0; i < 10000; ++i)
        {
            int ch = 0;
            for (int b = 0; b < 4952; ++b) { float w0, w1; const double x = (b + 0.5) / 4952.0; W (45, i / 10000.0f, x, &w0); W (45, (i + 1) / 10000.0f, x, &w1); if ((w0 < 0) != (w1 < 0)) ++ch; }
            flipWorst = std::max (flipWorst, ch);
        }
        double gWorst = 0; float pg0 = 1, pg1 = 0; NV::altOddEvenGains (46, 0.0f, 0, 0.0f, pg0, pg1);
        for (int i = 1; i <= 10000; ++i) { float g0 = 1, g1 = 0; NV::altOddEvenGains (46, i / 10000.0f, 0, 0.0f, g0, g1); gWorst = std::max ({ gWorst, (double) std::fabs (g0 - pg0), (double) std::fabs (g1 - pg1) }); pg0 = g0; pg1 = g1; }
        bar (worst <= 1e-3 && wrap <= 1e-6 && flipWorst <= 4 && gWorst <= 2.1e-4, "5",
             "a 1e-4 knob step moves a map at most %.1e of a cycle (mode %d), Flip %d of 4952 bins, Odd/Even's gains %.1e; across the wrap %.1e",
             worst, worstMode, flipWorst, gWorst, wrap);
    }

    // ── [6] ROBUSTNESS ─────────────────────────────────────────────────────────────────────────────
    {
        std::mt19937_64 rng (0x5EEDA17u);
        std::uniform_real_distribution<double> U (0.0, 1.0);
        long nonFinite = 0, outRange = 0, winBad = 0, subn = 0, pmoved = 0; const long N = 10000000;
        double hist[2] = { -1.0, -1.0 };
        for (long n = 0; n < N; ++n)
        {
            const int m = 39 + (int) (U (rng) * 8.0);
            const double r = U (rng);
            float a = (float) U (rng);
            if (r < 0.02) a = 0.0f; else if (r < 0.04) a = 1.0f; else if (r < 0.06) a = 0.5f; else if (r < 0.08) a = 1e-40f; else if (r < 0.10) a = 0.5f + 1e-7f;
            const double p = -3.0 + 6.0 * U (rng);
            double* h = &hist[n & 1]; if (U (rng) < 0.01) *h = (U (rng) < 0.5) ? -1.0 : U (rng);
            float win = 1.0f; bool sk = false;
            const double w = NV::applyPhaseWarp (m, a, p, win, sk, (float) U (rng), nullptr, (U (rng) < 0.5) ? h : nullptr);
            if (! std::isfinite (w) || ! std::isfinite (win)) ++nonFinite;
            if (m <= 44 && ! same (w, p) && (w < 0.0 || w >= 1.0)) ++outRange;
            if (m >= 45 && ! same (w, p)) ++pmoved;
            if (std::fabs (win) > 1.0f) ++winBad;
            if (std::fpclassify (w) == FP_SUBNORMAL || std::fpclassify (win) == FP_SUBNORMAL) ++subn;
        }
        bar (nonFinite == 0 && outRange == 0 && winBad == 0 && subn == 0 && pmoved == 0, "6",
             "%ld M random calls: %ld non-finite, %ld maps outside [0, 1), %ld |window| > 1, %ld subnormal out, %ld Flip/Odd-Even phases moved",
             N / 1000000, nonFinite, outRange, winBad, subn, pmoved);
    }

    // ── [7] ALIASING AT THE EXTREMES ───────────────────────────────────────────────────────────────
    buildTables();
    const int C6 = 1429, C7 = 2857;       // primes: 1429/65536·48k = 1046.6 Hz, 2857 -> 2092.6 Hz
    auto oo = [&] (tw::Wavetable& t, Slot a, Slot b, int k, RenderOpt o, float* mw = nullptr)
    { gWt = &t; const double v = oohrDb (spectrum (render (a, b, k, o, mw), k)); gWt = &gSaw; return v; };
    RenderOpt ON, HARD, BASE; HARD.blep = false; BASE.readRate = false;
    if (std::getenv ("AW_EXPLORE"))
    {
        const char* tn[3] = { "sine", "saw", "square" }; tw::Wavetable* tt[3] = { &gSine, &gSaw, &gSquare };
        for (int ti = 0; ti < 3; ++ti) for (int k : { C6, C7 })
        {
            std::printf ("  EXPLORE %-6s %s  Flip blep/hard:", tn[ti], k == C6 ? "C6" : "C7");
            for (float a : { 0.1f, 0.2f, 0.25f, 0.3f, 0.37f, 0.6f, 0.7f, 0.75f, 0.9f })
                std::printf (" %.0f%% %.1f/%.1f", a * 100, oo (*tt[ti], {45, a}, {}, k, ON), oo (*tt[ti], {45, a}, {}, k, HARD));
            RenderOpt fo = ON, fh = HARD; fo.fmIndex = fh.fmIndex = 0.35; fo.fmRatio = fh.fmRatio = 1.0;
            std::printf ("\n          sync %.1f/%.1f  fm %.1f/%.1f  mirror %.1f/%.1f\n",
                         oo (*tt[ti], {2, 0.37f}, {45, 0.3f}, k, ON), oo (*tt[ti], {2, 0.37f}, {45, 0.3f}, k, HARD),
                         oo (*tt[ti], {45, 0.3f}, {}, k, fo), oo (*tt[ti], {45, 0.3f}, {}, k, fh),
                         oo (*tt[ti], {6, 1.0f}, {45, 0.3f}, k, ON), oo (*tt[ti], {6, 1.0f}, {45, 0.3f}, k, HARD));
            std::printf ("          dry %.1f | Bend+ 100 rate/base %.1f/%.1f  50 %.1f/%.1f | Bend- %.1f  Asym+ %.1f  Asym- %.1f | Skew100 %.1f  Sync100 %.1f\n",
                         oo (*tt[ti], {}, {}, k, ON), oo (*tt[ti], {39, 1.0f}, {}, k, ON), oo (*tt[ti], {39, 1.0f}, {}, k, BASE),
                         oo (*tt[ti], {39, 0.5f}, {}, k, ON), oo (*tt[ti], {39, 0.5f}, {}, k, BASE),
                         oo (*tt[ti], {40, 1.0f}, {}, k, ON), oo (*tt[ti], {42, 1.0f}, {}, k, ON), oo (*tt[ti], {43, 1.0f}, {}, k, ON),
                         oo (*tt[ti], {5, 1.0f}, {}, k, ON), oo (*tt[ti], {2, 1.0f}, {}, k, ON));
        }
    }
    {
        double worst = 1e9;
        for (int k : { C6, C7 }) for (float a : { 0.1f, 0.2f, 0.3f, 0.37f, 0.6f, 0.7f, 0.9f })
            worst = std::min (worst, oo (gSine, {45, a}, {}, k, HARD) - oo (gSine, {45, a}, {}, k, ON));
        bar (worst >= 12.0, "7a", "Flip on a SINE table, C6 + C7, 7 amounts: band-limited edges beat hard edges by >= %.1f dB of OOHR (need >= 12; 37 %% at C6: %.1f vs %.1f dB)",
             worst, oo (gSine, {45, 0.37f}, {}, C6, ON), oo (gSine, {45, 0.37f}, {}, C6, HARD));
    }
    {
        double worst = -1e9;
        for (tw::Wavetable* t : { &gSaw, &gSquare }) for (int k : { C6, C7 }) for (float a : { 0.1f, 0.2f, 0.25f, 0.3f, 0.37f, 0.6f, 0.7f, 0.75f, 0.9f })
            worst = std::max (worst, oo (*t, {45, a}, {}, k, ON) - oo (*t, {45, a}, {}, k, HARD));
        bar (worst <= 1.0, "7a2", "Flip on the SAW and SQUARE (a table jump ON a flip edge = a fold, not a jump), C6 + C7, 9 amounts: worst %+.1f dB against hard edges (need <= +1)", worst);
    }
    {
        double worst = -1e9; float mw = 0, m1 = 0;
        RenderOpt fo = ON, fh = HARD; fo.fmIndex = fh.fmIndex = 0.35; fo.fmRatio = fh.fmRatio = 1.0;
        for (tw::Wavetable* t : { &gSine, &gSaw, &gSquare }) for (int k : { C6, C7 })
        {
            worst = std::max (worst, oo (*t, {2, 0.37f}, {45, 0.30f}, k, ON, &m1) - oo (*t, {2, 0.37f}, {45, 0.30f}, k, HARD)); mw = std::max (mw, m1);
            worst = std::max (worst, oo (*t, {45, 0.30f}, {}, k, fo, &m1) - oo (*t, {45, 0.30f}, {}, k, fh)); mw = std::max (mw, m1);
        }
        const double sg = oo (gSine, {2, 0.37f}, {45, 0.30f}, C6, HARD) - oo (gSine, {2, 0.37f}, {45, 0.30f}, C6, ON);
        const double fg = oo (gSine, {45, 0.30f}, {}, C6, fh) - oo (gSine, {45, 0.30f}, {}, C6, fo);
        bar (worst <= 1.0 && mw <= 1.0f, "7b", "Flip after Sync R = 3.25 and on an FM carrier (index 0.35 — it runs backwards), 3 tables x C6/C7: worst %+.1f dB vs hard edges "
             "(need <= +1); on the sine %.1f / %.1f dB cleaner; |window| max %.3f", worst, sg, fg, mw);
    }
    {
        const double g6 = oo (gSine, {6, 1.0f}, {45, 0.30f}, C6, HARD) - oo (gSine, {6, 1.0f}, {45, 0.30f}, C6, ON);
        const double g7 = oo (gSine, {6, 1.0f}, {45, 0.30f}, C7, HARD) - oo (gSine, {6, 1.0f}, {45, 0.30f}, C7, ON);
        bar (std::min (g6, g7) >= 6.0, "7c", "Flip after Mirror 100 %% (a BACKWARD half-cycle) on the sine: %.1f dB (C6) / %.1f dB (C7) cleaner than hard edges (need >= 6)", g6, g7);
    }
    {
        double worst = 1e9; char txt[400] = ""; int off = 0;
        for (auto c : { std::pair<int, float> {39, 1.0f}, {39, 0.5f}, {41, 0.0f} })
            for (int k : { C6, C7 })
            {
                const double ch = oo (gSquare, {c.first, c.second}, {}, k, ON), bs = oo (gSquare, {c.first, c.second}, {}, k, BASE);
                worst = std::min (worst, bs - ch);
                off += std::snprintf (txt + off, sizeof txt - (size_t) off, " %d@%.0f%%%s %.1f/%.1f", c.first, c.second * 100, k == C6 ? "C6" : "C7", ch, bs);
            }
        bar (worst >= 6.0, "7d", "Bend + on the SQUARE, its read rate r vs the base mip (OOHR dB):%s — worst %.1f dB cleaner (need >= 6)", txt, worst);
    }
    {
        double worst = -1e9; char txt[200] = "";
        for (tw::Wavetable* t : { &gSine, &gSaw, &gSquare }) for (int k : { C6, C7 })
        {
            const double skew = oo (*t, {5, 1.0f}, {}, k, ON);
            for (auto c : { std::pair<int, float> {42, 1.0f}, {43, 1.0f}, {44, 0.0f}, {44, 1.0f} })
                worst = std::max (worst, oo (*t, {c.first, c.second}, {}, k, ON) - skew);
        }
        std::snprintf (txt, sizeof txt, "saw C6: Asym + %.1f vs Skew %.1f dB", oo (gSaw, {42, 1.0f}, {}, C6, ON), oo (gSaw, {5, 1.0f}, {}, C6, ON));
        bar (worst <= 3.0, "7e", "Asym ± at full strength on the base mip (Serum's choice), 3 tables x C6/C7: worst %+.1f dB over the SHIPPED Skew 100 %% (need <= +3); %s", worst, txt);
    }
    {
        RenderOpt r9 = ON; r9.forceRate = 9.0;
        std::printf ("  info  [7f] REPORTED, NOT GATED. Bend − 100 %% on the base mip (Serum's measured choice) — saw %.1f / %.1f dB (C6/C7), square %.1f / %.1f, sine %.1f / %.1f; "
                     "the shipped Skew 100 %% on the saw %.1f / %.1f; the max-slope mip Serum does NOT use would give the saw %.1f / %.1f. "
                     "Bend + on the saw, r vs base: %.1f/%.1f (C6), %.1f/%.1f (C7) — r is conservative there (the checker's open r^0.8-0.9 point).\n",
                     oo (gSaw, {40, 1.0f}, {}, C6, ON), oo (gSaw, {40, 1.0f}, {}, C7, ON), oo (gSquare, {40, 1.0f}, {}, C6, ON), oo (gSquare, {40, 1.0f}, {}, C7, ON),
                     oo (gSine, {40, 1.0f}, {}, C6, ON), oo (gSine, {40, 1.0f}, {}, C7, ON), oo (gSaw, {5, 1.0f}, {}, C6, ON), oo (gSaw, {5, 1.0f}, {}, C7, ON),
                     oo (gSaw, {40, 1.0f}, {}, C6, r9), oo (gSaw, {40, 1.0f}, {}, C7, r9),
                     oo (gSaw, {39, 1.0f}, {}, C6, ON), oo (gSaw, {39, 1.0f}, {}, C6, BASE), oo (gSaw, {39, 1.0f}, {}, C7, ON), oo (gSaw, {39, 1.0f}, {}, C7, BASE));
    }

    // ── [8] ODD/EVEN PARITY ────────────────────────────────────────────────────────────────────────
    {
        const int K = 101;    // 74 Hz: the full-band mip, ~320 harmonics under Nyquist
        RenderOpt o;
        auto par = [&] (Slot a, Slot b, bool even) { return parityDbr (spectrum (render (a, b, K, o), K), even); };
        const double ev0   = par ({46, 0.0f}, {}, true);
        const double od1   = par ({46, 1.0f}, {}, false);
        const double ev0s2 = par ({}, {46, 0.0f}, true);
        const double od1s2 = par ({}, {46, 1.0f}, false);
        const double ev0T0 = par ({46, 0.0f}, {11, 0.6f, 0.0f}, true);
        const double od1T5 = par ({46, 1.0f}, {11, 0.6f, 0.5f}, false);
        const double od1R  = par ({46, 1.0f}, {9, 0.7f}, false);
        const double ev0T5 = par ({46, 0.0f}, {11, 0.6f, 0.5f}, true);
        const double ev0R  = par ({46, 0.0f}, {9, 0.7f}, true);
        const double ev0B  = par ({46, 0.0f}, {39, 0.5f}, true);
        const bool exact = ev0 <= -100 && od1 <= -100 && ev0s2 <= -100 && od1s2 <= -100;
        const bool shaped = ev0T0 <= -100 && od1T5 <= -100 && od1R <= -100;
        const bool leaks = ev0T5 > -60 && ev0R > -60;
        bar (exact && shaped && leaks, "8",
             "0 %%: evens %.0f dBr (slot 2: %.0f) · 100 %%: odds %.0f (slot 2: %.0f) · behind Tube VAR 0 evens %.0f · even-only behind Tube VAR .5 %.0f, Rectify %.0f · "
             "EXPECTED LEAKS: odd-only behind Tube VAR .5 %.0f, Rectify %.0f · (Bend + after it, no parity claimed: %.0f)",
             ev0, ev0s2, od1, od1s2, ev0T0, od1T5, od1R, ev0T5, ev0R, ev0B);
        // the cascade: 25 % then 75 % -> s = −.5, +.5 -> 0.75 · y on BOTH parities
        const Spec dry = spectrum (render ({}, {}, K, o), K), cas = spectrum (render ({46, 0.25f}, {46, 0.75f}, K, o), K);
        double worst = 0; for (int h = 1; h <= 40; ++h) worst = std::max (worst, std::fabs (std::sqrt (cas.p[(size_t) (h * K)] / dry.p[(size_t) (h * K)]) - 0.75));
        bar (worst <= 1e-4, "8b", "both slots on 46 cascade (gains multiply): 25 %% then 75 %% is 0.75 x on every harmonic, worst %.1e", worst);
    }

    // ── [9] DC ─────────────────────────────────────────────────────────────────────────────────────
    {
        int bad = 0;
        for (float a : { 0.01f, 0.25f, 0.5f, 0.75f, 0.99f }) if (! NV::warpAmpNeedsDc (45, a, 0.0f)) ++bad;
        for (float a : { 0.0f, 0.0005f, 0.99995f, 1.0f })     if (NV::warpAmpNeedsDc (45, a, 0.0f)) ++bad;
        for (int m : { 39, 40, 41, 42, 43, 44, 46 }) for (int i = 0; i <= 20; ++i) if (NV::warpAmpNeedsDc (m, i / 20.0f, 0.7f)) ++bad;
        if (! NV::warpAmpNeedsDc (47, 0.5f, 0.0f)) ++bad;
        RenderOpt o; const auto y = render ({45, 0.25f}, {}, 101, o);
        double mean = 0, rms = 0; for (float v : y) { mean += v; rms += (double) v * v; } mean /= y.size(); rms = std::sqrt (rms / y.size());
        bar (bad == 0 && std::fabs (mean) > 0.1 * rms, "9", "Flip arms the 38 Hz blocker exactly while live (%d wrong answers); raw Flip 25 %% on the saw carries DC %.3f (%.0f %% of rms) — "
             "what the blocker removes; 39-44/46 never arm it, 47 keeps its default", bad, mean, 100 * std::fabs (mean) / rms);
    }

    // ── [10] MODES 0-38 (+47) UNTOUCHED ────────────────────────────────────────────────────────────
    {
        long diffs = 0, calls = 0;
        NV::DrawCurve dn; OV::DrawCurve dol;
        for (int i = 0; i < NV::kDrawPts; ++i) { const float v = 0.5f + 0.45f * std::sin (i * 0.21f) * (i / 128.0f); dn.pts[i] = dol.pts[i] = v; }
        dn.slope = dol.slope = 3.7f;
        std::vector<double> ps; for (int i = 0; i < 1024; ++i) ps.push_back (i / 1024.0 + 1.3e-5 * i);
        for (double p : { -0.25, 0.0, 1e-300, std::nextafter (1.0, 0.0), 1.0, 1.5 }) ps.push_back (p);
        std::vector<float> amts; for (int i = 0; i <= 64; ++i) amts.push_back (i / 64.0f); amts.push_back (0.001f); amts.push_back (0.0011f); amts.push_back (1e-6f);
        for (int m = 0; m <= 47; ++m)
        {
            if (m >= 39 && m <= 46) continue;
            for (float a : amts) for (float var : { 0.0f, 0.37f, 1.0f })
            {
                for (double p : ps)
                {
                    float wn = 0.75f, wo = 0.75f, wh = 0.75f; bool sn = false, so = false, sh = false; double hist = 0.4;
                    const double rn = NV::applyPhaseWarp (m, a, p, wn, sn, var, &dn);
                    const double rh = NV::applyPhaseWarp (m, a, p, wh, sh, var, &dn, &hist);
                    const double ro = OV::applyPhaseWarp (m, a, p, wo, so, var, &dol);
                    ++calls; if (! same (rn, ro) || ! same (rh, ro) || ! samef (wn, wo) || ! samef (wh, wo) || sn != so || sh != so || hist != 0.4) ++diffs;
                }
                for (int i = 0; i <= 400; ++i)
                {
                    const float s = -1.5f + 3.0f * i / 400.0f;
                    ++calls; if (! samef (NV::applyAmpWarp (m, a, s, var, &dn), OV::applyAmpWarp (m, a, s, var, &dol))) ++diffs;
                }
                ++calls; if (NV::warpAmpNeedsDc (m, a, var) != OV::warpAmpNeedsDc (m, a, var)) ++diffs;
                for (double sl : { 1.0, 7.3 }) { ++calls; if (! same (NV::warpReadRate (m, a, sl), OV::warpReadRate (m, a, sl))) ++diffs; }
            }
        }
        bar (diffs == 0, "10", "modes 0-38 and 47 against the reference file: %ld of %ld calls differ (phase map with/without a history, window, skip, amp, read rate, DC)",
             diffs, calls);
    }

    // ── [11] EVERY NEW MODE IS LIVE ────────────────────────────────────────────────────────────────
    {
        int dead = 0; char txt[256] = ""; int off = 0;
        for (int m = 39; m <= 44; ++m)
        {
            const float a = (m == 41 || m == 44) ? 0.15f : 0.7f;
            double mv = 0; for (int i = 0; i < 1000; ++i) mv = std::max (mv, circ (W (m, a, i / 1000.0), i / 1000.0));
            if (mv < 0.01) { ++dead; off += std::snprintf (txt + off, sizeof txt - (size_t) off, " %d", m); }
        }
        bool flipLive = false; for (int i = 0; i < 1000; ++i) { float w; W (45, 0.3f, i / 1000.0, &w); if (w < 0) flipLive = true; }
        float g0, g1; const bool oeLive = NV::altOddEvenGains (46, 0.2f, 0, 0.0f, g0, g1);
        if (! flipLive) { ++dead; off += std::snprintf (txt + off, sizeof txt - (size_t) off, " 45"); }
        if (! oeLive)   { ++dead; off += std::snprintf (txt + off, sizeof txt - (size_t) off, " 46"); }
        bar (dead == 0, "11", "every mode 39-46 changes the sound off its dry point%s%s", dead ? " — SILENT:" : "", txt);
    }

    std::printf ("\n  %d passed, %d FAILED", gPass, gFail);
    if (! gRed.empty()) { std::printf ("   RED:"); for (auto& r : gRed) std::printf (" [%s]", r.c_str()); }
    std::printf ("\n");
    return gFail ? 1 : 0;
}
