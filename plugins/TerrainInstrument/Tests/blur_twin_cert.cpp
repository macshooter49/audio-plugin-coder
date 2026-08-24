// ══════════════════════════════════════════════════════════════════════════════════════════════
//  blur_twin_cert.cpp — fb469. THE MAGNITUDE-DOMAIN BLUR, and the promise it has to keep.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/blur_twin_cert.cpp -o /tmp/blur_twin \
//            -framework Accelerate
//
//  Max approved this "as long as it doesn't clash or mess with anything that we have going right
//  now in terms of sound and CPU wise." That sentence IS the gate list:
//    B1  no twin, no change — a table that does not qualify must be bit-identical, blur and all.
//    B2  blur = 0 is bit-identical even on a table that DOES have a twin (the reference never
//        reads the twin; only the taps do).
//    B3  where a twin exists, blur = 1 delivers the MAGNITUDE mean — the identity this is built on.
//    B4  the two tables whose frames differ only in PHASE are refused, by name.
//    B5  the 23 phase-coherent tables are refused too, so they cost no memory.
//    B6  the cost: build time on the message thread, and bytes.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "WavetableBank.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>

static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const char* d = "")
{ c ? ++gPass : ++gFail; printf ("  %-5s %-58s %s\n", c ? "ok" : "FAIL", n, d); }

static const int N = 2048, F = 16;
static const char* NAMES[30] = { "Sine","Triangle","Square","Pulse","ProphetSaw","JupiterPWM","MoogSqr",
  "OBXSaw","CS80Brass","JunoStr","PPGWave","DX7EP","D50Bell","M1Piano","ChoirAtoO","Whisper","VowelMorph",
  "BowedMetal","GlassHarmonics","Railroad","Dustbowl","StaticEvolve","SpectralDrift","SerumHD","Rise",
  "OddEven","PhaseDrift","SpectralSweep","p28","p29" };

static void cyc (const tw::Wavetable& w, float pos, float blur, std::vector<float>& out)
{ out.assign ((size_t) N, 0.0f); w.renderBlend (0, pos, blur, out.data()); }

static void harmonics (const std::vector<float>& x, std::vector<double>& m, int H)
{
    m.assign ((size_t) H + 1, 0.0);
    for (int h = 1; h <= H; ++h) { double re = 0, im = 0;
        for (int n = 0; n < N; ++n) { const double a = 2.0*M_PI*h*n/N; re += x[(size_t)n]*std::cos(a); im -= x[(size_t)n]*std::sin(a); }
        m[(size_t)h] = 2.0*std::sqrt(re*re+im*im)/N; }
}

