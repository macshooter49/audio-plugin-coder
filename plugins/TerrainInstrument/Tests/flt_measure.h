// ══════════════════════════════════════════════════════════════════════════════════════════════
//  flt_measure.h — fb603 · THE MEASUREMENT CORE FOR EVERY TERRAIN SYNTH FILTER TYPE.
//
//  Landed in Tests/ from the fb603 scratch harness that found the 13 defect classes. Two files
//  include it and NEITHER re-implements it (RECYCLE):
//      Tests/fltmeas.cpp    the human-readable report  (tables + Tests/flt_curves.csv)
//      Tests/flt_gate.cpp   the asserting gate         (pass/FAIL bars + mutation controls)
//
//  Metrics here are phase-INDEPENDENT (magnitude spectra, harmonic amplitudes, RMS). Sample-
//  difference RMS is never used as an audibility metric.
//
//  It drives FilterSlot EXACTLY as SynthVoice does:
//    · needsOversampling() types run through the voice's 2x linear-interp upsample / two filter
//      calls / box decimate wrapper, with coefSr = 2*fs.
//    · everything else runs once at fs.
//    · setParams(cutHz, res01, drv01, coefSr) per sample (change-gated inside).
//  Any deviation from that and the numbers are about a filter the plugin does not ship.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#pragma once

#include "TerrainFilters.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <complex>
#include <algorithm>
#include <chrono>

using namespace tw::filters;

static constexpr double FS = 48000.0;

// ─────────────────────────────────────────────────────────── names (must
// mirror terrainFilterEngineNames() in PluginProcessor.cpp, index for index).
static const char* kName[94] = {
 "Ladder LP 24","Ladder LP 12","Ladder HP 24","Diode LP","Acid 303",
 "SVF LP","SVF HP","SVF BP","SVF Notch","OB-X SVF",
 "Comb +","Comb -","Comb Shimmer","Karplus-Strong",
 "Formant A","Formant E","Formant I","Formant Morph",
 "Reverb Filter","Phaser 4P","Phaser 8P","Ring Mod","Bode Shifter",
 "Bit-Crush","Waveshaper","Grain Mask","Reverb Filter 2","None",
 "Ladder LP 6","Ladder LP 18","German LP","Germanium LP","French LP","Acid Scream",
 "Xpd HP 6","Xpd HP 12","Xpd HP 18","Xpd BP 12","Xpd BP 24","Xpd BP 6",
 "Xpd Notch","Xpd Phase","Xpd 1-Pole",
 "SVF LP 24","SVF HP 24","SVF BP 24","SVF Notch 24","SVF Peak",
 "SEM LP","SEM Notch","SEM HP","SEM BP",
 "Multi LP+HP","Multi LP+BP","Multi LP+Notch","Multi HP+BP","Multi HP+Notch",
 "Multi BP+BP","Multi BP+Notch","Multi Peak+Peak","Multi Notch+Notch","Multi Peak+HP",
 "Comb Wide","Comb Octave","Comb Fifth","Comb Damp","Karplus Bright","Karplus Mute",
 "Formant O","Formant U","Formant Wide","Formant Growl",
 "Phaser 6P","Phaser 12P","Phaser 16P","Diffusor","Bode Down",
 "Tilt","Low EQ","High EQ","Band EQ","Air","Add Bass",
 "Samp-Hold","Samp-Hold -","Scream LP","Scream BP",
 "Wasp","MS-20 LP","Polivoks","Ring Mod X2","Radio","Reverb Dark","Reverb Metal" };

// ─────────────────────────────────────────────────────────── runner
struct Runner
{
    FilterSlot f;
    bool   os = false;
    double coefSr = FS;
    float  prevL = 0.0f, prevR = 0.0f;
    bool   isNone = false;
    bool   bypassIdentity = false;   // "OS wrapper alone" reference mode

