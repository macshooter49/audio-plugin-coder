// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_waterfall_cert.cpp — fb589: THE ADDITIVE WATERFALL DRAWS THE BANK, NOT THE TABLE.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/harm_waterfall_cert.cpp \
//            -o /tmp/harm_waterfall_cert -framework Accelerate && /tmp/harm_waterfall_cert
//
//  Max: "I think the additive mode should only be waterfall or something. Let's talk about it."
//  Agreed: bars stay primary, the waterfall appears when the source is a wavetable.
//
//  This drives the SAME row sweep PluginProcessor::getOscWavetableJson runs for HARM/Table —
//  16 rows, HUE 0..1, one fixed seed, HarmonicEngine::displayCycle per row — and holds it to the
//  project's display law:
//
//    "Every filter response curve must mirror the DSP and MOVE with the knobs — no flat
//     placeholder lines, ever. Applies to the upcoming wavetable work too (no flat spectrums)."
//
//  THE BARS
//   1  THE ROTATION IS EXACT — displayCycle skips std::sin in its inner loop and advances each
//      partial by a complex rotation instead. That is a 1.3M-call saving and a claim about
//      accuracy, so it is measured against a literal std::sin reference.
//   2  IT IS A SURFACE, NOT NOISE — adjacent rows are strongly correlated, because every row is
//      seeded identically. The mutation (a per-row seed, which is what you get if you forget) is
//      run alongside and must destroy that correlation, or the bar is not testing anything.
//   3  THE SCULPT ROW MOVES THE PICTURE — Carve, Lean and Shine each visibly change the grid on
//      an unchanged table. This is the display law: drawing the raw table would fail here.
//   4  THE SCULPT MODE MOVES IT TOO — all six modes give distinguishable surfaces.
//   5  THE TABLE AXIS IS REAL — on a morphing table the rows genuinely differ end to end, and
//      the grid follows the TABLE (swapping the wavetable changes the picture).
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "WavetableBank.h"
#include "HarmonicEngine.h"
#include "HarmTableSource.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

using namespace tw;
static const int ROWS = WavetableSpec::kNumFrames;   // 16 — what the processor emits
static const int PTS  = 160;                         // identical to the wavetable path

static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const std::string& d = "")
{ c ? ++gPass : ++gFail; std::printf ("  %-5s %-56s %s\n", c ? "ok" : "FAIL", n, d.c_str()); }

static HarmParams baseParams()
{
    HarmParams p;
    p.mainMode = 6; p.sculptMode = 0; p.count = 1.0f; p.lean = 0.5f; p.carve = 0.0f;
    p.grit = 0.0f; p.braid = 0.0f; p.root = 0.0f; p.shine = 0.0f; p.wilt = 0.5f;
    p.fan = 0.0f; p.forge = 0.0f; p.churn = 0.5f;
    return p;
}

// EXACTLY the sweep PluginProcessor::getOscWavetableJson runs for HARM/Table.
// perRowSeed = the MUTATION for bar 2 (what you get if the seed is not pinned).
static std::vector<float> bakeGrid (const HarmTableSource::Grid& g, HarmParams p,
                                    bool perRowSeed = false)
{
    static float A[512], P[512];
    std::vector<float> grid ((size_t) ROWS * PTS, 0.f);
    HarmonicEngine e; e.prepare (48000.0, true); e.setDisplayMode (true);
    for (int r = 0; r < ROWS; ++r)
    {
        p.hue        = (float) r / (float) (ROWS - 1);
        p.tableN     = HarmTableSource::blend (g, p.hue, A, P, 512);
        p.tableAmp   = A; p.tablePhase = P;
        p.tableSig   = g.sig + p.hue * 1024.0f;
        e.setParams (p);
        e.noteOn (110.0, perRowSeed ? (0x5EEDFA11u + (std::uint32_t) r * 7919u) : 0x5EEDFA11u);
        e.prepareBank (1);
        e.displayCycle (&grid[(size_t) r * PTS], PTS);
    }
    // the processor normalises the WHOLE grid by its peak; match that so numbers are comparable
    float pk = 1e-9f; for (float v : grid) pk = std::max (pk, std::fabs (v));
    for (float& v : grid) v *= 0.98f / pk;
    return grid;
}
static double gridDist (const std::vector<float>& a, const std::vector<float>& b)
{ double n = 0, d = 0; for (size_t i = 0; i < a.size(); ++i)
  { const double q = a[i] - b[i]; n += q*q; d += (double) a[i]*a[i]; }
  return d > 0 ? std::sqrt (n / d) : 0.0; }
