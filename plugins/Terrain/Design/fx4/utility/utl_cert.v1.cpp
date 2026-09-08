// ─────────────────────────────────────────────────────────────────────────────
// utl_cert — the certification harness for the UTILITY strip (kind 14, fb445).
//   clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/utl_cert.cpp -o utl_cert
//
// THE ORDER OF THESE GATES IS THE POINT.
//   §A runs FIRST and it is THE MATRIX AND POLARITY. A Utility with L and R
//   swapped, or with one channel inverted, builds clean, measures "different",
//   passes every level gate, every click gate and every spectrum-changed gate —
//   and is the WRONG DEVICE. Worse than the Bode case: a shifter running
//   backwards is at least audible, while a swapped L/R on a near-mono source is
//   inaudible until the day it matters. So the very first thing this harness
//   prints is WHERE EACH CHANNEL WENT AND WITH WHICH SIGN, proved with signals
//   that admit exactly one explanation:
//     · two DIFFERENT tones, one per channel, so "which tone came out here"
//       is a fact and not a correlation;
//     · a CORRELATED pair, which `Difference` must NULL;
//     · an exact bit-level identity check for polarity (out == -in).
//   §A also opens with TRANSPARENCY, because if the do-nothing state is not
//   nothing, every number below it is measuring the wrong device.
//
//   §C is the second-most-important section and it is the BOUNDED-WIDTH LAW —
//   the explicit, numbered contrast with the WIDEN device's R11 contract
//   (TerrainWidenFx.h:71-73), whose cert REQUIRES corr <= -0.9 and a mono
//   fold-down at <= -25 dB. This device's cert requires the opposite. Two
//   devices, one piece of M/S math, two contracts that must not drift into
//   each other.
//
// fb441 — CERT MUST SEED BEFORE IT MEASURES. A fresh engine snaps to its first
// block and hides every steady-state bug; this device is ALL smoothers (the
// fader, the mix, the mid/side gains, and four crossfaders that decide whether
// a section is even in circuit), so a cold capture would measure a machine that
// is still assembling itself. Every capture below runs a seed interval first.
//
// fb444 — THE METRIC, NOT THE BAR. §H does not sum |output|. Gross energy is
// blind to exactly the controls this device is made of: `Twist` and `Image`
// move energy between MID and SIDE at nearly constant total, and a gross-energy
// probe would report the two most important knobs on the card as dead. So the
// fingerprint is FOUR magnitude spectra — L, R, M and S — concatenated, which
// is what an ear (two of them) actually has access to.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainUtilityFx.h"
#include <cstdio>
#include <cstdint>
#include <complex>
#include <string>
#include <vector>
#include <cmath>

static int gPass = 0, gFail = 0;
static void gate (const char* what, bool ok, const std::string& d)
{
    if (ok) { ++gPass; std::printf ("  ok    %-58s %s\n", what, d.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-58s %s\n", what, d.c_str()); }
}

// ── a small radix-2 FFT ──────────────────────────────────────────────────────
typedef std::vector<std::complex<double>> CVec;
static void fft (CVec& a)
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
        std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

static constexpr int   NFFT = 16384;
static constexpr float FS   = 48000.0f;
static constexpr float BUS  = 0.05f;     // the rack bus sits near -26 dBFS (fb313)

using E = tw::TerrainUtilityFx;
using P = tw::TerrainUtilityFx::Params;

static double db (double x) { return 20.0 * std::log10 (std::max (1e-12, x)); }
static double binHz()       { return (double) FS / NFFT; }

static CVec cspec (const std::vector<float>& v)
{
    CVec b ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));   // Hann
        b[(size_t) i] = (i < (int) v.size() ? (double) v[(size_t) i] : 0.0) * w;
    }
    fft (b);
    b.resize ((size_t) NFFT / 2);
    return b;
}
static double energyNear (const CVec& s, double hz, double widthHz = 30.0)
{
    const int lo = (int) std::max (0.0, (hz - widthHz) / binHz());
    const int hi = (int) std::min ((double) s.size() - 1, (hz + widthHz) / binHz());
    double e = 0.0;
    for (int i = lo; i <= hi; ++i) e += std::norm (s[(size_t) i]);
    return std::sqrt (e);
}
static double bandCorr (const CVec& L, const CVec& R, double lo, double hi)
{
    const int i0 = (int) (lo / binHz()), i1 = (int) (hi / binHz());
    double num = 0.0, dl = 0.0, dr = 0.0;
    for (int i = i0; i <= i1 && i < (int) L.size(); ++i)
    {
        num += (L[(size_t) i] * std::conj (R[(size_t) i])).real();
        dl  += std::norm (L[(size_t) i]);
        dr  += std::norm (R[(size_t) i]);
    }
    return num / std::sqrt (std::max (1e-30, dl * dr));
}
static double rms (const std::vector<float>& v)
{
    double a = 0.0; for (float x : v) a += (double) x * x;
    return std::sqrt (a / std::max<size_t> (1, v.size()));
}

// ── deterministic, decorrelated stereo noise. Two independent streams, reset
//    to the same seeds for every measurement, so a difference between two
//    captures can only have come from the ENGINE.
struct Noise
{
    uint32_t a = 2463534242u, b = 1234567891u;
    void reset() noexcept { a = 2463534242u; b = 1234567891u; }
    float l() noexcept { a ^= a << 13; a ^= a >> 17; a ^= a << 5; return ((float) (a & 0xFFFFu) / 32768.0f) - 1.0f; }
    float r() noexcept { b ^= b << 13; b ^= b >> 17; b ^= b << 5; return ((float) (b & 0xFFFFu) / 32768.0f) - 1.0f; }
};

struct Cap { std::vector<float> inL, inR, L, R; };

// fb441 — SEED, THEN MEASURE. seedSec runs the engine to steady state first.
template <class Gen>
static Cap capture (E& e, Gen gen, int n, double seedSec = 0.30)
{
    float l = 0.0f, r = 0.0f;
    int i = 0;
    const int sN = (int) (seedSec * FS);
    for (; i < sN; ++i) { const auto s = gen (i); e.processStereo (s.first, s.second, l, r); }
    Cap c;
    c.inL.resize ((size_t) n); c.inR.resize ((size_t) n);
    c.L.resize ((size_t) n);   c.R.resize ((size_t) n);
    for (int k = 0; k < n; ++k, ++i)
    {
        const auto s = gen (i);
        e.processStereo (s.first, s.second, l, r);
        c.inL[(size_t) k] = s.first; c.inR[(size_t) k] = s.second;
        c.L[(size_t) k] = l; c.R[(size_t) k] = r;
    }
    return c;
}

// two DIFFERENT tones, one per channel — the probe that makes "which channel
// did this come out of" a fact rather than an inference.
static std::pair<float,float> twoTone (int i)
{
    const double t = (double) i / FS;
    return { BUS * (float) std::sin (2.0 * M_PI * 300.0  * t),
             BUS * (float) std::sin (2.0 * M_PI * 1100.0 * t) };
}
static std::pair<float,float> monoTone (int i)
{
    const double t = (double) i / FS;
    const float  v = BUS * (float) std::sin (2.0 * M_PI * 440.0 * t);
    return { v, v };
}
static std::pair<float,float> antiTone (int i)
{
    const double t = (double) i / FS;
    const float  v = BUS * (float) std::sin (2.0 * M_PI * 440.0 * t);
    return { v, -v };
}

static P base() { return P {}; }   // the defaults ARE the transparent strip

// The fader law, recomputed here INDEPENDENTLY of the engine. A gate that asks
// the engine what it thinks its own gain is proves nothing (fb393).
static double refFaderDb (float t)
{
    if (t <= 0.0f) return -1000.0;
    if (std::fabs (t - E::kGainUnityT) < 1e-4f) return 0.0;
    double d = (double) E::kGainMinDb + ((double) E::kGainMaxDb - E::kGainMinDb) * t;
    if (t < E::kGainFoot) { const double u = t / E::kGainFoot; d += 20.0 * std::log10 (u * u); }
    return d;
}

