// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wtfft_cert.cpp — fb467 THE BAKE'S TRANSFORM. Proves the vDSP swap changes the SPEED and not
//  the SOUND, on the real 30-preset factory matrix, and proves the speed with a number.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/wtfft_cert.cpp -o /tmp/wtfft_cert \
//            -framework Accelerate
//
//  WHY NOT "BIT-IDENTICAL": measured, the vDSP f64 real transform changes 135,383 of the
//  33,423,360 finished mipData_ floats across all 30 presets, worst case 1.192e-07 absolute =
//  -138.5 dBFS = 2 float ULP at full scale. That is not a tolerance chosen to make the test pass;
//  it is the arithmetic difference between two orderings of the same butterflies, and it is 100 dB
//  below the -38 dBr defect this same session found in the partial cap. The gate is set at the
//  measured worst case, and gate T4 proves the gate can still SEE a real defect at that setting.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "WavetableBank.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include <algorithm>

static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const char* d = "")
{ c ? ++gPass : ++gFail; printf ("  %-5s %-56s %s\n", c ? "ok" : "FAIL", n, d); }

static const int N = 2048, H = N / 2;

// every finished float of one built table
static void dumpTable (const tw::Wavetable& wt, std::vector<float>& out)
{
    out.clear();
    for (int lvl = 0; lvl < tw::Wavetable::kNumMipLevels; ++lvl)
        for (int f = 0; f < 16; ++f)
            for (int i = 0; i < N; ++i)
                out.push_back (wt.lookup (lvl, (float) f / 15.0f, (float) i / (float) N));
}

