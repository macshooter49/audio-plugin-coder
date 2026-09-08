// ─────────────────────────────────────────────────────────────────────────────
// spl_cert — the certification harness for the BAND SPLITTER (kind 14).
//   clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/spl_cert.cpp -o spl_cert
//
// THE ORDER OF THESE GATES IS THE POINT.
//   §A runs FIRST and it is PERFECT RECONSTRUCTION. A splitter that combs when
//   its lanes are put back together is useless no matter how good the UI is, and
//   a comb passes every level gate, every click gate and every "the spectrum
//   changed" gate. So the very first numbers this harness prints are:
//     (a) sum(lanes) − the phase-matched dry, as a null, for every Type × Slope,
//     (b) |sum(lanes)| against the RAW input, as magnitude flatness — the
//         INDEPENDENT truth, because (a) alone could in principle be satisfied by
//         two paths that are wrong in the same way.
//   Everything after §A is only worth reading if §A is green.
//
//   §B is the same question asked where it actually bites: at the crossovers,
//   with the lanes at unity AND at partial gains, and across the Mix travel.
//
// fb441 — CERT MUST SEED BEFORE IT MEASURES. A fresh engine snaps to its first
// block's parameters and hides every steady-state bug. Every measurement below
// runs the engine on real signal for a seed interval FIRST. The transfer-function
// measurements then run SILENCE until the (denormal-flushed) state is exactly
// zero and only then fire the impulse — so the parameter smoothers are settled
// AND the impulse response is exact. That is both halves of the law, not one.
//
// fb425 — SWEEP THE MATRIX, DO NOT SAMPLE IT. Types × Slopes is 8 × 4 = 32
// cells and it is finite, so §A visits all 32. The Bode arc lost three rounds to
// gates that pinned one knob at one value and hid a 14 dB defect.
//
// fb417 — GEOMETRY IS NOT HEARING, and §F's corollary: a GROSS-ENERGY metric is
// blind to allpasses, to width and to pan. Every knob-travel gate below measures
// the MID and SIDE magnitude spectra of decorrelated stereo noise.
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainSplitterFx.h"
#include <cstdio>
#include <cstring>
#include <complex>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

static int gPass = 0, gFail = 0;
static void gate (const char* what, bool ok, const std::string& d)
{
    if (ok) { ++gPass; std::printf ("  ok    %-58s %s\n", what, d.c_str()); }
    else    { ++gFail; std::printf ("  FAIL  %-58s %s\n", what, d.c_str()); }
}

// ── a small radix-2 FFT ──────────────────────────────────────────────────────
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

using Spl = tw::TerrainSplitterFx;
using P   = Spl::Params;

static constexpr int   NFFT = 32768;
static constexpr float FS   = 48000.0f;
static constexpr float BUS  = 0.05f;     // the rack bus sits near −26 dBFS

static double db (double x) { return 20.0 * std::log10 (std::max (1e-12, x)); }

// Two INDEPENDENT streams — the Side channel must not be silent, or every width
// and pan gate below measures nothing (fb417: the probe has to contain the thing
// the knob acts on).
struct Noise
{
    uint32_t a = 2463534242u, b = 1234567891u;
    void reset() noexcept { a = 2463534242u; b = 1234567891u; }
    inline void next (float& l, float& r) noexcept
    {
        a ^= a << 13; a ^= a >> 17; a ^= a << 5;
        b ^= b << 13; b ^= b >> 17; b ^= b << 5;
        l = BUS * (((float) (a & 0xFFFFu) / 32768.0f) - 1.0f);
        r = BUS * (((float) (b & 0xFFFFu) / 32768.0f) - 1.0f);
    }
};

static P base()
{
    P p;                                  // engine defaults are the unity trims
    p.type = Spl::kLowMidHigh; p.slope = 2; p.mix = 1.0f;
    p.split = 0.5f; p.balance = 0.5f; p.spread = 0.5f; p.span = 0.4f;
    return p;
}

// fb441 — run REAL SIGNAL first (the smoothers must settle on something), then
// run silence until every denormal-flushed state is exactly zero.
static void seedThenQuiet (Spl& e, float fs, double sigSec = 0.40, double quietSec = 0.60)
{
    Noise n; float a, b, l, r;
    for (int i = 0; i < (int) (sigSec * fs); ++i)   { n.next (l, r); e.processStereo (l, r, a, b); }
    for (int i = 0; i < (int) (quietSec * fs); ++i) { e.processStereo (0.0f, 0.0f, a, b); }
}

struct IR { std::vector<double> l, r; };

/** Impulse response of the whole device (split → trims → merge). Correlated
 *  impulse on both channels, which is the honest probe for the frequency Types
 *  AND for Mid/Side (a correlated impulse puts nothing in the Side lane). */
static IR impulse (Spl& e, float fs, float amp = 1.0f)
{
    seedThenQuiet (e, fs);
    IR ir; ir.l.resize (NFFT); ir.r.resize (NFFT);
    float a, b;
    for (int i = 0; i < NFFT; ++i)
    {
        const float x = (i == 0) ? amp : 0.0f;
        e.processStereo (x, x, a, b);
        ir.l[(size_t) i] = a; ir.r[(size_t) i] = b;
    }
    return ir;
}

/** Impulse response of the RAW SPLIT — sum of the lanes with no trim path at
 *  all, plus the engine's own phase-matched dry, captured sample by sample. */
struct SplitIR { std::vector<double> sum, dry; };
static SplitIR impulseSplit (Spl& e, float fs)
{
    seedThenQuiet (e, fs);
    SplitIR ir; ir.sum.resize (NFFT); ir.dry.resize (NFFT);
    float LL[Spl::kMaxLanes], RR[Spl::kMaxLanes], o1, o2;
    const int N = e.laneCount();
    for (int i = 0; i < NFFT; ++i)
    {
        const float x = (i == 0) ? 1.0f : 0.0f;
        e.splitStereo (x, x, LL, RR);
        double s = 0.0; for (int k = 0; k < N; ++k) s += LL[k];
        ir.sum[(size_t) i] = s;
        ir.dry[(size_t) i] = e.dryAlignedL();
        e.mergeStereo (LL, RR, o1, o2);          // the matched pair, always
    }
    return ir;
}

static std::vector<double> magDb (const std::vector<double>& x)
{
    std::vector<std::complex<double>> buf ((size_t) NFFT);
    for (int i = 0; i < NFFT; ++i) buf[(size_t) i] = x[(size_t) i];
    fft (buf);
    std::vector<double> m ((size_t) (NFFT / 2));
    for (int i = 0; i < NFFT / 2; ++i) m[(size_t) i] = db (std::abs (buf[(size_t) i]));
    return m;
}
static constexpr double kBin = (double) FS / NFFT;
static int binOf (double hz) { return (int) std::lround (hz / kBin); }

/** Worst deviation from a ±1/3-octave running MEDIAN. A comb notch is narrow
 *  and deep so it leaves the median; a lane-gain shelf is smooth so it does not.
 *  This is what lets the comb gate run with the lanes at PARTIAL gains, which is
 *  the setting a real user is in and the one a naive flatness gate cannot test. */
