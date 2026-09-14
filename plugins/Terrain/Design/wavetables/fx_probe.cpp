// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fx_probe.cpp — "Saw through Terrain's own X": the TERRAIN-FILTER half of the Processed category.
//
//  gen2_processed.py builds most Processed tables in numpy (exact frequency-domain filters, 8x
//  oversampled drive/crush/FM). This probe renders the rest through the SHIPPING filter code —
//  tw::filters::FilterSlot from Source/TerrainFilters.h, driven exactly as SynthVoice drives it
//  (Tests/flt_measure.h's Runner: the voice's own 2x polyphase half-band for the types that
//  needsOversampling(), setParams(cut, res, drv, coefSr) per sample). Nothing in Source/ is edited.
//
//  PERIODIC STEADY STATE. The filter runs at FS = 48 kHz on a band-limited source whose period is
//  EXACTLY 2048 samples (f0 = 23.4375 Hz), so one period of the output IS one wavetable frame and
//  every harmonic up to 1023 is reachable (cutoff in Hz = harmonic x 23.4375). Every frame starts
//  from reset(), runs CYC periods, and keeps the LAST period. For a stable filter the output of a
//  periodic input converges to a periodic output (the frame then loops seamlessly); the .txt meta
//  records the residual  max|last - previous period| / max|last|  per frame so a table that has
//  not converged (a comb near unity feedback, a filter in chaotic self-oscillation) is visible.
//  Aliasing of a nonlinear core lands on harmonics too (the period is an integer sample count),
//  so even the un-oversampled crushers stay exactly periodic.
//
//  JOB LINE (stdin, one per table; '#' comments):
//     NAME TYPEINDEX SOURCE key=a[:b[:curve]] ...
//  SOURCE: saw | sqr | tri | sine | pulNN (NN % pulse, e.g. pul25)  (coherent phase, edges away from the wrap: saw riser at
//          t = 0.5, square edges at t = 0.25 / 0.75), harmonics 1..NH (key nh, default 1000).
//  Keys ramp a -> b across the 128 frames with u' = u^curve:
//     cut   cutoff in Hz, GEOMETRIC ramp        cuth  cutoff in HARMONICS (x f0), geometric;
//     res   0..1 linear                         round=1 rounds cuth to an integer (ring mod carriers)
//     drv   0..1 linear                         morph 0..1 linear (OB-X/SEM)
//     lvl   input gain (linear ramp)            cyc   periods to run (default 32, not ramped)
//     dc    DC offset added to the input after lvl (linear ramp): biases a symmetric drive/folder so it
//           makes even harmonics (the filter's own DC blockers remove the offset from the output)
//     shifth  BODE shift in HARMONICS (x f0, geometric, sign = direction; round=1 -> integer): the
//             CUT that Bode's own log map turns into exactly that shift, so the output stays periodic
//             (Bode's map tops out at 1000 Hz = 42.67 harmonics: keep |shifth| <= 42)
//     crushm  BIT CRUSH / RADIO: holds per PERIOD (geometric, round=1 -> integer). The crusher's clock is
//             then overridden to exactly crushm/2048 of the rate after setParams, so the hold grid
//             locks to the period instead of beating against it (an exact float: m * 2^-11)
//
//  Build + run (from this directory):
//     c++ -std=c++17 -O2 -I ../../Tests -I ../../Tests/shim -I ../../Source fx_probe.cpp \
//         -framework Accelerate -o /tmp/fx_probe
//     python3 gen2_processed.py --jobs | /tmp/fx_probe <wt500>/fxdump
//  Writes <dir>/<NAME>.f32 (128 x 2048 float32, frames concatenated) + <dir>/<NAME>.txt.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <complex>
#include <algorithm>
#include <chrono>
#include <limits>
#include <array>
#include <atomic>
#include <iostream>
#include <sstream>
#include <map>
#include <functional>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#define private public          // probe-only: crushm pins the crusher clock to the period (see JOB LINE)
#include "flt_measure.h"
#undef private

static constexpr int P  = 2048;
static constexpr int NF = 128;
static const double  F0 = FS / (double) P;      // 23.4375 Hz — one period = one frame

struct Ramp { double a = 0, b = 0, c = 1; bool set = false; };

static std::vector<double> makeSource (const std::string& s, int nh)
{
    std::vector<double> x (P, 0.0);
    for (int k = 1; k <= nh && k < P / 2; ++k)
    {
        double amp = 0.0, ph = 0.0;   // x += amp * sin(2 pi k n/P + ph)
        const bool odd = (k & 1) != 0;
        if (s == "saw")       { amp = ((k & 1) ? 1.0 : -1.0) / k * (2.0 / M_PI); ph = 0.0; }
        else if (s == "sqr")  { if (! odd) continue; amp = (((k - 1) / 2) % 2 ? -1.0 : 1.0) / k * (4.0 / M_PI); ph = M_PI / 2; }
        else if (s == "tri")  { if (! odd) continue; amp = (((k - 1) / 2) % 2 ? -1.0 : 1.0) / ((double) k * k) * (8.0 / (M_PI * M_PI)); ph = 0.0; }
        else if (s == "sine") { if (k != 1) continue; amp = 1.0; }
        else if (s.rfind ("pul", 0) == 0)   // pulNN: NN % pulse centred on t = 0 (edges at +-NN/200)
        { const double w = std::atof (s.c_str() + 3) / 100.0; amp = 2.0 / (M_PI * k) * std::sin (M_PI * k * w); ph = M_PI / 2; }
        else { std::fprintf (stderr, "unknown source %s\n", s.c_str()); std::exit (2); }
        for (int n = 0; n < P; ++n) x[n] += amp * std::sin (2.0 * M_PI * k * n / P + ph);
    }
    double pk = 0; for (double v : x) pk = std::max (pk, std::fabs (v));
    for (double& v : x) v /= std::max (pk, 1e-12);
    return x;
}

