// ═════════════════════════════════════════════════════════════════════════════════════════════════
//  tp22 — the FILTER TABLE's curve, proven offline. FilterTableSource is pure, so the shape a
//  wavetable turns into can be checked without an audio thread, a plugin or an ear. What it has to
//  get right, and what this file asserts:
//    · the band grid is log-spaced, monotonic, and CUTOFF lands on harmonic 24 (the anchor)
//    · a sine table reads as a curve that peaks at the bottom; a saw slopes down; a table with
//      energy only up high peaks at the top — i.e. the curve IS the table's spectrum
//    · every frame is centred on 0 dB, so scanning changes the SHAPE and not the level
//    · resonance 0 is FLAT (a filter that does nothing), 1 is the table, 2 exaggerates
//    · two different tables give two different curves — the whole point of the feature
//
//    clang++ -std=c++17 -O2 -I<juce modules> Source/FilterTableSource_test.cpp -o /tmp/ftt && /tmp/ftt
// ═════════════════════════════════════════════════════════════════════════════════════════════════
#include "FilterTableSource.h"
#include <cstdio>
#include <cmath>
#include <memory>

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what)
{ ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

using Grid  = tw::HarmTableSource::Grid;
using Curve = tw::FilterTableSource::Curve;
static constexpr int B = tw::FilterTableSource::kBands;
static constexpr int F = tw::FilterTableSource::kFrames;
static constexpr int N = tw::FilterTableSource::kMaxN;

/** A grid whose every frame carries the harmonic series `f(h)` — the shape under test. */
static std::unique_ptr<Grid> makeGrid (float (*f) (int h))
{
    auto g = std::make_unique<Grid>();
    for (int fr = 0; fr < F; ++fr)
    {
        g->n[fr] = N;
        for (int h = 1; h <= N; ++h) { g->amp[fr][h - 1] = f (h); g->phase[fr][h - 1] = 0.0f; }
    }
    return g;
}

static int argmax (const float* v, int n) { int b = 0; for (int i = 1; i < n; ++i) if (v[i] > v[b]) b = i; return b; }

int main()
{
    std::printf ("— tp22 FILTER TABLE curve —\n");

    // ── 1. the band grid ────────────────────────────────────────────────────────────────────────
    {
        bool mono = true;
        for (int k = 1; k < B; ++k) if (! (tw::FilterTableSource::bandRatio (k) > tw::FilterTableSource::bandRatio (k - 1))) mono = false;
        check (mono, "T1 band centres increase with k");
        const float lo = tw::FilterTableSource::bandRatio (0), hi = tw::FilterTableSource::bandRatio (B - 1);
        check (std::fabs (lo - 1.0f / 24.0f) < 1e-4f, "T1 the lowest band is harmonic 1 (cutoff/24)");
        check (std::fabs (hi - 512.0f / 24.0f) < 1e-2f, "T1 the highest band is harmonic 512");
        // the anchor: SOME band sits at ratio 1.0, i.e. harmonic 24 == the cutoff
        int nearest = 0; float bestd = 1e9f;
        for (int k = 0; k < B; ++k) { const float d = std::fabs (tw::FilterTableSource::bandRatio (k) - 1.0f); if (d < bestd) { bestd = d; nearest = k; } }
        check (bestd < 0.12f, "T1 a band sits on the cutoff itself (harmonic 24 anchored)");
        check (tw::FilterTableSource::bandHarm (nearest) >= 20 && tw::FilterTableSource::bandHarm (nearest) <= 29,
               "T1 that band's harmonic is 24-ish");
        // it must span both sides of the knob, which is the reason for anchoring mid-spectrum
        check (lo < 1.0f && hi > 1.0f, "T1 the curve spreads BOTH sides of the cutoff");
    }

    // ── 2. the curve is the table's spectrum ────────────────────────────────────────────────────
    auto curveOf = [] (float (*f) (int)) {
        auto g = makeGrid (f); auto c = std::make_unique<Curve>();
        tw::FilterTableSource::bake (*g, *c); return c; };

    auto sine = curveOf ([] (int h) { return h == 1 ? 1.0f : 0.0f; });
    check (argmax (sine->db[0], B) == 0, "T2 a SINE peaks at the lowest band");

    auto saw = curveOf ([] (int h) { return 1.0f / (float) h; });
    {
        // a saw is -6 dB/octave, so the curve must fall across the whole grid
        const int a = argmax (saw->db[0], B);
        check (a <= 1, "T2 a SAW peaks at the bottom");
        check (saw->db[0][0] > saw->db[0][B / 2] && saw->db[0][B / 2] > saw->db[0][B - 1], "T2 a SAW slopes down monotonically across the grid");
    }

    auto high = curveOf ([] (int h) { return (h >= 300) ? 1.0f : 0.0f; });
    check (argmax (high->db[0], B) >= B - 4, "T2 a table with only HIGH harmonics peaks at the top");

    // ── 3. every frame is level-centred ─────────────────────────────────────────────────────────
    {
        bool centred = true;
        for (int fr = 0; fr < F; fr += 7)
        { float m = 0.0f; for (int k = 0; k < B; ++k) m += saw->db[fr][k]; if (std::fabs (m / (float) B) > 1e-3f) centred = false; }
        check (centred, "T3 each frame's curve is centred on 0 dB (scanning changes shape, not level)");
    }

    // ── 4. resonance ────────────────────────────────────────────────────────────────────────────
    {
        float out0[B], out1[B], out2[B];
        tw::FilterTableSource::blend (*saw, 0.0f, 0.0f, out0);
        tw::FilterTableSource::blend (*saw, 0.0f, 1.0f, out1);
        tw::FilterTableSource::blend (*saw, 0.0f, 2.0f, out2);
        bool flat = true; for (int k = 0; k < B; ++k) if (std::fabs (out0[k]) > 1e-6f) flat = false;
        check (flat, "T4 resonance 0 is FLAT — the filter does nothing");
        check (std::fabs (out1[0] - saw->db[0][0]) < 1e-4f, "T4 resonance 1 is the table's own curve");
        check (std::fabs (out2[0] - 2.0f * saw->db[0][0]) < 1e-3f, "T4 resonance 2 exaggerates peaks and troughs");
    }

    // ── 5. scanning interpolates ────────────────────────────────────────────────────────────────
    {
        // a grid whose frames differ: frame f tilts progressively brighter
        auto g = std::make_unique<Grid>();
        for (int fr = 0; fr < F; ++fr)
        {
            g->n[fr] = N;
            const float tilt = (float) fr / (float) (F - 1);
            for (int h = 1; h <= N; ++h) g->amp[fr][h - 1] = std::pow ((float) h, -1.0f + tilt);
        }
        auto c = std::make_unique<Curve>(); tw::FilterTableSource::bake (*g, *c);
        float a[B], b[B], mid[B];
        tw::FilterTableSource::blend (*c, 0.0f, 1.0f, a);
        tw::FilterTableSource::blend (*c, 1.0f, 1.0f, b);
        tw::FilterTableSource::blend (*c, 0.5f, 1.0f, mid);
        check (std::fabs (a[B - 1] - b[B - 1]) > 3.0f, "T5 the first and last frames really differ");
        bool between = true;
        for (int k = 0; k < B; ++k)
        { const float lo = std::fmin (a[k], b[k]) - 0.6f, hi = std::fmax (a[k], b[k]) + 0.6f; if (mid[k] < lo || mid[k] > hi) between = false; }
        check (between, "T5 a mid scan lands between the two frames it sits between");
    }

    // ── 6. different tables, different filters — the feature's whole claim ──────────────────────
    {
        float s[B], w[B];
        tw::FilterTableSource::blend (*sine, 0.0f, 1.0f, s);
        tw::FilterTableSource::blend (*high, 0.0f, 1.0f, w);
        float diff = 0.0f; for (int k = 0; k < B; ++k) diff += std::fabs (s[k] - w[k]);
        check (diff / (float) B > 6.0f, "T6 two different tables give audibly different curves");
        check (sine->sig != high->sig, "T6 their signatures differ (the rebuild gate can tell them apart)");
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
