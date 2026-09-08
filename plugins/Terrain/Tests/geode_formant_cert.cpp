// ════════════════════════════════════════════════════════════════════════════
//  geode_formant_cert.cpp — RESYNTH FORMANT certification (Pattern A — NOT in CMakeLists).
//
//    shipped:  c++ -std=c++17 -O2 -Wall -Wextra -ISource Tests/geode_formant_cert.cpp -o /tmp/gfc && /tmp/gfc
//    proto:    c++ -std=c++17 -O2 -Wall -Wextra -I<scratch>/rs2-formant/proto Tests/geode_formant_cert.cpp -o /tmp/gfc && /tmp/gfc
//
//  THE PROPERTY (Max: "it's supposed to sound really formant-shifted"): FORMANT must slide the
//  spectral ENVELOPE (the vowel humps) up/down by the intended ratio while every partial FREQUENCY
//  and the played fundamental stay put, and it must not be a volume knob in disguise.
//
//  Modelled on Tests/geode_shape_cert.cpp (same analyser, same dF/dH hearing metric, same sources
//  A/B/C) plus ONE formant-rich source:
//    V  vowel — 40 harmonics, −6 dB/oct glottal roll-off × three tract resonances at ratios
//       3 / 8 / 16 (C4: 785 / 2093 / 4186 Hz), +24 / +20 / +16 dB over a −6 dB/oct base, 0.18 oct wide —
//       one hump per octave-ish so a ±1-octave slide lands each hump on a resolvable harmonic (6/16/32, 1.5/4/8).
//
//  MEASURED per source × formant ∈ {0, .25, .75, 1} against formant = 0.5:
//    dF          the house hearing metric (dB re fundamental → a pure gain change reads ~0)
//    ΔRMS        the gain change (dB) — a formant knob that is only ΔRMS duplicates the volume knob
//    ΔC          log2 spectral centroid shift of the harmonic peaks (octaves) — the envelope moved?
//    humps       on V: the three envelope humps, located as local maxima of the harmonic-peak table
//                (prominence ≥ 6 dB), vs their EXPECTED positions r_k·2^(2·(x−.5))
//    fund        McLeod NSDF fundamental (GeodeEngine_test.cpp idiom): NOTHING DETUNES
//    Δratio      max |ratio(x) − ratio(.5)| over live slots straight from the engine bank: 0 or bust
//
//  BARS (each with a -D mutation that turns it red — see the tail of main):
//    F1 audible      V: dF(1 vs .5) ≥ 6 and dF(0 vs .5) ≥ 6                       (shipped RED · -DGEODE_FORMANT_OLD_ENV)
//    F2 humps slide  V: every hump whose target is ≥ harmonic 2 lands within ±0.25 oct of r_k·s at x∈{0,.25,.75,1}   (shipped RED · -DGEODE_FORMANT_OLD_ENV)
//    F3 pitch holds  all sources, all x: Δratio == 0 and the ratio-1 slot alive; harmonic sources (A/C/V) stay periodic at C4
//                    (NSDF at the C4 lag ≥ 0.9); B is INHARMONIC by construction (its NSDF reads 266-267 Hz at every x on the
//                    shipped code too) so it is held to its own x=0.5 periodicity − 0.15                (-DGEODE_MUT_FORMANT_PITCH)
//    F4 not volume   all sources, all x: |ΔRMS| ≤ 1.5 dB                           (shipped RED · -DGEODE_NO_FORMANT_LEVEL)
//    F5 identity     formant=0.5 bit-identical to the SHIPPED engine on A/B/C/V     (-DGEODE_MUT_FORMANT0)
//    F6 amount       V: mean hump displacement within ±0.15 oct of log2(s) at every x (the knob moves the envelope by what it says)   (shipped RED · -DGEODE_FORMANT_OLD_ENV)
//    F7 contrast     V at x=1: hump1→hump2 peak-to-valley within 6 dB of its x=0.5 value (the humps keep their DEPTH; a ±12 dB
//                    gain clamp cannot lift a valley partial onto a 24 dB hump top — measured 30.4 → 17.6 dB)   (-DGEODE_FORMANT_CLAMP12)
//    F8 thinned      V under a shared partial budget of 24 (quality 0.8 → no bitrate quantisation; the G3 mechanism): F2 still holds —
//                    the shifted humps need LIVE carriers, so the governor's thin must come AFTER the shift   (shipped RED · -DGEODE_FORMANT_PRETHIN)
//                    (quality < 0.7 was the wrong lever: applyBitrate quantises to 2..48 levels and flattens the humps by design)
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

