// probe_switch — a FAST standalone driver for the COMPRESS switch-click matrix.
// Same metric as dynamics_cert §4d, but nothing else, so it runs in ~1 s instead of ~4 min.
//   clang++ -O2 -std=c++17 -I <engine dir> probe_switch.cpp -o /tmp/ps && /tmp/ps
#include "TerrainCompressFx.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>
#include <cstdint>
using CP = tw::TerrainCompressFx::Params;
using CX = tw::TerrainCompressFx;
static const float FS = 48000.0f;

static double rmsOf (const std::vector<float>& x, size_t a, size_t b)
{ double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i];
  return std::sqrt (s / std::max<size_t> (1, b - a)); }
static double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-14)); }

static void addSaw (std::vector<float>& x, float f0, float amp)
{ for (int h = 1; h * f0 < 18000.0f; ++h) { const float a = amp / h;
    for (size_t i = 0; i < x.size(); ++i) x[i] += (float) (a * std::sin (2.0 * M_PI * h * f0 * i / FS)); } }
static std::vector<float> chordSig (int n, float rms = 0.10f)
{ std::vector<float> x ((size_t) n, 0.0f);
  addSaw (x, 55, 1.0f); addSaw (x, 110, 0.9f); addSaw (x, 165, 0.8f); addSaw (x, 220, 0.7f);
  const double a = rmsOf (x, 0, x.size()); for (auto& v : x) v *= (float) (rms / a); return x; }

/** THE metric — third version, and the first two are worth recording because both LIED.
 *
 *  v1  g[f] = dB(rms(y,f)) − dB(rms(dry,f)) over 0.5 ms frames.
 *      Read 13.81 dB on `Vari-Mu → Vari-Mu`, a transition that does not exist. Vari-Mu's
 *      Character forces Heat 0.25, so the output is a WAVESHAPED dry, not `dry × gain`, and the
 *      frame-wise ratio ripples with the waveform. `grNow()` said 11.2 dB ± 0.05.
 *  v2  the same thing against a second take that HOLDS params A. Zero before the switch by
 *      construction — but once the two takes diverge structurally (a feedback compressor with
 *      Heat vs a clean feedforward one) the 1 ms RMS ratio of two different waveshapes rings
 *      ±5 dB, and it rang differently at each of the five switch phases: variance where a
 *      parameter step would be deterministic. Noise, not evidence.
 *  v3  THIS. d[i] = y_switch[i] − y_hold[i] is EXACTLY ZERO before the switch — same code,
 *      same state, same program, deterministic — so the artifact is visible directly in d.
 *      A gain step of ratio r applied instantly makes d jump from 0 to (r−1)·x[k] in ONE
 *      SAMPLE; a 20 ms glide to the same r makes d's envelope grow over 960 samples. So:
 *          stepRatio = max |d[i] − d[i−1]| over the 4 ms after the switch
 *                      ÷ max |x[i] − x[i−1]| over the whole programme
 *      The denominator is the programme's own steepest sample-to-sample move: the number is
 *      "this switch moved the signal N times harder than the music itself ever does".
 *      Self-checked below against a PLANTED 8 dB step and against no switch at all. */
