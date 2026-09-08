// wtfft_portable_cert (fb481) — the portable fast path must BE the scalar reference, then be fast.
//   G1  inverseRealPortable nulls against inverseRealScalar     (random half-spectra, N=256/2048/4096)
//   G2  forwardRealPortable nulls against forwardRealScalar     (random real vectors,  same sizes)
//   G3  round trip fwd->inv/N returns the input                 (portable end to end)
//   G4  the bake shape (544 x iFFT-2048) portable vs scalar     (the number that froze Windows)
#include "../Source/WtFft.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cmath>

static double urand() { return 2.0 * ((double) rand() / RAND_MAX) - 1.0; }
static double relDb (const double* a, const double* b, int n)
{
    double e = 0, r = 0;
    for (int i = 0; i < n; ++i) { const double d = a[i] - b[i]; e += d * d; r += b[i] * b[i]; }
    if (r <= 0) return -999.0;
    return 10.0 * std::log10 (e / r + 1e-300);
}

int main()
{
    srand (1234);
    int fails = 0;
    const int sizes[3] = { 256, 2048, 4096 };

    for (int si = 0; si < 3; ++si)
    {
        const int N = sizes[si], H = N / 2;
        std::vector<double> re ((size_t) H + 1), im ((size_t) H + 1), oS ((size_t) N), oP ((size_t) N);
        double worst = -999;
        for (int t = 0; t < 8; ++t)
        {
            for (int k = 0; k <= H; ++k) { re[(size_t) k] = urand(); im[(size_t) k] = urand(); }
            im[0] = 0; im[(size_t) H] = 0;
            tw::wtfft::inverseRealScalar   (re.data(), im.data(), N, oS.data());
            tw::wtfft::inverseRealPortable (re.data(), im.data(), N, oP.data());
            worst = std::max (worst, relDb (oP.data(), oS.data(), N));
        }
        const bool ok = worst < -250.0;
        printf ("  %s  G1 inverse N=%-5d portable == scalar reference   worst %.1f dBr (want < -250)\n", ok ? "ok  " : "FAIL", N, worst);
        fails += ! ok;

        std::vector<double> x ((size_t) N), rS ((size_t) H + 1), iS ((size_t) H + 1), rP ((size_t) H + 1), iP ((size_t) H + 1);
        worst = -999;
        for (int t = 0; t < 8; ++t)
        {
            for (int n = 0; n < N; ++n) x[(size_t) n] = urand();
            tw::wtfft::forwardRealScalar   (x.data(), N, rS.data(), iS.data());
            tw::wtfft::forwardRealPortable (x.data(), N, rP.data(), iP.data());
            const double a = relDb (rP.data(), rS.data(), H + 1);
            const double b = relDb (iP.data(), iS.data(), H + 1);
            worst = std::max (worst, std::max (a, b));
        }
        const bool ok2 = worst < -250.0;
        printf ("  %s  G2 forward N=%-5d portable == scalar reference   worst %.1f dBr (want < -250)\n", ok2 ? "ok  " : "FAIL", N, worst);
        fails += ! ok2;

        // G3 round trip
        for (int n = 0; n < N; ++n) x[(size_t) n] = urand();
        tw::wtfft::forwardRealPortable (x.data(), N, rP.data(), iP.data());
        tw::wtfft::inverseRealPortable (rP.data(), iP.data(), N, oP.data());
        for (int n = 0; n < N; ++n) oP[(size_t) n] /= (double) N;
        const double rt = relDb (oP.data(), x.data(), N);
        const bool ok3 = rt < -250.0;
        printf ("  %s  G3 round-trip N=%-5d fwd->inv/N == input          %.1f dBr (want < -250)\n", ok3 ? "ok  " : "FAIL", N, rt);
        fails += ! ok3;
    }

    // G4 — the bake shape: 544 inverse transforms of 2048 (34 mips x 16 frames)
    {
        const int N = 2048, H = N / 2, REP = 544;
        std::vector<double> re ((size_t) H + 1), im ((size_t) H + 1), o ((size_t) N);
        for (int k = 0; k <= H; ++k) { re[(size_t) k] = urand(); im[(size_t) k] = urand(); }
        im[0] = 0; im[(size_t) H] = 0;
        tw::wtfft::inverseRealPortable (re.data(), im.data(), N, o.data());   // warm plan+scratch
        auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < REP; ++r) tw::wtfft::inverseRealScalar (re.data(), im.data(), N, o.data());
        auto t1 = std::chrono::steady_clock::now();
        for (int r = 0; r < REP; ++r) tw::wtfft::inverseRealPortable (re.data(), im.data(), N, o.data());
        auto t2 = std::chrono::steady_clock::now();
        const double sMs = std::chrono::duration<double, std::milli> (t1 - t0).count();
        const double pMs = std::chrono::duration<double, std::milli> (t2 - t1).count();
        printf ("  ok    G4 bake shape 544 x iFFT-2048: scalar %.2f ms -> portable %.2f ms  (%.1fx faster)\n", sMs, pMs, sMs / pMs);
        const bool ok4 = pMs < sMs * 0.5;   // the whole point: at least 2x, expect 3-4x
        if (! ok4) { printf ("  FAIL  G4 portable is not at least 2x the scalar reference\n"); ++fails; }
    }

    printf (fails == 0 ? "SUCCESS\n" : "FAILURES: %d\n", fails);
    return fails == 0 ? 0 : 1;
}