static double worstLocalDev (const std::vector<double>& m, double f0, double f1, double* atHz)
{
    double worst = 0.0; if (atHz) *atHz = 0.0;
    for (double f = f0; f <= f1; f *= 1.0139)                       // ~1/50 octave
    {
        const int c  = binOf (f);
        int lo = binOf (f / 1.259921), hi = binOf (f * 1.259921);   // ±1/3 octave
        if (hi - lo < 8) { lo = c - 4; hi = c + 4; }
        lo = std::max (1, lo); hi = std::min ((int) m.size() - 1, hi);
        if (hi <= lo || c <= lo || c >= hi) continue;
        std::vector<double> w (m.begin() + lo, m.begin() + hi);
        std::nth_element (w.begin(), w.begin() + w.size() / 2, w.end());
        const double dev = std::fabs (m[(size_t) c] - w[w.size() / 2]);
        if (dev > worst) { worst = dev; if (atHz) *atHz = f; }
    }
    return worst;
}

/** The §A null: drive live decorrelated noise, sum the lanes with NO lane
 *  processing, and null against the engine's own phase-matched dry. */
static double reconNullDb (Spl& e, float fs)
{
    Noise n; n.reset();
    float LL[Spl::kMaxLanes], RR[Spl::kMaxLanes], o1, o2, l, r;
    for (int i = 0; i < (int) (0.40 * fs); ++i)      // fb441 — seed on real signal
    { n.next (l, r); e.splitStereo (l, r, LL, RR); e.mergeStereo (LL, RR, o1, o2); }

    const int N = e.laneCount();
    double err = 0.0, sig = 0.0;
    for (int i = 0; i < (int) (2.0 * fs); ++i)
    {
        n.next (l, r);
        e.splitStereo (l, r, LL, RR);
        double sL = 0.0, sR = 0.0;
        for (int k = 0; k < N; ++k) { sL += LL[k]; sR += RR[k]; }
        const double dL = e.dryAlignedL(), dR = e.dryAlignedR();
        e.mergeStereo (LL, RR, o1, o2);              // the matched pair, always
        err += (sL - dL) * (sL - dL) + (sR - dR) * (sR - dR);
        sig += dL * dL + dR * dR;
    }
    return 10.0 * std::log10 (err / std::max (1e-30, sig) + 1e-30);
}

/** Same probe as reconNullDb, but through the WHOLE device — split, the trim
 *  chain, merge. At default trims that chain must be the EXACT identity, and
 *  nothing else in this harness can see it: §A(a) stops before the trims and §B
 *  drives a CORRELATED impulse, which has no Side component for a Width offset
 *  to act on. (Mutation M11 — a Width that is 0.05 off unity — turned zero gates
 *  red until this existed.) */
static double trimIdentityDb (Spl& e, float fs)
{
    Noise n; n.reset();
    float LL[Spl::kMaxLanes], RR[Spl::kMaxLanes], o1, o2, l, r;
    for (int i = 0; i < (int) (0.40 * fs); ++i)
    { n.next (l, r); e.splitStereo (l, r, LL, RR); e.mergeStereo (LL, RR, o1, o2); }
    double err = 0.0, sig = 0.0;
    for (int i = 0; i < (int) (1.5 * fs); ++i)
    {
        n.next (l, r);
        e.splitStereo (l, r, LL, RR);
        const double dL = e.dryAlignedL(), dR = e.dryAlignedR();
        e.mergeStereo (LL, RR, o1, o2);
        err += (o1 - dL) * (o1 - dL) + (o2 - dR) * (o2 - dR);
        sig += dL * dL + dR * dR;
    }
    return 10.0 * std::log10 (err / std::max (1e-30, sig) + 1e-30);
}

static double flatnessDb (const std::vector<double>& m, double f0, double f1, double* atHz = nullptr)
{
    double worst = 0.0; if (atHz) *atHz = 0.0;
    for (int i = binOf (f0); i <= binOf (f1) && i < (int) m.size(); ++i)
        if (std::fabs (m[(size_t) i]) > worst) { worst = std::fabs (m[(size_t) i]); if (atHz) *atHz = i * kBin; }
    return worst;
}

