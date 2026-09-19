// ══════════════════════════════════════════════════════════════════════════════════════════════
//  looptempo_cert.cpp — tp57 · WHAT TEMPO IS THIS SAMPLE? (Source/LoopTempo.h, the shipped header)
//
//  Max: "if something is locked onto that BPM then it has to be stretched to the BPM so everything
//  can stay in time ... if I have multiple drum loops that I'm chopping up, the drum loops wouldn't
//  stay out of whack."  You cannot stretch a loop to the host until you know the loop's own tempo,
//  and nothing in a .wav tells you. This is the reading, and these are its edges.
//
//  clang++ -std=c++17 -O2 -I Source Tests/looptempo_cert.cpp -o /tmp/ltcert && /tmp/ltcert
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "LoopTempo.h"
#include <cstdio>
#include <string>
#include <cmath>
#include <vector>
static int pass = 0, fail = 0;
static void ok (bool c, const char* l, const std::string& d = {})
{ if (c) { ++pass; printf ("  PASS  %s\n", l); if (!d.empty()) printf ("        %s\n", d.c_str()); }
  else   { ++fail; printf ("  FAIL  %s\n        %s\n", l, d.c_str()); } }
static std::string f (double v, int d = 2) { char b[64]; snprintf (b, sizeof b, "%.*f", d, v); return b; }

