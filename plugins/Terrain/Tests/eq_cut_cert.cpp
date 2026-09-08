// ══════════════════════════════════════════════════════════════════════════════════════════════
//  eq_cut_cert.cpp — fb470. THE LOW CUT AND THE HIGH CUT.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/eq_cut_cert.cpp -o /tmp/eq_cut
//
//  Max: "I can't even make a low cut or a high cut." He was right, and it was not a regression —
//  FX-rack kind 9 had bells and shelves on every Type and no cut of any kind, on any band.
//
//  Every number below is measured by RUNNING AUDIO THROUGH THE ENGINE — a sine at the frequency
//  in question, steady state, output RMS against input RMS. Not from the coefficients, not from
//  the display curve. The display curve is then checked AGAINST that (gate K8), which is the only
//  way to know the picture and the sound agree.
//
//  THE BAR IS SERUM 2. Its spectral Lo/Hi markers are a FOURTH-ORDER BUTTERWORTH [M2 p.108], so
//  that is what a cut here is: 24 dB/oct, -3.01 dB at the corner, and the node's wheel rides the
//  first section so its detent is an EXACT Butterworth.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "TerrainEqualizerFx.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const char* d = "")
{ c ? ++gPass : ++gFail; printf ("  %-5s %-56s %s\n", c ? "ok" : "FAIL", n, d); }

static const double SR = 48000.0;
static float tOfHz (double hz) { return (float) (std::log (hz / 20.0) / std::log (1000.0)); }

// run a sine through the engine and return output/input in dB, steady state
static double respDb (tw::TerrainEqualizerFx& e, double hz)
{
    const int warm = 24000, meas = 24000;
    double si = 0.0, so = 0.0;
    for (int n = 0; n < warm + meas; ++n)
    {
        float l = (float) std::sin (2.0 * M_PI * hz * n / SR), r = l;
        const float in = l;
        e.processStereo (&l, &r, 1);
        if (n >= warm) { si += (double) in * in; so += (double) l * l; }
    }
    if (si <= 0.0) return -300.0;
    return 10.0 * std::log10 (std::max (1e-30, so / si));
}

static tw::TerrainEqualizerFx::Params cutPatch (int shape, double hz, float gain01 = 0.5f, float q01 = 0.5f)
{
    tw::TerrainEqualizerFx::Params p;
    p.mix = 1.0f;
    p.x1 = tOfHz (hz);      // free band 1 frequency
    p.x2 = gain01;          // its gain (0.5 = 0 dB)
    p.xOn1 = true;
    p.sh1 = shape;          // 0 Bell · 1 Low Cut · 2 High Cut · 3 Low Shelf · 4 High Shelf
    p.q5 = q01;             // the node's wheel; 0.5 = the law exactly
    return p;
}

