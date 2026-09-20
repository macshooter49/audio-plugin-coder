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

    // ══ tp58 — THE THIRD READING: fromAudio / detectFull ═════════════════════════════════════
    //  Max: "the audio in fact does NOT stretch to the bpm whenever I have it on global LOCK."
    //  Half of that was the DSP (Tests/bpmlock_cert.cpp). The other half was here: a name and a
    //  length only answer for files that were LABELLED or exported to an exact bar count, and
    //  "Drum Loop.wav" is neither, so detect() returned 0, 0 means do not stretch, and the lock
    //  did nothing with no way to tell why.
    {
        const double SR = 48000.0;
        // a kick/snare/hat loop at `bpm`, optionally with a tail that makes the LENGTH lie
        auto loop = [&] (double bpm, int beats, double tailS) {
            const double spb = SR * 60.0 / bpm;
            const int n = (int) std::round (spb * beats) + (int) (tailS * SR);
            std::vector<float> x ((size_t) n, 0.f); unsigned sd = 1;
            auto hit = [&] (int at, double dec, double amp, bool noise, double f0) {
                for (int i = 0; i < (int) (SR * 0.18) && at + i < n; ++i) {
                    const double t = i / SR, e = std::exp (-t * dec);
                    const double v = noise ? ((sd = sd * 1664525u + 1013904223u) / 2147483648.0 - 1.0) * amp * e
                                           : std::sin (2 * M_PI * (f0 - t * f0 * 2.6) * t) * amp * e;
                    x[(size_t)(at + i)] += (float) v; } };
            for (int b = 0; b < beats; ++b) { const int at = (int) std::round (b * spb);
                if (b % 2 == 0) hit (at, 20, 0.95, false, 60); else hit (at, 34, 0.6, true, 0);
                hit (at + (int) (spb / 2), 90, 0.18, true, 0); }
            return x; };

        // [8] it HEARS a spread of real tempos, with the session as the prior
        {
            const double tempos[] = { 90, 100, 110, 117, 128, 140, 150, 174 };
            std::string bad; int good = 0;
            for (double t : tempos) { auto x = loop (t, 16, 0.0);
                const double got = fromAudio (x.data(), (int) x.size(), SR, t, nullptr);
                if (got > 0.0 && std::fabs (got - t) <= t * 0.02) ++good;
                else bad += " " + f (t, 0) + "->" + f (got, 2); }
            ok (good >= 7, "[8] 🚨 IT HEARS THE BEAT — eight library tempos, read from the AUDIO alone",
                std::to_string (good) + " of 8 within 2 %" + (bad.empty() ? "" : ", missed:" + bad));
        }
        // [9] THE 117 THAT STARTED THIS, with a tail that makes the length lie
        {
            auto x = loop (117.0, 16, 0.9);
            const double lenS  = (double) x.size() / SR;
            const double byLen = fromLength (lenS, 130.0);
            const double heard = fromAudio (x.data(), (int) x.size(), SR, 130.0, nullptr);
            const double full  = detectFull ("Drum Loop.wav", lenS, 130.0, x.data(), (int) x.size(), SR);
            ok (std::fabs (full - 117.0) <= 117.0 * 0.02,
                "[9] 🚨 MAX'S LOOP: 117 BPM, NO NUMBER IN THE NAME, AND A TAIL THAT MAKES THE LENGTH LIE",
                "the length alone says " + f (byLen, 2) + " (wrong by " + f (byLen - 117.0, 2)
                + ") · the audio says " + f (heard, 2) + " · detectFull says " + f (full, 2));
        }
        // [10] a clean export SNAPS to the exact bar count — best of both readings
        {
            auto x = loop (117.0, 16, 0.0);
            const double lenS = (double) x.size() / SR;
            const double full = detectFull ("Drum Loop.wav", lenS, 130.0, x.data(), (int) x.size(), SR);
            ok (std::fabs (full - 117.0) < 0.001,
                "[10] A CLEAN 4-BAR EXPORT LOCKS DEAD ON, not to the estimator's 116.5",
                "detectFull = " + f (full, 4) + " (the audio heard "
                + f (fromAudio (x.data(), (int) x.size(), SR, 130.0, nullptr), 3)
                + ", the bar count is exact and within the 2 % snap)");
        }
        // [11] 🚨 IT STILL REFUSES. A pad and a one-shot must come back 0 — the whole point of the
        //      0 is that an unlabelled one-shot never enters a stretcher.
        {
            const int n = (int) (SR * 8.0); std::vector<float> pad ((size_t) n);
            for (int i = 0; i < n; ++i)
                pad[(size_t)i] = (float) (0.4 * std::sin (2 * M_PI * 110.0 * i / SR)
                                          * (1 - std::exp (-i / (SR * 0.5))));
            auto one = loop (120.0, 1, 0.0); one.resize ((size_t)(SR * 0.5));
            double cPad = 0, cOne = 0;
            const double gPad = fromAudio (pad.data(), (int) pad.size(), SR, 130.0, &cPad);
            const double gOne = fromAudio (one.data(), (int) one.size(), SR, 130.0, &cOne);
            ok (gPad == 0.0 && gOne == 0.0,
                "[11] 🚨 A SUSTAINED PAD AND A ONE-SHOT COME BACK 0 — a guess dressed as an answer "
                "would put every unlabelled sample through a stretcher",
                "pad " + f (gPad, 2) + " (confidence " + f (cPad, 3) + ")  ·  one-shot "
                + f (gOne, 2) + " (confidence " + f (cOne, 3) + ")");
        }
        // [12] silence and nonsense arguments cannot throw or invent a tempo
        {
            std::vector<float> zero ((size_t)(SR * 6.0), 0.f);
            const bool safe = fromAudio (nullptr, 0, SR, 120.0, nullptr) == 0.0
                           && fromAudio (zero.data(), (int) zero.size(), SR, 120.0, nullptr) == 0.0
                           && fromAudio (zero.data(), (int) zero.size(), 0.0, 120.0, nullptr) == 0.0
                           && fromAudio (zero.data(), 10, SR, 120.0, nullptr) == 0.0;
            ok (safe, "[12] NULL, SILENCE, NO SAMPLE RATE AND A TEN-SAMPLE BUFFER ALL RETURN 0", "no throw, no guess");
        }
    }

    printf ("\n  %d passed, %d failed\n\n", pass, fail);
    return fail ? 1 : 0;
}
