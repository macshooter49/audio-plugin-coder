// probe_ott_switch — the OTT switch-click matrix, same calibrated metric as probe_switch.cpp.
//   clang++ -O2 -std=c++17 probe_ott_switch.cpp -o /tmp/po && /tmp/po
#include "TerrainOttFx.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>
#include <cstdint>
using OP = tw::TerrainOttFx::Params; using OX = tw::TerrainOttFx;
static const float FS = 48000.0f;
static double rmsOf (const std::vector<float>& x, size_t a, size_t b)
{ double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i];
  return std::sqrt (s / std::max<size_t> (1, b - a)); }
static void addSaw (std::vector<float>& x, float f0, float amp)
{ for (int h = 1; h * f0 < 18000.0f; ++h) { const float a = amp / h;
    for (size_t i = 0; i < x.size(); ++i) x[i] += (float) (a * std::sin (2.0 * M_PI * h * f0 * i / FS)); } }

/** IDENTICAL to probe_switch.cpp's metric, and it is the answer to "the click bar sits 1.9x above
 *  the full scale of its own probe". The old gate divided the in-window max |Δy| by the max |Δy|
 *  ANYWHERE ELSE IN THE SAME TAKE — and the largest jump in the take was the engine's own t=0
 *  start-up burst (0.14163, 26x the steady-state max and 69x the input tone's own step), so the
 *  bar of 2.5x meant a click had to exceed |Δy| = 0.354 on a signal whose peak is 0.19. Nothing
 *  could trip it. There is no denominator here at all: k[f] = |y_switch − y_hold| / |y_hold| over
 *  1 ms frames is ZERO before the switch by construction, and the number reported is how many dB
 *  of gain the switch moved inside one millisecond. A planted +8.00 dB step reads 8.00. */
double stepDbPerMs (const std::vector<float>& ySw, const std::vector<float>& yHold, int at, double winMs)
{
    const int W = (int) (FS * 0.001f);
    const int off = at % W;
    const double ref = rmsOf (yHold, (size_t) (FS * 0.15f), yHold.size());
    const int nF = (int) ((ySw.size() - (size_t) off) / (size_t) W);
    std::vector<double> k ((size_t) nF, 0.0);
    for (int f = 0; f < nF; ++f)
    {
        double dd = 0.0, pp = 0.0;
        for (int i = off + f * W; i < off + (f + 1) * W; ++i)
        { const double d = (double) ySw[(size_t) i] - yHold[(size_t) i];
          dd += d * d; pp += (double) yHold[(size_t) i] * yHold[(size_t) i]; }
        const double rh = std::sqrt (pp / (double) W);
        k[(size_t) f] = (rh > 0.25 * ref) ? std::sqrt (dd / (double) W) / rh : (f > 0 ? k[(size_t) f - 1] : 0.0);
    }
    const int f0 = (at - off) / W, f1 = f0 + (int) winMs;
    double inW = 0.0;
    for (int f = 1; f < nF; ++f)
        if (f >= f0 && f <= f1) inW = std::max (inW, std::fabs (k[(size_t) f] - k[(size_t) f - 1]));
    return 20.0 * std::log10 (1.0 + inW);
}


/** ⚠️ WHY THE PROGRAMME IS NOISE AND NOT THE SAW CHORD.
 *  The metric below divides by the hold take's 1 ms RMS. The 55/110/165/220 Hz saw chord has an
 *  18.2 ms period with deep envelope nulls, so adjacent 1 ms frames legitimately differ by 25-35
 *  dB — measured: EVERY OTT Type x Character reads ~29 dB of "level step in one millisecond" on a
 *  STATIONARY programme with no parameter ever changed. That is the chord, not the device.
 *  Band-limited noise at the chord's own level has a steady 1 ms envelope (chi, +/-0.9 dB), is
 *  broadband so every band of a multiband device is driven, and is the standard dynamics probe. */
static std::vector<float> noiseProg (int n, float rms)
{
    std::vector<float> x ((size_t) n);
    uint32_t g = 0x2468ACE0u;
    for (int i = 0; i < n; ++i) { g = g * 1664525u + 1013904223u; x[(size_t) i] = (float) ((int32_t) g) * (1.0f / 2147483648.0f); }
    double a = 0.0; for (float v : x) a += (double) v * v;
    const float k = (float) (rms / std::sqrt (a / x.size()));
    for (auto& v : x) v *= k;
    return x;
}

