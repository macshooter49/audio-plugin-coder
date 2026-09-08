// ════════════════════════════════════════════════════════════════════════════
//  BlendEngine offline harness (Pattern A — NOT in CMakeLists / has its own main)
//
//  Build + run from the plugin dir:
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/BlendEngine_test.cpp -o /tmp/be && /tmp/be
//
//  Proves: identity (blend(A,A)==A), endpoint fidelity (morph 0/1 ≈ pure A/B),
//  OPTIMAL-TRANSPORT body morph (220+440 → mass BETWEEN, not a crossfade),
//  pitch-lock (no detuned shit), long+short length law, layer-knob audibility
//  (night-and-day), DICE determinism, finiteness/normalization/declick.
// ════════════════════════════════════════════════════════════════════════════
#include "BlendEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>

using tw::BlendEngine;
using tw::BlendParams;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what)
{ ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

static constexpr double SR = 48000.0;

// tonal one-shot: f0 + 3 harmonics, exponential decay
static void makeTone (std::vector<float>& v, int n, float f0, float decay = 3.f, uint32_t seed = 1)
{
    v.resize ((size_t) n);
    uint32_t x = seed ? seed : 1u;
    auto rnd = [&x] { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (float) ((x & 0xFFFFFF) / 8388608.0 - 1.0); };
    float ph[4] = { rnd() * 3.14f, rnd() * 3.14f, rnd() * 3.14f, rnd() * 3.14f };
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) SR;
        const float env = std::exp (-decay * t);
        float s = 0.f;
        const float amps[4] = { 1.f, 0.5f, 0.3f, 0.2f };
        for (int h = 0; h < 4; ++h)
            s += amps[h] * std::sin (2.f * 3.14159265f * f0 * (float) (h + 1) * t + ph[h]);
        v[(size_t) i] = env * s * 0.2f;
    }
}

// clicky drum-ish shot: noise burst + low thump
static void makeDrum (std::vector<float>& v, int n, uint32_t seed = 7)
{
    v.resize ((size_t) n);
    uint32_t x = seed;
    auto rnd = [&x] { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (float) ((x & 0xFFFFFF) / 8388608.0 - 1.0); };
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) SR;
        const float burst = std::exp (-90.f * t) * rnd() * 0.8f;
        const float thump = std::exp (-25.f * t) * std::sin (2.f * 3.14159265f * (55.f + 40.f * std::exp (-60.f * t)) * t) * 0.7f;
        v[(size_t) i] = burst + thump;
    }
}

static double rms (const std::vector<float>& v)
{ double s = 0; for (float x : v) s += (double) x * x; return std::sqrt (s / std::max<size_t> (1, v.size())); }

static double corr (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size());
    double sab = 0, sa = 0, sb = 0;
    for (size_t i = 0; i < n; ++i) { sab += (double) a[i] * b[i]; sa += (double) a[i] * a[i]; sb += (double) b[i] * b[i]; }
    return (sa > 1e-12 && sb > 1e-12) ? sab / std::sqrt (sa * sb) : 0.0;
}

