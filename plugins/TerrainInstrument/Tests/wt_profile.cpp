// wt_profile.cpp — measure the DELIVERED spectrum of every table in the bank.
// Answers one question: is a low harmonic count a designed TAPER or an absent-content CLIFF?
#include "WavetableBank.h"
#include "wtnames.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

struct Prof { int declared, Ntop, N60; double neff, edgeCliffDb, slopeDbOct; };

static Prof profileFrame (const tw::FrameSpec& f)
{
    Prof p {}; p.declared = f.numHarmonics;
    std::vector<double> a (tw::FrameSpec::kMaxHarmonics + 2, 0.0);
    double peak = 0.0;

    if (f.numPartials > 0)                       // inharmonic list -> bin by nearest harmonic no.
        for (int i = 0; i < f.numPartials; ++i)
        {
            const int h = (int) std::lround (f.partials[(size_t) i].ratio);
            if (h >= 1 && h <= tw::FrameSpec::kMaxHarmonics)
                a[(size_t) h] += std::abs ((double) f.partials[(size_t) i].amp);
        }
    else
        for (int h = 1; h <= f.numHarmonics && h <= tw::FrameSpec::kMaxHarmonics; ++h)
            a[(size_t) h] = std::abs ((double) f.amplitudes[(size_t) (h - 1)]);

    for (int h = 1; h <= tw::FrameSpec::kMaxHarmonics; ++h) peak = std::max (peak, a[(size_t) h]);
    if (peak <= 0.0) return p;

    const double thr = peak * 1.0e-3;            // -60 dBc
    for (int h = 1; h <= tw::FrameSpec::kMaxHarmonics; ++h)
        if (a[(size_t) h] >= thr) { ++p.N60; p.Ntop = h; }

    double s2 = 0.0, s4 = 0.0;
    for (int h = 1; h <= tw::FrameSpec::kMaxHarmonics; ++h)
    { const double e = a[(size_t) h] * a[(size_t) h]; s2 += e; s4 += e * e; }
    p.neff = (s4 > 0.0) ? (s2 * s2) / s4 : 0.0;  // participation ratio

    // How abruptly does it END? taper -> a few dB. hard cut -> tens of dB (or -inf).
    const double top  = a[(size_t) p.Ntop];
    const double next = (p.Ntop + 1 <= tw::FrameSpec::kMaxHarmonics) ? a[(size_t) (p.Ntop + 1)] : 0.0;
    p.edgeCliffDb = (next > 0.0) ? 20.0 * std::log10 (top / next) : 999.0;   // 999 = literally nothing above

    // local slope over the top octave of delivered content, dB/oct
    const int lo = std::max (1, p.Ntop / 2);
    if (p.Ntop > lo && a[(size_t) lo] > 0.0 && top > 0.0)
        p.slopeDbOct = 20.0 * std::log10 (a[(size_t) lo] / top);
    return p;
}

int main()
{
    printf ("%-4s %-16s %8s %6s %6s %8s %9s %8s %7s\n",
            "idx","table","declared","Ntop","N60","Neff","edgeCliff","slope","C1 BW%");
    printf ("%s\n", std::string (86, '-').c_str());

    const double c1Harm = 733.0;                 // 24000/32.703 — harmonics Nyquist allows at C1
    for (int i = 0; i < (int) tw::WavetableBank::kNumPresets; ++i)
    {
        const auto spec = tw::WavetableBank::specForPreset ((tw::WavetableBank::Preset) i);
        Prof best {}; best.Ntop = -1;
        for (int fr = 0; fr < tw::WavetableSpec::kNumFrames; ++fr)
        {
            const Prof p = profileFrame (spec.frames[(size_t) fr]);
            if (p.Ntop > best.Ntop) best = p;
        }
        printf ("%-4d %-16s %8d %6d %6d %8.1f %9s %8.1f %6.1f%%\n",
                i, kWtNames[i], best.declared, best.Ntop, best.N60, best.neff,
                best.edgeCliffDb >= 999.0 ? "INF" :
                    (std::string(std::to_string((int)std::lround(best.edgeCliffDb))) + " dB").c_str(),
                best.slopeDbOct, 100.0 * best.Ntop / c1Harm);
    }
    return 0;
}
