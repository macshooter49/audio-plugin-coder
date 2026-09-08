// ══════════════════════════════════════════════════════════════════════════════════════════════
//  hm_tables_cert.cpp — fb599 TABLES-ONLY certification (Pattern A — NOT in CMakeLists).
//
//    c++ -std=c++17 -O2 -Wall -Wextra -Isrc hm_tables_cert.cpp -o hm_tables_cert && ./hm_tables_cert
//
//  THE PROPERTY UNDER TEST is not "the header looks nicer". It is: with mainMode FORCED to 6 in
//  the HARM gather, does every HARM oscillator actually build the TABLE's spectrum — including
//  one restored from a preset whose stored HARM_MODE choice is 0 (Blade)?
//
//  METRIC (a hearing metric — the bank IS the spectrum, so it is measured directly, no FFT
//  needed and no phase-only difference can masquerade as a change): amplitude-weighted magnitude
//  distance in dB over the prepared partial bank. Each bank is converted to dB re its own loudest
//  partial and floored at -90 dB; per partial the |dB error| is weighted by the LINEAR amplitude
//  of the LOUDER of the two, so partials at the floor cannot dominate. dist = Σ w·|ΔdB| / Σ w.
//  A distance under ~1 dB is "the same sound"; the shipped Blade↔Table gap is printed for scale.
//
//  MUTATION SEAM:  -DHM_MUT=1  the fb588 bake gate is kept (`&& MODE == 6`) while the gather
//                              forces mainMode = 6  → bar 2 red (a Blade preset plays Blade)
//                  -DHM_MUT=2  the sculpt is NOT pinned (sculptMode read from the preset)
//                              → bar 4 red (a preset saved on Terrace still sculpts Terrace)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#define private public
#include "HarmonicEngine.h"
#undef private
using tw::HarmonicEngine; using tw::HarmParams;
namespace H = tw::harm;

#ifndef HM_MUT
#define HM_MUT 0
#endif