double jump (OP a, OP b, int at, const std::vector<float>& prog)
{
    std::vector<float> lS = prog, rS = prog, lH = prog, rH = prog;
    OX eS; eS.prepare (FS, 64); eS.setParams (a);
    OX eH; eH.prepare (FS, 64); eH.setParams (a);
    for (int i = 0; i + 64 <= (int) prog.size(); i += 64)
    {
        eS.setParams (i < at ? a : b); eS.processStereo (&lS[(size_t) i], &rS[(size_t) i], 64);
        eH.setParams (a);              eH.processStereo (&lH[(size_t) i], &rH[(size_t) i], 64);
    }
    return stepDbPerMs (lS, lH, at, 1.0);
}

int main()
{
    auto prog = noiseProg ((int) (FS * 1.2f), 0.10f);
    const int ats[5] = { 14912, 19968, 25088, 30016, 35072 };
    {
        OX e; e.prepare (FS, 64); OP p; e.setParams (p);
        std::vector<float> l = prog, r = prog;
        for (int i = 0; i + 64 <= (int) prog.size(); i += 64) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 64); }
        auto planted = l; for (size_t i = (size_t) ats[2]; i < planted.size(); ++i) planted[i] *= 2.5119f;
        std::printf ("self-check: PLANTED instant +8.00 dB step reads %.2f dB/ms\n", stepDbPerMs (planted, l, ats[2], 1.0));
        std::printf ("self-check: no step at all reads %.4f dB/ms\n\n", stepDbPerMs (l, l, ats[2], 1.0));
    }
    double worst = 0.0; std::string wn;
    std::printf ("── OTT Type switch matrix: dB of gain moved in ONE MILLISECOND (bar 2.0) ──\n%-11s", "from\\to");
    for (int t = 0; t < OX::kNumTypes; ++t) std::printf ("%10.9s", OX::typeNames()[t]);
    std::printf ("\n");
    for (int ta = 0; ta < OX::kNumTypes; ++ta)
    {
        std::printf ("%-11.10s", OX::typeNames()[ta]);
        for (int tb = 0; tb < OX::kNumTypes; ++tb)
        {
            OP a; a.type = ta; OP b; b.type = tb;
            double m = 0.0; for (int k = 0; k < 5; ++k) m = std::max (m, jump (a, b, ats[k], prog));
            std::printf ("%10.2f", m);
            if (m > worst) { worst = m; wn = std::string (OX::typeNames()[ta]) + "→" + OX::typeNames()[tb]; }
        }
        std::printf ("\n");
    }
    std::printf ("worst Type transition: %.2f dB/ms  (%s)\n\n", worst, wn.c_str());
    double cw = 0.0; std::string cn;
    for (int t = 0; t < OX::kNumTypes; ++t)
    {
        std::printf ("%-11.10s", OX::typeNames()[t]);
        for (int ca = 0; ca < OX::kNumChars; ++ca)
        {
            double m = 0.0;
            for (int cb = 0; cb < OX::kNumChars; ++cb)
            { if (ca == cb) continue; OP a; a.type = t; a.character = ca; OP b; b.type = t; b.character = cb;
              for (int k = 0; k < 3; ++k) m = std::max (m, jump (a, b, ats[k], prog)); }
            std::printf ("%7.2f", m);
            if (m > cw) { cw = m; cn = std::string (OX::typeNames()[t]) + " char " + OX::charNames (t)[ca]; }
        }
        std::printf ("\n");
    }
    std::printf ("worst Character transition: %.2f dB/ms  (from %s)\n", cw, cn.c_str());
    { OP a, b; double m = 0.0; for (int k = 0; k < 5; ++k) { b.axis = 2; m = std::max (m, jump (a, b, ats[k], prog)); }
      std::printf ("Stereo Linked → Mid-Side: %.2f dB/ms\n", m); }
    { OP a, b; double m = 0.0; for (int k = 0; k < 5; ++k) { b.amount = 1.0f; m = std::max (m, jump (a, b, ats[k], prog)); }
      std::printf ("Amount 0.5 → 1.0:         %.2f dB/ms\n", m); }
    { OP a; a.type = 0; OP b; b.type = 6; double m = 0.0;
      for (int k = 0; k < 5; ++k) m = std::max (m, jump (a, b, ats[k], prog));
      std::printf ("THE TREE SWAP  Over Top → Two Band: %.2f dB/ms\n", m); }
    return 0;
}