static constexpr int    kH        = 64;
static constexpr int    kSub      = 4;
static constexpr int    kBands    = kH * kSub + 2;
static constexpr double kFloorDb  = -90.0;
static constexpr int    kFFT      = 65536;
static constexpr double kDead     = 6.0;     // the shape-cert "knob does nothing" floor
static constexpr int    kNX       = 4;
static const float      XS[kNX]   = { 0.f, 0.25f, 0.75f, 1.f };
static const double     VOWEL_R[3] = { 3.0, 8.0, 16.0 };   // hump centres (ratio to fundamental)

struct Spec
{
    std::array<double, kH + 1> h {};      // dB re fundamental, index 1..64
    std::array<double, kBands> band {};   // dB re fundamental band
};

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
// V: the formant-rich source. 40 harmonics; glottal −6 dB/oct; three log-Gaussian humps.
static double vowelDb (double r)
{
    const double H[3] = { 24.0, 20.0, 16.0 }, W = 0.18;
    double db = -20.0 * std::log10 (r);
    for (int k = 0; k < 3; ++k) { const double d = std::log2 (r / VOWEL_R[k]) / W; db += H[k] * std::exp (-d * d); }
    return db;
}
static GeodeFrameStore makeV (float f0)
{
    std::vector<float> R, A;
    for (int n = 1; n <= 40; ++n) { R.push_back ((float) n); A.push_back ((float) std::pow (10.0, vowelDb (n) / 20.0)); }
    const float mx = *std::max_element (A.begin(), A.end());
    for (auto& a : A) a /= mx;
    return makeFrom (R, A, f0);
}

// ── render (as GeodeEngine_test.cpp renderFund) → steady state after a 100 ms skip ──────────
struct Render { std::vector<float> x; std::array<float, geode::kMaxPartials> ratio {}; std::array<float, geode::kMaxPartials> amp {}; int nP = 0; };
static Render renderSteady (GeodeParams p, GeodeFrameStore& st, double playHz, double sr)
{
    const int skip = (int) (sr * 0.1);
    const int N = skip + kFFT;
    std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
    GeodeEngine e; e.prepare (sr); e.setFrameStore (&st); e.setParams (p); e.noteOn (playHz, 999);
    const int blk = 256;
    for (int off = 0; off < N; off += blk)
    { const int m = std::min (blk, N - off); e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], m); }
    Render out; out.x.assign (L.begin() + skip, L.end());
    out.ratio = e.wr_.ratio; out.amp = e.wr_.amp; out.nP = e.wr_.nPartials;
    return out;
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