int main()
{
    using namespace tw::looptempo;
    printf ("\n== tp57 - THE LOOP'S OWN TEMPO (Source/LoopTempo.h) ==\n\n");

    // ── [0] THE NAME, WHEN A HUMAN WROTE ONE ──────────────────────────────────────────────────
    {
        struct C { const char* n; double want; };
        const C cs[] = {
            { "Pad 138.wav",                138 },   // Max's own sample
            { "174 dnb break.wav",          174 },
            { "Loop 4 Bars 120bpm.wav",     120 },   // the tagged number wins over the bar count
            { "kick_90_BPM.aiff",            90 },
            { "Break 4 Bars 174.wav",       174 },   // untagged: the LAST in-range number
            { "128.5 house top.wav",      128.5 },
        };
        bool all = true; std::string why;
        for (auto& c : cs) { const double g = fromName (c.n);
            if (std::fabs (g - c.want) > 0.001) { all = false; why += std::string (c.n) + " -> " + f (g) + " (want " + f (c.want) + ") "; } }
        ok (all, "[0] A TEMPO IN THE FILE NAME IS READ, AND A TAGGED ONE BEATS AN UNTAGGED ONE", why);
    }

    // ── [1] 🚨 AND A NUMBER THAT IS NOT A TEMPO IS NOT READ AS ONE ───────────────────────────
    //  This is the bar that stops the feature being a menace. Sample libraries are FULL of numbers:
    //  drum machines, catalogue indices, sample rates, key names. Reading "808" as a tempo would
    //  put a time-stretcher on every 808 in the folder.
    {
        const char* junk[] = { "808 Kick.wav", "909 Clap.wav", "Snare 05.wav", "Vox 44100.wav",
                               "Hat 3.wav", "Chord Cmaj7.wav", "", "FX 2000.wav" };
        std::string got; bool clean = true;
        for (auto* n : junk) { const double g = fromName (n);
            if (g != 0.0) { clean = false; got += std::string (n) + " -> " + f (g) + " "; } }
        ok (clean, "[1] 🚨 808, 909, 44100, TRACK NUMBERS AND KEY NAMES ARE NOT TEMPOS", got.empty() ? "all eight read 0" : got);
    }

    // ── [2] THE LENGTH, WHEN THE NAME SAYS NOTHING ───────────────────────────────────────────
    {
        // 4 bars of 4/4 at 120 = 8.0 s exactly
        const double a = fromLength (8.0, 120.0);
        // 2 bars at 140 = 3.4286 s
        const double b = fromLength (8.0 * 120.0 / 140.0 / 2.0, 140.0);
        ok (std::fabs (a - 120.0) < 0.01 && std::fabs (b - 140.0) < 0.01,
            "[2] THE LENGTH READS A WHOLE NUMBER OF BARS", "8.0 s -> " + f (a) + " · 3.43 s -> " + f (b));
    }

    // ── [3] IT SAYS 0 WHEN IT DOES NOT KNOW ──────────────────────────────────────────────────
    //  A guess dressed as an answer is worse than no answer: the lock would stretch a one-shot.
    {
        const double tiny = fromLength (0.02, 120.0);          // a 20 ms transient
        const double none = detect ("Snare.wav", 0.14, 120.0); // a snare one-shot
        ok (tiny == 0.0 && none == 0.0,
            "[3] 🚨 NO READING IS 0, AND 0 MEANS DO NOT STRETCH", "20 ms -> " + f (tiny) + " · a 140 ms snare -> " + f (none));
    }

    // ── [4] THE NAME OUTRANKS THE LENGTH ─────────────────────────────────────────────────────
    {
        // an 8 s file called 138: the length says 120, the human says 138
        const double d = detect ("Pad 138.wav", 8.0, 120.0);
        ok (std::fabs (d - 138.0) < 0.01, "[4] WHAT THE PRODUCER WROTE BEATS WHAT WE INFER", "-> " + f (d));
    }

    // ── [5] THE STRETCH IS THE RIGHT WAY UP ──────────────────────────────────────────────────
    //  stretchRatio > 1 means the chop plays LONGER (SignalsmithEngine's own words). A 138 loop in
    //  a 120 session has to last longer, not shorter — the opposite would speed up every import.
    {
        const double up = stretchTo (138.0, 120.0), dn = stretchTo (100.0, 120.0), same = stretchTo (120.0, 120.0);
        ok (std::fabs (up - 138.0 / 120.0) < 1e-9 && dn < 1.0 && same == 1.0,
            "[5] 🚨 A FASTER LOOP IN A SLOWER SESSION PLAYS LONGER, NOT SHORTER",
            "138@120 -> " + f (up, 4) + " · 100@120 -> " + f (dn, 4) + " · 120@120 -> " + f (same, 4));
    }

    // ── [6] 🚨 HALF AND DOUBLE TIME ARE FOLDED, NOT "FIXED" ──────────────────────────────────
    //  A 174 break in a 90 session is a DOUBLE-TIME break; a musician hears it as 87. Stretching it
    //  1.93x would keep the arithmetic and destroy the part, which is the opposite of the ask.
    {
        const double a = stretchTo (174.0, 90.0);    // 1.933 -> 0.967
        const double b = stretchTo (70.0, 140.0);    // 0.5   -> 1.0
        const double c = stretchTo (160.0, 80.0);    // 2.0   -> 1.0
        bool inWindow = a > 0.68 && a < 1.46 && b > 0.68 && b < 1.46 && c > 0.68 && c < 1.46;
        ok (inWindow && std::fabs (a - 174.0 / 180.0) < 1e-9,
            "[6] 🚨 EVERY RATIO LANDS INSIDE THE HALF/DOUBLE WINDOW — a break is re-timed, never crawled",
            "174@90 -> " + f (a, 4) + " · 70@140 -> " + f (b, 4) + " · 160@80 -> " + f (c, 4));
    }

    // ── [7] AND THE RESULT ACTUALLY LINES UP ─────────────────────────────────────────────────
    //  The whole point: after the stretch, a bar of the loop is a bar of the session (or a clean
    //  half or double of one). Walk a spread of real library tempos against a spread of sessions.
    {
        const double loops[] = { 85, 90, 100, 110, 120, 128, 130, 138, 140, 150, 160, 170, 174, 180 };
        const double hosts[] = { 70, 80, 90, 100, 120, 128, 140, 150, 174 };
        double worst = 0.0; std::string bad;
        for (double L : loops) for (double H : hosts)
        {
            const double r = stretchTo (L, H);
            const double played = L / r;              // the tempo it ends up sounding at
            const double oct = std::log2 (played / H);
            const double err = std::fabs (oct - std::round (oct));
            if (err > worst) { worst = err; bad = f (L, 0) + "@" + f (H, 0); }
        }
        ok (worst < 1.0e-9,
            "[7] 🚨 AFTER THE STRETCH EVERY LOOP SITS ON THE SESSION'S GRID (or a clean half / double of it)",
            "126 pairs, worst octave error " + f (worst, 12) + (bad.empty() ? "" : " (at " + bad + ")"));
    }

    printf ("\n  %d passed, %d failed\n\n", pass, fail);
    return fail ? 1 : 0;
}
