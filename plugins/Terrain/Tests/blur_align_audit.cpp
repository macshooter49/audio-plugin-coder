// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb466 — THE FALSIFICATION TEST for the spectral research's central claim.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/blur_align_audit.cpp -o /tmp/blur_align \
//            -framework Accelerate   (fb467 — Wavetable.h now bakes through vDSP via WtFft.h)
//
//  SPECTRAL-BUILD-BIBLE.md §2.3 derives WHY blur dulls: the frames of a wavetable are not
//  time-ALIGNED with each other, and averaging misaligned frames is a Gaussian LOW-PASS IN
//  HARMONIC NUMBER with corner  h3 = 271.4 / sigma_tau  (N = 2048). That single term is claimed to
//  explain BOTH measurements at once — the -12 dB ceiling (low harmonics survive) and the centroid
//  collapse 6.07 -> 3.81 (high harmonics die first).
//
//  It pre-registers a falsifiable prediction rather than asserting the mechanism:
//        sigma_tau should be ~14-27 samples; OUTSIDE 5-40 the derivation is WRONG and must be redone.
//
//  This measures it, because a build bible written by agents is a HYPOTHESIS, not evidence, and
//  the entire "align the frames" fix rests on this one number.
//
//  TWO INDEPENDENT ESTIMATES, deliberately:
//   · CROSS-CORRELATION of each frame against frame 0 (what the bible specified) — robust, makes
//     no assumption about which harmonic carries the misalignment.
//   · THE SPEC'S OWN FUNDAMENTAL PHASE, tau = (phi_f[1] - phi_0[1])·N/2pi — the exact quantity in
//     the derivation. If the two disagree, the mechanism is not what the bible thinks it is.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "WavetableBank.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static const int NPTS = 2048;

int main()
{
    tw::WavetableBank bank;
    printf ("\n══ fb466 — IS THE 'FRAMES ARE MISALIGNED' MECHANISM REAL? ══\n");
    printf ("   prediction: sigma_tau ~ 14-27 samples · FALSIFIED if outside 5-40\n\n");
    printf ("   %-4s %-16s %8s %8s %10s   %s\n", "idx", "preset", "sig_xcorr", "sig_phi", "h3=271/sig", "");

    std::vector<double> all;
    int inBand = 0, tested = 0;

    for (int p = 0; p < tw::WavetableBank::kNumPresets; ++p)
    {
        const tw::Wavetable* wt = bank.getTable (p);
        if (wt == nullptr) continue;
        const int NF = wt->getNumFrames();
        if (NF < 2) continue;

        // frame f's waveform, read exactly (framePos lands on the frame → no bilinear blend)
        auto frame = [&] (int f, std::vector<float>& out) {
            out.resize ((size_t) NPTS);
            const float fp = (NF > 1) ? (float) f / (float) (NF - 1) : 0.0f;
            for (int n = 0; n < NPTS; ++n) out[(size_t) n] = wt->lookup (0, fp, (float) n / (float) NPTS);
        };

        std::vector<float> f0, ff; frame (0, f0);
        double e0 = 0; for (float v : f0) e0 += (double) v * v;
        if (e0 < 1e-12) continue;                       // a silent reference cannot be aligned to

        std::vector<double> lags;
        for (int f = 1; f < NF; ++f)
        {
            frame (f, ff);
            double ef = 0; for (float v : ff) ef += (double) v * v;
            if (ef < 1e-12) continue;
            // circular cross-correlation, argmax
            double best = -1e30; int bestLag = 0;
            for (int L = 0; L < NPTS; ++L)
            {
                double c = 0;
                for (int n = 0; n < NPTS; ++n) c += (double) f0[(size_t) n] * ff[(size_t) ((n + L) & (NPTS - 1))];
                if (c > best) { best = c; bestLag = L; }
            }
            lags.push_back (bestLag > NPTS / 2 ? bestLag - NPTS : bestLag);   // signed
        }
        if (lags.size() < 2) continue;

        double m = 0; for (double v : lags) m += v; m /= (double) lags.size();
        double s = 0; for (double v : lags) s += (v - m) * (v - m);
        const double sigXc = std::sqrt (s / (double) lags.size());

        // independent estimate: the spec's own fundamental phase
        const tw::WavetableSpec sp = tw::WavetableBank::specForPreset (p);
        std::vector<double> tphi;
        const double ph0 = sp.frames[0].phases[0];
        for (int f = 1; f < (int) sp.frames.size(); ++f)
        {
            double d = sp.frames[(size_t) f].phases[0] - ph0;
            while (d >  M_PI) d -= 2.0 * M_PI;
            while (d < -M_PI) d += 2.0 * M_PI;
            tphi.push_back (d * NPTS / (2.0 * M_PI));
        }
        double mp = 0; for (double v : tphi) mp += v; mp /= (double) std::max<size_t> (1, tphi.size());
        double sp2 = 0; for (double v : tphi) sp2 += (v - mp) * (v - mp);
        const double sigPhi = tphi.empty() ? 0.0 : std::sqrt (sp2 / (double) tphi.size());

        const bool ok = (sigXc >= 5.0 && sigXc <= 40.0);
        if (sigXc > 0.0) { all.push_back (sigXc); ++tested; if (ok) ++inBand; }
        printf ("   %-4d %-16s %8.2f %8.2f %10.1f   %s\n", p, "", sigXc, sigPhi,
                sigXc > 0.01 ? 271.4 / sigXc : 9999.0, ok ? "in band" : "OUT OF BAND");
    }

    std::sort (all.begin(), all.end());
    const double med = all.empty() ? 0.0 : all[all.size() / 2];
    printf ("\n   tables measured %d · median sigma_tau %.2f samples · in 5-40 band: %d/%d\n",
            tested, med, inBand, tested);
    printf ("   VERDICT: %s\n\n",
            (med >= 5.0 && med <= 40.0) ? "MECHANISM SURVIVES — the frames really are misaligned"
                                        : "FALSIFIED — sigma_tau is outside 5-40; §2.3 must be re-derived");
    return 0;
}