// ── §H's metric: FOUR spectra (L, R, M, S) concatenated. See the header note.
static int fpLo()    { return (int) (20.0 / binHz()); }
static int fpHi()    { return (int) (16000.0 / binHz()); }
static int fpBlock() { return fpHi() - fpLo(); }
static std::vector<double> fingerprint (E& e)
{
    Noise n; n.reset();
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < (int) (0.35 * FS); ++i)          // seed to steady state
        e.processStereo (BUS * n.l(), BUS * n.r(), l, r);
    std::vector<float> L ((size_t) NFFT), R ((size_t) NFFT), M ((size_t) NFFT), S ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i)
    {
        e.processStereo (BUS * n.l(), BUS * n.r(), l, r);
        L[(size_t) i] = l; R[(size_t) i] = r;
        M[(size_t) i] = 0.5f * (l + r); S[(size_t) i] = 0.5f * (l - r);
    }
    const CVec c[4] = { cspec (L), cspec (R), cspec (M), cspec (S) };
    std::vector<double> out;
    out.reserve ((size_t) (4 * fpBlock()));
    for (int k = 0; k < 4; ++k)
        for (int i = fpLo(); i < fpHi() && i < (int) c[k].size(); ++i)
            out.push_back (db (std::abs (c[k][(size_t) i])));
    return out;
}

// the flat mean, kept for reporting only
static double spectralDist (const std::vector<double>& a, const std::vector<double>& b)
{
    double acc = 0.0; const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) acc += std::fabs (a[i] - b[i]);
    return acc / std::max<size_t> (1, n);
}

// 🚨 fb445 — THE METRIC HAS TO BE FAIR TO BAND-LIMITED CONTROLS, and the first
// version of this harness was not. A flat mean over 20 Hz..16 kHz dilutes any
// control that only acts on one octave by the ~9 octaves it does not touch:
// `Rumble` moves a real 2 dB in the bottom octave and averaged out to 0.001 dB,
// so the gate called a working knob dead. An ear does not average across the
// spectrum — it notices THE OCTAVE THAT CHANGED. So: mean |delta| per octave,
// then the MAX over octaves and over L / R / MID / SIDE. Same fb444 lesson as
// Bode's allpass, one level up: fix the metric, never lower the bar.
static double bandMaxDist (const std::vector<double>& a, const std::vector<double>& b)
{
    const int lo = fpLo(), hi = fpHi(), blk = fpBlock();
    double worst = 0.0;
    for (int k = 0; k < 4; ++k)
        for (double f = 20.0; f < 16000.0; f *= 2.0)
        {
            const int i0 = std::max (lo, (int) (f / binHz()));
            const int i1 = std::min (hi - 1, (int) (f * 2.0 / binHz()));
            if (i1 <= i0) continue;
            double acc = 0.0; int n = 0;
            for (int i = i0; i <= i1; ++i)
            {
                const size_t idx = (size_t) (k * blk + (i - lo));
                if (idx < a.size() && idx < b.size()) { acc += std::fabs (a[idx] - b[idx]); ++n; }
            }
            if (n > 0) worst = std::max (worst, acc / n);
        }
    return worst;
}