// McLeod-style first-peak NSDF (GeodeEngine_test.cpp)
static double estFund (const float* x, int n, double sr)
{
    double e0 = 0; for (int i = 0; i < n; ++i) e0 += (double) x[i] * x[i]; if (e0 < 1e-9) return 0;
    int minLag = (int) (sr / 2000.0), maxLag = (int) (sr / 50.0);
    std::vector<double> nsdf ((size_t) maxLag + 1, 0.0);
    double best = 0;
    for (int lag = minLag; lag < maxLag; ++lag)
    {
        double ac = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) { ac += (double) x[i] * x[i + lag]; e1 += (double) x[i] * x[i]; e2 += (double) x[i + lag] * x[i + lag]; }
        double v = (e1 + e2 > 1e-9) ? 2 * ac / (e1 + e2) : 0; nsdf[(size_t) lag] = v; if (v > best) best = v;
    }
    double thr = 0.85 * best;
    for (int lag = minLag + 1; lag < maxLag - 1; ++lag)
        if (nsdf[(size_t) lag] >= thr && nsdf[(size_t) lag] >= nsdf[(size_t) (lag - 1)] && nsdf[(size_t) lag] >= nsdf[(size_t) (lag + 1)])
        {
            double ym1 = nsdf[(size_t) (lag - 1)], y0 = nsdf[(size_t) lag], y1 = nsdf[(size_t) (lag + 1)];
            double den = ym1 - 2 * y0 + y1, d = (std::fabs (den) > 1e-12) ? 0.5 * (ym1 - y1) / den : 0;
            return sr / (lag + d);
        }
    return 0;
}

// normalised square-difference at ONE lag (McLeod): 1.0 = perfectly periodic at that lag
static double nsdfAt (const float* x, int n, double lagS)
{
    const int lag = (int) std::lround (lagS);
    double ac = 0, e1 = 0, e2 = 0;
    for (int i = 0; i + lag < n; ++i) { ac += (double) x[i] * x[i + lag]; e1 += (double) x[i] * x[i]; e2 += (double) x[i + lag] * x[i + lag]; }
    return (e1 + e2 > 1e-9) ? 2 * ac / (e1 + e2) : 0;
}
static double rmsOf (const std::vector<float>& x) { double e = 0; for (float v : x) e += (double) v * v; return std::sqrt (e / (double) x.size()); }
// log2 spectral centroid of the harmonic PEAKS (amplitude-weighted), octaves above the fundamental
static double centroidOct (const Spec& s)
{
    double num = 0, den = 0;
    for (int n = 1; n <= kH; ++n) { const double a = std::pow (10.0, s.h[(size_t) n] / 20.0); num += a * std::log2 ((double) n); den += a; }
    return den > 0 ? num / den : 0;
}
// envelope humps = local maxima of h[n] with ≥ 6 dB prominence over the nearer valley on each side
struct Hump { int n; double db; double prom; };
static std::vector<Hump> humps (const Spec& s, int nMax)
{
    std::vector<Hump> out;
    for (int n = 1; n <= nMax; ++n)
    {
        const double v = s.h[(size_t) n];
        if (n > 1 && s.h[(size_t) (n - 1)] > v) continue;
        if (n < nMax && s.h[(size_t) (n + 1)] >= v) continue;
        double vl = v, vr = v;
        for (int k = n - 1; k >= 1 && s.h[(size_t) k] < s.h[(size_t) (k + 1)] + 1e-9; --k) vl = s.h[(size_t) k];
        for (int k = n + 1; k <= nMax && s.h[(size_t) k] < s.h[(size_t) (k - 1)] + 1e-9; ++k) vr = s.h[(size_t) k];
        const double prom = v - std::max (vl, vr);
        if (prom >= 6.0 || (n == 1 && v - vr >= 6.0)) out.push_back ({ n, v, prom });
    }
    return out;
}
// nearest detected hump (in octaves) to an expected ratio
static double humpErrOct (const std::vector<Hump>& hs, double expectR, int* atN)
{
    double best = 99; int bn = -1;
    for (const Hump& h : hs) { const double e = std::fabs (std::log2 ((double) h.n / expectR)); if (e < best) { best = e; bn = h.n; } }
    if (atN) *atN = bn;
    return best;
}
// hump-to-valley contrast: max of h over [lo,hi] minus min of h between the first two humps' windows
static double contrastDb (const Spec& s, double r1, double r2)
{
    const int a = std::max (1, (int) std::lround (r1)), b = std::min (kH, (int) std::lround (r2));
    double pk = -999, vl = 999;
    for (int n = a; n <= b; ++n) { pk = std::max (pk, s.h[(size_t) n]); vl = std::min (vl, s.h[(size_t) n]); }
    return pk - vl;
}
static std::uint64_t fnv (const std::vector<float>& x, size_t n0, size_t n)
{
    std::uint64_t h = 1469598103934665603ull;
    for (size_t i = n0; i < n0 + n; ++i) { std::uint32_t b; std::memcpy (&b, &x[i], 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8*k)) & 0xff; h *= 1099511628211ull; } }
    return h;
}