int main()
{
    printf ("\n══ fb470 — THE LOW CUT AND THE HIGH CUT ══\n\n");

    // ── K0 — a default device is flat, and a BELL at 0 dB is still flat ───────────────────────
    {
        tw::TerrainEqualizerFx e; e.prepare (SR, 512);
        tw::TerrainEqualizerFx::Params p; p.mix = 1.0f; e.setParams (p);
        double worst = 0.0;
        for (double f : { 40.0, 200.0, 1000.0, 5000.0, 15000.0 }) worst = std::max (worst, std::abs (respDb (e, f)));
        char d[160]; snprintf (d, sizeof d, "worst |dB| across 40 Hz..15 kHz = %.4f", worst);
        gate (worst < 0.05, "K0  a default device is FLAT (nothing here changed that)", d);
    }
    {
        tw::TerrainEqualizerFx e; e.prepare (SR, 512);
        e.setParams (cutPatch (0, 500.0));                       // Bell, 0 dB, ON
        double worst = 0.0;
        for (double f : { 125.0, 500.0, 2000.0 }) worst = std::max (worst, std::abs (respDb (e, f)));
        char d[160]; snprintf (d, sizeof d, "worst |dB| = %.4f — an off stage is still bit-exact through", worst);
        gate (worst < 0.05, "K1  shape BELL at 0 dB is untouched by fb470", d);
    }

    // ── K2 — THE LOW CUT actually cuts, and passes what is above it ───────────────────────────
    double lo2 = 0, loC = 0, loHi = 0;
    {
        tw::TerrainEqualizerFx e; e.prepare (SR, 512);
        e.setParams (cutPatch (1, 500.0));
        lo2 = respDb (e, 125.0); loC = respDb (e, 500.0); loHi = respDb (e, 4000.0);
        char d[190]; snprintf (d, sizeof d, "125 Hz %+.2f dB · 500 Hz %+.2f dB · 4 kHz %+.2f dB", lo2, loC, loHi);
        gate (lo2 < -40.0 && loHi > -0.3, "K2  LOW CUT: two octaves below is gone, the top passes", d);
    }

    // ── K3 — and it is a FOURTH-ORDER BUTTERWORTH, which is what Serum 2 uses ─────────────────
    {
        char d[200]; snprintf (d, sizeof d, "corner reads %+.2f dB (Butterworth is -3.01), and 2 octaves down is %+.1f dB (24 dB/oct is -48.2)", loC, lo2);
        gate (std::abs (loC + 3.01) < 0.6 && std::abs (lo2 + 48.2) < 4.0,
              "K3  the corner is -3 dB and the slope is 24 dB/oct", d);
    }

    // ── K4 — THE HIGH CUT mirrors it ──────────────────────────────────────────────────────────
    {
        tw::TerrainEqualizerFx e; e.prepare (SR, 512);
        e.setParams (cutPatch (2, 2000.0));
        const double a = respDb (e, 8000.0), c = respDb (e, 2000.0), b = respDb (e, 250.0);
        char d[190]; snprintf (d, sizeof d, "250 Hz %+.2f dB · 2 kHz %+.2f dB · 8 kHz %+.2f dB", b, c, a);
        gate (a < -40.0 && b > -0.3 && std::abs (c + 3.01) < 0.6, "K4  HIGH CUT mirrors it, same order", d);
    }

    // ── K5 — THE TRAP: a cut is LIVE AT ZERO GAIN ─────────────────────────────────────────────
    //     Every other shape in this device is switched off when its gain is zero, because an off
    //     stage is bit-exact through. A cut HAS no gain, so the same test would have made it a
    //     permanent bypass — a "Low Cut" that does nothing, with every other gate still green.
    {
        tw::TerrainEqualizerFx e; e.prepare (SR, 512);
        e.setParams (cutPatch (1, 500.0, 0.5f));                 // gain EXACTLY 0 dB
        const double v = respDb (e, 125.0);
        char d[170]; snprintf (d, sizeof d, "gain parameter at its 0 dB detent, 125 Hz reads %+.1f dB", v);
        gate (v < -40.0, "K5  a cut works at ZERO gain (it has none to give)", d);
    }

    // ── K6 — the wheel adds RESONANCE at the corner, not a different slope ────────────────────
    {
        tw::TerrainEqualizerFx a, b; a.prepare (SR, 512); b.prepare (SR, 512);
        a.setParams (cutPatch (1, 500.0, 0.5f, 0.50f));          // detent = exact Butterworth
        b.setParams (cutPatch (1, 500.0, 0.5f, 0.85f));          // wheel up
        const double ca = respDb (a, 500.0), cb = respDb (b, 500.0);
        const double sa = respDb (a, 125.0), sb = respDb (b, 125.0);
        char d[200]; snprintf (d, sizeof d, "corner %+.2f -> %+.2f dB (resonance +%.2f); two octaves down %+.1f -> %+.1f dB (slope moves %.2f)",
                               ca, cb, cb - ca, sa, sb, std::abs (sb - sa));
        gate (cb - ca > 2.0 && std::abs (sb - sa) < 6.0, "K6  the wheel is RESONANCE, the slope stays put", d);
    }

    // ── K7 — the cut MOVES with its frequency ─────────────────────────────────────────────────
    {
        tw::TerrainEqualizerFx e; e.prepare (SR, 512);
        e.setParams (cutPatch (1, 120.0));
        const double lowSet = respDb (e, 1000.0);
        tw::TerrainEqualizerFx f2; f2.prepare (SR, 512);
        f2.setParams (cutPatch (1, 4000.0));
        const double highSet = respDb (f2, 1000.0);
        char d[190]; snprintf (d, sizeof d, "1 kHz reads %+.2f dB with the corner at 120 Hz and %+.1f dB with it at 4 kHz", lowSet, highSet);
        gate (lowSet > -0.3 && highSet < -20.0, "K7  the corner follows the band's frequency", d);
    }

    // ── K8 — THE PICTURE MATCHES THE SOUND. The card draws viz_.curve; this compares it against
    //         the measured response at the same frequencies.
    {
        tw::TerrainEqualizerFx e; e.prepare (SR, 512);
        e.setParams (cutPatch (1, 500.0));
        { float l = 0, r = 0; for (int n = 0; n < 4096; ++n) { l = r = 0.0f; e.processStereo (&l, &r, 1); } }
        const auto& z = e.viz();
        double worst = 0.0; int checked = 0; double worstAt = 0.0;
        for (int i = 4; i < tw::TerrainEqualizerFx::kCurveBins - 4; i += 12)
        {
            const double hz = tw::TerrainEqualizerFx::curveBinHz (i);
            if (hz < 40.0 || hz > 12000.0) continue;
            const double drawn = z.curve[i], heard = respDb (e, hz);
            if (std::abs (drawn - heard) > worst) { worst = std::abs (drawn - heard); worstAt = hz; }
            ++checked;
        }
        char d[190]; snprintf (d, sizeof d, "worst %.2f dB apart at %.0f Hz, over %d points", worst, worstAt, checked);
        gate (worst < 1.5 && checked >= 8, "K8  the curve the card draws IS the response you hear", d);
    }

    // ── K9 — THE CARD THAT NEVER PROCESSES AUDIO STILL FOLLOWS ITS KNOBS ─────────────────────
    //     Every new rack card boots UNROUTED, TW_FX4_APPLY skips processStereo when nothing is
    //     routed, and viz_ was written only from inside processStereo. So a fresh EQ drew this
    //     struct's construction defaults for ever — a flat line and four dots parked at
    //     100/550/3100/15500 Hz — and nothing the user did could move them. Not one sample of audio
    //     is rendered below; refreshVizIdle() is the entire path under test.
    {
        tw::TerrainEqualizerFx e; e.prepare (SR, 512);
        const auto& z = e.viz();
        const float bootHz = z.nodeHz[0], bootCurve = z.curve[tw::TerrainEqualizerFx::kCurveBins / 4];

        tw::TerrainEqualizerFx::Params p; p.mix = 1.0f;
        p.b1 = 0.95f;                       // Low Hz way up
        p.b2 = 0.05f;                       // Low gain way down — a deep cut nobody could miss
        p.x1 = tOfHz (4000.0); p.x2 = 0.5f; p.xOn1 = true; p.sh1 = 2;   // and a HIGH CUT at 4 kHz
        e.setParams (p);
        for (int b = 0; b < 8; ++b) e.refreshVizIdle();                 // eight "blocks", no audio

        const float gotHz = z.nodeHz[0], gotCurve = z.curve[tw::TerrainEqualizerFx::kCurveBins / 4];
        const float freeHz = z.nodeHz[tw::TerrainEqualizerFx::kNumBands];
        char d[230]; snprintf (d, sizeof d, "Low node %.0f -> %.0f Hz · curve at that bin %.2f -> %.2f dB · the added band reads %.0f Hz",
                               bootHz, gotHz, bootCurve, gotCurve, freeHz);
        gate (std::abs (gotHz - bootHz) > 20.0f && std::abs (gotCurve - bootCurve) > 1.0f
              && freeHz > 3000.0f && freeHz < 5000.0f,
              "K9  an UNROUTED card follows its knobs (no audio rendered at all)", d);

        char d2[170]; snprintf (d2, sizeof d2, "level reads %.3f", z.lvl);
        gate (z.lvl <= 0.0f, "K9b and it reports NO level, because it is passing no audio", d2);
    }

    printf ("\n  PASS %d   FAIL %d\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