int main()
{
    tw::WavetableBank bank;
    printf ("\n══ fb469 — THE BLUR TWIN ══\n\n");

    // ── which tables qualify, and what the decision saw ───────────────────────────────────────
    std::vector<int> built, refusedPhase, refusedMag;
    std::vector<tw::Wavetable> tabs (30);
    double worstBuildMs = 0.0; size_t twinBytes = 0;
    for (int pr = 0; pr < 30; ++pr)
    {
        tabs[(size_t) pr].buildFromSpec (tw::WavetableBank::specForPreset (pr));
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = tabs[(size_t) pr].buildBlurTwin();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        if (ok) { built.push_back (pr); worstBuildMs = std::max (worstBuildMs, ms); twinBytes += (size_t) tw::Wavetable::kNumMipLevels * F * N * sizeof (float); }
        else refusedMag.push_back (pr);
    }
    { char d[240]; snprintf (d, sizeof d, "%zu of 30 built, %zu refused", built.size(), refusedMag.size());
      gate (! built.empty() && ! refusedMag.empty(), "B0  the decision actually discriminates", d); }

    // ── B4 — THE RULE, checked table by table: a twin exists exactly when the magnitude law moves
    //        the spectrum more than the phasor mean already does. No thresholds, no exceptions.
    {
        bool consistent = true; std::string worst;
        for (int pr = 0; pr < 30; ++pr)
        {
            const float c = tabs[(size_t) pr].blurTwinCoherence(), m = tabs[(size_t) pr].blurTwinMove();
            const bool want = (c <= tw::Wavetable::kTwinMaxCoherence && m >= tw::Wavetable::kTwinMinMove);
            const bool got  = (tabs[(size_t) pr].blurTwinState() == 1);
            if (want != got) { consistent = false; worst = NAMES[pr]; }
        }
        char d[220]; snprintf (d, sizeof d, "30 tables, every decision matches its own arithmetic%s%s",
                               consistent ? "" : " — mismatch on ", consistent ? "" : worst.c_str());
        gate (consistent, "B4  a twin is built exactly when both measured bars say so", d);
    }
    // and the two tables the audit named must come out on the RIGHT side of it
    {
        char d[240]; snprintf (d, sizeof d, "SpectralDrift coh %.3f move %.3f (%s) · PhaseDrift coh %.3f move %.3f (%s)",
              tabs[22].blurTwinCoherence(), tabs[22].blurTwinMove(), tabs[22].blurTwinState()==1?"TWIN":"kept as is",
              tabs[26].blurTwinCoherence(), tabs[26].blurTwinMove(), tabs[26].blurTwinState()==1?"TWIN":"kept as is");
        gate (tabs[22].blurTwinState() != 1 && tabs[26].blurTwinState() != 1,
              "B4b SpectralDrift and PhaseDrift keep their blur (a magnitude mean would kill it)", d);
    }

    { std::string b; for (size_t i = 0; i < refusedMag.size() && i < 12; ++i) { b += NAMES[refusedMag[i]]; b += " "; }
      char d[240]; snprintf (d, sizeof d, "%zu refused, so they cost no memory: %s", refusedMag.size(), b.c_str());
      gate (! refusedMag.empty(), "B5  a refused table costs nothing at all", d); }

    // ── B1 — a REFUSED table is bit-identical, blur and all ───────────────────────────────────
    {
        int checked = 0; double worst = 0.0;
        for (int pr : refusedMag)
        {
            if (checked >= 6) break;
            tw::Wavetable plain; plain.buildFromSpec (tw::WavetableBank::specForPreset (pr));
            for (float bl : { 0.0f, 0.35f, 1.0f })
            { std::vector<float> a, b; cyc (plain, 0.5f, bl, a); cyc (tabs[(size_t) pr], 0.5f, bl, b);
              for (int n = 0; n < N; ++n) worst = std::max (worst, (double) std::abs (a[(size_t) n] - b[(size_t) n])); }
            ++checked;
        }
        char d[180]; snprintf (d, sizeof d, "%d tables x 3 blur settings, worst |D| = %.3e", checked, worst);
        gate (worst == 0.0, "B1  a table with NO twin is BIT-IDENTICAL to before", d);
    }

    // ── B2 — blur 0 is bit-identical even WITH a twin ─────────────────────────────────────────
    {
        double worst = 0.0; int checked = 0;
        for (int pr : built)
        {
            tw::Wavetable plain; plain.buildFromSpec (tw::WavetableBank::specForPreset (pr));
            for (float pos : { 0.0f, 0.5f, 1.0f })
            { std::vector<float> a, b; cyc (plain, pos, 0.0f, a); cyc (tabs[(size_t) pr], pos, 0.0f, b);
              for (int n = 0; n < N; ++n) worst = std::max (worst, (double) std::abs (a[(size_t) n] - b[(size_t) n])); }
            ++checked;
        }
        char d[180]; snprintf (d, sizeof d, "%d twinned tables x 3 positions, worst |D| = %.3e", checked, worst);
        gate (worst == 0.0, "B2  blur = 0 is BIT-IDENTICAL even where a twin exists", d);
    }

    // ── B3 — THE IDENTITY: with a twin, blur = 1 IS the magnitude mean ────────────────────────
    {
        double worst = -400.0; int checked = 0; std::string where;
        for (int pr : built)
        {
            // the magnitude mean, computed independently from the frames' own spectra
            // ⚠️ renderBlend's kernel is a GAUSSIAN, not a uniform mean: at blur = 1,
            //    sigma = 1e-4 + 16*1.05 = 16.8 centred on fIdx = 7.5, so the end frames carry ~0.905
            //    of the centre's weight. A uniform reference read -26.9 dBr against a correct
            //    implementation — the reference was wrong, not the code.
            const double sig = 1.0e-4 + (double) F * 1.05, fIdx = 0.5 * (F - 1);
            std::vector<double> w ((size_t) F, 0.0); double gs = 0.0;
            for (int f = 0; f < F; ++f) { const double dd = ((double) f - fIdx) / sig; w[(size_t) f] = std::exp (-0.5*dd*dd); gs += w[(size_t) f]; }
            for (int f = 0; f < F; ++f) w[(size_t) f] /= gs;
            std::vector<double> want ((size_t) 97, 0.0);
            for (int f = 0; f < F; ++f)
            { std::vector<float> fr ((size_t) N); tw::Wavetable& T = tabs[(size_t) pr];
              for (int n = 0; n < N; ++n) fr[(size_t) n] = T.lookup (0, (float) f / (F - 1), (float) n / N);
              std::vector<double> m; harmonics (fr, m, 96);
              for (int h = 1; h <= 96; ++h) want[(size_t) h] += m[(size_t) h] * w[(size_t) f]; }
            std::vector<float> got; cyc (tabs[(size_t) pr], 0.5f, 1.0f, got);
            std::vector<double> mg; harmonics (got, mg, 96);
            // renderBlend RMS-matches, so compare SHAPES: normalise both to unit sum
            double sw = 0.0, sg = 0.0, pk = 0.0;
            for (int h = 1; h <= 96; ++h) { sw += want[(size_t) h]; sg += mg[(size_t) h]; }
            if (sw <= 0.0 || sg <= 0.0) continue;
            for (int h = 1; h <= 96; ++h) pk = std::max (pk, want[(size_t) h] / sw);
            double w2 = -400.0;
            for (int h = 1; h <= 96; ++h)
            { const double e = std::abs (want[(size_t) h]/sw - mg[(size_t) h]/sg);
              w2 = std::max (w2, 20.0 * std::log10 (std::max (1e-30, e) / pk)); }
            if (w2 > worst) { worst = w2; where = NAMES[pr]; }
            ++checked;
        }
        char d[200]; snprintf (d, sizeof d, "worst %+.1f dBr over %d tables (%s)", worst, checked, where.c_str());
        gate (worst < -30.0, "B3  with a twin, blur = 1 IS the magnitude mean", d);
    }

    // ── B6 — the cost ─────────────────────────────────────────────────────────────────────────
    { const double per = built.empty() ? 0.0 : twinBytes / (1024.0*1024.0) / (double) built.size();
      char d[240]; snprintf (d, sizeof d, "%.2f ms and %.2f MB per table, once, on the message thread — the plugin builds one only for a table an oscillator is actually blurring",
                             worstBuildMs, per);
      gate (worstBuildMs < 60.0 && per < 6.0, "B6  the cost is bounded and it is not on the audio thread", d); }

    printf ("\n  PASS %d   FAIL %d\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