    void init (int typeIdx)
    {
        f.prepare (FS);
        f.setType (static_cast<Type> (typeIdx));
        os     = f.needsOversampling();
        coefSr = os ? FS * 2.0 : FS;
        isNone = (typeIdx == (int) Type::NONE);
        prevL = prevR = 0.0f;
    }
    void resetState() { f.reset(); prevL = prevR = 0.0f; }
    void setP (float cut, float res, float drv) { f.setParams (cut, res, drv, coefSr); }

    inline void step (float xL, float xR, float& oL, float& oR)
    {
        if (! os)
        {
            float l = xL, r = xR;
            if (! bypassIdentity) f.processStereo (l, r);
            oL = l; oR = r; return;
        }
        const float mL = 0.5f * (prevL + xL), mR = 0.5f * (prevR + xR);
        float aL = mL, aR = mR; if (! bypassIdentity) f.processStereo (aL, aR);
        float bL = xL, bR = xR; if (! bypassIdentity) f.processStereo (bL, bR);
        prevL = xL; prevR = xR;
        oL = 0.5f * (aL + bL); oR = 0.5f * (aR + bR);
    }
};

// ─────────────────────────────────────────────────────────── FFT / PSD
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

static constexpr int NFFT = 16384;

// Welch PSD (Hann, 50% overlap). Returns NFFT/2+1 power bins.
static std::vector<double> psd (const std::vector<float>& x, int skip)
{
    std::vector<double> P ((size_t) NFFT / 2 + 1, 0.0);
    static std::vector<double> win;
    if (win.empty()) { win.resize (NFFT); for (int i = 0; i < NFFT; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1)); }
    int segs = 0;
    for (int off = skip; off + NFFT <= (int) x.size(); off += NFFT / 2)
    {
        std::vector<std::complex<double>> a ((size_t) NFFT);
        for (int i = 0; i < NFFT; ++i)
        {
            double v = (double) x[(size_t)(off + i)];
            if (! std::isfinite (v)) v = 0.0;
            a[(size_t) i] = std::complex<double> (v * win[(size_t) i], 0.0);
        }
        fft (a);
        for (int k = 0; k <= NFFT / 2; ++k) P[(size_t) k] += std::norm (a[(size_t) k]);
        ++segs;
    }
    if (segs > 0) for (auto& v : P) v /= (double) segs;
    return P;
}

// log grid, 1/12 octave, 20 Hz .. 20 kHz
static std::vector<double> gridHz()
{
    std::vector<double> g;
    for (double f = 20.0; f <= 20000.001; f *= std::pow (2.0, 1.0 / 12.0)) g.push_back (f);
    return g;
}
static const std::vector<double> GRID = gridHz();

// 1/6-octave smoothed band power at a grid frequency
static double bandPow (const std::vector<double>& P, double fc)
{
    const double binHz = FS / NFFT;
    const double lo = fc / std::pow (2.0, 1.0 / 12.0), hi = fc * std::pow (2.0, 1.0 / 12.0);
    int k0 = (int) std::floor (lo / binHz), k1 = (int) std::ceil (hi / binHz);
    k0 = std::max (1, k0); k1 = std::min (NFFT / 2, k1);
    if (k1 < k0) k1 = k0;
    double s = 0.0; int n = 0;
    for (int k = k0; k <= k1; ++k) { s += P[(size_t) k]; ++n; }
    return n ? s / n : 1e-30;
}

struct Curve { std::vector<double> dB; };   // per GRID point

// ─────────────────────────────────────────────────────────── noise probe
static uint32_t rngS = 0x9e3779b9u;
static float rnd() { rngS ^= rngS << 13; rngS ^= rngS >> 17; rngS ^= rngS << 5; return (float) ((int32_t) rngS) * (1.0f / 2147483648.0f); }

struct RunFlags { bool nan = false; double peak = 0.0; };