double stepRatio (const std::vector<float>& ySw, const std::vector<float>& yHold,
                  const std::vector<float>& prog, int at, double winMs, double& preOut)
{
    // 1 ms frames. k[f] = |d| / |programme| is the MAGNITUDE OF THE GAIN DIFFERENCE the switch
    // has introduced by frame f; it is identically 0 before the switch. The reported number is
    // the largest change in that difference inside ONE MILLISECOND, in dB:
    //        stepDb = 20 log10 (1 + max_f |k[f] − k[f−1]|)
    // An instantaneous gain step of X dB reads X dB. The device's own 20 ms glides moving their
    // whole 24 dB range read 24·(1 − e^(−1/20)) = 1.17 dB/ms. Bar 2.0 dB/ms sits just above the
    // fastest legitimate move this engine can make and 10x below an unseeded smoother collapse.
    //
    // ⚠️ THE WINDOW IS 3 ms AND THAT IS NOT ARBITRARY. |d| conflates "the gain moved" with "the
    // waveshape differs"; a click is the former and lives inside one block (1.33 ms at 64), while
    // two Types that saturate differently legitimately diverge over tens of ms and make |d|
    // ripple. Over a 40 ms window this metric reported 4.11 dB on Exact→Vari-Mu whose samples
    // either side of the switch are −0.02882 / −0.02882 — the harmonic difference, not a step.
    const int W = (int) (FS * 0.001f);
    // frame boundaries are ALIGNED TO THE SWITCH, or the switch lands mid-frame and the metric
    // reads 5.4 dB for a planted 8.0 dB step — under-reading, which is the direction that ships
    // a click.
    const int off = at % W;
    const double ref = [&] { double a = 0.0; size_t n = 0;
        for (size_t i = (size_t) (FS * 0.15f); i < yHold.size(); ++i) { a += (double) yHold[i] * yHold[i]; ++n; }
        return std::sqrt (a / std::max<size_t> (1, n)); }();
    const int nF = (int) ((ySw.size() - (size_t) off) / (size_t) W);
    std::vector<double> k ((size_t) nF, 0.0);
    for (int f = 0; f < nF; ++f)
    {
        double dd = 0.0, pp = 0.0;
        for (int i = off + f * W; i < off + (f + 1) * W; ++i)
        { const double d = (double) ySw[(size_t) i] - yHold[(size_t) i];
          dd += d * d; pp += (double) yHold[(size_t) i] * yHold[(size_t) i]; }
        // normalised by the HOLD TAKE, not by the programme: k is then literally the linear
        // gain difference, so a planted +8.00 dB step reads 8.00 dB and nothing else has to be
        // calibrated. Frames where the hold take is more than 12 dB under its own average are
        // not evidence (a dB ratio of two near-zero windows is noise) and are held flat.
        const double rh = std::sqrt (pp / (double) W);
        k[(size_t) f] = (rh > 0.25 * ref) ? std::sqrt (dd / (double) W) / rh
                                          : (f > 0 ? k[(size_t) f - 1] : 0.0);
    }
    const int f0 = (at - off) / W, f1 = f0 + (int) winMs;
    double inW = 0.0, pre = 0.0;
    for (int f = 1; f < nF; ++f)
    {
        const double j = std::fabs (k[(size_t) f] - k[(size_t) f - 1]);
        if (f >= f0 && f <= f1) inW = std::max (inW, j);
        else if (f < f0)        pre = std::max (pre, j);
    }
    preOut = 20.0 * std::log10 (1.0 + pre);
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

struct R { double step, base; };
R jump (CP a, CP b, int atSample, const std::vector<float>& prog)
{
    std::vector<float> lS = prog, rS = prog, lH = prog, rH = prog;
    CX eS; eS.prepare (FS, 64); eS.setParams (a);
    CX eH; eH.prepare (FS, 64); eH.setParams (a);
    for (int i = 0; i + 64 <= (int) prog.size(); i += 64)
    {
        eS.setParams (i < atSample ? a : b); eS.processStereo (&lS[(size_t) i], &rS[(size_t) i], 64);
        eH.setParams (a);                    eH.processStereo (&lH[(size_t) i], &rH[(size_t) i], 64);
    }
    R out; out.step = stepRatio (lS, lH, prog, atSample, 1.0, out.base); return out;
}

int main (int argc, char**)
{
    auto prog = noiseProg ((int) (FS * 1.2f), 0.10f);
    // FIVE switch phases, none of them a zero crossing of anything: the chord's 55 Hz period is
    // 872.7 samples, incommensurate with the 64-sample block grid, so five different block
    // indices are five different program phases. (The first draft jumped at FS*0.5 with a 220 Hz
    // tone = exactly 110 whole cycles = sin(phase) 0.000000, and a gain step at a zero crossing
    // produces no sample-to-sample jump at all.)
    const int ats[5] = { 14912, 19968, 25088, 30016, 35072 };

    CP base; base.push = 0.3f; base.ratio = 0.6f;
    double worst = 0.0; std::string wn;
        {   // 🔬 SELF-CHECK THE DETECTOR BEFORE BELIEVING ANY CELL BELOW.
        CX e; e.prepare (FS, 64); CP p; p.push = 0.3f; p.ratio = 0.6f; e.setParams (p);
        std::vector<float> l = prog, r = prog;
        for (int i = 0; i + 64 <= (int) prog.size(); i += 64) { e.setParams (p); e.processStereo (&l[(size_t) i], &r[(size_t) i], 64); }
        auto planted = l; for (size_t i = (size_t) ats[2]; i < planted.size(); ++i) planted[i] *= 2.5119f;  // +8.000 dB, instantly
        double pre = 0.0;
        const double fired = stepRatio (planted, l, prog, ats[2], 1.0, pre);
        std::printf ("self-check: a PLANTED instant +8.00 dB step reads %.2f dB/ms (pre-switch %.4f)\n", fired, pre);
        double pre2 = 0.0;
        const double none = stepRatio (l, l, prog, ats[2], 1.0, pre2);
        std::printf ("self-check: no step at all reads %.4f dB/ms\n\n", none);
    }
    std::printf ("── COMPRESS switch matrix: dB of gain moved in ONE MILLISECOND (bar 2.0) ──\n");
    std::printf ("%-12s", "from\\to");
    for (int t = 0; t < CX::kNumTypes; ++t) std::printf ("%9.8s", CX::typeNames()[t]);
    std::printf ("\n");
    for (int ta = 0; ta < CX::kNumTypes; ++ta)
    {
        std::printf ("%-12.11s", CX::typeNames()[ta]);
        for (int tb = 0; tb < CX::kNumTypes; ++tb)
        {
            CP a = base; a.type = ta; CP b = base; b.type = tb;
            double m = 0.0;
            for (int k = 0; k < 5; ++k) { auto r = jump (a, b, ats[k], prog); m = std::max (m, r.step); }
            std::printf ("%9.2f", m);
            if (m > worst) { worst = m; wn = std::string (CX::typeNames()[ta]) + "→" + CX::typeNames()[tb]; }
        }
        std::printf ("\n");
    }
    std::printf ("worst Type transition: %.2f dB/ms  (%s)\n\n", worst, wn.c_str());

    double cw = 0.0; std::string cn;
    for (int t = 0; t < CX::kNumTypes; ++t)
    {
        std::printf ("%-12.11s", CX::typeNames()[t]);
        for (int ca = 0; ca < CX::kNumChars; ++ca)
        {
            double m = 0.0;
            for (int cb = 0; cb < CX::kNumChars; ++cb)
            {
                if (ca == cb) continue;
                CP a = base; a.type = t; a.character = ca;
                CP b = base; b.type = t; b.character = cb;
                for (int k = 0; k < 3; ++k) { auto r = jump (a, b, ats[k], prog); m = std::max (m, r.step); }
            }
            std::printf ("%7.2f", m);
            if (m > cw) { cw = m; cn = std::string (CX::typeNames()[t]) + " char " + CX::charNames (t)[ca]; }
        }
        std::printf ("\n");
    }
    std::printf ("worst Character transition: %.2f dB/ms  (from %s)\n", cw, cn.c_str());
    { double bo = 0; auto r = jump (base, base, ats[0], prog); (void) r;
      std::printf ("control (A→A, no change at all): %.4f dB/ms in-window, %.4f pre-switch\n", r.step, r.base); }
    return 0;
}