static int pass = 0, tot = 0;
static void bar (bool ok, const char* name, const char* detail)
{ ++tot; pass += ok ? 1 : 0; std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", name, detail); }

// ── the synthetic TABLE: a two-formant, odd-leaning spectrum no procedural family produces ────
static float gTblAmp[H::kMaxPartials], gTblPhase[H::kMaxPartials];
static int   gTblN = 0;
static void buildTable()
{
    gTblN = 96;
    double e = 0.0;
    for (int j = 0; j < gTblN; ++j)
    {
        const double n = j + 1;
        const double f1 = std::exp (-std::pow ((n - 5.0) / 2.2, 2.0));
        const double f2 = 0.55 * std::exp (-std::pow ((n - 23.0) / 5.0, 2.0));
        const double bed = 0.18 / std::pow (n, 1.35);
        const double odd = ((int) n & 1) ? 1.0 : 0.35;
        const double a = (f1 + f2 + bed) * odd;
        gTblAmp[j] = (float) a; e += a * a;
        gTblPhase[j] = (float) (std::fmod (0.7 * n * n, 6.28318530718));   // structured, not random
    }
    const float g = (float) (1.0 / std::sqrt (std::max (1e-12, e)));
    for (int j = 0; j < gTblN; ++j) gTblAmp[j] *= g;
    for (int j = gTblN; j < H::kMaxPartials; ++j) { gTblAmp[j] = 0.f; gTblPhase[j] = 0.f; }
}

// ── the SHIPPED gather, transcribed: what PluginProcessor.cpp:9526-9600 hands the voice ───────
//    `storedMode` is the preset's HARM_MODE choice index; `gridLive` is whether the message
//    thread has baked a grid for this oscillator (rebuildHarmTableIfNeeded, ~1491).
struct Gathered { HarmParams p; };
static Gathered gatherSHIPPED (int storedMode, int storedSculpt, bool gridLive)
{
    Gathered g; g.p.mainMode = storedMode; g.p.sculptMode = storedSculpt;
    if (g.p.mainMode == 6) {
        if (gridLive) { g.p.tableN = gTblN; g.p.tableAmp = gTblAmp; g.p.tablePhase = gTblPhase; g.p.tableSig = 1.0f; }
        else            g.p.mainMode = 0;                        // the one-tick null-grid fallback
    }
    return g;
}
static Gathered gatherFIXED (int storedMode, int storedSculpt)
{
    // fb599 — the choice params stay registered; the engine stops reading them.
    (void) storedMode;
    Gathered g; g.p.mainMode = 6;
#if HM_MUT == 2
    g.p.sculptMode = storedSculpt;            // MUTATION: the sculpt is not pinned
#else
    g.p.sculptMode = 0;                       // Keel — Max: "if we have to use one ... keel"
    (void) storedSculpt;
#endif
    // rebuildHarmTableIfNeeded's gate:
#if HM_MUT == 1
    const bool wanted = (storedMode == 6);    // MUTATION: the fb588 gate is kept
#else
    const bool wanted = true;                 // fb599: HARM always resolves the table path
#endif
    if (wanted) { g.p.tableN = gTblN; g.p.tableAmp = gTblAmp; g.p.tablePhase = gTblPhase; g.p.tableSig = 1.0f; }
    else          g.p.mainMode = 0;           // the null-grid fallback fires — FOREVER, not one tick
    return g;
}

// ── prepare the bank and read it: the spectrum the voice will actually render ──────────────────
struct Bank { std::array<double, H::kMaxPartials> db; std::array<double, H::kMaxPartials> ratio, pan; int n; };
static Bank prepared (const HarmParams& p, int blocks = 2, double hz = 110.0, double sr = 48000.0)
{
    HarmonicEngine e; e.prepare (sr, true); e.setParams (p); e.noteOn (hz, 0x5EEDFA11u);
    for (int b = 0; b < blocks; ++b) e.prepareBank (256);   // 256-sample blocks advance the engine's tB_
    Bank b; b.n = e.nP_;
    double mx = 1e-12; for (int j = 0; j < b.n; ++j) mx = std::max (mx, (double) e.amp_[(size_t) j]);
    for (int j = 0; j < H::kMaxPartials; ++j)
    {
        b.db[(size_t) j]    = (j < b.n) ? std::max (-90.0, 20.0 * std::log10 (std::max ((double) e.amp_[(size_t) j], 1e-12) / mx)) : -90.0;
        b.ratio[(size_t) j] = (j < b.n) ? (double) e.ratio_[(size_t) j] : 0.0;
        b.pan  [(size_t) j] = (j < b.n) ? (double) e.panL_[(size_t) j] - (double) e.panR_[(size_t) j] : 0.0;
    }
    return b;
}
// weighted |cents| pitch distance + weighted pan distance — SHINE detunes ratios, FAN moves pans,
// BRAID does both: an amplitude-only metric is deaf to all three, which is why this exists.
static double wmove (const Bank& a, const Bank& b)
{
    double num = 0, den = 0;
    for (size_t j = 0; j < H::kMaxPartials; ++j)
    {
        const double w = std::max (std::pow (10.0, a.db[j] / 20.0), std::pow (10.0, b.db[j] / 20.0));
        const double cents = (a.ratio[j] > 1e-9 && b.ratio[j] > 1e-9)
                               ? std::fabs (1200.0 * std::log2 (a.ratio[j] / b.ratio[j])) : 0.0;
        num += w * (std::fabs (a.db[j] - b.db[j]) + cents * 0.02 + std::fabs (a.pan[j] - b.pan[j]) * 6.0);
        den += w;
    }
    return den > 0 ? num / den : 0.0;
}
static double wdist (const Bank& a, const Bank& b)
{
    double num = 0, den = 0;
    for (size_t j = 0; j < H::kMaxPartials; ++j)
    { const double w = std::max (std::pow (10.0, a.db[j] / 20.0), std::pow (10.0, b.db[j] / 20.0));
      num += w * std::fabs (a.db[j] - b.db[j]); den += w; }
    return den > 0 ? num / den : 0.0;
}

int main()
{
    buildTable();
    std::printf ("\n══ hm_tables_cert — fb599 TABLES ONLY ══%s\n", HM_MUT ? "   (HM_MUT is set)" : "");
    std::printf ("metric: amplitude-weighted |dB| distance over the prepared 512-partial bank\n\n");

    // the two reference sounds
    const Bank BLADE = prepared (gatherSHIPPED (0, 0, false).p);          // family 0, no table
    const Bank TABLE = prepared (gatherSHIPPED (6, 0, true ).p);          // family 6, grid live
    const double scale = wdist (BLADE, TABLE);
    char d[512];

    std::snprintf (d, sizeof d, "Blade bank %d partials, Table bank %d partials, distance %.2f dB — "
                                "the two sounds this gate has to tell apart", BLADE.n, TABLE.n, scale);
    bar (scale > 6.0, "[1] THE TWO SOUNDS ARE DIFFERENT — the scale this cert measures against", d);

    // ── the deciding bar: a preset saved on Blade, restored on the fb599 build ────────────────
    const Bank RESTORED = prepared (gatherFIXED (0, 0).p);
    const double dTbl = wdist (RESTORED, TABLE), dBld = wdist (RESTORED, BLADE);
    std::snprintf (d, sizeof d, "stored HARM_MODE = 0 (Blade) → distance to Table %.2f dB, to Blade %.2f dB "
                                "(the preset keeps its index 0; the engine plays the table)", dTbl, dBld);
    bar (dTbl < 1.0 && dBld > 6.0, "[2] A PRESET SAVED ON BLADE PLAYS ITS TABLE — mainMode is forced, the choice is not renumbered", d);

    // every stored family index must land on the same table sound
    double worst = 0.0; int worstF = -1;
    for (int f = 0; f < 7; ++f) { const double x = wdist (prepared (gatherFIXED (f, 0).p), TABLE);
                                  if (x > worst) { worst = x; worstF = f; } }
    std::snprintf (d, sizeof d, "all 7 stored family indices render the table; worst distance %.3f dB (index %d)", worst, worstF);
    bar (worst < 1.0, "[3] THE SIX RETIRED FAMILIES ARE UNREACHABLE FROM THE PLUGIN — every stored index resolves to Table", d);

    // ── the SCULPT pin ────────────────────────────────────────────────────────────────────────
    HarmParams pk = gatherFIXED (6, 4).p; pk.carve = 0.7f;             // stored sculpt 4 = Terrace
    HarmParams pref = gatherFIXED (6, 0).p; pref.carve = 0.7f;
    HarmParams pterr = pref; pterr.sculptMode = 4;
    const double dPin = wdist (prepared (pk), prepared (pref));
    const double dTerr = wdist (prepared (pterr), prepared (pref));
    std::snprintf (d, sizeof d, "stored sculpt 4 (Terrace) at Carve 0.70 → %.3f dB from Keel (pinned); "
                                "an UNPINNED Terrace would sit %.2f dB away — the pin is the whole difference", dPin, dTerr);
    bar (dPin < 0.01 && dTerr > 3.0, "[4] THE SCULPT IS PINNED TO KEEL — sculptMode 0, the registered default", d);

    // ── and at the shipped default (Carve = 0) the pin costs nothing at all ────────────────────
    HarmParams c0a = gatherFIXED (6, 4).p, c0b = gatherFIXED (6, 0).p;
    c0a.carve = c0b.carve = 0.0f; c0b.sculptMode = 4;
    const double dC0 = wdist (prepared (c0a), prepared (c0b));
    std::snprintf (d, sizeof d, "Carve = 0.00 (the registered default) → %.4f dB between a Keel bank and a Terrace bank: "
                                "applySculpt returns at HarmonicEngine.h:788 before it reads sculptMode at all", dC0);
    bar (dC0 < 1e-6, "[5] AT THE DEFAULT CARVE THE PIN IS A NO-OP — nothing an untouched preset can hear", d);

    // ── CROSS-ITEM DIAGNOSTIC (item 2, CHURN) — not a bar, a number the Churn agent needs. ──
    //    With the sculpt pinned to KEEL, applySculpt never reads churnMul (HarmonicEngine.h:789-800
    //    case 0). Its only remaining readers are SHINE's detune rate (:753), BRAID's beat wobble
    //    (:1002) and FAN's orbit (:1023) — each gated on its own knob, and all three of those knobs
    //    default to 0.0 (PluginProcessor.cpp:4176-4177 dv[]). So on a FACTORY-DEFAULT HARM patch:
    //
    //    🚨 fb601 — THIS BLOCK USED TO PRINT "Churn 0.00 vs 1.00 -> 0.0000" AND STOP THERE, which
    //    reads exactly like "CHURN DOES NOTHING" and is the first thing a future session would
    //    find. It is not a measurement of CHURN at all. Since fb600 the knob's whole effect is the
    //    per-VOICE frame drift at HarmonicEngine.h:308-322, and that path is gated on
    //        chRate > 0 && mainMode == 6 && tableGridAmp != nullptr
    //                   && tableFrames > 1 && tableStride > 0 && ! displayMode_
    //    — and the table this cert builds is a SINGLE RESOLVED FRAME (tableAmp/tablePhase, no
    //    GRID at all), exactly like the display bake. tableGridAmp is null, so driftOn_ is false
    //    and the number is STRUCTURALLY zero: it says nothing about the knob. It is kept, labelled,
    //    and then the real thing is measured underneath it against a 16-frame grid.
    {
        HarmParams base = gatherFIXED (6, 0).p;
        base.hue = 0.35f; base.count = 0.5f; base.lean = 0.5f; base.fan = 0.0f; base.grit = 0.0f;
        base.braid = 0.0f; base.carve = 0.0f; base.root = 0.0f; base.shine = 0.0f; base.wilt = 0.5f;
        base.forge = 0.0f;
        HarmParams lo = base, hi = base; lo.churn = 0.0f; hi.churn = 1.0f;
        const double dDef = wmove (prepared (lo, 188), prepared (hi, 188));    // ~1 s of engine time
        HarmParams b2 = base; b2.shine = 0.6f; b2.fan = 0.7f; b2.braid = 0.5f;
        HarmParams lo2 = b2, hi2 = b2; lo2.churn = 0.0f; hi2.churn = 1.0f;
        const double dLive = wmove (prepared (lo2, 188), prepared (hi2, 188));
        std::printf ("\n  CHURN DIAGNOSTIC (item 2), NO GRID — the sculpt readers only:\n"
                     "        Churn 0.00 vs 1.00 on the registered defaults (Fan=Braid=Shine=Carve=0)\n"
                     "        → %.4f (amp dB + pitch cents + pan, all weighted).\n"
                     "        With Shine 0.6 / Fan 0.7 / Braid 0.5 up → %.4f on the same metric.\n"
                     "        ⚠️ THIS ZERO IS STRUCTURAL, NOT A VERDICT ON CHURN. tableGridAmp is null on a\n"
                     "        statically-prepared single-frame bank, so driftOn_ (HarmonicEngine.h:308) is\n"
                     "        false and the fb600 frame drift — the knob's ENTIRE effect — cannot run.\n"
                     "        Pinning the sculpt to Keel also removes Tide and Terrace, the only two sculpt\n"
                     "        readers of churnMul, so what is left here can only shrink.\n", dDef, dLive);

        // ── fb601 — AND NOW THE REAL THING: the same knob against a real 16-frame GRID. ────────
        //    HarmTableSource::frameToGrid hands the voice tableFrames rows of tableStride floats
        //    and CHURN walks that axis per voice. A single snapshot is worthless — the drift is a
        //    sine, so at some instants it is back at Hue and the distance is 0 by construction.
        //    Sampled across ~1 s of engine time instead, and both the peak and the mean printed.
        static float gGrid[16 * H::kMaxPartials];
        const int F = 16, STR = H::kMaxPartials;
        for (int f = 0; f < F; ++f)
        {
            const double u = (double) f / (F - 1);
            double e = 0.0;
            for (int j = 0; j < STR; ++j)
            {   // a formant that walks up the bank as the frame index rises — the frame axis a
                // wavetable actually has, not noise
                const double n = j + 1;
                const double ctr = 3.0 + 26.0 * u;
                const double a = std::exp (-std::pow ((n - ctr) / 3.0, 2.0)) + 0.10 / std::pow (n, 1.2);
                gGrid[f * STR + j] = (float) a; e += a * a;
            }
            const float g = (float) (1.0 / std::sqrt (std::max (1e-12, e)));
            for (int j = 0; j < STR; ++j) gGrid[f * STR + j] *= g;
        }
        // the RESOLVED frame the processor would hand a churn-0 voice at this Hue — so the two
        // sides differ by the DRIFT and by nothing else
        static float gRes[H::kMaxPartials];
        {
            const float fp = base.hue * (float) (F - 1);
            int f0 = (int) fp; if (f0 > F - 2) f0 = F - 2; if (f0 < 0) f0 = 0;
            const float t = fp - (float) f0;
            for (int j = 0; j < STR; ++j)
                gRes[j] = gGrid[f0 * STR + j] + (gGrid[(f0 + 1) * STR + j] - gGrid[f0 * STR + j]) * t;
        }
        HarmParams gl = base, gh = base;
        for (HarmParams* q : { &gl, &gh })
        { q->tableAmp = gRes; q->tableN = STR; q->tableGridAmp = gGrid; q->tableFrames = F; q->tableStride = STR; }
        gl.churn = 0.0f; gh.churn = 1.0f;
        double peak = 0.0, sum = 0.0; int nS = 0;
        const Bank still = prepared (gl, 188);
        for (int blocks : { 24, 48, 72, 96, 120, 144, 168, 188 })
        { const double d2 = wmove (still, prepared (gh, blocks)); peak = std::max (peak, d2); sum += d2; ++nS; }
        HarmParams g25 = gh; g25.churn = 0.25f;
        double peak25 = 0.0;
        for (int blocks : { 24, 48, 72, 96, 120, 144, 168, 188 })
            peak25 = std::max (peak25, wmove (still, prepared (g25, blocks)));
        std::printf ("\n  CHURN DIAGNOSTIC (item 2), WITH A 16-FRAME GRID — the fb600 path, live:\n"
                     "        Churn 0.00 vs 1.00, sampled at 8 instants over ~1 s → peak %.4f, mean %.4f\n"
                     "        Churn 0.00 vs 0.25 on the same sampling            → peak %.4f\n"
                     "        (0 here WOULD be a verdict: it would mean the drift is not reaching the bank.)\n",
                     peak, sum / nS, peak25);
    }

    std::printf ("\n  %s %d passed, %d failed\n\n", (pass == tot) ? "OK" : "FAILED", pass, tot - pass);
    return (pass == tot) ? 0 : 1;
}
