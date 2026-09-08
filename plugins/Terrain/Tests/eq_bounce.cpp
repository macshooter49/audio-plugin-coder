// eq_bounce — MEASURE Max's "it bounces up and down like a goofball when I drag a sharp notch".
// Walk a sharp free bell's centre frequency across the log axis in fine steps, settle the engine
// fully at each step, and read the DRAWN extremum out of the engine's own viz curve. The peak-to-
// peak of that drawn extremum across the walk IS the bounce, in dB. 1 dB = 1.1 px on the card.
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include "TerrainEqualizerFx.h"
using EQ = tw::TerrainEqualizerFx;
static constexpr float FS = 48000.0f;

struct Walk { double drawnMin, drawnMax, ptp, worstStep; };

// settle the engine on ONE setting, then read the curve
static void settled (EQ& e, const EQ::Params& p, std::vector<float>& out)
{
    std::vector<float> L (128, 0.0f), R (128, 0.0f);
    for (int b = 0; b < 400; ++b)                       // ~1.07 s at 48k/128 — every smoother home
    { for (int i = 0; i < 128; ++i) { L[(size_t) i] = 0.0f; R[(size_t) i] = 0.0f; }
      e.setParams (p); e.processStereo (L.data(), R.data(), 128); }
    out.resize ((size_t) EQ::kCurveBins);
    for (int i = 0; i < EQ::kCurveBins; ++i) out[(size_t) i] = e.viz().curve[i];
}

// the deepest point the CARD would draw (the curve's extremum, which is what the eye tracks)
static double drawnDepth (const std::vector<float>& c)
{ double m = 1e9; for (float v : c) m = std::min (m, (double) v); return m; }

int main (int argc, char** argv)
{
    const double qMul  = (argc > 1) ? atof (argv[1]) : 1.0;    // 1.0 = the wheel at max = x8 Q
    const int    steps = (argc > 2) ? atoi (argv[2]) : 240;

    EQ::Params p;                       // Type 0, everything neutral
    p.xOn1 = true;                      // one free bell
    p.x2   = 0.0f;                      // its gain: full CUT
    p.q5   = (float) qMul;              // its Q multiplier

    std::vector<double> depth ((size_t) steps, 0.0);
    std::vector<double> hz    ((size_t) steps, 0.0);
    std::vector<float>  c;
    // walk one decade of the axis: t 0.40 -> 0.60 == 251 Hz -> 1259 Hz
    for (int s = 0; s < steps; ++s)
    {
        const double t = 0.40 + 0.20 * (double) s / (double) (steps - 1);
        p.x1 = (float) t;
        EQ e; e.prepare ((double) FS, 128);             // fresh + fully settled: pure grid effect
        settled (e, p, c);
        depth[(size_t) s] = drawnDepth (c);
        hz[(size_t) s]    = 20.0 * std::pow (1000.0, t);
    }
    double lo = 1e9, hiV = -1e9, worst = 0.0; int worstAt = 0;
    for (int s = 0; s < steps; ++s) { lo = std::min (lo, depth[(size_t) s]); hiV = std::max (hiV, depth[(size_t) s]); }
    for (int s = 1; s < steps; ++s)
    { const double d = std::fabs (depth[(size_t) s] - depth[(size_t) s - 1]);
      if (d > worst) { worst = d; worstAt = s; } }

    printf ("bins=%-5d  qMul=%.2f  steps=%d\n", EQ::kCurveBins, qMul, steps);
    printf ("  drawn notch depth over the walk:  %.2f .. %.2f dB\n", lo, hiV);
    printf ("  BOUNCE (peak-to-peak)          :  %.2f dB   (= %.1f px on the card at 1.1 px/dB)\n",
            hiV - lo, (hiV - lo) * 1.1);
    printf ("  worst single step              :  %.2f dB  near %.0f Hz\n", worst, hz[(size_t) worstAt]);
    if (argc > 3)   // dump the walk
        for (int s = 0; s < steps; ++s) printf ("    %8.1f Hz  %8.2f dB\n", hz[(size_t) s], depth[(size_t) s]);
    return 0;
}
