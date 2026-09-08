// ══════════════════════════════════════════════════════════════════════════════════════════════
//  dly_spread_cert.cpp — fb601: THE DELAY SPREAD DEFAULT, MEASURED THROUGH THE REAL DelayEngine.
//
//    c++ -std=c++17 -O2 -Wall -I Tests/shim -I Source Tests/dly_spread_cert.cpp \
//        -framework Accelerate -o /tmp/dly_spread_cert && /tmp/dly_spread_cert
//    (run from plugins/Terrain — it READS Source/PluginProcessor.cpp, see below)
//
//  Max, fb600: "every time I load up a delay the spread is up and then offsets the time."
//
//  fb600 moved the registered default 0.60 -> 0.05 and shipped GREEN with the arithmetic proved
//  in a /private/tmp scratch cert that evaporated. Nothing in Tests/ referenced DLY_SPREAD at
//  all. This is that bar, promoted and taken further:
//
//   • the tap position is MEASURED, not transcribed — an impulse through the shipped
//     DelayEngine::processSample, and the L/R energy CENTROIDS of what comes back. Both taps ride
//     the identical loop filter and output tilt, and the centroid of a convolution is the sum of
//     the centroids, so centroid(R) - centroid(L) IS the tap offset, exactly, with no assumption
//     about where readAt() indexes from.
//   • the default under test is READ OUT OF Source/PluginProcessor.cpp at run time. A harness
//     that hardcodes a registered default is an eleventh site (Tests/README.md's own warning
//     about all_menus.js and the wavetable count). Move the default back to 0.60 in the processor
//     and bar [2] goes red without anyone editing this file.
//
//  THE BARS
//   0  THE DEFAULT IS THE ONE THE PROCESSOR REGISTERS — parsed, printed, not assumed
//   1  THE TAP TABLE — 0.00 / 0.03 / 0.05 / 0.60 / 1.00 at 120 BPM, sync 1/4 (base 500.000 ms)
//   2  AT THE NEW DEFAULT THE R TAP IS ON THE GRID — inside the Haas fusion window on repeat 1
//      (< 10 ms: two taps read as ONE image placed off-centre, not as a flam) and inside a 1/16
//      note by repeat 4 (< 125 ms at 120 BPM). 0.60 misses both by an order of magnitude.
//   3  🚨 THE RANGE NEVER QUIETLY SHRANK — spread 1.00 still delivers the full +35.00 %.
//      This is the DRAMATICISM half: fixing a default must not cost the knob its ceiling.
//   4  THE MEASUREMENT AND THE HEADER COMMENT AGREE — the measured offset matches
//      DelayEngine.h:107's `rMul = 1 + spread*0.35` to under ONE SAMPLE. (It is not exact, and
//      the reason is written down at the bar: the float32 glide stalls (ulp/2)/smCoef short.)
//   5  SPREAD 0.00 IS EXACTLY MONO — the two taps land on the same sample, 0.000 ms apart.
//
//  MUTATION CONTROL (fb421 — a gate that has never failed has never been tested):
//      c++ ... -DDLY_SPREAD_MUT=1 ...   pretend the processor still registers 0.60  -> bar [2] RED
//      c++ ... -DDLY_SPREAD_MUT=2 ...   pretend the taper was trimmed to 0.10       -> bars [3][4] RED
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "DelayEngine.h"

#ifndef DLY_SPREAD_MUT
#define DLY_SPREAD_MUT 0
#endif