int main()
{
    const double sr = 48000.0, playHz = 261.63;   // C4, as GeodeEngine_test.cpp
    auto A = makeA(); auto B = makeB ((float) playHz); auto C = makeC ((float) playHz); auto V = makeV ((float) playHz);
    GeodeFrameStore* SRC[4] = { &A, &B, &C, &V }; const char* SN[4] = { "A default-48@1/n", "B sparse-inharm-10", "C dull-12@1/n2", "V vowel-40 (humps 3/8/16)" };

    std::printf ("=== RESYNTH FORMANT CERT (played C4 = %.2f Hz, %d-pt FFT, BH window) ===\n", playHz, kFFT);
    std::printf ("shift law: shiftMul = 2^((formant-0.5)*2)  → x=0: /2   x=.25: /1.41   x=.75: ×1.41   x=1: ×2\n");
    std::printf ("columns: dF = hearing distance vs x=.5 (dB re fund; a pure gain reads ~0) · ΔRMS = gain (dB) · ΔC = centroid shift (oct, want ≈ log2 shiftMul on a hump source)\n");

    // per-source, per-x measurements
    struct M { double dF, dRms, dC, fund, per, dRatio; bool fundAlive; Spec s; std::vector<Hump> hs; };
    M m[4][kNX]; Spec s5[4]; double c5[4]; double rms5[4]; double per5[4]; std::vector<Hump> hs5[4];
    int worstPerSrc = -1; double worstPer = 1.0, worstRatio = 0, bDrop = 0; bool allFundAlive = true;
    for (int si = 0; si < 4; ++si)
    {
        GeodeParams base; base.scan = 0.f; base.quality = 0.8f;
        GeodeParams p5 = base; p5.formant = 0.5f;
        Render r5 = renderSteady (p5, *SRC[si], playHz, sr);
        s5[si] = analyse (r5.x, sr, playHz); c5[si] = centroidOct (s5[si]); rms5[si] = rmsOf (r5.x); hs5[si] = humps (s5[si], 40);
        per5[si] = nsdfAt (&r5.x[(size_t) (kFFT / 2)], kFFT / 2, sr / playHz);
        std::printf ("\n=== SOURCE %s | quality=0.80 | x=.5 centroid %.2f oct, perC4 %.3f", SN[si], c5[si], per5[si]);
        if (si == 3) { std::printf (" | humps at n ="); for (auto& h : hs5[si]) std::printf (" %d(%.0fdB,prom %.0f)", h.n, h.db, h.prom); }
        std::printf (" ===\n");
        std::printf ("%-6s %7s %7s %7s %8s %6s %8s", "x", "dF", "ΔRMS", "ΔC", "fund", "perC4", "Δratio");
        if (si == 3) std::printf ("  | humps (want %.1f/%.1f/%.1f × shiftMul)", VOWEL_R[0], VOWEL_R[1], VOWEL_R[2]);
        std::printf ("\n");
        for (int xi = 0; xi < kNX; ++xi)
        {
            GeodeParams p = base; p.formant = XS[xi];
            Render r = renderSteady (p, *SRC[si], playHz, sr);
            M& mm = m[si][xi];
            mm.s = analyse (r.x, sr, playHz);
            mm.dF = dF (mm.s, s5[si]);
            mm.dRms = 20.0 * std::log10 (rmsOf (r.x) / rms5[si]);
            mm.dC = centroidOct (mm.s) - c5[si];
            mm.fund = estFund (&r.x[(size_t) (kFFT / 2)], kFFT / 2, sr);
            mm.per  = nsdfAt (&r.x[(size_t) (kFFT / 2)], kFFT / 2, sr / playHz);
            mm.fundAlive = false;
            for (int j = 0; j < r.nP; ++j) if (std::fabs (r.ratio[(size_t) j] - 1.f) < 1e-3f && r.amp[(size_t) j] > 0.f) mm.fundAlive = true;
            mm.dRatio = 0;
            for (int j = 0; j < std::min (r.nP, r5.nP); ++j)
                if (r.amp[(size_t) j] > 0.f && r5.amp[(size_t) j] > 0.f) mm.dRatio = std::max (mm.dRatio, (double) std::fabs (r.ratio[(size_t) j] - r5.ratio[(size_t) j]));
            mm.hs = humps (mm.s, 40);
            if (si == 1) bDrop = std::max (bDrop, per5[si] - mm.per);
            else if (mm.per < worstPer) { worstPer = mm.per; worstPerSrc = si; }
            worstRatio = std::max (worstRatio, mm.dRatio); allFundAlive = allFundAlive && mm.fundAlive;
            std::printf ("%-6.2f %7.2f %+7.2f %+7.2f %8.1f %6.3f %8.4f", XS[xi], mm.dF, mm.dRms, mm.dC, mm.fund, mm.per, mm.dRatio);
            if (si == 3)
            {
                const double sm = std::pow (2.0, (XS[xi] - 0.5) * 2.0);
                std::printf ("  |");
                for (int k = 0; k < 3; ++k) { int at = -1; const double e = humpErrOct (mm.hs, VOWEL_R[k] * sm, &at); std::printf (" want %.1f got n=%d (%+.2f oct)", VOWEL_R[k] * sm, at, at > 0 ? std::log2 (at / (VOWEL_R[k] * sm)) : 9.0); (void) e; }
            }
            std::printf ("\n");
        }
        if (si == 3)
        {
            std::printf ("harmonics 1..32 dB re fund, x = .5 | 0 | 1:\n");
            for (int n = 1; n <= 32; ++n) std::printf ("  %2d: %6.1f | %6.1f | %6.1f\n", n, s5[si].h[(size_t) n], m[si][0].s.h[(size_t) n], m[si][3].s.h[(size_t) n]);
        }
    }

    // ═══ THE BARS ═══
    int pass = 0, fail = 0; char ln[512];
    auto bar = [&] (bool good, const char* name, const std::string& detail)
    { good ? ++pass : ++fail; std::printf ("  %s  %s\n        %s\n", good ? "PASS" : "FAIL", name, detail.c_str()); };
    std::printf ("\n=== BARS ===\n");
    // F1 audible on V
    {
        const double d0 = m[3][0].dF, d1 = m[3][3].dF;
        std::snprintf (ln, sizeof ln, "V: dF(0 vs .5) = %.1f dB, dF(1 vs .5) = %.1f dB (limit ≥ %.0f) · for scale, A: %.1f / %.1f", d0, d1, kDead, m[0][0].dF, m[0][3].dF);
        bar (d0 >= kDead && d1 >= kDead, "[F1] AUDIBLE — on the vowel, FORMANT at 0 and 1 is ≥ 6 dB from 0.5 (the shape-cert DEAD floor)", ln);
    }
    // F2 humps slide
    {
        double worst = 0; std::string det;
        for (int xi = 0; xi < kNX; ++xi)
        {
            const double sm = std::pow (2.0, (XS[xi] - 0.5) * 2.0);
            for (int k = 0; k < 3; ++k)
            {
                if (VOWEL_R[k] * sm < 2.0) continue;   // a hump below harmonic 2 has no harmonic to sit on (1.5 → n=1/2 split)
                int at = -1; const double e = humpErrOct (m[3][xi].hs, VOWEL_R[k] * sm, &at);
                worst = std::max (worst, e);
                std::snprintf (ln, sizeof ln, "x=%.2f h%d want %.1f got %d (%.2f) · ", XS[xi], k + 1, VOWEL_R[k] * sm, at, e); det += ln;
            }
        }
        bar (worst <= 0.25, "[F2] THE HUMPS SLIDE — every resolvable vowel hump lands within ±0.25 oct of r·shiftMul at x = 0 .25 .75 1", det);
    }
    // F3 pitch holds
    {
        std::snprintf (ln, sizeof ln, "harmonic sources: worst periodicity at the C4 lag = %.3f (%s, want ≥ 0.9) · inharmonic B: worst drop from its own x=.5 (%.3f) = %.3f (limit 0.15) · ratio-1 slot alive everywhere: %s · max Δratio over live slots = %.5f (want 0)",
                       worstPer, worstPerSrc >= 0 ? SN[worstPerSrc] : "-", per5[1], bDrop, allFundAlive ? "yes" : "NO", worstRatio);
        bar (worstPer >= 0.9 && bDrop <= 0.15 && allFundAlive && worstRatio == 0.0, "[F3] NOTHING DETUNES — no partial frequency moves, the fundamental partial stays alive, harmonic sources stay periodic at C4, all x", ln);
    }
    // F4 not a volume knob
    {
        double worst = 0; std::string w;
        for (int si = 0; si < 4; ++si) for (int xi = 0; xi < kNX; ++xi)
            if (std::fabs (m[si][xi].dRms) > worst) { worst = std::fabs (m[si][xi].dRms); std::snprintf (ln, sizeof ln, "%s x=%.2f %+.2f dB", SN[si], XS[xi], m[si][xi].dRms); w = ln; }
        bar (worst <= 1.5, "[F4] NOT A VOLUME KNOB — RMS within ±1.5 dB of x=0.5, all sources, all x", "worst " + w);
    }
    // F5 identity at 0.5 (fingerprints taken from the SHIPPED header d677a1c through these stores)
    {
        const std::uint64_t SHIPPED[4] = { 0x99ad5041369bebaaull, 0x3c3fd0c462ea60bfull, 0x808ddbd9a0211a21ull, 0x9a40a618040a4344ull };
        bool same = true; std::string det;
        for (int si = 0; si < 4; ++si)
        {
            GeodeEngine e; e.prepare (sr); e.setFrameStore (SRC[si]);
            GeodeParams p; p.scan = 0.f; p.quality = 0.8f; p.formant = 0.5f; e.setParams (p); e.noteOn (playHz, 999);
            const int Nn = 8192; std::vector<float> L ((size_t) Nn, 0.f), R ((size_t) Nn, 0.f);
            for (int off = 0; off < Nn; off += 256) { e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], 256); e.postProcess (&L[(size_t) off], &R[(size_t) off], 256); }
            const std::uint64_t h = fnv (L, 4096, 4096);
            std::snprintf (ln, sizeof ln, "%c %016llx%s ", SN[si][0], (unsigned long long) h, h == SHIPPED[si] ? "" : "≠SHIPPED"); det += ln;
            same = same && (h == SHIPPED[si]);
        }
        bar (same, "[F5] FORMANT=0.5 IS BIT-IDENTICAL TO THE SHIPPED ENGINE — four fingerprints (A/B/C as the shape cert, + V)", det);
    }
    // F6 centroid
    {
        double worst = 0; std::string det;
        for (int xi = 0; xi < kNX; ++xi)
        {
            const double want = (XS[xi] - 0.5) * 2.0;   // log2 shiftMul
            double sum = 0; int cnt = 0;
            for (int k = 0; k < 3; ++k)
            {
                const double target = VOWEL_R[k] * std::pow (2.0, want);
                if (target < 2.0) continue;
                int at = -1; humpErrOct (m[3][xi].hs, target, &at);
                if (at > 0) { sum += std::log2 ((double) at / VOWEL_R[k]); ++cnt; }
            }
            const double got = cnt ? sum / cnt : 0.0;
            worst = std::max (worst, std::fabs (got - want));
            std::snprintf (ln, sizeof ln, "x=%.2f want %+.2f got %+.2f oct · ", XS[xi], want, got); det += ln;
        }
        bar (worst <= 0.15, "[F6] BY THE INTENDED AMOUNT — mean hump displacement within ±0.15 oct of log2(shiftMul) at every x", det);
    }
    // F7 contrast at x=1 (humps survive the slide) — measured over the hump-1 → hump-2 span at its shifted location (6..18)
    {
        const double k5 = contrastDb (s5[3], 3.0, 8.0), k1 = contrastDb (m[3][3].s, 6.0, 16.0), k0 = contrastDb (m[3][0].s, 1.5, 4.0);
        std::snprintf (ln, sizeof ln, "hump1→hump2 peak-to-valley: x=.5 %.1f dB (n 3..8) · x=1 %.1f dB (n 6..16) · x=0 %.1f dB (n 2..4)", k5, k1, k0);
        bar (k1 >= k5 - 6.0, "[F7] THE HUMPS KEEP THEIR DEPTH — at x=1 the shifted hump1→hump2 peak-to-valley is within 6 dB of its x=0.5 value", ln);
    }
    // F8 thinned bank — under a shared budget of 24 partials (quality 0.8: no bitrate quantisation) the humps still slide
    {
        GeodeParams base; base.scan = 0.f; base.quality = 0.8f;
        double worst = 0; std::string det; int aliveMax = 0;
        for (float x : { 0.f, 1.f })
        {
            GeodeParams p = base; p.formant = x;
            Render r;
            {   // renderSteady with a 24-partial shared budget (the G3 idiom: reset `used` every block)
                const int skip = (int) (sr * 0.1), N = skip + kFFT; int used = 0;
                std::vector<float> L ((size_t) N, 0.f), R ((size_t) N, 0.f);
                GeodeEngine e; e.prepare (sr); e.setFrameStore (&V); e.setPartialBudget (&used, 24); e.setParams (p); e.noteOn (playHz, 999);
                for (int off = 0; off < N; off += 256) { const int mN = std::min (256, N - off); used = 0; e.setParams (p); e.renderBlockAdd (&L[(size_t) off], &R[(size_t) off], mN); }
                r.x.assign (L.begin() + skip, L.end()); r.ratio = e.wr_.ratio; r.amp = e.wr_.amp; r.nP = e.wr_.nPartials;
                aliveMax = std::max (aliveMax, e.preparedActive());
            }
            const Spec s = analyse (r.x, sr, playHz);
            const auto hs = humps (s, 40);
            const double sm = std::pow (2.0, (x - 0.5) * 2.0);
            for (int k = 0; k < 3; ++k)
            {
                if (VOWEL_R[k] * sm < 2.0) continue;
                int at = -1; const double e = humpErrOct (hs, VOWEL_R[k] * sm, &at); worst = std::max (worst, e);
                std::snprintf (ln, sizeof ln, "x=%.0f hump%d want %.1f got %d · ", x, k + 1, VOWEL_R[k] * sm, at); det += ln;
            }
        }
        std::snprintf (ln, sizeof ln, "(live partials ≤ %d of 40) ", aliveMax); det = ln + det;
        bar (worst <= 0.25 && aliveMax <= 24, "[F8] …AND ON A THINNED BANK — a 24-partial shared budget on the 40-partial vowel: every hump still lands within ±0.25 oct", det);
    }

    std::printf ("\n  %s %d passed, %d failed\n", fail ? "❌" : "✅", pass, fail);
    return fail == 0 ? 0 : 1;
}
