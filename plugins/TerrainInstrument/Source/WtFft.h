#pragma once
//
//  WtFft.h — Terrain Instrument
//  ─────────────────────────────────────────────────────────────────────────
//  THE WAVETABLE BAKE'S TRANSFORM. One job: turn a half spectrum into a real cycle and back.
//
//  WHY THIS FILE EXISTS (fb467, measured on an M2 Max, `clang++ -O2`):
//    tw::Wavetable::buildFromSpec runs 34 mip levels x 16 frames = 544 inverse transforms of 2048
//    points per table. Profiled, ONE bake is 19.12 ms and the transform is 16.96 ms of it — 88.7%.
//    Everything else in the bake put together (the partial resolve, the bin fill, the float store,
//    the allocation) is 0.70 ms. The bank's constructor is 594 ms and 87% of that is this transform.
//    Nothing else in the wavetable path is worth optimising until this is.
//
//      candidate                                          ns/transform     vs shipped
//      hand-rolled radix-2 on std::complex<double>            31,206          1.00x   (what shipped)
//      same radix-2, hand-written complex arithmetic          23,205          1.30x
//      vDSP_fft_zipD  (complex f64, + interleave/deinterleave) 4,843          6.0x
//      vDSP_fft_zripD (packed split REAL, f64)                 2,548         11.9x   <- SHIPPED
//      vDSP_fft_zrip  (packed split REAL, f32)                 1,249         24.7x
//      juce::dsp::FFT (f32 only in JUCE 8.0.12; wraps zrip)    4,066          7.4x
//
//    Real, not complex, because the bake's input IS conjugate-symmetric (buildFromSpec writes
//    X[N-h] = conj(X[h]) itself at Wavetable.h:180) and its output IS real — so half the transform
//    was always redundant. Double, not float, because f32 changes 81% of the finished floats and
//    lands at -132 dBr where f64 lands at -138.5 dBFS worst-case over the whole 30-preset,
//    33,423,360-float matrix. The 1.4 ms f32 would save on a 100 ms bank bake does not buy six
//    decades of headroom back.
//
//  THE SCALAR PATH IS NOT DEAD CODE. It is (a) the non-Apple fallback and (b) the REFERENCE the
//  certification harness nulls the vDSP path against. Deleting it would leave the fast path with
//  nothing to be checked against — see Tests/wtfft_cert.cpp.
//
//  🚨 CONVENTIONS, MEASURED RATHER THAN RECALLED (fb467). vDSP's real transforms carry scale
//     factors that are easy to state backwards:
//        INVERSE  vDSP_fft_zripD  ->  scale exactly 1.0   (least-squares fit 1.000000001, and
//                                     dedicated DC-only and Nyquist-only probes both read 1.000000)
//        FORWARD  vDSP_fft_zripD  ->  scale exactly 2.0 on EVERY bin including DC and Nyquist,
//                                     so a forward transform must be multiplied by 0.5.
//     The famous "vDSP factor of two" is in the FORWARD direction ONLY. Both are gated.
//
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <memory>

#if defined (__APPLE__) && __has_include (<Accelerate/Accelerate.h>)
 #define TW_WTFFT_ACCELERATE 1
 #include <Accelerate/Accelerate.h>
#else
 #define TW_WTFFT_ACCELERATE 0
#endif

namespace tw { namespace wtfft
{
    inline constexpr bool accelerated() noexcept { return TW_WTFFT_ACCELERATE != 0; }

    inline int log2i (int n) noexcept { int k = 0; while ((1 << k) < n) ++k; return k; }
    inline bool isPow2 (int n) noexcept { return n >= 4 && (n & (n - 1)) == 0; }