int main()
{
    std::printf ("\n══ utl_cert — UTILITY strip (kind 14) ══\n");
    std::printf ("   roster: %d Types live of %d slots · %d Characters · %d Wirings live of %d slots\n",
                 E::kNumTypes, E::kNumTypeSlots, E::kNumChars, E::kNumWirings, E::kNumWireSlots);

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§A — THE MATRIX AND POLARITY. Run FIRST: a swapped or inverted\n"
                 "     channel builds clean, measures 'different', and is the wrong device.\n");
    {
        // The anchor. If the do-nothing state is not nothing, nothing below means anything.
        E e; e.prepare (FS);
        auto p = base(); e.setParams (p);
        Noise n;
        auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, 4000, 1.0);
        double worst = 0.0;
        for (size_t i = 0; i < c.L.size(); ++i)
            worst = std::max (worst, std::max (std::fabs ((double) c.L[i] - c.inL[i]),
                                               std::fabs ((double) c.R[i] - c.inR[i])));
        char d[192];
        std::snprintf (d, sizeof d, "worst |out - in| over 4000 samples of stereo noise = %.3e", worst);
        gate ("DEFAULTS ARE TRANSPARENT — the strip does literally nothing", worst < 1e-7, d);
    }

    struct WCase { int w; const char* nm; bool l300, l1100, r300, r1100; };
    const WCase wc[] = {
        { 0, "Through",   true,  false, false, true  },
        { 2, "L To Both", true,  false, true,  false },
        { 3, "R To Both", false, true,  false, true  },
        { 4, "L Only",    true,  false, false, false },
        { 5, "R Only",    false, false, false, true  },
    };
    for (const auto& w : wc)
    {
        E e; e.prepare (FS);
        auto p = base(); p.wiring = w.w; e.setParams (p);
        auto c  = capture (e, twoTone, NFFT, 0.35);
        auto sl = cspec (c.L), sr = cspec (c.R);
        const double L3 = energyNear (sl, 300.0),  L11 = energyNear (sl, 1100.0);
        const double R3 = energyNear (sr, 300.0),  R11 = energyNear (sr, 1100.0);
        const double ref = std::max (std::max (L3, L11), std::max (R3, R11));
        auto present = [&] (double v) { return v > ref * 0.2; };
        const bool ok = present (L3) == w.l300 && present (L11) == w.l1100
                     && present (R3) == w.r300 && present (R11) == w.r1100;
        char d[224];
        std::snprintf (d, sizeof d,
                       "%-10s L:300=%+6.1f 1100=%+6.1f  R:300=%+6.1f 1100=%+6.1f dB",
                       w.nm, db (L3 / ref), db (L11 / ref), db (R3 / ref), db (R11 / ref));
        gate ((std::string ("Wiring '") + w.nm + "' routes exactly what it says").c_str(), ok, d);
    }
    {
        // `Difference` gets its own probe: presence alone cannot tell it apart
        // from `Sum`. A CORRELATED pair must NULL, an ANTI-correlated pair must
        // come through at full amplitude. Only one wiring does both.
        E e1; e1.prepare (FS); auto p = base(); p.wiring = 1; e1.setParams (p);
        auto cc = capture (e1, monoTone, 8000, 0.4);
        E e2; e2.prepare (FS); e2.setParams (p);
        auto ca = capture (e2, antiTone, 8000, 0.4);
        const double kill = rms (cc.L) / BUS, pass = rms (ca.L) / (BUS * 0.7071);
        char d[192];
        std::snprintf (d, sizeof d, "correlated in -> %.1f dB (want silence) · anti-correlated -> %.2f dB (want 0)",
                       db (kill), db (pass));
        gate ("Wiring 'Difference' NULLS a correlated pair, passes an inverted one",
              kill < 1e-4 && pass > 0.95 && pass < 1.05, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.trade = true; e.setParams (p);
        auto c  = capture (e, twoTone, NFFT, 0.35);
        auto sl = cspec (c.L), sr = cspec (c.R);
        const double L11 = energyNear (sl, 1100.0), R3 = energyNear (sr, 300.0);
        const double L3  = energyNear (sl, 300.0),  R11 = energyNear (sr, 1100.0);
        char d[192];
        std::snprintf (d, sizeof d, "L now carries 1100 (%.1f dB over its 300) · R carries 300 (%.1f dB over its 1100)",
                       db (L11 / std::max (1e-12, L3)), db (R3 / std::max (1e-12, R11)));
        gate ("pill 'Trade' really exchanges L and R", L11 > 20.0 * L3 && R3 > 20.0 * R11, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.sum = true; e.setParams (p);
        auto c  = capture (e, twoTone, NFFT, 1.0);
        auto sl = cspec (c.L), sr = cspec (c.R);
        double dif = 0.0;
        for (size_t i = 0; i < c.L.size(); ++i) dif = std::max (dif, std::fabs ((double) c.L[i] - c.R[i]));
        const double L3 = energyNear (sl, 300.0), L11 = energyNear (sl, 1100.0);
        char d[192];
        std::snprintf (d, sizeof d, "both tones in both channels within %.2f dB · |L-R| = %.2e",
                       std::fabs (db (L3 / L11)), dif);
        gate ("pill 'Sum' folds to mono (both tones, both channels, identical)",
              std::fabs (db (L3 / L11)) < 0.5 && dif < 1e-7 && energyNear (sr, 300.0) > 0.0, d);
    }
    {
        // POLARITY, at the bit level. Flip both channels and the output must be
        // the exact negative of the input — nothing else in the device may move.
        E e; e.prepare (FS);
        auto p = base(); p.flipL = true; p.flipR = true; e.setParams (p);
        Noise n;
        auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, 4000, 1.0);
        double worst = 0.0;
        for (size_t i = 0; i < c.L.size(); ++i)
            worst = std::max (worst, std::max (std::fabs ((double) c.L[i] + c.inL[i]),
                                               std::fabs ((double) c.R[i] + c.inR[i])));
        char d[192];
        std::snprintf (d, sizeof d, "worst |out + in| = %.3e  ·  engine meter says L %+.0f  R %+.0f",
                       worst, e.meterPolarity (0), e.meterPolarity (1));
        gate ("'Flip L' + 'Flip R' gives EXACTLY -in (and the meter says so)",
              worst < 1e-7 && e.meterPolarity (0) < 0.0 && e.meterPolarity (1) < 0.0, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.flipL = true; e.setParams (p);
        auto c = capture (e, monoTone, 8000, 0.5);
        double fold = 0.0;
        for (size_t i = 0; i < c.L.size(); ++i) fold += std::pow (0.5 * (c.L[i] + c.R[i]), 2.0);
        fold = std::sqrt (fold / c.L.size());
        char d[192];
        std::snprintf (d, sizeof d, "mono in, L inverted -> fold-down = %.1f dBFS (want silence) · meter L %+.0f R %+.0f",
                       db (fold), e.meterPolarity (0), e.meterPolarity (1));
        gate ("'Flip L' alone nulls a mono source on fold-down",
              fold < 1e-6 && e.meterPolarity (0) < 0.0 && e.meterPolarity (1) > 0.0, d);
    }
    {
        // The reserved slots. fb373: a dropdown normalised on its own option
        // count instead of the param's cardinality silently plays a different
        // machine. Ask for slot 7 and prove it lands on entry 0, loudly.
        E e; e.prepare (FS);
        auto p = base(); p.type = 7; p.wiring = 7; e.setParams (p);
        float l, r; e.processStereo (BUS, BUS, l, r);
        char d[192];
        std::snprintf (d, sizeof d, "asked Type 7 / Wiring 7 of 8 slots -> engine reports Type %d, Wiring %d",
                       e.meterType(), e.meterWiring());
        gate ("reserved slots resolve to entry 0, never off the end of the table",
              e.meterType() == 0 && e.meterWiring() == 0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§B — GAIN. The one control that justifies the whole device.\n");
    {
        double worst = 0.0; float worstT = 0.0f;
        for (int i = 1; i <= 10; ++i)
        {
            const float t = (float) i / 10.0f;
            E e; e.prepare (FS);
            auto p = base(); p.gain = t; e.setParams (p);
            auto c = capture (e, monoTone, 8000, 0.5);
            const double meas = db (rms (c.L) / rms (c.inL));
            const double err  = std::fabs (meas - refFaderDb (t));
            if (err > worst) { worst = err; worstT = t; }
        }
        char d[192];
        std::snprintf (d, sizeof d, "worst error vs an INDEPENDENTLY computed dB reference: %.4f dB (at t = %.1f)",
                       worst, worstT);
        gate ("Gain is accurate in dB across the whole travel", worst < 0.05, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.gain = 0.0f; e.setParams (p);
        auto c = capture (e, monoTone, 8000, 1.0);
        double pk = 0.0;
        for (float v : c.L) pk = std::max (pk, (double) std::fabs (v));
        char d[192];
        std::snprintf (d, sizeof d, "peak after the glide = %.3e (%.1f dBFS) · engine meter %.0f dB",
                       pk, db (pk), e.meterGainDb());
        gate ("Gain 0 is a true, glided -inf — an automatable MUTE, not a floor",
              pk == 0.0 && e.meterGainDb() < -150.0, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.gain = E::kGainUnityT; e.setParams (p);
        auto c = capture (e, monoTone, 4000, 0.5);
        double worst = 0.0;
        for (size_t i = 0; i < c.L.size(); ++i) worst = std::max (worst, std::fabs ((double) c.L[i] - c.inL[i]));
        char d[192];
        std::snprintf (d, sizeof d, "|out - in| at the unity detent = %.3e (must be EXACT, it is the double-click home)", worst);
        gate ("unity at 2/3 travel is exactly 0.000 dB", worst == 0.0, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.gain = 1.0f; e.setParams (p);
        auto c = capture (e, monoTone, 8000, 0.5);
        const double meas = db (rms (c.L) / rms (c.inL));
        char d[192];
        std::snprintf (d, sizeof d, "full travel = %+.2f dB (a -26 dBFS bus becomes +4 dBFS: it WILL hit the DAC)", meas);
        gate ("the top of the fader is +30 dB, not a polite +12", meas > 29.5 && meas < 30.5, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.dim = true; e.setParams (p);
        auto c = capture (e, monoTone, 8000, 0.5);
        char d[192];
        std::snprintf (d, sizeof d, "pill 'Dim' = %+.3f dB", db (rms (c.L) / rms (c.inL)));
        gate ("pill 'Dim' is exactly -20 dB", std::fabs (db (rms (c.L) / rms (c.inL)) + 20.0) < 0.05, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§C — THE BOUNDED-WIDTH LAW. The explicit contrast with WIDEN's R11,\n"
                 "     whose cert REQUIRES corr <= -0.9 and a mono fold-down at <= -25 dB.\n");
    {
        // The engine's own number, swept across the entire Image travel on every
        // live Type (fb425 — sweep the matrix, do not sample it).
        bool bad = false; double lowest = 9.9; int badT = -1; float badI = 0.0f;
        for (int t = 0; t < E::kNumTypes; ++t)
            for (int i = 0; i <= 40; ++i)
            {
                E e; e.prepare (FS);
                auto p = base(); p.type = t; p.image = (float) i / 40.0f; e.setParams (p);
                const double mg = e.meterMidGain();
                if (mg < lowest) { lowest = mg; badT = t; badI = p.image; }
                if (mg < E::kMidFloor - 1e-6) bad = true;
            }
        char d[224];
        std::snprintf (d, sizeof d, "5 Types x 41 Image positions: lowest mid gain = %.4f (floor %.2f) at Type %d, Image %.2f",
                       lowest, E::kMidFloor, badT, badI);
        gate ("the mid gain NEVER reaches the floor, anywhere on the Image travel", ! bad, d);
    }
    for (int t = 0; t < E::kNumTypes; ++t)
    {
        E en; en.prepare (FS); auto pn = base(); pn.type = t; pn.image = 0.5f; en.setParams (pn);
        Noise n1; auto cn = capture (en, [&n1](int){ return std::make_pair (BUS * n1.l(), BUS * n1.r()); }, NFFT, 0.35);
        E ex; ex.prepare (FS); auto px = base(); px.type = t; px.image = 1.0f; ex.setParams (px);
        Noise n2; auto cx = capture (ex, [&n2](int){ return std::make_pair (BUS * n2.l(), BUS * n2.r()); }, NFFT, 0.35);

        std::vector<float> fn ((size_t) NFFT), fx ((size_t) NFFT);
        for (int i = 0; i < NFFT; ++i)
        {
            fn[(size_t) i] = 0.5f * (cn.L[(size_t) i] + cn.R[(size_t) i]);
            fx[(size_t) i] = 0.5f * (cx.L[(size_t) i] + cx.R[(size_t) i]);
        }
        const double foldDb = db (rms (fx) / std::max (1e-12, rms (fn)));
        char d[240];
        std::snprintf (d, sizeof d, "%-7s at Image 300%%: mono fold-down %+.2f dB vs neutral  (WIDEN R11 demands <= -25.00)",
                       E::typeNames()[t], foldDb);
        gate ((std::string ("Type '") + E::typeNames()[t] + "' survives a mono fold-down at max Image").c_str(),
              foldDb > -8.0, d);
    }
    {
        // 🚨 fb445 / fb421 — NOTHING GATED THE TOP OF `Image`. Shrinking
        // kImageMax from 300 % to a polite 130 % passed the entire harness:
        // every gate here asks that the mid SURVIVES, and a timid maximum makes
        // that easier, not harder. R11 cuts both ways — the bound has a floor
        // AND the travel has to reach somewhere worth going.
        E e; e.prepare (FS);
        auto p = base(); p.type = 0; p.image = 1.0f; e.setParams (p);
        Noise n; auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, NFFT, 0.4);
        std::vector<float> m ((size_t) NFFT), sd ((size_t) NFFT);
        for (int i = 0; i < NFFT; ++i)
        { m[(size_t) i] = 0.5f * (c.L[(size_t) i] + c.R[(size_t) i]);
          sd[(size_t) i] = 0.5f * (c.L[(size_t) i] - c.R[(size_t) i]); }
        const double ratio = (rms (sd) / rms (m));
        char d[224];
        std::snprintf (d, sizeof d, "engine says the image factor is %.2fx · side/mid came out %.2fx the input's 1.00",
                       e.meterImageW(), ratio);
        gate ("Image 100 % really reaches 300 % (a polite maximum is a maximum set wrong)",
              e.meterImageW() > 2.99 && ratio > 2.5, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.type = 0; p.image = 0.0f; e.setParams (p);
        Noise n; auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, NFFT, 0.35);
        auto sl = cspec (c.L), sr = cspec (c.R);
        const double cr = bandCorr (sl, sr, 40.0, 16000.0);
        char d[192];
        std::snprintf (d, sizeof d, "correlation at Image 0%% = %+.5f · engine meter %+.4f", cr, e.meterCorr());
        gate ("Image 0 % on 'Strip' is real mono (corr = +1)",
              cr > 0.9999 && e.meterCorr() > 0.999, d);
    }
    {
        // `Outer`'s stated mechanism: the mid is bit-preserved, so the mono
        // fold-down is INVARIANT under Image. If that ever stops being true the
        // Type has silently become `Strip`.
        double a = 0.0, b = 0.0;
        for (int k = 0; k < 2; ++k)
        {
            E e; e.prepare (FS);
            auto p = base(); p.type = 2; p.image = (k == 0 ? 0.0f : 1.0f); e.setParams (p);
            Noise n; auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, NFFT, 0.35);
            std::vector<float> f ((size_t) NFFT);
            for (int i = 0; i < NFFT; ++i) f[(size_t) i] = 0.5f * (c.L[(size_t) i] + c.R[(size_t) i]);
            (k == 0 ? a : b) = rms (f);
        }
        char d[192];
        std::snprintf (d, sizeof d, "fold-down at Image 0 vs Image 300 %% differs by %.4f dB", std::fabs (db (a / b)));
        gate ("Type 'Outer' leaves the mono fold-down INVARIANT (its whole point)",
              std::fabs (db (a / b)) < 0.02, d);
    }
    {
        // `Turn`'s distinguishing mechanism: it manufactures side out of mid, so
        // it widens a MONO source. `Strip` provably cannot — scaling zero is zero.
        double sideTurn = 0.0, sideStrip = 0.0;
        for (int t : { 1, 0 })
        {
            E e; e.prepare (FS);
            auto p = base(); p.type = t; p.image = 1.0f; e.setParams (p);
            auto c = capture (e, monoTone, 8000, 0.4);
            std::vector<float> s ((size_t) c.L.size());
            for (size_t i = 0; i < c.L.size(); ++i) s[i] = 0.5f * (c.L[i] - c.R[i]);
            (t == 1 ? sideTurn : sideStrip) = rms (s);
        }
        char d[224];
        std::snprintf (d, sizeof d, "mono in at Image 300%%: 'Turn' side = %.1f dBFS, 'Strip' side = %.1f dBFS",
                       db (sideTurn), db (sideStrip));
        gate ("'Turn' widens a MONO source; 'Strip' provably cannot (different mechanisms)",
              sideTurn > 1e-3 && sideStrip < 1e-9, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§D — MONO BELOW. Below the corner it is mono; above it, stereo survives.\n");
    {
        E e; e.prepare (FS);
        auto p = base(); p.monoBelow = 0.75f; p.slope = 1.0f; e.setParams (p);  // ~640 Hz, 3 poles
        Noise n; auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, NFFT, 0.4);
        auto sl = cspec (c.L), sr = cspec (c.R);
        const double lo = bandCorr (sl, sr,   40.0,   100.0);   // two octaves below the corner
        const double hi = bandCorr (sl, sr, 3000.0, 12000.0);
        char d[192];
        std::snprintf (d, sizeof d, "corner ~640 Hz · corr 40-100 Hz = %+.4f (want +1) · corr 3-12 kHz = %+.4f (want ~0)", lo, hi);
        gate ("Mono Below makes the bass mono and leaves the top alone", lo > 0.995 && std::fabs (hi) < 0.2, d);
    }
    {
        // The corner really moves, monotonically, across the whole travel.
        double prev = -2.0; bool mono = true; std::string row;
        for (int i = 1; i <= 5; ++i)
        {
            E e; e.prepare (FS);
            auto p = base(); p.monoBelow = (float) i / 5.0f; p.slope = 1.0f; e.setParams (p);
            Noise n; auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, NFFT, 0.4);
            auto sl = cspec (c.L), sr = cspec (c.R);
            const double cr = bandCorr (sl, sr, 250.0, 400.0);
            char b[32]; std::snprintf (b, sizeof b, "%+.2f ", cr); row += b;
            if (cr < prev - 0.01) mono = false;
            prev = cr;
        }
        gate ("the corner climbs monotonically (corr at 250-400 Hz, 5 steps)", mono, row);
    }
    {
        // `Slope` has to be a real filter order, not a cosmetic. Measure BELOW
        // the corner, where order actually shows: the corner is compensated so
        // that the -3 dB point does NOT move, which means the only thing left
        // for `Slope` to change is how fast the skirt falls.
        double c1 = 0.0, c3 = 0.0, a1 = 0.0, a3 = 0.0;
        for (int k = 0; k < 2; ++k)
        {
            E e; e.prepare (FS);
            auto p = base(); p.monoBelow = 0.35f; p.slope = (k == 0 ? 0.0f : 1.0f); e.setParams (p);
            Noise n; auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, NFFT, 0.4);
            auto sl = cspec (c.L), sr = cspec (c.R);
            (k == 0 ? c1 : c3) = bandCorr (sl, sr,  55.0,   95.0);    // an octave BELOW
            (k == 0 ? a1 : a3) = bandCorr (sl, sr, 320.0,  600.0);    // an octave ABOVE
        }
        char d[240];
        std::snprintf (d, sizeof d, "corner ~164 Hz · below (55-95): 1p %+.3f -> 3p %+.3f · above (320-600): 1p %+.3f -> 3p %+.3f",
                       c1, c3, a1, a3);
        gate ("'Slope' steepens the skirt without dragging the corner with it",
              c3 > c1 + 0.05 && a3 < a1 + 0.05, d);
    }

    {
        // 🚨 fb445 / fb421 — NOTHING GATED `Bleed`'s BAND. Making the crosstalk
        // full-band instead of HF-only turned zero gates red: §H only asks that
        // the knob DOES something, and it still did. But HF-only is the whole
        // claim — `Bleed` is the exact inverse of `Mono Below`, closing the TOP
        // of the image while the bottom stays wide, and a full-band version is
        // just a second, worse mono knob. So gate the band, not the motion.
        E e; e.prepare (FS);
        auto p = base(); p.bleed = 1.0f; p.hinge = 0.5f; p.slope = 1.0f; e.setParams (p);
        Noise n; auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, NFFT, 0.4);
        auto sl = cspec (c.L), sr = cspec (c.R);
        const double lo = bandCorr (sl, sr,   40.0,   150.0);
        const double hi = bandCorr (sl, sr, 3000.0, 12000.0);
        char d[224];
        std::snprintf (d, sizeof d, "hinge ~600 Hz, Bleed 100 %%: corr 3-12 kHz = %+.4f (want +1) · corr 40-150 Hz = %+.4f (want ~0)",
                       hi, lo);
        gate ("Bleed couples the TOP of the image and leaves the bottom WIDE",
              hi > 0.95 && std::fabs (lo) < 0.25, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§E — DC. On, it goes; off, it PASSES — a bypass has to be a real bypass.\n");
    {
        auto dcGen = [] (int i) {
            const float v = BUS * (float) std::sin (2.0 * M_PI * 440.0 * i / FS) + 0.20f;
            return std::make_pair (v, v);
        };
        for (int k = 0; k < 2; ++k)
        {
            E e; e.prepare (FS);
            auto p = base(); p.dc = (k == 1); p.rumble = 0.0f; e.setParams (p);
            auto c = capture (e, dcGen, 16000, 1.2);
            double mean = 0.0; for (float v : c.L) mean += v;
            mean /= c.L.size();
            char d[192];
            std::snprintf (d, sizeof d, "DC %s: 0.200 in -> %.5f out", k ? "ON " : "OFF", mean);
            if (k == 0) gate ("DC OFF really passes DC (the bypass is a bypass)", std::fabs (mean - 0.20) < 0.002, d);
            else        gate ("DC ON removes it", std::fabs (mean) < 0.002, d);
        }
    }
    {
        // The documented interaction: `Tuck` is an asymmetric stage, so it MAKES
        // DC, and the block sits downstream precisely to catch it.
        double off = 0.0, on = 0.0;
        for (int k = 0; k < 2; ++k)
        {
            E e; e.prepare (FS);
            auto p = base(); p.chr = 4; p.strain = 0.7f; p.clamp = 1.0f; p.dc = (k == 1); p.rumble = 0.3f;
            e.setParams (p);
            auto c = capture (e, monoTone, 16000, 1.2);
            double mean = 0.0; for (float v : c.L) mean += v;
            (k == 0 ? off : on) = mean / c.L.size();
        }
        char d[192];
        std::snprintf (d, sizeof d, "'Tuck' leaves %+.5f of DC on its own; with the DC pill: %+.5f", off, on);
        gate ("'Tuck' makes DC and the DC block catches it (the documented pair)",
              std::fabs (off) > 1e-4 && std::fabs (on) < std::fabs (off) * 0.05, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§F — THE MIX LAW.\n");
    {
        E e; e.prepare (FS);
        auto p = base(); p.mix = 0.0f; p.gain = 1.0f; p.image = 1.0f; p.strain = 0.8f;
        p.flipL = true; p.trade = true; p.monoBelow = 0.6f; e.setParams (p);
        Noise n; auto c = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, 8000, 1.0);
        double worst = 0.0;
        for (size_t i = 0; i < c.L.size(); ++i)
            worst = std::max (worst, std::max (std::fabs ((double) c.L[i] - c.inL[i]),
                                               std::fabs ((double) c.R[i] - c.inR[i])));
        char d[192];
        std::snprintf (d, sizeof d, "every control slammed, Mix 0: worst |out - in| = %.3e", worst);
        gate ("Mix 0 passes the dry BIT-IDENTICALLY, whatever else is set", worst == 0.0, d);
    }
    {
        E e; e.prepare (FS);
        auto p = base(); p.mix = 1.0f; p.gain = 0.0f; e.setParams (p);
        auto c = capture (e, monoTone, 8000, 1.0);
        double pk = 0.0; for (float v : c.L) pk = std::max (pk, (double) std::fabs (v));
        char d[192];
        std::snprintf (d, sizeof d, "Mix 100 %% with the fader at -inf: residual = %.1f dBFS", db (pk));
        gate ("Mix 100 % is FULLY wet — no dry leak at all", pk < 1e-6, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§G — THE GUARD. Eight Characters, no time constants (CONTRACT R2).\n");
    {
        double worst = 0.0; int badC = -1;
        for (int c = 0; c < E::kNumChars; ++c)
            for (float st : { 0.15f, 0.5f, 1.0f })
            {
                E e; e.prepare (FS);
                auto p = base(); p.chr = c; p.strain = st; e.setParams (p);
                // fb445 — 1e-4 IS NOT A SMALL SIGNAL HERE. `Fuse` drives by
                // 48 dB and then by another 4x into a HALVED rail, so 1e-4
                // arrives at 4x the rail and the gate read -9.6 dB and called a
                // correct makeup broken. The probe has to be small relative to
                // the WORST-CASE drive (about 1000x), not to full scale.
                auto cap = capture (e, [] (int i) {
                    const float v = 1e-6f * (float) std::sin (2.0 * M_PI * 300.0 * i / FS);
                    return std::make_pair (v, v); }, 8000, 0.5);
                const double g = std::fabs (db (rms (cap.L) / rms (cap.inL)));
                if (g > worst) { worst = g; badC = c; }
            }
        char d[192];
        std::snprintf (d, sizeof d, "worst small-signal deviation over 8 Characters x 3 Strains: %.3f dB (%s)",
                       worst, badC >= 0 ? E::charNames()[badC] : "-");
        gate ("every Character has UNIT SLOPE at zero (Strain is not a level knob)", worst < 0.5, d);
    }
    {
        // fb425 — SWEEP THE MATRIX. A bound checked at one knob position is a
        // bound that has not been checked: diffusion, asymmetry and foldback all
        // move the worst case to a different cell.
        bool bad = false; double worst = 0.0; int wc2 = 0; float wS = 0, wK = 0, wIn = 0;
        for (int c = 0; c < E::kNumChars; ++c)
        for (float st : { 0.2f, 0.6f, 1.0f })
        for (float kn : { 0.0f, 0.5f, 1.0f })
        for (float in : { BUS, 0.7f })
        {
            E e; e.prepare (FS);
            auto p = base(); p.chr = c; p.strain = st; p.clamp = kn; e.setParams (p);
            float l, r; double ph = 0.0; const double inc = 2.0 * M_PI * 90.0 / FS;
            for (int i = 0; i < (int) (0.35 * FS); ++i)
            {
                const float s = in * (float) std::sin (ph); ph += inc;
                e.processStereo (s, s, l, r);
                const double m = std::max (std::fabs (l), std::fabs (r));
                if (! std::isfinite (m)) bad = true;
                if (m > worst) { worst = m; wc2 = c; wS = st; wK = kn; wIn = in; }
            }
        }
        char d[240];
        std::snprintf (d, sizeof d, "144 cells: worst peak %.3f (%.1f dBFS) at %s / Strain %.1f / Clamp %.1f / in %.2f%s",
                       worst, db (worst), E::charNames()[wc2], wS, wK, wIn, bad ? "  <- NON-FINITE" : "");
        gate ("the guard keeps the output BOUNDED on every Character x Strain x Clamp",
              ! bad && worst < 1.0, d);
    }
    {
        double worst = 0.0;
        for (int c = 0; c < E::kNumChars; ++c)
        {
            E e; e.prepare (FS);
            auto p = base(); p.chr = c; p.strain = 0.0f; e.setParams (p);
            Noise n; auto cap = capture (e, [&n](int){ return std::make_pair (BUS * n.l(), BUS * n.r()); }, 4000, 1.0);
            for (size_t i = 0; i < cap.L.size(); ++i)
                worst = std::max (worst, std::fabs ((double) cap.L[i] - cap.inL[i]));
        }
        char d[192];
        std::snprintf (d, sizeof d, "worst |out - in| at Strain 0 over all 8 Characters = %.3e", worst);
        gate ("Strain 0 is a BIT-EXACT bypass of the guard", worst == 0.0, d);
    }
    {
        // 🚨 fb445 / fb421 — NOTHING GATED THE FOLDBACK. Deleting the BIBO
        // period-wrap inside railFold turned zero gates red, and the reason is
        // subtle: without the wrap the "fold" degenerates into |x| - 2c, which
        // the 1/drive makeup then scales back to roughly the input — so it is
        // still perfectly BOUNDED and still measurably "different", it has just
        // stopped being a wavefolder. `Clamp` at 0 on `Brick` is supposed to be
        // the most destructive corner of this device; a fold that only reflects
        // once is a clipper with extra steps. Gate the HARMONICS, which is the
        // only place the difference lives.
        double hfFold = 0.0, hfClip = 0.0;
        for (int k = 0; k < 2; ++k)
        {
            E e; e.prepare (FS);
            auto p = base(); p.chr = 1; p.strain = 0.8f; p.clamp = (k == 0 ? 0.0f : 1.0f);
            e.setParams (p);
            auto c = capture (e, monoTone, NFFT, 0.5);
            auto sp = cspec (c.L);
            double f0 = energyNear (sp, 440.0), hi = 0.0;
            for (int h = 10; h <= 30; ++h) hi += std::pow (energyNear (sp, 440.0 * h), 2.0);
            (k == 0 ? hfFold : hfClip) = std::sqrt (hi) / std::max (1e-12, f0);
        }
        char d[240];
        std::snprintf (d, sizeof d, "harmonics 10-30 vs the fundamental: Clamp 0 (fold) %.2f · Clamp 1 (clip) %.2f",
                       hfFold, hfClip);
        gate ("'Brick' at Clamp 0 really FOLDS (many reflections), it does not just clip",
              hfFold > hfClip * 1.6, d);
    }
    {
        // 🚨 fb445 / fb421 — A MAXIMUM THAT IS A MUTE IS NOT A MAXIMUM, and NO
        // gate caught that until this one. With an exact 1/drive makeup (which
        // is what keeps sat'(0) == 1) a hard-clipped output sits at rail/drive,
        // so a FIXED rail put the top of `Strain` 43 dB down: the loudest knob
        // position on the card was, in practice, silence. Every other guard gate
        // stayed green through it — "bounded" gets MORE true as the output
        // vanishes, and "unit slope at zero" only ever looks at small signals.
        // The mutation that exposed it turned zero gates red, so per fb421 the
        // finding is about the GATE. This is the gate.
        E e; e.prepare (FS);
        auto p = base(); p.chr = 1; p.strain = 1.0f; p.clamp = 1.0f; e.setParams (p);
        auto c = capture (e, monoTone, NFFT, 0.5);
        const double lvl = db (rms (c.L) / rms (c.inL));
        auto sp = cspec (c.L);
        const double f0 = energyNear (sp, 440.0), h3 = energyNear (sp, 1320.0);
        char d[240];
        std::snprintf (d, sizeof d, "Strain 100 %%: output %+.1f dB vs input, 3rd harmonic %+.1f dB of the fundamental",
                       lvl, db (h3 / std::max (1e-12, f0)));
        gate ("Strain at 100 % is DESTRUCTIVE, not ABSENT (level survives, wave is square)",
              lvl > -30.0 && h3 > f0 * 0.1, d);
    }
    {
        // `Rail`'s distinguishing mechanism, ASKED CORRECTLY. The first version
        // of this gate measured CORRELATION and came back +0.004 vs -0.003 —
        // and the gate was wrong, not the engine: a common gain applied to both
        // channels cannot change a correlation, so the probe could never have
        // seen anything. What a stereo LINK actually preserves is the L/R
        // RATIO. Probe with an UNEQUAL correlated pair and watch the side/mid
        // ratio: independent limiting holds the louder channel back harder and
        // drags the image to the centre; the linked one leaves it exactly where
        // it was. (This is also why the engine's own comment was backwards.)
        auto lop = [] (int i) {
            const float v = (float) std::sin (2.0 * M_PI * 220.0 * i / FS);
            return std::make_pair (0.55f * v, 0.20f * v);
        };
        const double refRatio = 0.175 / 0.375;               // side/mid of the input
        double rRail = 0.0, rCush = 0.0;
        for (int k = 0; k < 2; ++k)
        {
            E e; e.prepare (FS);
            auto p = base(); p.chr = (k == 0 ? 5 : 0); p.strain = 0.75f; p.clamp = 0.9f;
            e.setParams (p);
            auto c = capture (e, lop, 8000, 0.5);
            std::vector<float> m ((size_t) c.L.size()), sd ((size_t) c.L.size());
            for (size_t i = 0; i < c.L.size(); ++i)
            { m[i] = 0.5f * (c.L[i] + c.R[i]); sd[i] = 0.5f * (c.L[i] - c.R[i]); }
            (k == 0 ? rRail : rCush) = rms (sd) / std::max (1e-12, rms (m));
        }
        char d[240];
        std::snprintf (d, sizeof d,
                       "side/mid vs the input's %.3f -> 'Rail' %.3f (%+.2f dB) · 'Cushion' %.3f (%+.2f dB)",
                       refRatio, rRail, db (rRail / refRatio), rCush, db (rCush / refRatio));
        gate ("'Rail' is stereo-LINKED: it preserves the image, 'Cushion' collapses it",
              std::fabs (db (rRail / refRatio)) < 0.5 && db (rCush / refRatio) < -3.0, d);
    }
    {
        // Night-and-day: no two Characters may be tellable apart only on paper.
        std::vector<std::vector<double>> fp;
        for (int c = 0; c < E::kNumChars; ++c)
        {
            E e; e.prepare (FS);
            auto p = base(); p.chr = c; p.strain = 0.55f; p.clamp = 0.5f; e.setParams (p);
            fp.push_back (fingerprint (e));
        }
        double worst = 1e9; int a = 0, b = 0;
        for (int i = 0; i < E::kNumChars; ++i)
            for (int j = i + 1; j < E::kNumChars; ++j)
            {
                const double dd = bandMaxDist (fp[(size_t) i], fp[(size_t) j]);
                if (dd < worst) { worst = dd; a = i; b = j; }
            }
        char d[224];
        std::snprintf (d, sizeof d, "closest of 28 pairs: %s vs %s = %.3f dB (busiest octave)",
                       E::charNames()[a], E::charNames()[b], worst);
        gate ("every Character is a different MECHANISM, measured pairwise", worst > 1.0, d);
    }
    {
        // and the same for the Types, on the axis they actually live on.
        std::vector<std::vector<double>> fp;
        for (int t = 0; t < E::kNumTypes; ++t)
        {
            E e; e.prepare (FS);
            auto p = base(); p.type = t; p.image = 0.95f; e.setParams (p);
            fp.push_back (fingerprint (e));
        }
        double worst = 1e9; int a = 0, b = 0;
        for (int i = 0; i < E::kNumTypes; ++i)
            for (int j = i + 1; j < E::kNumTypes; ++j)
            {
                const double dd = bandMaxDist (fp[(size_t) i], fp[(size_t) j]);
                if (dd < worst) { worst = dd; a = i; b = j; }
            }
        char d[224];
        std::snprintf (d, sizeof d, "closest of 10 pairs: %s vs %s = %.3f dB (busiest octave)",
                       E::typeNames()[a], E::typeNames()[b], worst);
        gate ("every Type is a different image LAW, measured pairwise", worst > 1.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§H — EVERY KNOB TRANSFORMS ACROSS ITS WHOLE TRAVEL.\n"
                 "     Metric: L + R + MID + SIDE magnitude spectra. Gross energy is blind\n"
                 "     to Image and Twist, which move energy between M and S at constant total.\n");
    {
        struct K { const char* nm; float P::* fld; };
        const K ks[] = { { "Gain",       &P::gain      },
                         { "Image",      &P::image     },
                         { "Steer",      &P::steer     },
                         { "Mix",        &P::mix       },
                         { "Strain",     &P::strain    },
                         { "Clamp",      &P::clamp     },
                         { "Mono Below", &P::monoBelow },
                         { "Slope",      &P::slope     },
                         { "Twist",      &P::twist     },
                         { "Rumble",     &P::rumble    },
                         { "Bleed",      &P::bleed     },
                         { "Hinge",      &P::hinge     } };
        for (const auto& k : ks)
        {
            const std::string nm = k.nm;
            std::vector<double> prev; double minStep = 1e9, minFlat = 1e9;
            for (int i = 0; i <= 6; ++i)
            {
                E e; e.prepare (FS);
                P p = base();
                // Each knob is swept over a base where it HAS something to act
                // on — the bod_cert precedent (it sweeps with fdbk = 0.60,
                // because in-loop controls are inert with the loop open).
                if (nm == "Clamp")      { p.strain = 0.6f; p.chr = 1; }
                if (nm == "Slope")      { p.monoBelow = 0.45f; p.bleed = 0.7f; }
                if (nm == "Hinge")      { p.bleed = 0.8f; p.type = 3; p.image = 0.9f; }
                if (nm == "Rumble")     { p.dc = true; }
                // Mix blends WET against DRY, so on a transparent strip it is
                // blending a signal with itself and the only thing left moving
                // is the equal-power law's own +3 dB bulge. That is not a dead
                // knob, it is a knob with nothing to do — so give it something,
                // exactly as bod_cert sweeps its in-loop controls with the loop
                // closed.
                if (nm == "Mix")        { p.wiring = 1; p.strain = 0.45f; p.image = 1.0f;
                                          p.monoBelow = 0.8f; p.bleed = 0.8f; p.twist = 0.9f; }
                p.*(k.fld) = (float) i / 6.0f;
                e.setParams (p);
                auto f = fingerprint (e);
                if (! prev.empty())
                {
                    minStep  = std::min (minStep,  bandMaxDist  (prev, f));
                    minFlat  = std::min (minFlat,  spectralDist (prev, f));
                }
                prev = f;
            }
            char d[224];
            std::snprintf (d, sizeof d, "%-11s smallest step across 6: %.3f dB in its busiest octave (%.3f dB flat)",
                           k.nm, minStep, minFlat);
            gate ((nm + " transforms across its whole travel").c_str(), minStep > 0.5, d);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§I — NO CLICKS. Every continuous control is swept under a sustained tone.\n");
    {
        struct K { const char* nm; float P::* fld; };
        const K ks[] = { { "Gain",       &P::gain      },
                         { "Image",      &P::image     },
                         { "Steer",      &P::steer     },
                         { "Mix",        &P::mix       },
                         { "Mono Below", &P::monoBelow },
                         { "Twist",      &P::twist     },
                         { "Bleed",      &P::bleed     },
                         { "Hinge",      &P::hinge     } };
        // fb445 — THE CLICK METRIC HAS TO BE LEVEL-NORMALISED. The first version
        // compared the worst sample step against a FIXED reference and reported
        // `Gain` at 28x — which was not a click at all, it was the tone being
        // 30 dB louder at the end of a +90 dB sweep. An absolute step bar cannot
        // tell a discontinuity from a crescendo. Normalise inside short blocks:
        // per block, the biggest step a clean 220 Hz tone of THAT block's own RMS
        // could take is rms*sqrt(2)*2*pi*f/fs, and a click is a step much larger
        // than that.
        const double slope1 = 1.41421356 * 2.0 * M_PI * 220.0 / FS;
        for (const auto& k : ks)
        {
            E e; e.prepare (FS);
            P p = base(); e.setParams (p);
            float l = 0.0f, r = 0.0f, pl = 0.0f, pr = 0.0f;
            double worst = 0.0; const int N = (int) (1.5 * FS);
            const int B = 256;
            double blkStep = 0.0, blkPow = 0.0;
            for (int i = 0; i < N; ++i)
            {
                if ((i % 64) == 0) { p.*(k.fld) = (float) i / (float) N; e.setParams (p); }
                const float s = BUS * (float) std::sin (2.0 * M_PI * 220.0 * i / FS);
                e.processStereo (s, s, l, r);
                if (i > (int) (0.05 * FS))
                {
                    blkStep = std::max (blkStep, std::max (std::fabs ((double) l - pl),
                                                            std::fabs ((double) r - pr)));
                    // fb445 — NORMALISE ON BOTH CHANNELS. The first version
                    // divided the worst step of EITHER channel by the RMS of the
                    // LEFT one, so `Steer` at the end of its travel (left muted,
                    // right at +3 dB) read 32x and `Twist` read 9.5x — both of
                    // them the surviving channel's perfectly ordinary slope,
                    // divided by a channel that had been turned off on purpose.
                    blkPow += 0.5 * ((double) l * l + (double) r * r);
                }
                pl = l; pr = r;
                if ((i % B) == B - 1)
                {
                    const double rmsB = std::sqrt (blkPow / B);
                    if (rmsB > 1e-6) worst = std::max (worst, blkStep / (rmsB * slope1));
                    blkStep = 0.0; blkPow = 0.0;
                }
            }
            char d[192];
            std::snprintf (d, sizeof d, "%-11s worst step vs the local tone's own slope = %.2fx", k.nm, worst);
            gate ((std::string (k.nm) + " sweeps without a click").c_str(), worst < 8.0, d);
        }
    }
    {
        // The pills are DISCRETE and are the loudest click risk on the card —
        // a polarity flip is a full-scale step. They are smoothed as one 2x2
        // matrix, so this gate covers every combination at once.
        struct B { const char* nm; bool P::* fld; };
        const B bs[] = { { "Flip L", &P::flipL }, { "Flip R", &P::flipR },
                         { "Trade",  &P::trade }, { "Sum",    &P::sum   },
                         { "DC",     &P::dc    }, { "Dim",    &P::dim   } };
        // 🚨 fb445 / fb421 — THIS GATE COULD NOT FAIL, AND A MUTATION PROVED IT.
        // Deleting the matrix smoothing entirely — switching polarity, Trade,
        // Sum and Wiring as hard branches, the loudest click this device can
        // make — turned ZERO gates red. The reason was the probe: it toggled at
        // i = 0.4 * 48000 = 19200, which at 220 Hz is EXACTLY 88 whole cycles,
        // so the tone was sitting on a zero crossing and inverting it produced
        // no step at all. A gate that toggles at one arbitrary instant is
        // testing one arbitrary phase. Sweep the toggle across a whole cycle and
        // take the WORST phase — and tighten the bar, because 40x was loose
        // enough to let a real full-scale flip through.
        const double refStep = BUS * 2.0 * M_PI * 220.0 / FS;   // level is constant here
        const int    PER     = (int) (FS / 220.0);              // one period
        for (const auto& b : bs)
        {
            double worst = 0.0; int worstPh = 0;
            for (int ph = 0; ph < 8; ++ph)
            {
                E e; e.prepare (FS);
                P p = base(); e.setParams (p);
                float l = 0.0f, r = 0.0f, pl = 0.0f, pr = 0.0f;
                const int N = (int) (1.2 * FS);
                const int t0 = (int) (0.4 * FS) + ph * PER / 8;
                const int t1 = (int) (0.8 * FS) + ph * PER / 8;
                for (int i = 0; i < N; ++i)
                {
                    if (i == t0) { p.*(b.fld) = true;  e.setParams (p); }
                    if (i == t1) { p.*(b.fld) = false; e.setParams (p); }
                    const float s = BUS * (float) std::sin (2.0 * M_PI * 220.0 * i / FS);
                    e.processStereo (s, s, l, r);
                    if (i > (int) (0.05 * FS))
                    {
                        const double st = std::max (std::fabs ((double) l - pl), std::fabs ((double) r - pr));
                        if (st > worst) { worst = st; worstPh = ph; }
                    }
                    pl = l; pr = r;
                }
            }
            char d[224];
            std::snprintf (d, sizeof d, "pill %-7s worst over 8 toggle phases = %.2fx the tone's own (phase %d/8)",
                           b.nm, worst / refStep, worstPh);
            gate ((std::string ("pill ") + b.nm + " toggles without a click").c_str(), worst < refStep * 6.0, d);
        }
        for (int w = 1; w < E::kNumWirings; ++w)
        {
            double worst = 0.0;
            for (int ph = 0; ph < 8; ++ph)
            {
                E e; e.prepare (FS);
                P p = base(); e.setParams (p);
                float l = 0.0f, r = 0.0f, pl = 0.0f, pr = 0.0f;
                const int N = (int) (1.0 * FS);
                const int t0 = (int) (0.4 * FS) + ph * PER / 8;
                for (int i = 0; i < N; ++i)
                {
                    if (i == t0) { p.wiring = w; e.setParams (p); }
                    const float s = BUS * (float) std::sin (2.0 * M_PI * 220.0 * i / FS);
                    e.processStereo (s, 0.6f * s, l, r);
                    if (i > (int) (0.05 * FS))
                        worst = std::max (worst, std::max (std::fabs ((double) l - pl),
                                                            std::fabs ((double) r - pr)));
                    pl = l; pr = r;
                }
            }
            char d[224];
            std::snprintf (d, sizeof d, "Through -> %-11s worst over 8 toggle phases = %.2fx the tone's own",
                           E::wiringNames()[w], worst / refStep);
            gate ((std::string ("Wiring change to '") + E::wiringNames()[w] + "' does not click").c_str(),
                  worst < refStep * 6.0, d);
        }
    }

    {
        // 🚨 fb445 / fb421 — THE SECTION CROSSFADES WERE NOT GATED. Deleting all
        // four of them (guard, DC, Mono Below, Bleed switch instead of fade)
        // turned ZERO gates red, because §I only swept CONTINUOUS knobs and only
        // toggled PILLS — and the sections that come in and out when a knob
        // LEAVES ZERO were covered by neither. A knob crossing zero is a switch
        // wearing a knob's clothes.
        // (`Strain` cannot go in the sweep list above: at the top of its travel
        //  it makes a square wave, whose honest edges dwarf a sine's slope. So
        //  it is toggled 0 <-> 0.2, where the guard is engaged but the waveform
        //  is still a bent sine.)
        struct S { const char* nm; float P::* fld; float on; };
        const S ss[] = { { "Strain",     &P::strain,    0.20f },
                         { "Mono Below", &P::monoBelow, 0.60f },
                         { "Bleed",      &P::bleed,     0.80f } };
        const double refStep = BUS * 2.0 * M_PI * 220.0 / FS;
        const int    PER     = (int) (FS / 220.0);
        for (const auto& sec : ss)
        {
            double worst = 0.0;
            for (int ph = 0; ph < 8; ++ph)
            {
                E e; e.prepare (FS);
                P p = base(); e.setParams (p);
                float l = 0.0f, r = 0.0f, pl = 0.0f, pr = 0.0f;
                const int N = (int) (1.2 * FS);
                const int t0 = (int) (0.4 * FS) + ph * PER / 8;
                const int t1 = (int) (0.8 * FS) + ph * PER / 8;
                for (int i = 0; i < N; ++i)
                {
                    if (i == t0) { p.*(sec.fld) = sec.on;  e.setParams (p); }
                    if (i == t1) { p.*(sec.fld) = 0.0f;    e.setParams (p); }
                    const double t = 2.0 * M_PI * 220.0 * i / FS;
                    e.processStereo (BUS * (float) std::sin (t),
                                     BUS * (float) std::sin (t + 1.2), l, r);
                    if (i > (int) (0.05 * FS))
                        worst = std::max (worst, std::max (std::fabs ((double) l - pl),
                                                            std::fabs ((double) r - pr)));
                    pl = l; pr = r;
                }
            }
            char d[224];
            std::snprintf (d, sizeof d, "%-11s 0 <-> on, worst over 8 toggle phases = %.2fx the tone's own",
                           sec.nm, worst / refStep);
            gate ((std::string (sec.nm) + " comes in and out of circuit without a click").c_str(),
                  worst < refStep * 6.0, d);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§J — SAMPLE RATE. Prove it at 44.1 and 96, not just at 48.\n");
    for (double sr : { 44100.0, 96000.0 })
    {
        {
            E e; e.prepare (sr);
            auto p = base(); p.gain = 0.9f; e.setParams (p);
            float l, r; double sum = 0.0, ref = 0.0; int n = 0;
            for (int i = 0; i < (int) (0.6 * sr); ++i)
            {
                const float s = BUS * (float) std::sin (2.0 * M_PI * 440.0 * i / sr);
                e.processStereo (s, s, l, r);
                if (i > (int) (0.3 * sr)) { sum += (double) l * l; ref += (double) s * s; ++n; }
            }
            const double meas = db (std::sqrt (sum / n) / std::sqrt (ref / n));
            char d[192];
            std::snprintf (d, sizeof d, "%.1f kHz: Gain 0.9 measures %+.3f dB, reference %+.3f dB",
                           sr / 1000.0, meas, refFaderDb (0.9f));
            gate ("Gain is rate-independent", std::fabs (meas - refFaderDb (0.9f)) < 0.05, d);
        }
        {
            // an 80 Hz pair in quadrature: with Mono Below well above it, the two
            // channels must converge. Time-domain, so it needs no FFT bin math.
            E e; e.prepare (sr);
            auto p = base(); p.monoBelow = 0.75f; p.slope = 1.0f; e.setParams (p);
            float l, r; double dif = 0.0, lvl = 0.0; int n = 0;
            for (int i = 0; i < (int) (1.0 * sr); ++i)
            {
                const double t = 2.0 * M_PI * 80.0 * i / sr;
                e.processStereo (BUS * (float) std::sin (t), BUS * (float) std::cos (t), l, r);
                if (i > (int) (0.6 * sr)) { dif += std::pow (l - r, 2.0); lvl += (double) l * l; ++n; }
            }
            const double sep = db (std::sqrt (dif / n) / std::sqrt (std::max (1e-30, lvl / n)));
            char d[192];
            std::snprintf (d, sizeof d, "%.1f kHz: 80 Hz quadrature pair, |L-R| sits %.1f dB below the level", sr / 1000.0, sep);
            gate ("Mono Below folds the bass at this rate too", sep < -30.0, d);
        }
        {
            bool bad = false;
            for (int t = 0; t < E::kNumTypes; ++t)
                for (int c = 0; c < E::kNumChars; ++c)
                {
                    E e; e.prepare (sr);
                    auto p = base(); p.type = t; p.chr = c; p.strain = 0.9f; p.image = 0.95f;
                    p.monoBelow = 0.5f; p.bleed = 0.5f; p.twist = 0.9f; p.dc = true; p.gain = 0.95f;
                    e.setParams (p);
                    float l, r;
                    for (int i = 0; i < (int) (0.25 * sr); ++i)
                    {
                        const float s = 0.7f * (float) std::sin (2.0 * M_PI * 130.0 * i / sr);
                        e.processStereo (s, -0.8f * s, l, r);
                        if (! std::isfinite (l) || ! std::isfinite (r) || std::fabs (l) > 8.0f) bad = true;
                    }
                }
            char d[192];
            std::snprintf (d, sizeof d, "%.1f kHz: %d Types x %d Characters, everything slammed", sr / 1000.0,
                           E::kNumTypes, E::kNumChars);
            gate ("no NaN, no runaway, on any Type x Character", ! bad, d);
        }
    }

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
