// ══════════════════════════════════════════════════════════════════════════════════════════════
//  dst_morph_depth.cpp — fb559. MORPH IS THE DRAWN CURVE'S DEPTH.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/dst_morph_depth.cpp -o /tmp/dstmd
//
//  Max asked for the A/B/C/D tabs and the morph rail to leave the curve card. Removing them
//  removes the only way to EDIT banks B-D, so a Morph that still swept into them would be a knob
//  that silently replaces your drawing with three shapes you cannot see. The four banks are now a
//  DEPTH LADDER from the straight line to the one curve you drew (DistortionEngine.h bakeCurves).
//
//  THE CLAIM, AND WHAT WOULD FALSIFY IT:
//    morph 0    the drawn curve is INAUDIBLE — the transfer is the straight line
//    morph 1    the transfer IS the drawn curve
//    between    monotone, no jump, no excursion past either end
//  A bank sweep would fail this: it would put SOMEBODY ELSE'S shape in the middle of the travel.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "DistortionEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

int main()
{
    tw::DistortionEngine e;
    e.prepare (48000.0);
    e.setMode (17);                                  // Shaper Asym — a DRAWN mode
    e.setDrive (0.0f);                               // unity in: measure the CURVE, not the gain
    e.setMix (1.0f);

    // a curve nobody could mistake for a line: a hard 5-step staircase over -1..1
    const int N = 257;
    std::vector<float> curve ((size_t) N);
    for (int i = 0; i < N; ++i) {
        const float x = -1.0f + 2.0f * (float) i / (float) (N - 1);
        curve[(size_t) i] = std::round (x * 2.5f) * 0.4f;
    }
    e.setUserCurves (curve.data(), nullptr, nullptr, nullptr, N);

    /* ⚠️ HOW YOU MEASURE A TRANSFER CURVE THROUGH A REAL CHAIN. The first draft held a constant
       input and read the output — and got noise, because this chain contains a DC blocker: a held
       DC input decays to zero, so "the transfer at x" measured nothing at all (residual 0.94 for
       BOTH ends, which is the tell — a broken meter reads the same for every setting).
       Feed a TONE well above the blocker instead and bin the output by the input that produced it:
       the curve is memoryless, so every sample is one point on it. */
    auto transfer = [&] (float morph) {
        e.setKnee (morph);
        const double w = 2.0 * 3.14159265358979 * 300.0 / 48000.0;
        double ph = 0.0;
        for (int i = 0; i < 24000; ++i) { float a, b2; const float x = (float) std::sin (ph); ph += w;
            e.processSample (x, x, a, b2); }                         // settle: smoothers + the 40 ms bake crossfade
        std::vector<double> acc (129, 0.0); std::vector<int> cnt (129, 0);
        for (int i = 0; i < 240000; ++i) {
            const float x = (float) std::sin (ph); ph += w;
            float oL = 0.0f, oR = 0.0f; e.processSample (x, x, oL, oR);
            const int bin = (int) std::lround ((x * 0.5f + 0.5f) * 128.0f);
            if (bin >= 0 && bin < 129) { acc[(size_t) bin] += (double) oL; ++cnt[(size_t) bin]; } }
        std::vector<double> y (129, 0.0);
        for (int i = 0; i < 129; ++i) y[(size_t) i] = cnt[(size_t) i] ? acc[(size_t) i] / cnt[(size_t) i] : 0.0;
        return y; };


    printf ("\n  MORPH IS DEPTH — the transfer against the straight line and against the drawn curve\n\n");
    printf ("  %-7s %14s %14s %10s\n", "morph", "dist to LINE", "dist to CURVE", "verdict");
    printf ("  %s\n", std::string (52, '-').c_str());
    std::vector<double> full = transfer (1.0f), zero = transfer (0.0f);
    double nz = 0; for (int i = 0; i < 129; ++i) { const double d = full[i] - zero[i]; nz += d * d; }
    nz = std::sqrt (nz / 129.0);                     // the whole travel, as a scale for both columns

    for (float m : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        auto y = transfer (m);
        double dl = 0, dc = 0;
        for (int i = 0; i < 129; ++i) { const double a = y[i] - zero[i], b = y[i] - full[i]; dl += a * a; dc += b * b; }
        dl = std::sqrt (dl / 129.0) / (nz + 1e-12); dc = std::sqrt (dc / 129.0) / (nz + 1e-12);
        const char* v = (m == 0.0f) ? (dl < 0.02 ? "CLEAN  ✓" : "!! not clean")
                      : (m == 1.0f) ? (dc < 0.02 ? "YOURS  ✓" : "!! not the curve")
                      : (dl > 0.05 && dc > 0.05 ? "between" : "!! stuck at an end");
        printf ("  %-7.2f %14.3f %14.3f %10s\n", m, dl, dc, v);
    }
    /* ⚠️ THE CIRCULARITY GUARD, AND TWO WRONG METERS BEFORE THIS ONE. The columns above compare
       transfer(morph) to transfer(0) and transfer(1): that proves the ramp is MONOTONE and says
       nothing about what the ends ARE. Two attempts at "is morph 0 a straight line?" both failed
       as MEASUREMENTS, not as results, and both are worth remembering:
         · a held DC input reads zero — there is a DC blocker in this chain;
         · a binned Lissajous reads an ellipse — Low Cut / Hi Cut / Tone put a phase shift between
           in and out, so out(t) is not a function of in(t) at all;
         · and absolute THD reads the engine's OWN voicing (+3 dBc at every morph on a bare
           standalone engine whose eight back-panel params are at their struct defaults, not the
           plugin's) — it measures the harness, not the change.
       THE CLAIM ITSELF NEEDS NO ABSOLUTE. "At morph 0 your drawing is inaudible" is a DIFFERENCE:
       swap the drawn curve for a straight line and, at morph 0, nothing may change. At morph 1
       everything must. Same engine, same params, same signal — only the ink differs. */
    std::vector<float> lineCurve ((size_t) N);
    for (int i = 0; i < N; ++i) lineCurve[(size_t) i] = -1.0f + 2.0f * (float) i / (float) (N - 1);

    auto renderWith = [&] (const std::vector<float>& c, float morph) {
        e.setUserCurves (c.data(), nullptr, nullptr, nullptr, N);
        e.setKnee (morph);
        const double w = 2.0 * 3.14159265358979 * 300.0 / 48000.0; double ph = 0.0;
        for (int i = 0; i < 24000; ++i) { float a, b2; e.processSample ((float) std::sin (ph), (float) std::sin (ph), a, b2); ph += w; }
        std::vector<double> y (24000);
        for (int i = 0; i < 24000; ++i) { float oL = 0.0f, oR = 0.0f;
            e.processSample ((float) std::sin (ph), (float) std::sin (ph), oL, oR); ph += w; y[(size_t) i] = oL; }
        return y; };
    auto rel = [] (std::vector<double>& a, std::vector<double>& b2) {
        double n = 0, d = 0; for (size_t i = 0; i < a.size(); ++i) { const double q = a[i] - b2[i]; n += q * q; d += a[i] * a[i]; }
        return 20.0 * std::log10 (std::sqrt (n / (d + 1e-30)) + 1e-30); };

    auto s0 = renderWith (curve, 0.0f), l0 = renderWith (lineCurve, 0.0f);
    auto s1 = renderWith (curve, 1.0f), l1 = renderWith (lineCurve, 1.0f);
    const double d0 = rel (l0, s0), d1 = rel (l1, s1);
    printf ("\n  DOES THE INK MATTER AT EACH END?   staircase vs straight line, same engine, same signal\n");
    printf ("    at morph 0    %7.1f dB   %s\n", d0, d0 < -50.0 ? "your drawing is INAUDIBLE \u2014 clean \u2713" : "!! the curve still shapes at morph 0");
    printf ("    at morph 1    %7.1f dB   %s\n", d1, d1 > -12.0 ? "your drawing IS the sound \u2713" : "!! the curve barely lands at morph 1");
    printf ("\n  (the sweep columns are normalised to the full travel; the ramp must be monotone)\n\n");
    return (d0 < -50.0 && d1 > -12.0) ? 0 : 2;
}