int main()
{
    printf ("\n══ fb467 — THE BAKE'S TRANSFORM (vDSP real-f64 vs the shipped radix-2) ══\n\n");
    gate (tw::wtfft::accelerated(), "T0  the Accelerate backend is COMPILED IN",
          tw::wtfft::accelerated() ? "" : "built without <Accelerate/Accelerate.h> — T1..T3 would be vacuous");

    // ── T1 — the primitives, on the spectra the bake actually produces ────────────────────────
    {
        std::vector<double> re (H+1), im (H+1), a (N), b (N);
        unsigned s = 0xC0FFEEu;
        auto rnd = [&] { s ^= s<<13; s ^= s>>17; s ^= s<<5; return (double) s / 4294967295.0 * 2.0 - 1.0; };
        double worst = 0.0, pk = 0.0;
        for (int trial = 0; trial < 400; ++trial)
        {
            std::fill (re.begin(), re.end(), 0.0); std::fill (im.begin(), im.end(), 0.0);
            const int hMax = tw::Wavetable::kMipMaxHarmonics[(size_t) (trial % tw::Wavetable::kNumMipLevels)];
            // half the trials get ALL-ZERO phase — SerumHD is authored that way (Wavetable.h) and
            // structured phase is what makes exact cancellations happen. A purely random set hides them.
            const bool zeroPhase = (trial % 2) == 0;
            for (int h = 1; h <= std::min (hMax, H-1); ++h)
            { const double A = 1.0 / h, p = zeroPhase ? 0.0 : rnd() * 3.14159265358979;
              re[(size_t) h] =  0.5 * A * std::sin (p);
              im[(size_t) h] = -0.5 * A * std::cos (p); }
            tw::wtfft::inverseRealScalar (re.data(), im.data(), N, a.data());
            tw::wtfft::inverseReal       (re.data(), im.data(), N, b.data());
            for (int n = 0; n < N; ++n) { pk = std::max (pk, std::abs (a[(size_t) n]));
                                          worst = std::max (worst, std::abs (a[(size_t) n] - b[(size_t) n])); }
        }
        char d[160]; snprintf (d, sizeof d, "worst %.3e = %.1f dBr (400 spectra, half of them zero-phase)",
                               worst, 20.0 * std::log10 (std::max (1e-300, worst / pk)));
        gate (worst / pk < 1e-12, "T1  INVERSE: vDSP nulls against the shipped radix-2", d);
    }
    {
        std::vector<double> x (N), r1 (H+1), i1 (H+1), r2 (H+1), i2 (H+1);
        unsigned s = 0xBEEF01u; auto rnd = [&] { s ^= s<<13; s ^= s>>17; s ^= s<<5; return (double) s / 4294967295.0 * 2.0 - 1.0; };
        double worst = 0.0, pk = 0.0;
        for (int trial = 0; trial < 200; ++trial)
        {
            for (int n = 0; n < N; ++n) x[(size_t) n] = rnd();
            tw::wtfft::forwardRealScalar (x.data(), N, r1.data(), i1.data());
            tw::wtfft::forwardReal       (x.data(), N, r2.data(), i2.data());
            for (int k = 0; k <= H; ++k)
            { pk = std::max (pk, std::hypot (r1[(size_t) k], i1[(size_t) k]));
              worst = std::max (worst, std::hypot (r1[(size_t) k]-r2[(size_t) k], i1[(size_t) k]-i2[(size_t) k])); }
        }
        char d[160]; snprintf (d, sizeof d, "worst %.3e = %.1f dBr — the x0.5 scale is what this pins",
                               worst, 20.0 * std::log10 (std::max (1e-300, worst / pk)));
        gate (worst / pk < 1e-12, "T2  FORWARD: vDSP nulls against the shipped radix-2", d);
    }

    // ── T3 — THE ONE THAT MATTERS: every finished float of every factory table, both ways ─────
    double worstAbs = 0.0; long long differ = 0, total = 0; int worstPreset = -1;
    {
        std::vector<float> vd, sc;
        for (int pr = 0; pr < 30; ++pr)
        {
            const tw::WavetableSpec spec = tw::WavetableBank::specForPreset (pr);
            tw::Wavetable wt;
            tw::wtfft::useScalarReference() = false; wt.buildFromSpec (spec); dumpTable (wt, vd);
            tw::wtfft::useScalarReference() = true;  wt.buildFromSpec (spec); dumpTable (wt, sc);
            tw::wtfft::useScalarReference() = false;
            for (size_t i = 0; i < sc.size(); ++i)
            { const double d = std::abs ((double) vd[i] - (double) sc[i]);
              if (d != 0.0) ++differ;
              if (d > worstAbs) { worstAbs = d; worstPreset = pr; } }
            total += (long long) sc.size();
        }
        char d[200]; snprintf (d, sizeof d, "worst |D| %.4e (%.1f dBFS) on preset %d; %lld/%lld floats differ",
                               worstAbs, 20.0 * std::log10 (std::max (1e-300, worstAbs)), worstPreset, differ, total);
        gate (worstAbs <= 1.2e-07, "T3  30 factory tables, EVERY finished float, both backends", d);
    }

    // ── T4 — THE POSITIVE CONTROL. T3's threshold is loose enough to admit 2 ULP; prove it is
    //        still tight enough to REJECT a real defect. Truncating one mip level's top harmonic
    //        is the smallest genuine bake error there is.
    {
        const tw::WavetableSpec spec = tw::WavetableBank::specForPreset (1);
        tw::WavetableSpec hurt = spec;
        int touched = 0;
        for (int f = 0; f < 16; ++f)
        {
            tw::FrameSpec& fs = hurt.frames[(size_t) f];
            if (fs.numPartials != 0) continue;
            // ⚠️ find the TOP NON-ZERO harmonic. Trimming a fixed index (399) trimmed a ZERO on the
            // first table tried — preset 1 is odd-harmonic only, so 0.999 x 0 = 0 and this control
            // read a perfect 0.000e+00 while claiming to prove the bar had teeth. A positive control
            // that cannot inject the defect is worse than no control (fb453).
            int top = 0;
            for (int h = 1; h <= fs.numHarmonics && h <= 512; ++h)
                if (fs.amplitudes[(size_t) (h-1)] != 0.0f) top = h;
            if (top >= 8) { fs.amplitudes[(size_t) (top-1)] *= 0.999f; ++touched; }   // -0.0087 dB on ONE harmonic
        }
        tw::Wavetable a, b; std::vector<float> va, vb;
        a.buildFromSpec (spec); dumpTable (a, va);
        b.buildFromSpec (hurt); dumpTable (b, vb);
        double w = 0.0; for (size_t i = 0; i < va.size(); ++i) w = std::max (w, std::abs ((double) va[i] - (double) vb[i]));
        char d[200]; snprintf (d, sizeof d, "one harmonic trimmed by 0.0087 dB on %d frames reads %.3e = %.0fx the T3 bar",
                               touched, w, w / 1.2e-07);
        gate (w > 1.2e-07 && touched == 16, "T4  POSITIVE CONTROL: T3's bar still SEES a real bake defect", d);
    }

    // ── T5 — the speed, since that is the entire point ────────────────────────────────────────
    {
        const tw::WavetableSpec spec = tw::WavetableBank::specForPreset (1);
        tw::Wavetable wt;
        auto timeIt = [&] (bool scalar) {
            tw::wtfft::useScalarReference() = scalar;
            wt.buildFromSpec (spec);                                  // warm
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 5; ++i) wt.buildFromSpec (spec);
            const auto t1 = std::chrono::steady_clock::now();
            tw::wtfft::useScalarReference() = false;
            return std::chrono::duration<double, std::milli> (t1 - t0).count() / 5.0; };
        const double sc = timeIt (true), vd = timeIt (false);
        char d[200]; snprintf (d, sizeof d, "%.2f ms -> %.2f ms = %.1fx (544 transforms per bake)", sc, vd, sc / vd);
        gate (vd < sc * 0.35, "T5  the bake is at least 3x faster than the shipped transform", d);
        // fb462 — the FLOOR that a speed ceiling needs: a bake that got FASTER by doing less work
        // would also pass T5. T3 is that floor, and it is what makes this number trustworthy.
    }

    printf ("\n  PASS %d   FAIL %d\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