int main()
{
    std::printf ("\n══ spl_cert — BAND SPLITTER (kind 14) ══\n");
    const char* const* TN = Spl::typeNames();
    const char* const* SN = Spl::slopeNames();

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§A — PERFECT RECONSTRUCTION. Runs first. If this is red nothing else matters.\n");
    std::printf ("     sum(lanes), NO lane processing, nulled against the phase-matched dry.\n");
    for (int t = 0; t < Spl::kNumTypes; ++t)
    {
        double worst = -999.0; int worstS = 0;
        for (int s = 0; s < Spl::kNumSlopes; ++s)
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = t; p.slope = s; e.setParams (p);
            const double n = reconNullDb (e, FS);
            if (n > worst) { worst = n; worstS = s; }
            char d[200];
            std::snprintf (d, sizeof d, "%-22s slope %-5s  %d lanes  ->  %8.2f dB",
                           TN[t], SN[s], Spl::laneCountFor (t), n);
            char nm[96]; std::snprintf (nm, sizeof nm, "Type %d x %s recombines (bar -100 dB)", t, SN[s]);
            gate (nm, n < -100.0, d);
        }
        (void) worst; (void) worstS;
    }
    {
        // The INDEPENDENT truth. (a) above compares the lane sum to the engine's
        // own dry; both are computed inside the same engine. This one compares
        // |sum(lanes)| to the RAW INPUT — an impulse is flat 0 dB by definition,
        // so a device that dropped a band, or ran two wrong paths that happened
        // to agree, cannot survive it.
        for (int t = 0; t < Spl::kNumReal; ++t)
        {
            double worst = 0.0; int worstS = 0; double atHz = 0.0, wHz = 0.0;
            for (int s = 0; s < Spl::kNumSlopes; ++s)
            {
                Spl e; e.prepare (FS);
                auto p = base(); p.type = t; p.slope = s; e.setParams (p);
                auto ir = impulseSplit (e, FS);
                const double f = flatnessDb (magDb (ir.sum), 20.0, 18000.0, &atHz);
                if (f > worst) { worst = f; worstS = s; wHz = atHz; }
            }
            char d[200];
            std::snprintf (d, sizeof d, "%-22s worst %.4f dB at %.0f Hz (slope %s)",
                           TN[t], worst, wHz, SN[worstS]);
            char nm[96]; std::snprintf (nm, sizeof nm, "... and |sum(lanes)| is FLAT vs the raw input (Type %d)", t);
            gate (nm, worst < 0.1, d);
        }
    }

    {
        // THE TRIM CHAIN MUST BE THE EXACT IDENTITY AT ITS DEFAULTS, on
        // DECORRELATED stereo. Every default is chosen for this: Pan centre is
        // equal-power scaled to UNITY (not −3 dB), Width 100 % is m ± 1·s, the
        // fader's centre detent is exactly 0 dB, Balance 0.5 contributes exactly
        // 0 dB and the mute/solo gate rests at exactly 1.0.
        double worst = -999.0; int wT = 0, wS = 0;
        for (int t = 0; t < Spl::kNumTypes; ++t)
        for (int s = 0; s < Spl::kNumSlopes; ++s)
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = t; p.slope = s; e.setParams (p);
            const double n = trimIdentityDb (e, FS);
            if (n > worst) { worst = n; wT = t; wS = s; }
        }
        char d[220]; std::snprintf (d, sizeof d, "32 cells: worst merged-output vs phase-matched dry = %.2f dB (%s / %s)",
                                    worst, TN[wT], SN[wS]);
        gate ("the default TRIM CHAIN is the exact identity (Gain·Pan·Width·Flip)", worst < -100.0, d);
    }
    {
        // fb373 — A RESERVED SLOT MUST NOT BE A DIFFERENT MACHINE. The cert can
        // reach Type 5-7 and a host's automation can too. `resolveType` aliases
        // them to the default, and "aliases" has to mean BIT-IDENTICAL, not
        // "also reconstructs" — falling through to a 2-lane split reconstructs
        // perfectly and is still the wrong instrument, which is precisely the
        // failure that stayed green through four rounds of measurement.
        double worst = -999.0;
        for (int t = Spl::kNumReal; t < Spl::kNumTypes; ++t)
        {
            std::vector<double> aL, aR, bL, bR;
            for (int which = 0; which < 2; ++which)
            {
                Spl e; e.prepare (FS);
                auto p = base(); p.type = which ? Spl::kDefaultType : t;
                p.laneGain[0] = 0.3f; p.laneGain[2] = 0.7f; p.spread = 0.8f; e.setParams (p);
                Noise n; n.reset(); float a, b, l, r;
                for (int i = 0; i < (int) (0.4 * FS); ++i) { n.next (l, r); e.processStereo (l, r, a, b); }
                auto& oL = which ? bL : aL; auto& oR = which ? bR : aR;
                oL.resize (20000); oR.resize (20000);
                for (int i = 0; i < 20000; ++i)
                { n.next (l, r); e.processStereo (l, r, a, b); oL[(size_t) i] = a; oR[(size_t) i] = b; }
            }
            double err = 0.0, sig = 0.0;
            for (size_t i = 0; i < aL.size(); ++i)
            { err += (aL[i]-bL[i])*(aL[i]-bL[i]) + (aR[i]-bR[i])*(aR[i]-bR[i]);
              sig += bL[i]*bL[i] + bR[i]*bR[i]; }
            worst = std::max (worst, 10.0 * std::log10 (err / std::max (1e-30, sig) + 1e-30));
        }
        char d[200]; std::snprintf (d, sizeof d, "Types 5-7 vs the default Type: worst residual %.1f dB (bar: bit-identical)", worst);
        gate ("reserved Type slots are BIT-IDENTICAL to the default (fb373)", worst < -290.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§B — NO COMB AT THE CROSSOVERS. At unity, at PARTIAL gains, and across Mix.\n");
    for (int t = 0; t < Spl::kNumReal; ++t)
    {
        const int N = Spl::laneCountFor (t);
        if (t >= Spl::kMidSide) continue;                    // no crossovers to comb
        // Wide bands, so the log grid has room either side of each crossover.
        for (int s = 0; s < Spl::kNumSlopes; ++s)
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = t; p.slope = s; p.span = 0.6f; e.setParams (p);
            auto ir = impulse (e, FS);
            const auto m = magDb (ir.l);
            const float f0 = Spl::splitHzFor (t, p.split);
            const float r  = Spl::spanRatioFor (t, f0, p.span);
            std::string at;
            char buf[64]; float f = f0;
            for (int k = 0; k < N - 1; ++k)
            { std::snprintf (buf, sizeof buf, "%s%.0f Hz %+0.3f dB", k ? " · " : "", f, m[(size_t) binOf (f)]);
              at += buf; f *= r; }
            const double fl = flatnessDb (m, 20.0, 18000.0);
            char d[240]; std::snprintf (d, sizeof d, "%-16s %-5s  flat %.4f dB · %s", TN[t], SN[s], fl, at.c_str());
            char nm[100]; std::snprintf (nm, sizeof nm, "Type %d x %s: unity lanes, no notch at any crossover", t, SN[s]);
            gate (nm, fl < 0.1, d);
        }
    }
    {
        // PARTIAL GAINS — the setting a real user is in. A flatness gate cannot
        // test this (the response is a legitimate multi-shelf), so the metric is
        // the deviation from a ±1/3-octave running median: a comb notch leaves
        // the median, a shelf does not.
        for (int t = 0; t < Spl::kMidSide; ++t)
        for (int s = 0; s < Spl::kNumSlopes; ++s)
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = t; p.slope = s; p.span = 0.6f;
            p.laneGain[0] = 0.62f; p.laneGain[1] = 0.38f;
            p.laneGain[2] = 0.70f; p.laneGain[3] = 0.44f;
            e.setParams (p);
            auto ir = impulse (e, FS);
            double atHz = 0.0;
            const double dev = worstLocalDev (magDb (ir.l), 40.0, 15000.0, &atHz);
            char d[220]; std::snprintf (d, sizeof d, "%-16s %-5s  worst local dip/peak %.3f dB at %.0f Hz",
                                        TN[t], SN[s], dev, atHz);
            char nm[110]; std::snprintf (nm, sizeof nm, "Type %d x %s: PARTIAL lane gains still do not comb", t, SN[s]);
            gate (nm, dev < 1.0, d);
        }
    }
    {
        // THE MIX LAW IN ONE NUMBER. Wet and dry are phase-matched, so at default
        // trims the output must be INVARIANT across the whole Mix travel. A
        // device that blends un-rotated dry against rotated wet notches at every
        // crossover; this gate is 180 degrees away from surviving that.
        for (int t = 0; t < Spl::kNumReal; ++t)
        {
            Spl ref; ref.prepare (FS);
            auto p = base(); p.type = t; p.mix = 1.0f; ref.setParams (p);
            const auto m1 = magDb (impulse (ref, FS).l);
            double worst = 0.0; float worstMix = 0.0;
            for (float mx : { 0.0f, 0.25f, 0.5f, 0.75f })
            {
                Spl e; e.prepare (FS);
                auto q = base(); q.type = t; q.mix = mx; e.setParams (q);
                const auto m2 = magDb (impulse (e, FS).l);
                for (int i = binOf (25.0); i <= binOf (18000.0); ++i)
                { const double dd = std::fabs (m1[(size_t) i] - m2[(size_t) i]);
                  if (dd > worst) { worst = dd; worstMix = mx; } }
            }
            char d[200]; std::snprintf (d, sizeof d, "%-22s worst |ΔdB| across Mix 0/25/50/75/100 = %.5f dB (at Mix %.2f)",
                                        TN[t], worst, worstMix);
            char nm[110]; std::snprintf (nm, sizeof nm, "Type %d: output is INVARIANT across Mix at unity trims", t);
            gate (nm, worst < 0.01, d);
        }
    }
    {
        // ... and Mix at a partial position with partial gains still cannot comb.
        for (int t = 0; t < Spl::kMidSide; ++t)
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = t; p.span = 0.6f; p.mix = 0.5f;
            p.laneGain[0] = 0.20f; p.laneGain[1] = 0.80f;
            p.laneGain[2] = 0.30f; p.laneGain[3] = 0.65f;
            e.setParams (p);
            double atHz = 0.0;
            const double dev = worstLocalDev (magDb (impulse (e, FS).l), 40.0, 15000.0, &atHz);
            char d[200]; std::snprintf (d, sizeof d, "%-22s Mix 50%% + hard lane gains: worst %.3f dB at %.0f Hz",
                                        TN[t], dev, atHz);
            char nm[100]; std::snprintf (nm, sizeof nm, "Type %d: Mix 50%% with hard trims does not comb", t);
            gate (nm, dev < 1.0, d);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§C — LANE ISOLATION, and the SLOPE dropdown is PHYSICS.\n");
    for (int t = 0; t < Spl::kMidSide; ++t)
    {
        const int N = Spl::laneCountFor (t);
        // 24 and 48 dB/oct only. 6 and 12 dB/oct DO NOT isolate to 40 dB two
        // octaves out and are not supposed to — that IS the point of a Slope
        // dropdown, and it is gated as a slope law two blocks below instead of
        // being quietly excused here.
        for (int s = 2; s < Spl::kNumSlopes; ++s)
        {
            auto p = base(); p.type = t; p.slope = s; p.span = 0.6f;
            const float f0 = Spl::splitHzFor (t, p.split);
            const float r  = Spl::spanRatioFor (t, f0, p.span);
            float fx[3] = { 0, 0, 0 }; { float f = f0; for (int k = 0; k < N - 1; ++k) { fx[k] = f; f *= r; } }

            double worst = 999.0; int worstLane = 0;
            for (int k = 0; k < N; ++k)
            {
                Spl e; e.prepare (FS);
                auto q = p; q.laneSolo[k] = true; e.setParams (q);
                const auto m = magDb (impulse (e, FS).l);
                const double lo = (k == 0)     ? 0.0 : (double) fx[k - 1];
                const double hi = (k == N - 1) ? 0.0 : (double) fx[k];
                const double inHz = (k == 0)     ? fx[0] * 0.25
                                  : (k == N - 1) ? fx[N - 2] * 4.0 : std::sqrt (lo * hi);
                const double inDb = m[(size_t) binOf (inHz)];
                if (k > 0)     { const double d2 = inDb - m[(size_t) binOf (lo * 0.25)];
                                 if (d2 < worst) { worst = d2; worstLane = k; } }
                if (k < N - 1) { const double d2 = inDb - m[(size_t) binOf (hi * 4.0)];
                                 if (d2 < worst) { worst = d2; worstLane = k; } }
            }
            char d[220]; std::snprintf (d, sizeof d,
                "%-22s %-5s  worst leakage 2 oct out of band = %.1f dB down (lane %d)",
                TN[t], SN[s], worst, worstLane);
            char nm[110]; std::snprintf (nm, sizeof nm, "Type %d x %s: each lane is >40 dB down in the others", t, SN[s]);
            gate (nm, worst > 40.0, d);
        }
    }
    {
        // THE SLOPE LAW. Solo the bottom lane and read the level two octaves
        // above its crossover: 6/12/24/48 dB/oct must land near −12/−24/−48/−96.
        // A dropdown whose entries measure the same is a dropdown that is not
        // changing physics (fb345), and a "types are night and day" gate that
        // never checks the number is a gate that cannot fail.
        double lvl[Spl::kNumSlopes];
        for (int s = 0; s < Spl::kNumSlopes; ++s)
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = Spl::kLowMidHigh; p.slope = s; p.span = 0.6f;
            p.laneSolo[0] = true; e.setParams (p);
            const auto m = magDb (impulse (e, FS).l);
            const float f0 = Spl::splitHzFor (p.type, p.split);
            lvl[s] = m[(size_t) binOf (f0 * 4.0)] - m[(size_t) binOf (f0 * 0.25)];
        }
        char d[220]; std::snprintf (d, sizeof d, "2 oct above fc:  6dB %.1f · 12dB %.1f · 24dB %.1f · 48dB %.1f",
                                    lvl[0], lvl[1], lvl[2], lvl[3]);
        gate ("Slope really is 6/12/24/48 dB per octave (each ≥8 dB steeper)",
              lvl[1] < lvl[0] - 8.0 && lvl[2] < lvl[1] - 8.0 && lvl[3] < lvl[2] - 8.0, d);
    }
    {
        // The two matrix Types, where "isolation" is exact rather than filtered.
        Spl e; e.prepare (FS);
        auto p = base(); p.type = Spl::kMidSide; e.setParams (p);
        float LL[Spl::kMaxLanes], RR[Spl::kMaxLanes], o1, o2;
        Noise n; float l, r;
        for (int i = 0; i < (int) (0.3 * FS); ++i)
        { n.next (l, r); e.splitStereo (l, r, LL, RR); e.mergeStereo (LL, RR, o1, o2); }
        double sE = 0.0, mE = 0.0, sE2 = 0.0, mE2 = 0.0;
        for (int i = 0; i < (int) (0.5 * FS); ++i)
        {
            n.next (l, r);
            e.splitStereo (l, l, LL, RR);                    // CORRELATED
            mE += (double) LL[0] * LL[0]; sE += (double) LL[1] * LL[1];
            e.mergeStereo (LL, RR, o1, o2);
            e.splitStereo (l, -l, LL, RR);                   // ANTI-correlated
            mE2 += (double) LL[0] * LL[0]; sE2 += (double) LL[1] * LL[1];
            e.mergeStereo (LL, RR, o1, o2);
        }
        char d[200]; std::snprintf (d, sizeof d, "mono probe: Side %.1f dB below Mid  ·  antiphase probe: Mid %.1f dB below Side",
                                    10.0 * std::log10 (sE / std::max (1e-30, mE) + 1e-30),
                                    10.0 * std::log10 (mE2 / std::max (1e-30, sE2) + 1e-30));
        gate ("Mid/Side: a mono probe leaves the Side lane digitally silent",
              sE / std::max (1e-30, mE) < 1e-20 && mE2 / std::max (1e-30, sE2) < 1e-20, d);
    }
    {
        Spl e; e.prepare (FS);
        auto p = base(); p.type = Spl::kLeftRight; e.setParams (p);
        float LL[Spl::kMaxLanes], RR[Spl::kMaxLanes], o1, o2;
        Noise n; float l, r; double leak = 0.0, want = 0.0;
        for (int i = 0; i < (int) (0.3 * FS); ++i)
        { n.next (l, r); e.splitStereo (l, r, LL, RR); e.mergeStereo (LL, RR, o1, o2); }
        for (int i = 0; i < (int) (0.5 * FS); ++i)
        {
            n.next (l, r);
            e.splitStereo (l, 0.0f, LL, RR);                 // LEFT ONLY
            want += (double) LL[0] * LL[0];
            leak += (double) LL[1] * LL[1] + (double) RR[1] * RR[1];
            e.mergeStereo (LL, RR, o1, o2);
        }
        char d[200]; std::snprintf (d, sizeof d, "left-only probe: lane 2 carries %.1f dB relative to lane 1",
                                    10.0 * std::log10 (leak / std::max (1e-30, want) + 1e-30));
        gate ("Left/Right: a left-only probe puts EXACTLY nothing in lane 2", leak == 0.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§D — MUTE and SOLO behave EXACTLY. Solo overrides, multiple solos sum.\n");
    {
        auto run = [] (const P& p, std::vector<double>& oL, std::vector<double>& oR)
        {
            Spl e; e.prepare (FS); e.setParams (p);
            Noise n; n.reset(); float a, b, l, r;
            for (int i = 0; i < (int) (0.40 * FS); ++i)      // fb441
            { n.next (l, r); e.processStereo (l, r, a, b); }
            const int M = (int) (0.50 * FS);
            oL.resize ((size_t) M); oR.resize ((size_t) M);
            for (int i = 0; i < M; ++i)
            { n.next (l, r); e.processStereo (l, r, a, b); oL[(size_t) i] = a; oR[(size_t) i] = b; }
        };
        auto nullOf = [] (const std::vector<double>& aL, const std::vector<double>& aR,
                          const std::vector<double>& bL, const std::vector<double>& bR)
        {
            double e = 0.0, s = 0.0;
            for (size_t i = 0; i < aL.size(); ++i)
            { e += (aL[i]-bL[i])*(aL[i]-bL[i]) + (aR[i]-bR[i])*(aR[i]-bR[i]);
              s += aL[i]*aL[i] + aR[i]*aR[i]; }
            return 10.0 * std::log10 (e / std::max (1e-30, s) + 1e-30);
        };
        auto energyDb = [] (const std::vector<double>& l, const std::vector<double>& r)
        { double s = 0.0; for (size_t i = 0; i < l.size(); ++i) s += l[i]*l[i] + r[i]*r[i];
          return 10.0 * std::log10 (s / (double) l.size() + 1e-30); };

        for (int t = 0; t < Spl::kNumReal; ++t)
        {
            const int N = Spl::laneCountFor (t);
            std::vector<double> aL, aR, bL, bR;
            double wMute = -999.0, wSolo = -999.0, wOver = -999.0, wLin = -999.0, minDrop = 999.0;

            for (int k = 0; k < N; ++k)
            {
                auto p = base(); p.type = t;
                // 1. Mute == that lane's gain at true kill.
                auto pm = p; pm.laneMute[k] = true;
                auto pg = p; pg.laneGain[k] = 0.0f;
                std::vector<double> mL, mR;
                run (pm, mL, mR); run (pg, bL, bR);
                wMute = std::max (wMute, nullOf (mL, mR, bL, bR));

                // 2. Solo k == mute everything else.
                auto ps = p; ps.laneSolo[k] = true;
                auto pe = p; for (int j = 0; j < N; ++j) if (j != k) pe.laneMute[j] = true;
                run (ps, aL, aR); run (pe, bL, bR);
                wSolo = std::max (wSolo, nullOf (aL, aR, bL, bR));

                // 1b. ... and MUTE MUST ACTUALLY REMOVE THE LANE.
                // 🔬 THIS GATE WAS REWRITTEN. Its first form asked whether muting
                // dropped the BROADBAND energy by 3 dB, and it went red on a
                // perfectly correct engine: on white noise the Low lane of a
                // 500 Hz Low/High split carries ~2 % of the energy, so removing
                // it moves the total by 0.1 dB. fb417 exactly — a gross-energy
                // metric is the wrong instrument, and the temptation is to drop
                // the bar until it passes, which would have left a no-op mute
                // uncovered. The right question is LINEARITY: at Mix 100 % the
                // merge is a plain sum, so (mute k) + (solo k) must reconstruct
                // the untouched output EXACTLY — and it cannot if mute is a
                // no-op, because then the lane is counted twice.
                std::vector<double> uL, uR, sL2, sR2;
                run (p, uL, uR); run (ps, sL2, sR2);
                std::vector<double> addL (uL.size()), addR (uR.size());
                for (size_t z = 0; z < uL.size(); ++z)
                { addL[z] = mL[z] + sL2[z]; addR[z] = mR[z] + sR2[z]; }
                wLin  = std::max (wLin, nullOf (uL, uR, addL, addR));
                minDrop = std::min (minDrop, energyDb (sL2, sR2) + 100.0);  // lane not empty

                // 3. SOLO OVERRIDES MUTE: a lane that is both muted and soloed
                //    SOUNDS. This is the one everybody gets backwards.
                auto po = ps; po.laneMute[k] = true;
                run (po, aL, aR); run (ps, bL, bR);
                wOver = std::max (wOver, nullOf (aL, aR, bL, bR));
            }
            // 4. Multiple solos SUM.
            double wMulti = -999.0;
            if (N >= 3)
            {
                auto p2 = base(); p2.type = t; p2.laneSolo[0] = p2.laneSolo[2] = true;
                auto pe = base(); pe.type = t;
                for (int j = 0; j < N; ++j) if (j != 0 && j != 2) pe.laneMute[j] = true;
                run (p2, aL, aR); run (pe, bL, bR);
                wMulti = nullOf (aL, aR, bL, bR);
            }
            char ms[24]; if (N >= 3) std::snprintf (ms, sizeof ms, "%.0f", wMulti); else std::snprintf (ms, sizeof ms, "n/a");
            char d[260];
            std::snprintf (d, sizeof d, "%-22s mute %.0f · solo %.0f · solo-over-mute %.0f · 2 solos %s · (mute k)+(solo k)==untouched %.0f dB · quietest lane %.1f dBFS",
                           TN[t], wMute, wSolo, wOver, ms, wLin, minDrop - 100.0);
            char nm[110]; std::snprintf (nm, sizeof nm, "Type %d: mute/solo/override/multi are all exact", t);
            gate (nm, wMute < -100.0 && wSolo < -100.0 && wOver < -100.0
                      && (N < 3 || wMulti < -100.0) && wLin < -100.0 && minDrop > 40.0, d);
        }
        {
            // fb432 — READ THE ENGINE'S OWN RESOLVED NUMBER, not the parameter.
            // A solo that never engaged still measures "different" if the only
            // thing checked is the output.
            Spl e; e.prepare (FS);
            auto p = base(); p.type = Spl::kLowMidHigh;
            p.laneSolo[1] = true; p.laneMute[1] = true; p.laneMute[0] = true;
            e.setParams (p);
            float a, b; Noise n; float l, r;
            for (int i = 0; i < (int) (0.3 * FS); ++i) { n.next (l, r); e.processStereo (l, r, a, b); }
            char d[200]; std::snprintf (d, sizeof d, "resolved gates: lane1 %.3f · lane2 %.3f · lane3 %.3f (want 0 / 1 / 0)",
                                        e.meterLaneGate (0), e.meterLaneGate (1), e.meterLaneGate (2));
            gate ("the engine's OWN gate meter reports the resolved solo state",
                  e.meterLaneGate (0) < 1e-3 && e.meterLaneGate (1) > 0.999 && e.meterLaneGate (2) < 1e-3, d);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§E — CROSSOVER ORDERING, enforced by CONSTRUCTION and not by a clamp.\n");
    {
        bool ok = true; double worstRatio = 1e9, loHz = 1e9, hiHz = 0.0;
        int badT = -1;
        for (int t = 0; t < Spl::kMidSide; ++t)
        {
            const int N = Spl::laneCountFor (t);
            for (int i = 0; i <= 200; ++i)
            for (int j = 0; j <= 100; ++j)
            {
                const float sk = (float) i / 200.0f, pk = (float) j / 100.0f;
                const float f0 = Spl::splitHzFor (t, sk);
                const float r  = Spl::spanRatioFor (t, f0, pk);
                float f = f0, prev = 0.0f;
                for (int k = 0; k < N - 1; ++k)
                {
                    if (k > 0) worstRatio = std::min (worstRatio, (double) (f / prev));
                    if (f <= prev) { ok = false; badT = t; }
                    if (f < 19.9f || f > 10001.0f) { ok = false; badT = t; }
                    loHz = std::min (loHz, (double) f); hiHz = std::max (hiHz, (double) f);
                    prev = f; f *= r;
                }
            }
        }
        char d[220]; std::snprintf (d, sizeof d,
            "3 Types x 201 Split x 101 Span: every crossover in [%.0f, %.0f] Hz, tightest ratio %.3f%s",
            loHz, hiHz, worstRatio, ok ? "" : "  <- INVERTED");
        gate ("dragging Mid below Low is IMPOSSIBLE (ratio never reaches 1.0)",
              ok && worstRatio >= 1.399, d);
        (void) badT;
    }
    {
        // NO CLAMP REPEAT. A range that runs past a clamp buys ordering by making
        // the top of the travel a repeated value — the plateau the house law
        // forbids, and the exact defect mutation testing found twice in the Bode
        // engine. Every adjacent knob step must move the frequency set.
        double worstCents = 1e9; const char* worstKnob = "";
        for (int t = 0; t < Spl::kMidSide; ++t)
        {
            const int N = Spl::laneCountFor (t);
            for (int j = 0; j <= 4; ++j)                     // a few Span positions
            {
                const float pk = (float) j / 4.0f;
                float prev[3] = { 0, 0, 0 };
                for (int i = 0; i <= 200; ++i)
                {
                    const float f0 = Spl::splitHzFor (t, (float) i / 200.0f);
                    const float r  = Spl::spanRatioFor (t, f0, pk);
                    float f = f0;
                    for (int k = 0; k < N - 1; ++k)
                    {
                        // Only the crossover `Split` NAMES is required to move for
                        // every Split step. The upper ones are ratio-derived and
                        // the TOP one sits at the 10 kHz ceiling once Span is at
                        // 100 % — that is the ceiling doing its job, not dead
                        // travel, and the knob still transforms (§F proves it on
                        // the output spectrum, which is the law's actual test).
                        if (i > 0 && k == 0) { const double c = 1200.0 * std::log2 ((double) f / prev[k]);
                                     if (std::fabs (c) < worstCents) { worstCents = std::fabs (c); worstKnob = "Split"; } }
                        prev[k] = f; f *= r;
                    }
                }
            }
            if (N < 3) continue;
            for (int i = 0; i <= 4; ++i)
            {
                const float f0 = Spl::splitHzFor (t, (float) i / 4.0f);
                float prev[3] = { 0, 0, 0 };
                for (int j = 0; j <= 100; ++j)
                {
                    const float r = Spl::spanRatioFor (t, f0, (float) j / 100.0f);
                    float f = f0;
                    for (int k = 0; k < N - 1; ++k)
                    {
                        if (j > 0 && k == 1) { const double c = 1200.0 * std::log2 ((double) f / prev[k]);
                                              if (std::fabs (c) < worstCents) { worstCents = std::fabs (c); worstKnob = "Span"; } }
                        prev[k] = f; f *= r;
                    }
                }
            }
        }
        char d[220]; std::snprintf (d, sizeof d, "smallest adjacent-step move over the whole grid = %.2f cents (on %s)",
                                    worstCents, worstKnob);
        gate ("no knob position is a CLAMP REPEAT (every step moves >1 cent)", worstCents > 1.0, d);
    }
    {
        // The negative control: the design this replaced. Two absolute log knobs
        // with `fHi = max(fHi, fLo * 2)` — the obvious way — and the top of the
        // Split-Hi travel goes DEAD as soon as Split is high. This is what proves
        // the gate above can fail.
        int repeats = 0;
        const float fLo = 4000.0f;                            // Split parked high
        float prev = -1.0f;
        for (int j = 0; j <= 100; ++j)
        {
            const float raw  = 800.0f * std::pow (8000.0f / 800.0f, (float) j / 100.0f);
            const float clamped = std::max (raw, fLo * 2.0f);           // the naive clamp
            if (prev > 0.0f && std::fabs (1200.0f * std::log2 (clamped / prev)) < 1.0f) ++repeats;
            prev = clamped;
        }
        char d[200]; std::snprintf (d, sizeof d, "the NAIVE absolute-knob-plus-clamp design repeats %d of 100 steps at Split 4 kHz", repeats);
        gate ("... and the clamp-repeat gate FAILS on that design (negative control)", repeats > 20, d);
    }
    {
        // The meter must agree with the mapping. A mapping authored in two places
        // is the fb373 defect waiting to happen (a choice normalised on the wrong
        // count stayed bit-identical through four rounds of green measurement).
        double worst = 0.0;
        for (int t = 0; t < Spl::kMidSide; ++t)
        for (float sk : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        for (float pk : { 0.0f, 0.5f, 1.0f })
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = t; p.split = sk; p.span = pk; e.setParams (p);
            float a, b; for (int i = 0; i < (int) (0.3 * FS); ++i) e.processStereo (0.0f, 0.0f, a, b);
            const float f0 = Spl::splitHzFor (t, sk);
            const float r  = Spl::spanRatioFor (t, f0, pk);
            float f = f0;
            for (int k = 0; k < Spl::laneCountFor (t) - 1; ++k)
            { worst = std::max (worst, std::fabs (1200.0 * std::log2 ((double) e.meterXoverHz (k) / f))); f *= r; }
        }
        char d[200]; std::snprintf (d, sizeof d, "worst meter-vs-mapping disagreement = %.4f cents", worst);
        gate ("the engine's crossover METER agrees with the public mapping", worst < 1.0, d);
    }

    {
        // fb350 THE POOL LAW, applied to this engine's own per-block resolve. The
        // crossover coefficients are refreshed on an amortised counter; delete
        // that refresh and EVERY parameter still appears to work, because the
        // first block already set the filters — only params changed AFTER the
        // first block silently stop resolving. That is exactly how a missing
        // `updateCoefficients()` presented as "just one knob is broken".
        // So: move Split mid-stream and require the result to match an engine
        // that started there.
        double worst = 0.0; int wT = 0;
        for (int t = 0; t < Spl::kMidSide; ++t)
        {
            std::vector<double> m[2];
            for (int which = 0; which < 2; ++which)
            {
                Spl e; e.prepare (FS);
                // ⚠️ THE LANE GAINS ARE NOT DECORATION HERE. The first form of
                // this gate ran at UNITY trims and mutation M17 sailed through
                // it: at unity the sum is an ALLPASS, so |H| is flat wherever
                // the crossover sits and a frozen crossover is invisible. The
                // crossover only shows up in the magnitude once the lanes it
                // separates are at DIFFERENT levels.
                auto p = base(); p.type = t; p.split = which ? 0.80f : 0.20f;
                p.laneGain[0] = 0.25f; p.laneGain[1] = 0.72f;
                p.laneGain[2] = 0.30f; p.laneGain[3] = 0.68f;
                e.setParams (p);
                float a, b, l, r; Noise n;
                for (int i = 0; i < (int) (0.4 * FS); ++i) { n.next (l, r); e.processStereo (l, r, a, b); }
                if (! which) { p.split = 0.80f; e.setParams (p);        // ← the mid-stream move
                               float x, y; for (int i = 0; i < 20000; ++i) e.processStereo (0,0,x,y); }
                m[which] = magDb (impulse (e, FS).l);
            }
            for (int i = binOf (30.0); i <= binOf (16000.0); ++i)
            { const double dd = std::fabs (m[0][(size_t) i] - m[1][(size_t) i]);
              if (dd > worst) { worst = dd; wT = t; } }
        }
        char d[220]; std::snprintf (d, sizeof d, "moved Split 0.20→0.80 after the first block: worst |ΔdB| vs starting there = %.4f dB (%s)",
                                    worst, TN[wT]);
        gate ("the crossover RESOLVE really runs after the first block", worst < 0.1, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§F — EVERY KNOB TRANSFORMS ACROSS ITS WHOLE TRAVEL (no plateaus, no dead travel).\n");
    {
        // fb417 — GEOMETRY IS NOT HEARING, and gross energy is not either. Width,
        // Pan and Spread move energy BETWEEN channels and an |output| metric is
        // blind to all three; the allpass alignment is invisible to it as well.
        // So the fingerprint is the MID and SIDE magnitude spectra of DECORRELATED
        // stereo noise — the probe contains the thing every knob acts on.
        auto fingerprint = [] (Spl& e) -> std::vector<double>
        {
            Noise n; n.reset(); float a, b, l, r;
            for (int i = 0; i < (int) (0.45 * FS); ++i)      // fb441 — seed first
            { n.next (l, r); e.processStereo (l, r, a, b); }
            std::vector<std::complex<double>> bm ((size_t) NFFT), bs ((size_t) NFFT);
            for (int i = 0; i < NFFT; ++i)
            {
                n.next (l, r); e.processStereo (l, r, a, b);
                const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1));
                // 🔬 THIS WAS MID AND SIDE, AND MID/SIDE IS BLIND TO PAN. An
                // equal-power pan preserves gL² + gR², so on DECORRELATED content
                // the Mid power and the Side power are both invariant under it —
                // every Pan knob in the device measured dead at 0.08 dB on an
                // engine where they work. L and R see pan, width (L = m + w·s),
                // gain, the crossovers and Mix, so L and R it is. The bar did not
                // move; the instrument did. (fb417, one level down again.)
                bm[(size_t) i] = a * w;
                bs[(size_t) i] = b * w;
            }
            fft (bm); fft (bs);       // bm/bs now hold L and R — see the note above
            // 🔬 THIS METRIC WAS REWRITTEN. Its first form averaged |ΔdB| over
            // LINEAR FFT bins, and it called Split, Lane 1 Width and Lane 1 Pan
            // dead on an engine where all three work: 90 % of the bins in
            // 40 Hz–16 kHz sit above 1.6 kHz, so a control that owns the bottom
            // two octaves is averaged into nothing. fb417's own argument, one
            // level down — geometry is not hearing, and neither is a linear bin
            // grid. Averaged in 1/24-octave bands instead, which is what an ear
            // does. The bar did not move; the instrument did.
            const double step = std::pow (2.0, 1.0 / 24.0);
            std::vector<double> out;
            for (int half = 0; half < 2; ++half)
            {
                const auto& X = half ? bs : bm;
                for (double f = 30.0; f < 16000.0; f *= step)
                {
                    const int lo = std::max (1, binOf (f));
                    const int hi = std::max (lo + 1, binOf (f * step));
                    double en = 0.0; int cnt = 0;
                    for (int i = lo; i < hi && i < NFFT / 2; ++i) { en += std::norm (X[(size_t) i]); ++cnt; }
                    out.push_back (10.0 * std::log10 (en / std::max (1, cnt) + 1e-30));
                }
            }
            return out;
        };
        auto sdist = [] (const std::vector<double>& x, const std::vector<double>& y)
        {
            double acc = 0.0;
            for (size_t i = 0; i < x.size(); ++i) acc += std::fabs (x[i] - y[i]);
            return acc / std::max<size_t> (1, x.size());
        };

        enum KId { kSplit, kSpan, kBal, kSpr, kMix, kG0, kG1, kG2, kG3, kW0, kWt, kP0, kPt };
        struct KDesc { KId id; const char* nm; };
        static const KDesc kAll[] = {
            { kSplit, "Split" }, { kSpan, "Span" }, { kBal, "Balance" }, { kSpr, "Spread" },
            { kMix, "Mix" }, { kG0, "Lane 1 Gain" }, { kG1, "Lane 2 Gain" },
            { kG2, "Lane 3 Gain" }, { kG3, "Lane 4 Gain" },
            { kW0, "Lane 1 Width" }, { kWt, "Top Lane Width" },
            { kP0, "Lane 1 Pan" }, { kPt, "Top Lane Pan" } };

        for (int t = 0; t < Spl::kNumReal; ++t)
        {
            const int N = Spl::laneCountFor (t);
            const bool freq = (t < Spl::kMidSide);
            for (const auto& k : kAll)
            {
                // Only gate a knob that is BOUND in this Type. `Span` does not
                // exist below 3 lanes (the chassis relabels the slot) and lane
                // 3/4 gains do not exist below 3/4 lanes — gating them would be
                // gating a control that is not there, which is a gate that
                // cannot fail and therefore is not coverage.
                if ((k.id == kSplit || k.id == kSpan) && ! freq) continue;
                if (k.id == kSpan && N < 3) continue;
                if (k.id == kG2 && N < 3) continue;
                if (k.id == kG3 && N < 4) continue;
                // Mid/Side lane 1 is (M, M) — perfectly correlated BY
                // CONSTRUCTION, so it has no Side component for a Width control
                // to act on. Measured at exactly 0.000 dB across the whole
                // travel. That is not a bug to paper over: it is a slot the
                // chassis MUST relabel in this Type, and it is recorded as such
                // in MUTATION.md rather than gated on a control that is not there.
                if (k.id == kW0 && t == Spl::kMidSide) continue;

                // A per-lane trim is measured with ITS OWN LANE SOLOED. Measured
                // in the full mix, the bottom half of a lane fader is masked by
                // the other lanes — a lane 34 dB under its neighbour is inaudible
                // whether it is at −60 dB or at −33 dB, so the summed spectrum
                // barely moves and the gate calls a working fader dead. Soloing
                // measures the CONTROL instead of the masking, and it still
                // catches a mis-wired knob (a knob wired to the wrong lane leaves
                // the soloed lane unchanged, which is an instant RED). That the
                // trim reaches the SUM is proven separately and exactly by §D.
                const bool perLane = (k.id >= kG0);
                const int  ln = (k.id == kG0 || k.id == kW0 || k.id == kP0) ? 0
                              : (k.id == kG1) ? 1 : (k.id == kG2) ? 2
                              : (k.id == kG3) ? 3 : N - 1;
                std::vector<double> prev; double minStep = 1e9;
                for (int i = 0; i <= 6; ++i)
                {
                    const float v = (float) i / 6.0f;
                    // A MUSICAL base: every knob is measured in a state where it
                    // can act. Mix at unity trims is invariant BY DESIGN (that is
                    // §B's gate) — measuring it there would measure the wrong
                    // thing and then "fix" the Mix law to make the number move.
                    auto p = base(); p.type = t; p.slope = 2; p.span = 0.55f;
                    // Alternating lane gains, so that moving a crossover
                    // actually moves something: with every lane at the same
                    // level, sliding the boundary between them is INAUDIBLE and
                    // the probe, not the knob, is what is dead.
                    p.laneGain[0] = 0.30f; p.laneGain[1] = 0.66f;
                    p.laneGain[2] = 0.34f; p.laneGain[3] = 0.62f;
                    p.laneWidth[0] = 0.35f; p.laneWidth[N-1] = 0.72f;
                    p.lanePan[0] = 0.44f;   p.lanePan[N-1] = 0.58f;
                    if (perLane) p.laneSolo[ln] = true;
                    switch (k.id)
                    {
                        case kSplit: p.split = v; break;
                        case kSpan:  p.span = v; break;
                        case kBal:   p.balance = v; break;
                        case kSpr:   p.spread = v; break;
                        case kMix:   p.mix = v; break;
                        case kG0:    p.laneGain[0] = v; break;
                        case kG1:    p.laneGain[1] = v; break;
                        case kG2:    p.laneGain[2] = v; break;
                        case kG3:    p.laneGain[3] = v; break;
                        case kW0:    p.laneWidth[0] = v; break;
                        case kWt:    p.laneWidth[N-1] = v; break;
                        case kP0:    p.lanePan[0] = v; break;
                        case kPt:    p.lanePan[N-1] = v; break;
                    }
                    Spl e; e.prepare (FS); e.setParams (p);
                    auto f = fingerprint (e);
                    if (! prev.empty()) minStep = std::min (minStep, sdist (prev, f));
                    prev = f;
                }
                char d[200]; std::snprintf (d, sizeof d, "%-18s %-15s smallest spectral step across 6 = %.3f dB",
                                            TN[t], k.nm, minStep);
                char nm[120]; std::snprintf (nm, sizeof nm, "Type %d · %s keeps moving 0→100", t, k.nm);
                gate (nm, minStep > 0.25, d);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§G — 44.1 and 96 kHz. The mapping is rate-independent; the null must be too.\n");
    for (float fs : { 44100.0f, 96000.0f })
    {
        double worst = -999.0; int wT = 0, wS = 0;
        for (int t = 0; t < Spl::kNumReal; ++t)
        for (int s = 0; s < Spl::kNumSlopes; ++s)
        {
            Spl e; e.prepare ((double) fs);
            auto p = base(); p.type = t; p.slope = s; e.setParams (p);
            const double n = reconNullDb (e, fs);
            if (n > worst) { worst = n; wT = t; wS = s; }
        }
        char d[200]; std::snprintf (d, sizeof d, "%.1f kHz: worst of 20 cells = %.2f dB (%s / %s)",
                                    fs / 1000.0f, worst, TN[wT], SN[wS]);
        gate ("reconstruction holds at this rate too", worst < -100.0, d);
    }
    {
        // ... and the crossover Hz must not MOVE with the rate. 10 kHz is the
        // shared ceiling precisely so the 0.245·fs filter clamp never bites at
        // 44.1 kHz, which is the rate where it would.
        double worst = 0.0;
        for (int t = 0; t < Spl::kMidSide; ++t)
        for (float sk : { 0.0f, 0.5f, 1.0f })
        {
            Spl a, b; a.prepare (44100.0); b.prepare (96000.0);
            auto p = base(); p.type = t; p.split = sk; p.span = 1.0f;
            a.setParams (p); b.setParams (p);
            float x, y;
            for (int i = 0; i < 20000; ++i) { a.processStereo (0,0,x,y); b.processStereo (0,0,x,y); }
            for (int k = 0; k < Spl::laneCountFor (t) - 1; ++k)
                worst = std::max (worst, std::fabs (1200.0 * std::log2 ((double) a.meterXoverHz (k) / b.meterXoverHz (k))));
        }
        char d[200]; std::snprintf (d, sizeof d, "worst 44.1 vs 96 kHz crossover drift at Span 100%% = %.3f cents", worst);
        gate ("a preset means the same Hz at 44.1 and 96 kHz", worst < 1.0, d);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§H — NO CLICKS, and the bounded switch transient.\n");
    {
        auto slew = [] (bool sweep, int slopeB) -> double
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = Spl::kSubLowMidHigh; p.split = 0.5f; e.setParams (p);
            float a, b, l, r; Noise n; double pk = 0.0, prevL = 0.0;
            for (int i = 0; i < (int) (0.4 * FS); ++i) { n.next (l, r); e.processStereo (l, r, a, b); }
            const int M = (int) (0.9 * FS);
            for (int i = 0; i < M; ++i)
            {
                if ((i % 64) == 0)                        // host block-rate automation
                {
                    if (sweep) p.split = (float) i / (float) M;
                    if (slopeB >= 0 && i > M / 2) p.slope = slopeB;
                    e.setParams (p);
                }
                n.next (l, r); e.processStereo (l, r, a, b);
                if (i > 100) pk = std::max (pk, std::fabs ((double) a - prevL));
                prevL = a;
            }
            return pk;
        };
        const double stat = slew (false, -1), swept = slew (true, -1);
        char d[200]; std::snprintf (d, sizeof d, "peak |Δy| static %.5f · Split swept 0→100 over 0.9 s %.5f (x%.2f)",
                                    stat, swept, swept / std::max (1e-9, stat));
        gate ("sweeping Split under program adds no discontinuity", swept < stat * 3.0, d);

        {
            // A slow automation ramp is the EASY case. The one that clicks is a
            // single large jump — a preset recall, or a mouse released across the
            // knob. Measured separately, because mutation M12 (delete the glide)
            // survived the ramp gate untouched.
            Spl e; e.prepare (FS);
            auto p = base(); p.type = Spl::kSubLowMidHigh; p.split = 0.05f; e.setParams (p);
            float a, b, l, r; Noise n; double pk = 0.0, prevL = 0.0;
            for (int i = 0; i < (int) (0.5 * FS); ++i) { n.next (l, r); e.processStereo (l, r, a, b); prevL = a; }
            p.split = 0.95f; e.setParams (p);                       // ← ONE step, full travel
            for (int i = 0; i < (int) (0.3 * FS); ++i)
            { n.next (l, r); e.processStereo (l, r, a, b);
              pk = std::max (pk, std::fabs ((double) a - prevL)); prevL = a; }
            char d3[220]; std::snprintf (d3, sizeof d3, "peak |Δy| across a 5%%→95%% Split jump in ONE setParams = %.5f (x%.2f static)",
                                         pk, pk / std::max (1e-9, stat));
            gate ("a full-travel Split JUMP does not click either", pk < stat * 3.0, d3);
        }
        const double sw = slew (false, 3);
        char d2[220]; std::snprintf (d2, sizeof d2, "peak |Δy| across a 24→48 dB/oct switch = %.5f (x%.2f static) — caller fades it",
                                     sw, sw / std::max (1e-9, stat));
        gate ("a Slope switch transient stays bounded (a 15 ms fade hides it)", sw < stat * 40.0, d2);
    }

    // ═══════════════════════════════════════════════════════════════════════
    std::printf ("\n§I — BOUNDED, FINITE, AND SILENT ON SILENCE.\n");
    {
        bool bad = false; double worst = 0.0, slowest = 0.0; int wT = 0, wS = 0;
        for (int t = 0; t < Spl::kNumTypes; ++t)
        for (int s = 0; s < Spl::kNumSlopes; ++s)
        {
            Spl e; e.prepare (FS);
            auto p = base(); p.type = t; p.slope = s;
            for (int i = 0; i < Spl::kMaxLanes; ++i)
            { p.laneGain[i] = 1.0f; p.laneWidth[i] = 1.0f; }      // every trim maxed
            p.spread = 1.0f; p.balance = 0.0f; p.span = 0.0f;
            e.setParams (p);
            float a, b, l, r; Noise n;
            for (int i = 0; i < (int) (2.0 * FS); ++i)
            {
                n.next (l, r); l *= 18.0f; r *= 18.0f;             // ~0.9 peak, HOT
                e.processStereo (l, r, a, b);
                const double m = std::max (std::fabs ((double) a), std::fabs ((double) b));
                if (! std::isfinite (m)) bad = true;
                if (m > worst) { worst = m; wT = t; wS = s; }
            }
            // 2 s of silence, gated on the last half second. MEASURED: the
            // slowest cell (4 lanes x 48 dB/oct, every trim maxed, so the lane is
            // multiplied by +18 dB on the way out) reaches EXACTLY zero at
            // 1.046 s. The bar is still exact zero — only the window grew, and
            // the number it needed is printed rather than assumed.
            double tail = 0.0, lastNZ = 0.0;
            for (int i = 0; i < (int) (2.0 * FS); ++i)
            { e.processStereo (0.0f, 0.0f, a, b);
              const double m = std::max (std::fabs ((double) a), std::fabs ((double) b));
              if (m != 0.0) lastNZ = (double) i / FS;
              if (i > (int) (1.5 * FS)) tail = std::max (tail, m); }
            if (tail != 0.0) bad = true;
            slowest = std::max (slowest, lastNZ);
        }
        char d[240]; std::snprintf (d, sizeof d, "32 cells, hot input + every trim maxed: worst peak %.2f (%s / %s) · output reaches EXACTLY 0 by %.3f s of silence",
                                    worst, TN[wT], SN[wS], slowest);
        gate ("never NaNs, never runs away, and silence in is EXACT silence out", ! bad && worst < 12.0, d);
    }

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