static double ramp (const Ramp& r, double u, bool geo)
{
    const double w = std::pow (u, r.c);
    if (geo) return r.a * std::pow (r.b / r.a, w);
    return r.a + (r.b - r.a) * w;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: fx_probe OUTDIR < jobs\n"); return 2; }
    const std::string dir = argv[1];
    std::string line;
    int nJobs = 0;
    while (std::getline (std::cin, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is (line);
        std::string name, src; int typeIdx = -1;
        if (! (is >> name >> typeIdx >> src)) continue;
        std::map<std::string, Ramp> kv;
        std::string tok;
        while (is >> tok)
        {
            const auto eq = tok.find ('=');
            if (eq == std::string::npos) continue;
            Ramp r; r.set = true;
            std::string v = tok.substr (eq + 1);
            std::vector<double> parts; std::stringstream ss (v); std::string p;
            while (std::getline (ss, p, ':')) parts.push_back (std::atof (p.c_str()));
            r.a = parts.size() > 0 ? parts[0] : 0; r.b = parts.size() > 1 ? parts[1] : r.a; r.c = parts.size() > 2 ? parts[2] : 1.0;
            kv[tok.substr (0, eq)] = r;
        }
        auto has = [&] (const char* k) { return kv.count (k) && kv[k].set; };
        const int nh  = has ("nh")  ? (int) kv["nh"].a  : 1000;
        const int cyc = has ("cyc") ? (int) kv["cyc"].a : 32;
        const bool rnd = has ("round") && kv["round"].a > 0.5;
        const std::vector<double> x = makeSource (src, nh);
        std::vector<float> out ((size_t) NF * P, 0.f);
        double worstConv = 0.0; int worstF = 0; bool bad = false;
        Runner R; R.init (typeIdx);
        for (int f = 0; f < NF; ++f)
        {
            const double u = (double) f / (NF - 1);
            double cut = 1000.0;
            if (has ("cut"))  cut = ramp (kv["cut"], u, true);
            if (has ("cuth")) { double h = ramp (kv["cuth"], u, true); if (rnd) h = std::round (h); cut = h * F0; }
            if (has ("shifth"))
            {   // invert BodeShifter::setParams: fshift = sign * (1001^(2|cut01-0.5|) - 1), cut01 = log(cut/20)/log(1000)
                double h = ramp (kv["shifth"], u, true); if (rnd) h = std::round (h);
                const double hz = std::fabs (h) * F0, sg = h < 0 ? -1.0 : 1.0;
                const double cut01 = 0.5 + sg * std::log (1.0 + hz) / (2.0 * std::log (1001.0));
                cut = 20.0 * std::pow (1000.0, cut01);
            }
            const float res = has ("res") ? (float) ramp (kv["res"], u, false) : 0.5f;
            const float drv = has ("drv") ? (float) ramp (kv["drv"], u, false) : 0.0f;
            const double lvl = has ("lvl") ? ramp (kv["lvl"], u, false) : 1.0;
            const double dcx = has ("dc") ? ramp (kv["dc"], u, false) : 0.0;
            R.resetState();
            R.f.setMorph (has ("morph") ? (float) ramp (kv["morph"], u, false) : 0.0f);
            R.setP ((float) cut, res, drv);
            if (has ("crushm"))
            {
                double m = ramp (kv["crushm"], u, true); if (rnd) m = std::round (m);
                const float rr = (float) (m / (P * (R.os ? 2.0 : 1.0)));
                R.f.crushL_.rateRatio = rr; R.f.crushR_.rateRatio = rr;
            }
            std::vector<float> prev (P), cur (P);
            for (int c = 0; c < cyc; ++c)
            {
                for (int n = 0; n < P; ++n)
                {
                    R.setP ((float) cut, res, drv);
                    float oL, oR;
                    const float in = (float) (x[n] * lvl + dcx);
                    R.step (in, in, oL, oR);
                    if (c == cyc - 2) prev[n] = oL;
                    if (c == cyc - 1) cur[n]  = oL;
                }
            }
            double pk = 0, dmax = 0;
            for (int n = 0; n < P; ++n)
            {
                if (! std::isfinite (cur[n])) { bad = true; cur[n] = 0.f; }
                pk = std::max (pk, (double) std::fabs (cur[n]));
                dmax = std::max (dmax, (double) std::fabs (cur[n] - prev[n]));
            }
            const double conv = dmax / std::max (pk, 1e-12);
            if (conv > worstConv) { worstConv = conv; worstF = f; }
            std::copy (cur.begin(), cur.end(), out.begin() + (size_t) f * P);
        }
        const std::string fp = dir + "/" + name + ".f32";
        std::FILE* o = std::fopen (fp.c_str(), "wb");
        if (! o) { std::fprintf (stderr, "cannot write %s\n", fp.c_str()); return 1; }
        std::fwrite (out.data(), sizeof (float), out.size(), o); std::fclose (o);
        const std::string mp = dir + "/" + name + ".txt";
        std::FILE* m = std::fopen (mp.c_str(), "w");
        std::fprintf (m, "%s\nworst_conv %.6f frame %d nonfinite %d\n", line.c_str(), worstConv, worstF, bad ? 1 : 0);
        std::fclose (m);
        std::printf ("  %-26s type %3d %-5s os %d  worst period residual %.4f (frame %3d)%s\n", name.c_str(), typeIdx,
                     src.c_str(), R.os ? 1 : 0, worstConv, worstF, bad ? "  NONFINITE" : "");
        std::fflush (stdout);
        ++nJobs;
    }
    std::printf ("fx_probe: %d tables -> %s\n", nJobs, dir.c_str());
    return 0;
}
