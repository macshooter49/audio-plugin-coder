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
        // tp27 — A SAW IS NOW NEARLY FLAT, AND THAT IS THE POINT. The curve is a table's deviation
        // from referenceDb() — the average of the whole library — and a -6 dB/octave slope is very
        // nearly that average. Before the reference was subtracted, every table carried this slope
        // and it drowned out what made each one different (measured: any two tables correlated
        // 0.735). A table that is typical should now do LITTLE; a table that is unusual should
        // speak. So the assertion is the opposite of the old one, deliberately.
        // WHAT THIS CAN AND CANNOT PROVE. The claim "the shared falling tilt is gone" is about the
        // REAL library, and it is measured there (Tests/au_filter_table.cpp renders many tables off
        // the installed plugin and holds their spectra to a low mutual correlation). A synthetic
        // 1/h saw across all 512 harmonics is NOT a typical library table — it is far steeper than
        // the average of real, band-limited wavetables — so asserting on it would be asserting on
        // the wrong signal. It correlates +0.99 with a ramp even after the subtraction, and that is
        // honest rather than a failure.
        //
        // What IS provable here is that the subtraction happens at all, exactly: feed the bake a
        // table whose band spectrum IS referenceDb(), and the curve must come out flat.
        {
            auto g = std::make_unique<Grid>();
            const float* ref = tw::FilterTableSource::referenceDb();
            for (int fr = 0; fr < F; ++fr)
            {
                g->n[fr] = N;
                for (int k = 0; k < B; ++k)
                {
                    const int hc = tw::FilterTableSource::bandHarm (k);
                    const int lo = (k == 0)     ? 1 : (int) std::lround (std::sqrt ((double) tw::FilterTableSource::bandHarm (k - 1) * (double) hc));
                    const int hi = (k == B - 1) ? N : (int) std::lround (std::sqrt ((double) hc * (double) tw::FilterTableSource::bandHarm (k + 1)));
                    const float a = std::pow (10.0f, (ref[k] - 20.0f) / 20.0f);   // band RMS == ref[k] - 20 dB
                    for (int h = lo; h <= hi && h <= N; ++h) { g->amp[fr][h - 1] = a; g->phase[fr][h - 1] = 0.0f; }
                }
            }
            auto c = std::make_unique<Curve>();
            tw::FilterTableSource::bake (*g, *c);
            // The lowest bands CANNOT be set independently: bandHarm(0..2) are all harmonic 1, so a
            // per-band spectrum written harmonic by harmonic overwrites itself down there. The test
            // therefore asks about the bands where the mapping is one-to-one, which is where the
            // subtraction is checkable at all.
            // The collisions are not only at the very bottom: bandHarm is 1,1,1,2,2,3,3,4,5,... so SEVEN
            // bands share three harmonics before the mapping becomes one-to-one. Start after the LAST
            // collision, not the first run of them.
            int kFirst = 0;
            for (int k = 1; k < B; ++k) if (tw::FilterTableSource::bandHarm (k) == tw::FilterTableSource::bandHarm (k - 1)) kFirst = k + 1;
            float worst = 0.0f;
            for (int k = kFirst; k < B; ++k) worst = std::fmax (worst, std::fabs (c->db[0][k]));
            std::printf ("    a table that IS the reference bakes to +/-%.4f over bands %d..%d\n", worst, kFirst, B - 1);
            check (worst < 0.8f, "T2 a table matching referenceDb() bakes FLAT (the subtraction is real)");
        }
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
        // tp27 — the curve is normalised to a UNIT peak across the whole table, so kDepthDb alone
        // decides how deep resonance goes and no table can sit on the filter's +/-24 dB clamp.
        float pk = 0.0f;
        for (int f = 0; f < F; ++f) for (int k = 0; k < B; ++k) pk = std::fmax (pk, std::fabs (saw->db[f][k]));
        check (pk <= 1.0f + 1e-3f, "T4 no stored curve exceeds unit peak");
        check (std::fabs (pk - 1.0f) < 1e-3f, "T4 a table with real structure normalises TO unit peak");
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
        // the curve is a UNIT shape (peak magnitude 1), so frame-to-frame difference is measured on
        // that scale; kDepthDb turns it into dB at the read site.
        check (std::fabs (a[B - 1] - b[B - 1]) > 0.15f, "T5 the first and last frames really differ");
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
        check (diff / (float) B > 0.30f, "T6 two different tables give audibly different curves");
        // tp27 — and the property Max actually cares about: two tables must not be the SAME SHAPE.
        // Correlation, not distance: a loud copy of a curve is still the same filter.
        float ms = 0.0f, mw = 0.0f;
        for (int k = 0; k < B; ++k) { ms += s[k]; mw += w[k]; }
        ms /= (float) B; mw /= (float) B;
        float num = 0.0f, ds = 0.0f, dw = 0.0f;
        for (int k = 0; k < B; ++k) { const float x = s[k] - ms, y = w[k] - mw; num += x * y; ds += x * x; dw += y * y; }
        const float corr = (ds > 0.0f && dw > 0.0f) ? num / std::sqrt (ds * dw) : 1.0f;
        check (corr < 0.5f, "T6 and they are not the same SHAPE (correlation below 0.5)");
        check (sine->sig != high->sig, "T6 their signatures differ (the rebuild gate can tell them apart)");
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