// Goertzel magnitude at frequency f
static double goertzel (const std::vector<float>& v, double f, int n = -1)
{
    if (n < 0 || n > (int) v.size()) n = (int) v.size();
    const double w = 2.0 * 3.14159265358979 * f / SR, c = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) { s0 = (double) v[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (0.5 * n);
}

// dominant f0 via autocorrelation (test-side, independent of the engine's detector)
static double domF0 (const std::vector<float>& v)
{
    const int win = std::min ((int) v.size(), 8192);
    const int minLag = (int) (SR / 1200.0), maxLag = (int) (SR / 40.0);
    double bestV = 0; int bestLag = 0;
    for (int lag = minLag; lag <= maxLag && lag < win / 2; ++lag)
    {
        double ac = 0, e = 0;
        for (int i = 0; i < win - lag; ++i) { ac += (double) v[(size_t) i] * v[(size_t) (i + lag)]; e += (double) v[(size_t) i] * v[(size_t) i]; }
        const double nv = (e > 1e-12) ? ac / e : 0.0;
        if (nv > bestV) { bestV = nv; bestLag = lag; }
    }
    return bestLag > 0 ? SR / bestLag : 0.0;
}

int main()
{
    const int N = 48000;   // 1 s shots

    // ── identity: blend(A,A) at centered knobs reproduces A ──
    {
        std::vector<float> a; makeTone (a, N, 220.f);
        BlendEngine be;
        be.analyze (a.data(), a.data(), N, SR, a.data(), a.data(), N, SR);
        check (be.isAnalyzed(), "identity: analyze succeeds");
        std::vector<float> oL, oR;
        BlendParams p;   // all centered, dice 0
        be.render (p, oL, oR);
        // normalize both to unit RMS before correlating (render normalizes level)
        check (corr (a, oL) > 0.985, "identity: blend(A,A) reproduces A (corr > .985)");
        bool finite = true; for (float x : oL) if (! std::isfinite (x)) finite = false;
        check (finite, "identity: output finite");
    }

    // ── endpoints: morph 0 ≈ A, morph 1 ≈ B ──
    {
        std::vector<float> a, b; makeTone (a, N, 220.f, 3.f, 2); makeDrum (b, N, 9);
        BlendEngine be;
        be.analyze (a.data(), a.data(), N, SR, b.data(), b.data(), N, SR);
        std::vector<float> oL, oR;
        BlendParams p; p.morph = 0.f; be.render (p, oL, oR);
        check (corr (a, oL) > 0.9, "endpoint: morph=0 is A");
        p.morph = 1.f; be.render (p, oL, oR);
        check (corr (b, oL) > 0.75, "endpoint: morph=1 is B");
        // and it's louder than a whisper — normalized up
        check (rms (oL) > 0.02, "endpoint: output level is UP (normalized)");
    }

    // ── OPTIMAL TRANSPORT: 220 + 440 at body 0.5 puts energy BETWEEN, not both ──
    {
        std::vector<float> a, b; makeTone (a, N, 220.f, 1.5f, 3); makeTone (b, N, 440.f, 1.5f, 4);
        BlendEngine be;
        be.analyze (a.data(), a.data(), N, SR, b.data(), b.data(), N, SR);
        // NOTE: pitch-lock would tune B to 220 — that's correct product behavior, but for
        // the transport proof we want distinct pitches: detune-lock only fires w/ both
        // voiced. Both ARE voiced here → B gets tuned to A. So instead prove transport by
        // sweeping BODY with dice=0 and checking the fundamental's ENERGY PATH: with a
        // crossfade the 220 partial only ever loses energy monotonically and no NEW
        // in-between partial appears; with transport, mid-morph mass sits at bins BETWEEN
        // the harmonic stacks of the (now-tuned) pair — which coincide. That collapses the
        // test, so instead run the transport check on an UNVOICED pair: two inharmonic
        // bell tones (no f0 detected → no tuning → distinct partials survive).
        std::vector<float> c ((size_t) N), d ((size_t) N);
        for (int i = 0; i < N; ++i)
        {
            const float t = (float) i / (float) SR, env = std::exp (-2.f * t);
            // inharmonic partial sets — detector reads them as unvoiced
            c[(size_t) i] = env * 0.3f * (std::sin (2.f * 3.14159265f * 500.f * t) + 0.6f * std::sin (2.f * 3.14159265f * 1310.f * t));
            d[(size_t) i] = env * 0.3f * (std::sin (2.f * 3.14159265f * 900.f * t) + 0.6f * std::sin (2.f * 3.14159265f * 2170.f * t));
        }
        // tuneLock=false isolates the transport itself (two pure-sine pairs are always
        // quasi-periodic at SOME lag, so the f0 detector may legally call them voiced —
        // and in-product the octave-folded lock is exactly what we want anyway).
        BlendEngine bi;
        bi.analyze (c.data(), c.data(), N, SR, d.data(), d.data(), N, SR, false);
        check (! bi.tunedForTesting(), "transport: tuneLock=false leaves partials untouched");
        std::vector<float> oL, oR;
        BlendParams p; p.morph = 0.5f;
        bi.render (p, oL, oR);
        const double at500 = goertzel (oL, 500.0, 24000), at900 = goertzel (oL, 900.0, 24000);
        const double at700 = goertzel (oL, 700.0, 24000);   // the displacement midpoint
        check (at700 > at500 * 1.2 && at700 > at900 * 1.2,
               "transport: mid-morph mass lands BETWEEN source partials (700 > 500/900) — not a crossfade");
    }

    // ── pitch-lock: 220 vs 261.6 → both voiced → B tuned to A (no detuned shit) ──
    {
        std::vector<float> a, b; makeTone (a, N, 220.f, 2.f, 5); makeTone (b, N, 261.63f, 2.f, 6);
        BlendEngine be;
        be.analyze (a.data(), a.data(), N, SR, b.data(), b.data(), N, SR);
        check (be.tunedForTesting(), "pitch-lock: engaged for two voiced shots");
        check (std::fabs (be.f0AForTesting() - 220.f) < 6.f, "pitch-lock: A's f0 detected (~220)");
        check (std::fabs (be.f0BForTesting() - 261.6f) < 8.f, "pitch-lock: B's f0 detected (~261.6)");
        std::vector<float> oL, oR;
        BlendParams p; p.morph = 1.f;   // pure (tuned) B
        be.render (p, oL, oR);
        const double f0 = domF0 (oL);
        check (std::fabs (f0 - 220.0) < 6.0, "pitch-lock: pure-B output sits AT A's pitch (~220)");
    }

    // ── lengths: short pluck + long pad → output = long; both audible ──
    {
        std::vector<float> a, b; makeTone (a, 12000, 330.f, 8.f, 7); makeTone (b, 72000, 165.f, 1.f, 8);
        BlendEngine be;
        be.analyze (a.data(), a.data(), 12000, SR, b.data(), b.data(), 72000, SR);
        std::vector<float> oL, oR;
        BlendParams p; p.morph = 0.5f;
        be.render (p, oL, oR);
        check (be.outLenForTesting() >= 71000, "lengths: output keeps the longer shot's tail");
        double tail = 0; for (size_t i = 60000; i < oL.size(); ++i) tail += (double) oL[i] * oL[i];
        check (std::sqrt (tail / 12000.0) > 0.005, "lengths: the pad tail is audible in the blend");
        bool finite = true; for (float x : oL) if (! std::isfinite (x)) finite = false;
        check (finite, "lengths: mismatched lengths stay finite");
    }

    // ── night-and-day: each layer knob audibly changes the output ──
    {
        std::vector<float> a, b; makeTone (a, N, 220.f, 2.f, 10); makeDrum (b, N, 11);
        BlendEngine be;
        be.analyze (a.data(), a.data(), N, SR, b.data(), b.data(), N, SR);
        std::vector<float> base, oL, oR;
        BlendParams p; p.morph = 0.5f;
        be.render (p, base, oR);
        const char* names[4] = { "ATTACK", "BODY", "BREATH", "SCULPT" };
        for (int k = 0; k < 4; ++k)
        {
            BlendParams q; q.morph = 0.5f;
            (k == 0 ? q.attack : k == 1 ? q.body : k == 2 ? q.breath : q.sculpt) = 1.f;
            be.render (q, oL, oR);
            // difference energy vs base must be substantial (night-and-day)
            double diff = 0, ref = 0;
            const size_t n = std::min (base.size(), oL.size());
            for (size_t i = 0; i < n; ++i) { const double d = (double) oL[i] - base[i]; diff += d * d; ref += (double) base[i] * base[i]; }
            static char msg[64];
            std::snprintf (msg, sizeof (msg), "night-and-day: %s knob is audible", names[k]);
            check (diff > ref * 0.02, msg);
        }
        // DICE: deterministic + audible
        BlendParams d1; d1.morph = 0.5f; d1.dice = 0.7f;
        std::vector<float> r1, r2;
        be.render (d1, r1, oR);
        be.render (d1, r2, oR);
        check (corr (r1, r2) > 0.9999, "dice: same value → identical bake (deterministic)");
        double diff = 0, ref = 0;
        const size_t n = std::min (base.size(), r1.size());
        for (size_t i = 0; i < n; ++i) { const double d = (double) r1[i] - base[i]; diff += d * d; ref += (double) base[i] * base[i]; }
        check (diff > ref * 0.02, "dice: rolling the dice audibly changes the blend");
        BlendParams d2; d2.morph = 0.5f; d2.dice = 0.31f;
        be.render (d2, r2, oR);
        check (corr (r1, r2) < 0.999, "dice: different value → different blend");
    }

    // ── drums + tone: unvoiced side skips tuning, still blends clean ──
    {
        std::vector<float> a, b; makeDrum (a, 24000, 13); makeTone (b, 24000, 440.f, 4.f, 14);
        BlendEngine be;
        be.analyze (a.data(), a.data(), 24000, SR, b.data(), b.data(), 24000, SR);
        check (be.isAnalyzed(), "drum+tone: analyzes");
        std::vector<float> oL, oR;
        BlendParams p; p.morph = 0.5f; p.attack = 0.f; p.body = 1.f;   // drum attack + tone body
        be.render (p, oL, oR);
        bool finite = true; for (float x : oL) if (! std::isfinite (x)) finite = false;
        check (finite, "drum+tone: finite");
        check (rms (oL) > 0.02, "drum+tone: audible");
        // declick: edges ramp from ~0
        check (std::fabs (oL[0]) < 1e-3f && std::fabs (oL[oL.size() - 1]) < 1e-3f, "declick: edges at ~0");
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