// White-noise transfer curve, both channels. probeRms low so tanh cores stay
// in their linear region (stopbands below -80 dB stay honest).
// fb603 — two optional arguments, both DEFAULTED so every existing caller (and every number
// in Tests/fltmeas.cpp) is bit-identical to the fb602 harness:
//   segs      how many overlapping Welch segments. 8 = the report's resolution; the gate uses 4,
//             which halves the FFT cost for assertions whose thresholds are 0.5-3 dB.
//   leftOnly  skip the RIGHT channel's PSD. The anti-duplication rule is a LEFT-channel rule
//             (the Comb Wide/Octave/Fifth retunes are right-channel-only and must still collapse).
// The INPUT psd is cached on (probeRms, N): the probe is regenerated from a fixed seed every
// call, so Px is a pure function of those two and recomputing it was ~1/3 of the FFT budget.
static void noiseCurve (Runner& R, float cut, float res, float drv, float probeRms,
                        Curve& cl, Curve& cr, RunFlags& fl, int segs = 8, bool leftOnly = false)
{
    const int N = NFFT * (segs + 1);     // `segs` overlapping segs + settle
    const int skip = NFFT;               // discard first block (settling)
    std::vector<float> xin ((size_t) N), yl ((size_t) N), yr ((size_t) N);
    rngS = 0x9e3779b9u;
    for (int i = 0; i < N; ++i) xin[(size_t) i] = rnd() * probeRms * 1.7320508f;   // uniform -> rms
    R.resetState(); R.setP (cut, res, drv);
    for (int i = 0; i < N; ++i)
    {
        R.setP (cut, res, drv);
        float a, b; R.step (xin[(size_t) i], xin[(size_t) i], a, b);
        if (! std::isfinite (a) || ! std::isfinite (b)) { fl.nan = true; a = b = 0.0f; }
        fl.peak = std::max (fl.peak, (double) std::max (std::fabs (a), std::fabs (b)));
        yl[(size_t) i] = a; yr[(size_t) i] = b;
    }
    static std::vector<double> pxCache; static float pxRms = -1.0f; static int pxN = -1;
    if (pxRms != probeRms || pxN != N) { pxCache = psd (xin, skip); pxRms = probeRms; pxN = N; }
    const auto& Px = pxCache;
    const auto Pl = psd (yl, skip);
    cl.dB.assign (GRID.size(), -200.0); cr.dB.assign (GRID.size(), -200.0);
    if (! leftOnly)
    {
        const auto Pr = psd (yr, skip);
        for (size_t i = 0; i < GRID.size(); ++i)
            cr.dB[i] = 10.0 * std::log10 (std::max (1e-30, bandPow (Pr, GRID[i]))
                                        / std::max (1e-30, bandPow (Px, GRID[i])));
    }
    for (size_t i = 0; i < GRID.size(); ++i)
        cl.dB[i] = 10.0 * std::log10 (std::max (1e-30, bandPow (Pl, GRID[i]))
                                    / std::max (1e-30, bandPow (Px, GRID[i])));
}

// ─────────────────────────────────────────────────────────── sine probe
// Hann-windowed DFT amplitude at f (phase independent).
static double ampAt (const std::vector<float>& y, double f)
{
    const int N = (int) y.size();
    double re = 0, im = 0, ws = 0;
    for (int i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (N - 1));
        double v = (double) y[(size_t) i]; if (! std::isfinite (v)) v = 0.0;
        const double th = 2.0 * M_PI * f * i / FS;
        re += w * v * std::cos (th); im -= w * v * std::sin (th); ws += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / ws;
}

// gain (dB) at one frequency with a long settle (high-Q safe)
static double sineGainDb (Runner& R, float cut, float res, float drv,
                          double f, double amp, double settleSec, double measSec)
{
    const int ns = (int) (settleSec * FS), nm = (int) (measSec * FS);
    std::vector<float> y ((size_t) nm);
    R.resetState(); R.setP (cut, res, drv);
    for (int i = 0; i < ns + nm; ++i)
    {
        const float x = (float) (amp * std::sin (2.0 * M_PI * f * i / FS));
        R.setP (cut, res, drv);
        float a, b; R.step (x, x, a, b);
        if (i >= ns) y[(size_t)(i - ns)] = std::isfinite (a) ? a : 0.0f;
    }
    const double g = ampAt (y, f) / amp;
    return 20.0 * std::log10 (std::max (1e-12, g));
}