static int pass = 0, tot = 0;
static void bar (bool ok, const char* name, const std::string& detail)
{ ++tot; pass += ok ? 1 : 0; std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", name, detail.c_str()); }

// ── the registered default, read out of the processor (never hardcoded here) ───────────────────
//    matches:  addDlyF (ParameterIDs::SYN_DLY_SPREAD,   "Delay Spread",     0.05f);
static bool registeredDefault (const char* path, float& out, std::string& how)
{
    std::ifstream f (path);
    if (! f) { how = std::string ("could not open ") + path; return false; }
    std::stringstream ss; ss << f.rdbuf(); const std::string src = ss.str();
    const std::string key = "ParameterIDs::SYN_DLY_SPREAD";
    size_t at = 0;
    while ((at = src.find (key, at)) != std::string::npos)
    {
        // only the REGISTRATION line (addDlyF/addF ... , "…", <default>f);
        size_t bol = src.rfind ('\n', at); bol = (bol == std::string::npos) ? 0 : bol + 1;
        size_t eol = src.find ('\n', at);  if (eol == std::string::npos) eol = src.size();
        const std::string line = src.substr (bol, eol - bol);
        at = eol;
        if (line.find ("add") == std::string::npos || line.find ('"') == std::string::npos) continue;
        const size_t q2 = line.rfind ('"');            // end of the display name
        const size_t cm = line.find (',', q2);
        if (q2 == std::string::npos || cm == std::string::npos) continue;
        const size_t rp = line.find (')', cm);
        std::string num = line.substr (cm + 1, (rp == std::string::npos ? line.size() : rp) - cm - 1);
        // strip spaces and the trailing 'f'
        std::string clean; for (char c : num) if (! std::isspace ((unsigned char) c) && c != 'f') clean += c;
        try { out = std::stof (clean); } catch (...) { continue; }
        how = "PluginProcessor.cpp: " + line.substr (line.find_first_not_of (" \t"));
        return true;
    }
    how = std::string ("no addDlyF registration for SYN_DLY_SPREAD found in ") + path;
    return false;
}

// ── the MEASUREMENT: an impulse through the shipped engine, L/R energy centroids ───────────────
struct Taps { double lMs, rMs, dMs; };
static Taps measure (double baseMs, float spread, double sr = 48000.0)
{
    DelayEngine d;
    // everything that could smear or recirculate is parked BEFORE prepare(), because prepare()
    // adopts every target immediately ("no start-up glide sweep") — so spreadCur is exact, not ramped.
    d.setType (0);            // Digital — the clean tap, no tape/BBD/diffuse colour
    d.setTimeMs ((float) baseMs);
    d.setLink (true);
    d.setFeedback (0.0f);     // one echo only
    // fb601 — the in-loop band edges and the output tilt are made EXACTLY IDENTITY, not just
    // gentle: onePole(0) = 0 so the HP subtracts nothing, onePole(>= 0.49*fs) = 1 so the LP is a
    // wire, and toneTilt at 0.5 returns low+high = x. With the chain transparent the impulse
    // response IS the two-sample linear interpolator, so the AMPLITUDE centroid below is the
    // fractional tap position exactly — no filter tail, no truncation bias. (Measured: leaving a
    // 5 Hz low-cut in place biased the 0.03 reading by 0.25 %, which is the whole reason bar [4]
    // exists.)
    d.setTone (0.5f);
    d.setLowCutHz (0.0f); d.setHiCutHz ((float) sr);
    d.setWidth (1.0f); d.setPing (false);
    d.setModDepth (0.0f); d.setWow (0.0f); d.setDucking (0.0f);
    d.setHQ (false);          // linear read: the fraction lands on exactly two samples
    d.prepare (sr);
    // ⚠️ SPREAD IS SET **AFTER** prepare(), the way the processor does it (setSpread + a per-block
    //    updateCoefficients from processBlock), because prepare() writes `spreadCur = spreadTgt`
    //    AFTER its own updateCoefficients() call — so a spread parked before prepare is invisible
    //    to that first coefficient pass and the R tap then GLIDES to its target. Measured: taking
    //    the impulse immediately after prepare read every non-zero spread 0.72 samples (0.015 ms)
    //    short, at every spread — a constant offset that looks like a bug in the taper and is
    //    actually the 15 ms delay-length glide (the comb-click law) still in flight.
#if DLY_SPREAD_MUT == 2
    d.setSpread (spread * (0.10f / 0.35f));   // MUTATION: as if the taper had been trimmed to +10 %
#else
    d.setSpread (spread);
#endif
    // ⚠️ AND updateCoefficients() IS PER BLOCK, not once. It reads spreadCur (the SMOOTHED value),
    //    never spreadTgt, so calling it a single time after setSpread pins delTgtR at the base and
    //    the R tap never moves at all (measured: R-L = 0.000 ms at every spread — a gate that
    //    drove the engine wrong would have read "Spread is dead" and been believed).
    const int BLK = 128;
    { float a = 0, b = 0;
      for (int i = 0; i < (int) (sr * 0.5); ++i)
      { if ((i % BLK) == 0) d.updateCoefficients(); d.processSample (0.f, 0.f, a, b); } }   // let spread + the glide land
    d.reset();                                                     // empty line, settled taps

    const int N = (int) (sr * (baseMs * 2.5) * 0.001) + 512;
    double sL = 0, sR = 0, nL = 0, nR = 0;
    for (int i = 0; i < N; ++i)
    {
        if ((i % BLK) == 0) d.updateCoefficients();
        const float in = (i == 0) ? 0.25f : 0.0f;     // small: softClip stays linear
        float oL = 0, oR = 0;
        d.processSample (in, in, oL, oR);
        sL += (double) oL * i; nL += (double) oL;
        sR += (double) oR * i; nR += (double) oR;
    }
    Taps t {};
    t.lMs = (nL > 0 ? sL / nL : 0.0) * 1000.0 / sr;
    t.rMs = (nR > 0 ? sR / nR : 0.0) * 1000.0 / sr;
    t.dMs = t.rMs - t.lMs;
    return t;
}

int main (int argc, char** argv)
{
    const char* proc = (argc > 1) ? argv[1] : "Source/PluginProcessor.cpp";
    const double BPM = 120.0, BASE = 60000.0 / BPM;          // 1/4 note at 120 BPM = 500.000 ms
    const double SR  = 48000.0;

    std::printf ("\n══ dly_spread_cert — fb601 ══  base %.3f ms (1/4 @ %.0f BPM), %.0f Hz, Digital, FB 0, HQ off\n\n",
                 BASE, BPM, SR);

#if DLY_SPREAD_MUT
    std::printf ("  ⚠️  MUTATION ACTIVE: DLY_SPREAD_MUT=%d — bars are EXPECTED to go red.\n\n", DLY_SPREAD_MUT);
#endif

    // ── bar 0 ─────────────────────────────────────────────────────────────────────────────────
    float regDefault = -1.f; std::string how;
    bool found = registeredDefault (proc, regDefault, how);
#if DLY_SPREAD_MUT == 1
    regDefault = 0.60f; how = "MUTATION DLY_SPREAD_MUT=1 — pretending the processor still registers 0.60f";
#endif
    { char d[512]; std::snprintf (d, sizeof d, "%s", how.c_str());
      bar (found && regDefault >= 0.f && regDefault <= 1.f,
           "[0] THE DEFAULT IS THE ONE THE PROCESSOR REGISTERS — parsed, not assumed", d); }
    if (! found) { std::printf ("\n  ❌ cannot read the registered default — refusing to assert on a guess\n"
                                "     (run from plugins/Terrain, or pass the path as argv[1])\n\n"); return 1; }

    // ── bar 1 — the table ─────────────────────────────────────────────────────────────────────
    const float sweep[] = { 0.00f, 0.03f, 0.05f, 0.60f, 1.00f };
    std::printf ("  spread |  L tap ms |  R tap ms |  R-L ms  | off-grid | R by repeat 4 | drift@4\n");
    std::printf ("  -------+-----------+-----------+----------+----------+---------------+---------\n");
    Taps t[5];
    for (int i = 0; i < 5; ++i)
    {
        t[i] = measure (BASE, sweep[i], SR);
        std::printf ("   %4.2f%s |  %8.3f |  %8.3f | %+8.3f | %6.2f %% |    %9.3f | %+7.2f\n",
                     sweep[i], (std::fabs (sweep[i] - regDefault) < 1e-4f ? "*" : " "),
                     t[i].lMs, t[i].rMs, t[i].dMs, 100.0 * t[i].dMs / BASE,
                     BASE + 4.0 * t[i].dMs, 4.0 * t[i].dMs);
    }
    std::printf ("        (* = the registered default, %.4f)\n", regDefault);
    bar (t[0].dMs >= -1e-6 && t[4].dMs > t[3].dMs && t[3].dMs > t[2].dMs && t[2].dMs > t[1].dMs && t[1].dMs > t[0].dMs,
         "[1] THE TAP TABLE — the R tap moves MONOTONICALLY with Spread and nothing is dead",
         [&]{ char d[256]; std::snprintf (d, sizeof d, "R-L ms: %.3f -> %.3f -> %.3f -> %.3f -> %.3f across 0.00/0.03/0.05/0.60/1.00",
                                          t[0].dMs, t[1].dMs, t[2].dMs, t[3].dMs, t[4].dMs); return std::string (d); }());

    // ── bar 2 — the default is on the grid ────────────────────────────────────────────────────
    const Taps def = measure (BASE, regDefault, SR);
    const Taps old = measure (BASE, 0.60f,      SR);
    const double HAAS = 10.0;                    // fusion window: below this, two taps are ONE image
    const double SIX  = BASE / 4.0;              // a 1/16 note at this tempo = 125.000 ms
    const bool onGrid = std::fabs (def.dMs) < HAAS && std::fabs (4.0 * def.dMs) < SIX;
    const bool oldOff = std::fabs (old.dMs) >= HAAS || std::fabs (4.0 * old.dMs) >= SIX;
    { char d[512]; std::snprintf (d, sizeof d,
        "default %.4f -> R is %+.3f ms late (Haas window %.1f ms) and %+.2f ms by repeat 4 (1/16 = %.1f ms). "
        "The 0.60 it replaced: %+.3f ms / %+.2f ms — %.0fx and %.0fx over.",
        regDefault, def.dMs, HAAS, 4.0 * def.dMs, SIX, old.dMs, 4.0 * old.dMs,
        def.dMs > 1e-6 ? old.dMs / def.dMs : 0.0, def.dMs > 1e-6 ? old.dMs / def.dMs : 0.0);
      bar (onGrid && oldOff,
           "[2] AT THE NEW DEFAULT THE R TAP IS ON THE GRID — and the old one was not", d); }

    // ── bar 3 — the range never shrank ────────────────────────────────────────────────────────
    const double full = 100.0 * t[4].dMs / BASE;
    { char d[256]; std::snprintf (d, sizeof d,
        "spread 1.00 -> R is %+.3f ms = %+.2f %% of the beat (DelayEngine.h:107 says +35.00 %%). "
        "The default moved; the CEILING did not.", t[4].dMs, full);
      bar (std::fabs (full - 35.0) < 0.05, "[3] 🚨 THE RANGE NEVER QUIETLY SHRANK — 100 % is still the full +35 %", d); }

    // ── bar 4 — measurement vs the header comment, in SAMPLES ─────────────────────────────────
    //  ⚠️ THE RESIDUAL IS REAL AND IT IS NOT THE TAPER. The R tap lands a fixed fraction of a
    //  sample SHORT of base*(1+spread*0.35), at every non-zero spread. It is the float32
    //  exponential smoother in processSample stalling: once smCoef*(delTgt-delCur) falls below
    //  half an ulp of delCur, the += is a no-op and the glide freezes. Predicted stall =
    //  (ulp/2)/smCoef; measured at spread 0.05, four delay times: 125 ms -> 0.178 samples
    //  (predicted 0.176), 250 -> 0.356 (0.352), 500 -> 0.713 (0.704), 1000 -> 1.426 (1.407).
    //  At the 500 ms beat this cert runs on that is 0.0149 ms — 0.003 % of one beat, three orders
    //  below the Haas window bar [2] uses — so it is DOCUMENTED, not chased. The bar is one
    //  sample, which is what separates "the taper is what the source says" from "the smoother is
    //  finite-precision".
    double worstSamp = 0.0; int worstAt = 0;
    for (int i = 0; i < 5; ++i)
    {
        const double want = BASE * (double) sweep[i] * 0.35;
        const double errS = std::fabs (t[i].dMs - want) * SR * 0.001;      // in SAMPLES
        if (errS > worstSamp) { worstSamp = errS; worstAt = i; }
    }
    const double smCoef  = 1.0 - std::exp (-1.0 / (0.015 * SR));
    const double ulpHere = std::nextafterf ((float) (BASE * 1.35 * SR * 0.001), 1e9f)
                         - (float) (BASE * 1.35 * SR * 0.001);
    { char d[420]; std::snprintf (d, sizeof d,
        "worst |measured - base*spread*0.35| = %.3f samples (%.4f ms) at spread %.2f. "
        "That residual is the float32 glide stall, predicted (ulp/2)/smCoef = %.3f samples, not a taper error.",
        worstSamp, worstSamp / (SR * 0.001), sweep[worstAt], 0.5 * ulpHere / smCoef);
      bar (worstSamp < 1.0, "[4] THE MEASUREMENT AND DelayEngine.h:107 AGREE TO UNDER ONE SAMPLE — this is the engine, not a transcription", d); }

    // ── bar 5 — spread 0 is exactly mono ──────────────────────────────────────────────────────
    { char d[256]; std::snprintf (d, sizeof d, "spread 0.00 -> R-L = %.6f ms (%.3f samples)", t[0].dMs, t[0].dMs * SR * 0.001);
      bar (std::fabs (t[0].dMs) < 1e-6, "[5] SPREAD 0.00 IS EXACTLY MONO — both taps on the same sample", d); }

    std::printf ("\n  %s %d passed, %d failed\n\n", (pass == tot) ? "✅ OK" : "❌ FAILED", pass, tot - pass);
    return (pass == tot) ? 0 : 1;
}