    // ── THE REFERENCE — the transform Terrain shipped from Phase 10a to fb466, unchanged.
    //    In-place radix-2 inverse DFT (raw, UNnormalized): a[n] <- SUM_k a[k] e^{+i2pi kn/N}.
    inline void radix2Inverse (std::complex<double>* a, int n) noexcept
    {
        for (int i = 1, j = 0; i < n; ++i)                       // bit-reversal permutation
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * 3.14159265358979323846 / (double) len;   // +sign = inverse
            const std::complex<double> wlen (std::cos (ang), std::sin (ang));
            for (int i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (int k = 0; k < len / 2; ++k)
                {
                    const std::complex<double> u = a[i + k];
                    const std::complex<double> v = a[i + k + len / 2] * w;
                    a[i + k]           = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    // ── SCALAR half-spectrum wrappers (the fallback AND the gate's reference) ─────────────────
    inline void inverseRealScalar (const double* re, const double* im, int N, double* out) noexcept
    {
        std::vector<std::complex<double>> a ((size_t) N);
        const int H = N / 2;
        a[0]         = std::complex<double> (re[0], 0.0);
        a[(size_t) H] = std::complex<double> (re[(size_t) H], 0.0);
        for (int k = 1; k < H; ++k)
        { const std::complex<double> c (re[(size_t) k], im[(size_t) k]);
          a[(size_t) k] = c; a[(size_t) (N - k)] = std::conj (c); }
        radix2Inverse (a.data(), N);
        for (int n = 0; n < N; ++n) out[(size_t) n] = a[(size_t) n].real();
    }

    inline void forwardRealScalar (const double* in, int N, double* re, double* im) noexcept
    {
        // Forward via the conjugation identity F = conj( inverse( conj(x) ) ) — the SAME identity
        // Wavetable::forwardFFT used, so the reference is byte-for-byte the transform that shipped.
        std::vector<std::complex<double>> a ((size_t) N);
        for (int n = 0; n < N; ++n) a[(size_t) n] = std::complex<double> (in[(size_t) n], 0.0);
        for (auto& z : a) z = std::conj (z);
        radix2Inverse (a.data(), N);
        for (auto& z : a) z = std::conj (z);
        const int H = N / 2;
        for (int k = 0; k <= H; ++k) { re[(size_t) k] = a[(size_t) k].real(); im[(size_t) k] = a[(size_t) k].imag(); }
    }

   #if TW_WTFFT_ACCELERATE
    // One setup per log2n, created once and shared. A vDSP FFT setup is a read-only twiddle table;
    // the per-call state is the DATA buffers, and those are thread_local below — so the bank ctor,
    // the 60 Hz message-thread rebuild and the import pool worker can all transform at once.
    inline FFTSetupD setupFor (int log2n) noexcept
    {
        static std::mutex m;
        static FFTSetupD cache[18] = {};
        if (log2n < 2 || log2n > 17) return nullptr;
        std::lock_guard<std::mutex> lk (m);
        if (cache[log2n] == nullptr) cache[log2n] = vDSP_create_fftsetupD ((vDSP_Length) log2n, kFFTRadix2);
        return cache[log2n];
    }
    inline double* scratch (int need) noexcept
    {
        static thread_local std::vector<double> buf;
        if ((int) buf.size() < need) buf.resize ((size_t) need);
        return buf.data();
    }
   #endif

    /** max |x[i]| over a contiguous float run, and an in-place scale. Both are EXACT — vDSP_maxmgv
     *  computes the same max-magnitude the scalar loop does and vDSP_vsmul is a plain float multiply,
     *  so this pair is bit-identical to the loop it replaces, not an approximation of it. They live
     *  here because normalizeMipLevels became the largest item in the bake the moment the transform
     *  stopped being it: 1.37 ms of a 3.04 ms bake = 45% (fb467). */
    inline float peakMagnitude (const float* x, int n) noexcept
    {
       #if TW_WTFFT_ACCELERATE
        float p = 0.0f; vDSP_maxmgv (x, 1, &p, (vDSP_Length) n); return p;
       #else
        float p = 0.0f; for (int i = 0; i < n; ++i) p = std::max (p, std::abs (x[i])); return p;
       #endif
    }
    inline void scaleInPlace (float* x, int n, float g) noexcept
    {
       #if TW_WTFFT_ACCELERATE
        vDSP_vsmul (x, 1, &g, x, 1, (vDSP_Length) n);
       #else
        for (int i = 0; i < n; ++i) x[i] *= g;
       #endif
    }


    // ── PORTABLE FAST PATH (fb481) — what ships on every non-Apple platform ──────────────────
    //
    //  The scalar reference above is correct but was never meant to ship hot: it heap-allocates
    //  a complex<double> vector PER TRANSFORM and runs a full complex-N pass on real data. On
    //  Windows that made one table bake (544 transforms) ~8x the Mac's, and the bake runs on the
    //  MESSAGE thread — a modulated spectral knob on a slow laptop backlogged the queue and froze
    //  every control in the host (input is the lowest-priority Win32 message class).
    //
    //  This path is the same mathematics with the standard real-packing: an N-point real
    //  transform rides an N/2-point complex FFT (iterative radix-2, precomputed twiddles and
    //  bit-reversal, cached per size, thread_local work buffer -- zero per-call allocation).
    //  It is nulled against the scalar reference by Tests/wtfft_portable_cert.cpp.
    struct PortablePlan
    {
        int N = 0, H = 0;                                   // real length, complex half
        std::vector<int> rev;                                // bit-reversal for H
        std::vector<std::complex<double>> twF, twI;          // e^{-+i 2pi k/H}, k < H/2
        std::vector<std::complex<double>> rotF, rotI;        // e^{-+i 2pi j/N}, j < H
        explicit PortablePlan (int n) : N (n), H (n / 2)
        {
            rev.resize ((size_t) H);
            for (int i = 1, j = 0; i < H; ++i)
            {
                int bit = H >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit; rev[(size_t) i] = j;
            }
            const double base = 2.0 * 3.14159265358979323846;
            twF.resize ((size_t) (H / 2)); twI.resize ((size_t) (H / 2));
            for (int k = 0; k < H / 2; ++k)
            { const double a = base * k / (double) H;
              twF[(size_t) k] = { std::cos (a), -std::sin (a) };
              twI[(size_t) k] = { std::cos (a),  std::sin (a) }; }
            rotF.resize ((size_t) H); rotI.resize ((size_t) H);
            for (int j = 0; j < H; ++j)
            { const double a = base * j / (double) N;
              rotF[(size_t) j] = { std::cos (a), -std::sin (a) };
              rotI[(size_t) j] = { std::cos (a),  std::sin (a) }; }
        }
    };
    inline const PortablePlan& portablePlan (int N) noexcept
    {
        static std::mutex m;
        static std::unique_ptr<PortablePlan> cache[18];
        const int l = log2i (N);
        std::lock_guard<std::mutex> lk (m);
        if (cache[l] == nullptr || cache[l]->N != N) cache[l].reset (new PortablePlan (N));
        return *cache[l];
    }
    inline std::complex<double>* portableScratch (int need) noexcept
    {
        static thread_local std::vector<std::complex<double>> buf;
        if ((int) buf.size() < need) buf.resize ((size_t) need);
        return buf.data();
    }
    // iterative radix-2 over H points; tw = per-plan table (stride-indexed, one table all stages)
    inline void portableFftH (std::complex<double>* a, const PortablePlan& pl, bool inverse) noexcept
    {
        const int H = pl.H;
        for (int i = 0; i < H; ++i)
        { const int j = pl.rev[(size_t) i]; if (i < j) std::swap (a[i], a[j]); }
        const auto* tw = (inverse ? pl.twI : pl.twF).data();
        for (int len = 2; len <= H; len <<= 1)
        {
            const int half = len >> 1, step = H / len;
            for (int i = 0; i < H; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const std::complex<double> w = tw[(size_t) (k * step)];
                    const std::complex<double> u = a[i + k];
                    const std::complex<double> v = a[i + k + half] * w;
                    a[i + k]        = u + v;
                    a[i + k + half] = u - v;
                }
        }
    }
    /** Portable inverse: x[n] = SUM X[k] e^{+i2pi kn/N}, real, raw — same contract as the
     *  reference. Even/odd repack: C[j] = (X[j]+conj(X[H-j])) + i rotI[j] (X[j]-conj(X[H-j])),
     *  z = IFFT_H(C), x[2m] = Re z[m], x[2m+1] = Im z[m]. */
    inline void inverseRealPortable (const double* re, const double* im, int N, double* out) noexcept
    {
        const auto& pl = portablePlan (N);
        const int H = pl.H;
        auto* c = portableScratch (H);
        for (int j = 0; j < H; ++j)
        {
            const std::complex<double> Xj  (re[(size_t) j], j == 0 ? 0.0 : im[(size_t) j]);
            const int hj = H - j;
            const std::complex<double> Xc = (j == 0) ? std::complex<double> (re[(size_t) H], 0.0)
                                                     : std::conj (std::complex<double> (re[(size_t) hj], hj == H ? 0.0 : im[(size_t) hj]));
            const std::complex<double> A = Xj + Xc;
            const std::complex<double> B = pl.rotI[(size_t) j] * (Xj - Xc);
            c[j] = A + std::complex<double> (0.0, 1.0) * B;
        }
        portableFftH (c, pl, true);
        for (int m = 0; m < H; ++m) { out[(size_t) (2 * m)] = c[m].real(); out[(size_t) (2 * m + 1)] = c[m].imag(); }
    }
    /** Portable forward: X[k] = SUM x[n] e^{-i2pi kn/N}, k = 0..N/2, raw — same contract as the
     *  reference. z[m] = x[2m] + i x[2m+1], Z = FFT_H(z), then the standard split:
     *  E = (Z[k]+conj(Z[H-k]))/2, O = (Z[k]-conj(Z[H-k]))/(2i), X[k] = E + rotF[k] O. */
    inline void forwardRealPortable (const double* in, int N, double* re, double* im) noexcept
    {
        const auto& pl = portablePlan (N);
        const int H = pl.H;
        auto* z = portableScratch (H);
        for (int m = 0; m < H; ++m) z[m] = { in[(size_t) (2 * m)], in[(size_t) (2 * m + 1)] };
        portableFftH (z, pl, false);
        const std::complex<double> mi (0.0, -0.5);   // 1/(2i)
        for (int k = 0; k <= H; ++k)
        {
            const std::complex<double> Zk  = z[k == H ? 0 : k];
            const std::complex<double> Zc  = std::conj (z[(H - k) % H]);
            const std::complex<double> E = 0.5 * (Zk + Zc);
            const std::complex<double> O = mi  * (Zk - Zc);
            const std::complex<double> X = (k == H) ? (0.5 * (z[0] + std::conj (z[0])) - mi * (z[0] - std::conj (z[0]))) * 0.5
                                                    : E + pl.rotF[(size_t) k] * O;
            // k == H: X[H] = E[0] - O[0] with E,O from Z[0] -- write it plainly below instead
            re[(size_t) k] = X.real(); im[(size_t) k] = X.imag();
        }
        // the Nyquist bin, plainly: E0 - O0
        {
            const std::complex<double> Z0 = z[0], Zc = std::conj (z[0]);
            const std::complex<double> E0 = 0.5 * (Z0 + Zc);
            const std::complex<double> O0 = mi  * (Z0 - Zc);
            const std::complex<double> XH = E0 - O0;
            re[(size_t) H] = XH.real(); im[(size_t) H] = XH.imag();
        }
        im[0] = 0.0; im[(size_t) H] = 0.0;   // DC and Nyquist of a real signal are real
    }

    /** CERTIFICATION SWITCH — flips both transforms to the scalar reference. It exists so ONE
     *  harness binary can bake the same wavetable BOTH ways and null them against each other
     *  (Tests/wtfft_cert.cpp). Without it the only way to null the swap is to compare against a
     *  git checkout, which no committed gate can do — and a gate that cannot run is not a gate.
     *  Never touched at run time: buildFromSpec is message-thread-only and reads this once per
     *  2.5 us transform. */
    inline bool& useScalarReference() noexcept { static bool b = false; return b; }

    /** out[n] = SUM_k X[k] e^{+i2pi kn/N}, real, raw (no 1/N). X is given as its half spectrum
     *  re[0..N/2], im[0..N/2]; im[0] and im[N/2] are ignored (DC and Nyquist are real by symmetry). */
    inline void inverseReal (const double* re, const double* im, int N, double* out) noexcept
    {
       #if TW_WTFFT_ACCELERATE
        const int log2n = log2i (N);
        FFTSetupD s = (isPow2 (N) && ! useScalarReference()) ? setupFor (log2n) : nullptr;
        if (s != nullptr)
        {
            const int H = N / 2;
            double* rp = scratch (N);
            double* ip = rp + H;
            rp[0] = re[0];                    // packed real format: DC in realp[0] ...
            ip[0] = re[(size_t) H];           // ... and Nyquist in imagp[0]
            for (int k = 1; k < H; ++k) { rp[k] = re[(size_t) k]; ip[k] = im[(size_t) k]; }
            DSPDoubleSplitComplex sc { rp, ip };
            vDSP_fft_zripD (s, &sc, 1, (vDSP_Length) log2n, kFFTDirection_Inverse);
            vDSP_ztocD (&sc, 1, (DSPDoubleComplex*) out, 2, (vDSP_Length) H);   // scale = exactly 1.0
            return;
        }
       #else
        if (isPow2 (N) && ! useScalarReference()) { inverseRealPortable (re, im, N, out); return; }
       #endif
        inverseRealScalar (re, im, N, out);
    }

    /** X[k] = SUM_n x[n] e^{-i2pi kn/N} for k = 0..N/2, raw (no 1/N). */
    inline void forwardReal (const double* in, int N, double* re, double* im) noexcept
    {
       #if TW_WTFFT_ACCELERATE
        const int log2n = log2i (N);
        FFTSetupD s = (isPow2 (N) && ! useScalarReference()) ? setupFor (log2n) : nullptr;
        if (s != nullptr)
        {
            const int H = N / 2;
            double* rp = scratch (N);
            double* ip = rp + H;
            DSPDoubleSplitComplex sc { rp, ip };
            vDSP_ctozD ((const DSPDoubleComplex*) in, 2, &sc, 1, (vDSP_Length) H);
            vDSP_fft_zripD (s, &sc, 1, (vDSP_Length) log2n, kFFTDirection_Forward);
            re[0]          = rp[0] * 0.5;  im[0]          = 0.0;    // vDSP forward is 2x the DFT
            re[(size_t) H] = ip[0] * 0.5;  im[(size_t) H] = 0.0;
            for (int k = 1; k < H; ++k) { re[(size_t) k] = rp[k] * 0.5; im[(size_t) k] = ip[k] * 0.5; }
            return;
        }
       #else
        if (isPow2 (N) && ! useScalarReference()) { forwardRealPortable (in, N, re, im); return; }
       #endif
        forwardRealScalar (in, N, re, im);
    }
}} // namespace tw::wtfft