static double rowCorr (const std::vector<float>& g, int r0, int r1)
{
    double sa=0,sb=0,saa=0,sbb=0,sab=0; const int n = PTS;
    for (int i = 0; i < n; ++i)
    { const double a = g[(size_t) r0*PTS+i], b = g[(size_t) r1*PTS+i];
      sa+=a; sb+=b; saa+=a*a; sbb+=b*b; sab+=a*b; }
    const double ca = saa - sa*sa/n, cb = sbb - sb*sb/n, cab = sab - sa*sb/n;
    return (ca > 1e-12 && cb > 1e-12) ? cab / std::sqrt (ca*cb) : 0.0;
}

int main()
{
    std::printf ("\n══ harm_waterfall_cert — fb589 ══  the additive waterfall draws the BANK\n\n");
    static HarmTableSource::Grid G, G2;
    HarmTableSource::bake (WavetableBank::specForPreset (16), G);    // VowelMorph — frames really differ
    HarmTableSource::bake (WavetableBank::specForPreset (4),  G2);   // ProphetSaw

    // ── 1 · the rotation recurrence is exact ────────────────────────────────────────────────
    {
        static float A[512], P[512];
        HarmParams p = baseParams();
        p.hue = 0.4f; p.tableN = HarmTableSource::blend (G, 0.4f, A, P, 512);
        p.tableAmp = A; p.tablePhase = P; p.tableSig = G.sig + 0.4f * 1024.f;
        HarmonicEngine e; e.prepare (48000.0, true); e.setDisplayMode (true);
        e.setParams (p); e.noteOn (110.0, 0x5EEDFA11u); e.prepareBank (1);
        std::vector<float> fast ((size_t) PTS, 0.f);
        e.displayCycle (fast.data(), PTS);
        // the literal reference: sum the same bank with std::sin, no recurrence
        std::vector<float> ref ((size_t) PTS, 0.f);
        const int np = e.debugNumPartials();
        for (int j = 0; j < np; ++j)
        { const float a = e.debugAmp (j); if (a <= 1e-6f) continue;
          const float r = e.debugRatio (j), ph = e.debugPhase (j);
          for (int i = 0; i < PTS; ++i)
            ref[(size_t) i] += a * std::sin (2.0f * harm::kPi * (r * ((float) i / PTS) + ph)); }
        double mx = 0, pk = 1e-12;
        for (int i = 0; i < PTS; ++i) { mx = std::max (mx, (double) std::fabs (fast[i] - ref[i]));
                                        pk = std::max (pk, (double) std::fabs (ref[i])); }
        char b[200]; std::snprintf (b, sizeof b, "%d partials; worst deviation from a literal std::sin sum = %.4f%% of peak",
                                    np, 100.0 * mx / pk);
        gate (mx / pk < 0.002, "[1] THE ROTATION IS EXACT — no std::sin in the inner loop", b);
    }

    // ── 2 · a surface, not noise (and the mutation proves the bar bites) ────────────────────
    //  ⚠️ BOTH HALVES ARE STATED ABSOLUTELY, not as a ratio tuned to what was observed: pinned must
    //     be CLEARLY correlated (a legible surface) and the mutation must be CLEARLY not (noise).
    //     Two tables, because a threshold that only holds on one table is a threshold about that
    //     table. VowelMorph is the hard case — its first and last rows differ by over 100%, so its
    //     adjacent rows are genuinely far apart and still must read as one surface.
    {
        const char* TN[2] = { "VowelMorph", "ProphetSaw" };
        const HarmTableSource::Grid* GG[2] = { &G, &G2 };
        double cg[2] = {0,0}, cb[2] = {0,0};
        for (int t = 0; t < 2; ++t)
        {
            const auto good = bakeGrid (*GG[t], baseParams(), false);
            const auto bad  = bakeGrid (*GG[t], baseParams(), true);   // MUTATION: a seed per row
            for (int r = 0; r + 1 < ROWS; ++r)
            { cg[t] += rowCorr (good, r, r+1); cb[t] += rowCorr (bad, r, r+1); }
            cg[t] /= (ROWS - 1); cb[t] /= (ROWS - 1);
        }
        char b[240]; std::snprintf (b, sizeof b,
            "%s %.3f vs %.3f mutated · %s %.3f vs %.3f mutated  (adjacent-row correlation)",
            TN[0], cg[0], cb[0], TN[1], cg[1], cb[1]);
        gate (cg[0] > 0.70 && cg[1] > 0.70 && cb[0] < 0.30 && cb[1] < 0.30,
              "[2] IT IS A SURFACE, NOT NOISE — one seed for every row", b);
    }

    // ── 3 · the sculpt knobs move the picture ───────────────────────────────────────────────
    {
        const auto ref = bakeGrid (G, baseParams());
        double worst = 1e9; std::string tell;
        const char* names[3] = { "Carve", "Lean", "Shine" };
        for (int k = 0; k < 3; ++k)
        {
            HarmParams q = baseParams();
            if (k == 0) { q.sculptMode = 2; q.carve = 1.0f; }   // Cull at full depth
            if (k == 1) q.lean  = 1.0f;
            if (k == 2) q.shine = 1.0f;
            const double d = gridDist (ref, bakeGrid (G, q));
            if (d < worst) { worst = d; tell = names[k]; }
        }
        char b[200]; std::snprintf (b, sizeof b, "weakest of Carve/Lean/Shine moves the whole grid by %.1f%% (%s)",
                                    100.0 * worst, tell.c_str());
        gate (worst > 0.10, "[3] THE SCULPT ROW MOVES THE PICTURE — not the raw table", b);
    }

    // ── 4 · every sculpt MODE gives its own surface ─────────────────────────────────────────
    {
        std::vector<std::vector<float>> gs;
        for (int m = 0; m < 6; ++m)
        { HarmParams q = baseParams(); q.sculptMode = m; q.carve = 0.8f; gs.push_back (bakeGrid (G, q)); }
        double closest = 1e9; int ia = -1, ib = -1;
        for (size_t i = 0; i < gs.size(); ++i) for (size_t j = i+1; j < gs.size(); ++j)
        { const double d = gridDist (gs[i], gs[j]); if (d < closest) { closest = d; ia = (int) i; ib = (int) j; } }
        char b[200]; std::snprintf (b, sizeof b, "closest pair of the six sculpt modes (%d vs %d) still differs by %.1f%%",
                                    ia, ib, 100.0 * closest);
        gate (closest > 0.05, "[4] EVERY SCULPT MODE GIVES ITS OWN SURFACE", b);
    }

    // ── 5 · the table axis is real, and it is THIS table ────────────────────────────────────
    {
        const auto g1 = bakeGrid (G,  baseParams());
        const auto g2 = bakeGrid (G2, baseParams());
        const double across = gridDist (g1, g2);
        double endToEnd = 0;
        { double n = 0, d = 0;
          for (int i = 0; i < PTS; ++i)
          { const double a = g1[(size_t) i], b2 = g1[(size_t) (ROWS-1)*PTS + i];
            n += (a-b2)*(a-b2); d += a*a; }
          endToEnd = d > 0 ? std::sqrt (n/d) : 0.0; }
        char b[220]; std::snprintf (b, sizeof b,
            "first row vs last row differs by %.1f%%; swapping the wavetable moves the whole grid %.1f%%",
            100.0 * endToEnd, 100.0 * across);
        gate (endToEnd > 0.15 && across > 0.20, "[5] THE TABLE AXIS IS REAL, and it is THIS table", b);
    }

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
