// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fltmeas.cpp — fb603 · THE MEASURED RESPONSE OF ALL 94 SHIPPING FILTER TYPES.
//
//    c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source Tests/fltmeas.cpp \
//        -framework Accelerate -o /tmp/fltmeas
//    /tmp/fltmeas                       > /tmp/fltmeas.txt     # ~40 s, writes Tests/flt_curves.csv
//    /tmp/fltmeas --quick               > /tmp/fltmeas.txt     # first 6 types, ~3 s
//    /tmp/fltmeas --csv <path>                                 # write the curve dump elsewhere
//
//  This file REPORTS. It does not assert — Tests/flt_gate.cpp is the pass/FAIL gate, and both
//  read the same Tests/flt_measure.h so there is only ever one measurement.
//
//  It is also what writes Tests/flt_curves.csv, the ground truth Tests/flt_curve_diff.js uses to
//  check index.html's drawn curve against the filter that actually runs. Regenerate the csv
//  whenever the DSP changes or that diff is comparing against a filter that no longer exists.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "flt_measure.h"

int main (int argc, char** argv)
{
    bool quick = false;
    std::string csvPath = "Tests/flt_curves.csv";
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if      (a == "--quick")               quick = true;
        else if (a == "--csv" && i + 1 < argc) csvPath = argv[++i];
    }
    std::printf ("# fltmeas — Terrain filter measurement (fs=%.0f, kNumTypes=%d)\n", FS, kNumTypes);
    std::printf ("# probe: white noise rms 0.002 (linear-region), sine probes amp 0.01 / THD 0.25\n");
    std::printf ("# oversampled types run the SynthVoice 2x wrapper (linear-interp up / box decimate)\n\n");

    // ── OS wrapper reference: what the 2x wrapper alone does to a flat signal
    Curve WRAP;
    {
        Runner R; R.init ((int) Type::LADDER_LP24); R.bypassIdentity = true;
        Curve cl, cr; RunFlags fl;
        noiseCurve (R, 1000.0f, 0.0f, 0.0f, 0.002f, cl, cr, fl);
        WRAP = cl;
        std::printf ("[OS-WRAPPER REFERENCE] identity filter through the voice's 2x path:\n ");
        for (double fq : { 100.0, 1000.0, 4000.0, 8000.0, 12000.0, 16000.0, 20000.0 })
        {
            size_t bi = 0; double bd = 1e9;
            for (size_t i = 0; i < GRID.size(); ++i) if (std::fabs (GRID[i] - fq) < bd) { bd = std::fabs (GRID[i] - fq); bi = i; }
            std::printf ("  %5.0fHz %+6.2f dB", fq, cl.dB[bi]);
        }
        std::printf ("\n\n");
    }

    struct Res
    {
        int idx = 0;
        bool osUsed = false;
        Curve c100, c1k, c8k;         // res 0
        Curve r05, r09, r10;          // res sweeps @1k
        Curve dhi;                    // drive 1 @1k res0
        double gLo = 0, gAt = 0, gHi = 0;
        std::string shape;
        double slope = 0; std::string slopeWhere;
        double resPk[4] = { 0,0,0,0 };
        double resSens = 0;
        bool   selfOsc = false; double soRms = -200;
        bool   nanFlag = false; double stabPeak = 0; bool stabNan = false;
        double thd0 = 0, thd1 = 0, lvl0 = 0, lvl1 = 0;
        double thdHot0 = 0, thdHot1 = 0;
        double coreSlope = 0;
        double passGainPB = 0;                 // passband gain (dB) at cut=1k
        double corner[3] = { 0,0,0 };          // measured -3 dB corner @100/1k/8k
        double thdFc = 0;
        double slopeFar = 0, slopeFarLo = 0;   // asymptotic slope, low-level probe
        double trackSpread = 0;                // % spread of corner/fc across the usable pair
        Curve  cOpen;                          // WIDE OPEN (cut=20 kHz) — the plugin default
        double openMid = 0, openBass = 0, openHf = 0;
        double resSensHot = 0, drvSensHot = 0;   // at the REAL bus level (rms 0.05)
        double lvlGain[4] = { 0,0,0,0 };         // gain (dB) at in-rms 5e-4 / 5e-3 / 5e-2 / 0.2
        std::string driveVerdict;
        double nsPerSample = 0;
        double lrDiff = 0;            // max |L-R| dB over the res0 1k curve
        double passGain = 0;          // broadband gain @1k res0 (dB, rms)
        double denormRatio = 1.0;
        bool   silent = false;
    };
    std::vector<Res> R (94);

    auto gridIdx = [] (double f) { size_t bi = 0; double bd = 1e9;
        for (size_t i = 0; i < GRID.size(); ++i) if (std::fabs (GRID[i] - f) < bd) { bd = std::fabs (GRID[i] - f); bi = i; }
        return bi; };
    auto meanDb = [&] (const Curve& c, double f0, double f1) {
        double s = 0; int n = 0;
        for (size_t i = 0; i < GRID.size(); ++i) if (GRID[i] >= f0 && GRID[i] <= f1) { s += c.dB[i]; ++n; }
        return n ? s / n : -200.0; };
    // least-squares dB/octave over [f0,f1]
    auto fitSlope = [&] (const Curve& c, double f0, double f1) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (size_t i = 0; i < GRID.size(); ++i)
            if (GRID[i] >= f0 && GRID[i] <= f1 && std::isfinite (c.dB[i]))
            { const double x = std::log2 (GRID[i]), y = c.dB[i]; sx += x; sy += y; sxx += x * x; sxy += x * y; ++n; }
        if (n < 3) return 0.0;
        const double d = n * sxx - sx * sx;
        return (std::fabs (d) < 1e-12) ? 0.0 : (n * sxy - sx * sy) / d; };

    const int NT = quick ? 6 : 94;
    for (int t = 0; t < NT; ++t)
    {
        Runner rn; rn.init (t);
        Res& r = R[(size_t) t]; r.idx = t; r.osUsed = rn.os;
        RunFlags fl;
        Curve tmp;

        noiseCurve (rn,  100.0f, 0.0f, 0.0f, 0.002f, r.c100, tmp, fl);
        Curve c1kR; noiseCurve (rn, 1000.0f, 0.0f, 0.0f, 0.002f, r.c1k, c1kR, fl);
        noiseCurve (rn, 8000.0f, 0.0f, 0.0f, 0.002f, r.c8k, tmp, fl);
        noiseCurve (rn, 20000.0f, 0.0f, 0.0f, 0.002f, r.cOpen, tmp, fl);
        noiseCurve (rn, 1000.0f, 0.5f, 0.0f, 0.002f, r.r05, tmp, fl);
        Curve r09R; noiseCurve (rn, 1000.0f, 0.9f, 0.0f, 0.002f, r.r09, r09R, fl);
        noiseCurve (rn, 1000.0f, 1.0f, 0.0f, 0.002f, r.r10, tmp, fl);
        noiseCurve (rn, 1000.0f, 0.0f, 1.0f, 0.002f, r.dhi, tmp, fl);
        r.nanFlag = fl.nan;

        // L/R difference must be read at REAL resonance: at res=0 every comb core has
        // zero feedback and is a pure feed-through, so an L/R test there measures nothing.
        for (size_t i = 0; i < GRID.size(); ++i)
        {
            if (GRID[i] > 18000.0) continue;
            r.lrDiff = std::max (r.lrDiff, std::fabs (r.c1k.dB[i] - c1kR.dB[i]));
            r.lrDiff = std::max (r.lrDiff, std::fabs (r.r09.dB[i] - r09R.dB[i]));
        }

        // HOT probes at the real bus level (rms 0.05): a nonlinear core's RES/DRV can
        // be inert at -54 dBFS and alive at -26 dBFS. The low-level verdict alone lies.
        {
            Curve h0, h9, hd, t2; RunFlags f3;
            noiseCurve (rn, 1000.0f, 0.0f, 0.0f, 0.05f, h0, t2, f3);
            noiseCurve (rn, 1000.0f, 0.9f, 0.0f, 0.05f, h9, t2, f3);
            noiseCurve (rn, 1000.0f, 0.0f, 1.0f, 0.05f, hd, t2, f3);
            for (size_t i = 0; i < GRID.size(); ++i) if (GRID[i] <= 18000.0)
            {
                r.resSensHot = std::max (r.resSensHot, std::fabs (h9.dB[i] - h0.dB[i]));
                r.drvSensHot = std::max (r.drvSensHot, std::fabs (hd.dB[i] - h0.dB[i]));
            }
        }

        // LEVEL LADDER: broadband gain at four input levels. A core whose gain
        // collapses at low input is amplitude-gated (a quantiser with too few bits).
        {
            const float lv[4] = { 0.0005f, 0.005f, 0.05f, 0.2f };
            for (int q = 0; q < 4; ++q)
            {
                const int N = (int) (0.4 * FS);
                rngS = 0x5bf03635u; rn.resetState(); rn.setP (1000.0f, 0.5f, 0.5f);
                double ai = 0, ao = 0;
                for (int i = 0; i < N; ++i)
                {
                    const float x = rnd() * lv[q] * 1.7320508f;
                    rn.setP (1000.0f, 0.5f, 0.5f);
                    float a, b; rn.step (x, x, a, b);
                    if (! std::isfinite (a)) a = 0.0f;
                    if (i > N / 4) { ai += (double) x * x; ao += (double) a * a; }
                }
                r.lvlGain[q] = 20.0 * std::log10 (std::max (1e-12, std::sqrt (ao)) / std::max (1e-12, std::sqrt (ai)));
            }
        }

        // WIDE OPEN (cut = 20 kHz = the shipped default cutoff): is this type
        // transparent when the knob is fully open, and what does it cost at the
        // extremes?  DC blockers and the 2x box decimator both live here.
        r.openMid  = meanDb (r.cOpen,  700.0, 1400.0);
        r.openBass = meanDb (r.cOpen,   32.0,   50.0) - r.openMid;
        r.openHf   = meanDb (r.cOpen, 14000.0,18000.0) - r.openMid;

        // classification @1k
        r.gLo = meanDb (r.c1k,   62.0,  126.0);
        r.gAt = meanDb (r.c1k,  840.0, 1190.0);
        r.gHi = meanDb (r.c1k, 8000.0,16000.0);
        const double gBroad = meanDb (r.c1k, 20.0, 18000.0);
        r.passGain = gBroad;
        r.silent = (gBroad < -80.0);

        if      (r.gLo - r.gHi > 12.0) { r.shape = "LP";    r.slope = -fitSlope (r.c1k, 2000.0, 8000.0);  r.slopeWhere = "2k-8k"; }
        else if (r.gHi - r.gLo > 12.0) { r.shape = "HP";    r.slope =  fitSlope (r.c1k,  125.0,  500.0);  r.slopeWhere = "125-500"; }
        else if (r.gAt > r.gLo + 6 && r.gAt > r.gHi + 6) { r.shape = "BP"; r.slope = -fitSlope (r.c1k, 2000.0, 8000.0); r.slopeWhere = "2k-8k(hi)"; }
        else if (r.gAt < r.gLo - 6 && r.gAt < r.gHi - 6) { r.shape = "NOTCH"; r.slope = 0; r.slopeWhere = "-"; }
        else                            { r.shape = "flat/other"; r.slope = -fitSlope (r.c1k, 2000.0, 8000.0); r.slopeWhere = "2k-8k"; }
        if (r.silent) { r.shape = "SILENT"; }

        // wrapper-corrected CORE slope: the voice's 2x box decimator alone
        // droops in the same band, so subtract its own slope for OS types.
        {
            double wrapSlope = 0.0;
            if (r.osUsed)
            {
                if      (r.shape == "HP") wrapSlope =  fitSlope (WRAP, 125.0, 500.0);
                else                      wrapSlope = -fitSlope (WRAP, 2000.0, 8000.0);
            }
            r.coreSlope = r.slope - wrapSlope;
        }

        // ── ASYMPTOTIC SLOPE, low-level probe (rms 5e-4 -> tanh distortion floor
        //    ~ -132 dB, so a -120 dB stopband is still an honest measurement) and
        //    a band 2..5 octaves from the corner, far from Nyquist and from the
        //    2x wrapper's own droop.  LP: fc=100, fit 400..3200 Hz.
        //                             HP: fc=8k,  fit 250..2000 Hz.
        if (r.shape == "LP" || r.shape == "BP" || r.shape == "flat/other")
        {
            Curve cs, dummy2; RunFlags f2;
            noiseCurve (rn, 100.0f, 0.0f, 0.0f, 0.0005f, cs, dummy2, f2);
            double w = r.osUsed ? -fitSlope (WRAP, 400.0, 3200.0) : 0.0;
            r.slopeFar = -fitSlope (cs, 400.0, 3200.0) - w;
        }
        if (r.shape == "HP" || r.shape == "BP")
        {
            Curve cs, dummy2; RunFlags f2;
            noiseCurve (rn, 8000.0f, 0.0f, 0.0f, 0.0005f, cs, dummy2, f2);
            double w = r.osUsed ? fitSlope (WRAP, 250.0, 2000.0) : 0.0;
            r.slopeFarLo = fitSlope (cs, 250.0, 2000.0) - w;
        }

        // passband gain (level-match number) and measured -3 dB corner
        auto cornerOf = [&] (const Curve& c, double fcReq, const std::string& sh) -> double
        {
            if (sh != "LP" && sh != "HP") return 0.0;
            double pb;
            if (sh == "LP") pb = meanDb (c, std::max (20.0, fcReq / 16.0), std::max (25.0, fcReq / 5.0));
            else            pb = meanDb (c, std::min (18000.0, fcReq * 5.0), 18000.0);
            const double target = pb - 3.0;
            if (sh == "LP")
            {
                for (size_t i = 1; i < GRID.size(); ++i)
                    if (c.dB[i] <= target && c.dB[i-1] > target)
                    { const double t = (c.dB[i-1] - target) / std::max (1e-9, c.dB[i-1] - c.dB[i]);
                      return GRID[i-1] * std::pow (GRID[i] / GRID[i-1], t); }
            }
            else
            {
                for (size_t i = GRID.size() - 1; i > 0; --i)
                    if (c.dB[i-1] <= target && c.dB[i] > target)
                    { const double t = (c.dB[i] - target) / std::max (1e-9, c.dB[i] - c.dB[i-1]);
                      return GRID[i] * std::pow (GRID[i-1] / GRID[i], t); }
            }
            return 0.0;
        };
        r.corner[0] = cornerOf (r.c100,   100.0, r.shape);
        r.corner[1] = cornerOf (r.c1k,   1000.0, r.shape);
        r.corner[2] = cornerOf (r.c8k,   8000.0, r.shape);
        r.passGainPB = (r.shape == "HP") ? meanDb (r.c1k, 6000.0, 16000.0)
                                         : meanDb (r.c1k,   20.0,  125.0);
        {
            // LP: compare the 1 kHz and 8 kHz corners (the 100 Hz one collides with
            //     every core's DC blocker and with the PSD's low-frequency resolution).
            // HP: compare the 100 Hz and 1 kHz corners (8 kHz is too near Nyquist).
            const double want[3] = { 100.0, 1000.0, 8000.0 };
            const int qa = (r.shape == "HP") ? 0 : 1, qb = (r.shape == "HP") ? 1 : 2;
            if (r.corner[qa] > 0 && r.corner[qb] > 0)
            { const double ra = r.corner[qa] / want[qa], rb = r.corner[qb] / want[qb];
              r.trackSpread = 100.0 * (std::max (ra, rb) / std::min (ra, rb) - 1.0); }
        }

        // resonance sensitivity (max |curve delta| res0 -> res0.9, 20..18k)
        for (size_t i = 0; i < GRID.size(); ++i)
            if (GRID[i] <= 18000.0)
                r.resSens = std::max (r.resSens, std::fabs (r.r09.dB[i] - r.c1k.dB[i]));

        // resonance PEAK: fine sine sweep in 0.55..1.9 x fc, long settle
        {
            const float resV[4] = { 0.0f, 0.5f, 0.9f, 1.0f };
            const int NP = quick ? 9 : 19;
            for (int q = 0; q < 4; ++q)
            {
                double best = -200.0;
                for (int p = 0; p < NP; ++p)
                {
                    const double fr = 0.55 * std::pow (1.9 / 0.55, (double) p / (NP - 1));
                    const double f  = 1000.0 * fr;
                    const double g  = sineGainDb (rn, 1000.0f, resV[q], 0.0f, f, 0.01, 0.55, 0.25);
                    best = std::max (best, g);
                }
                r.resPk[q] = best;
            }
        }

        // self-oscillation: noise burst then 3 s of silence, RMS of last 0.5 s
        {
            rn.resetState(); rn.setP (1000.0f, 1.0f, 0.0f);
            const int burst = (int) (0.10 * FS), total = (int) (3.0 * FS);
            double acc = 0; int n = 0; bool nn = false;
            rngS = 0x2545f491u;
            for (int i = 0; i < total; ++i)
            {
                const float x = (i < burst) ? rnd() * 0.05f : 0.0f;
                rn.setP (1000.0f, 1.0f, 0.0f);
                float a, b; rn.step (x, x, a, b);
                if (! std::isfinite (a)) { nn = true; a = 0.0f; }
                if (i > total - (int) (0.5 * FS)) { acc += (double) a * a; ++n; }
            }
            const double rms = n ? std::sqrt (acc / n) : 0.0;
            r.soRms = 20.0 * std::log10 (std::max (1e-12, rms));
            r.selfOsc = (r.soRms > -60.0) && ! nn;
            if (nn) r.nanFlag = true;
        }

        // stability: fast cutoff sweep 20<->20k at res 1 drive 1, plus extremes
        {
            rngS = 0x1234567u;
            rn.resetState();
            const int total = (int) (4.0 * FS);
            double pk = 0; bool nn = false;
            for (int i = 0; i < total; ++i)
            {
                const double ph = std::fmod (6.0 * i / FS, 1.0);
                const double c01 = 0.5 - 0.5 * std::cos (2.0 * M_PI * ph);
                const float cut = (float) (20.0 * std::pow (1000.0, c01));
                rn.setP (cut, 1.0f, 1.0f);
                const float x = rnd() * 0.05f;
                float a, b; rn.step (x, x, a, b);
                if (! std::isfinite (a) || ! std::isfinite (b)) { nn = true; a = b = 0.0f; }
                pk = std::max (pk, (double) std::max (std::fabs (a), std::fabs (b)));
            }
            for (float cut : { 20.0f, 20000.0f })
            {
                rn.resetState(); rn.setP (cut, 1.0f, 1.0f);
                for (int i = 0; i < (int) (2.0 * FS); ++i)
                {
                    rn.setP (cut, 1.0f, 1.0f);
                    const float x = rnd() * 0.05f;
                    float a, b; rn.step (x, x, a, b);
                    if (! std::isfinite (a) || ! std::isfinite (b)) { nn = true; a = b = 0.0f; }
                    pk = std::max (pk, (double) std::max (std::fabs (a), std::fabs (b)));
                }
            }
            r.stabPeak = pk; r.stabNan = nn;
        }

        // THD @ drive 0 vs 1 — 220 Hz sine, cutoff chosen so 220 Hz sits IN the
        // passband for this type's measured shape (a HP measured at fc=8k would
        // report the noise floor as "distortion", which is not a measurement).
        {
            const float thdFc = (r.shape == "HP")    ? 20.0f
                              : (r.shape == "BP")    ? 220.0f
                              : (r.shape == "NOTCH") ? 4000.0f
                              :                        20000.0f;
            r.thdFc = thdFc;
            auto thdRun = [&] (float drv, double amp, double& thd, double& lvl)
            {
                const int ns = (int) (0.3 * FS), nm = (int) (0.5 * FS);
                std::vector<float> y ((size_t) nm);
                rn.resetState(); rn.setP (thdFc, 0.0f, drv);
                for (int i = 0; i < ns + nm; ++i)
                {
                    const float x = (float) (amp * std::sin (2.0 * M_PI * 220.0 * i / FS));
                    rn.setP (thdFc, 0.0f, drv);
                    float a, b; rn.step (x, x, a, b);
                    if (i >= ns) y[(size_t)(i - ns)] = std::isfinite (a) ? a : 0.0f;
                }
                const double h1 = ampAt (y, 220.0);
                double hs = 0;
                for (int h = 2; h <= 10; ++h) { const double a = ampAt (y, 220.0 * h); hs += a * a; }
                thd = (h1 > 1e-9) ? std::sqrt (hs) / h1 : 0.0;
                lvl = h1;
            };
            double dummy;
            thdRun (0.0f, 0.25, r.thd0, r.lvl0);
            thdRun (1.0f, 0.25, r.thd1, r.lvl1);
            thdRun (0.0f, 1.00, r.thdHot0, dummy);
            thdRun (1.0f, 1.00, r.thdHot1, dummy);
            const double dLvl = 20.0 * std::log10 (std::max (1e-9, r.lvl1) / std::max (1e-9, r.lvl0));
            const double thdRatio = (r.thd0 > 1e-6) ? r.thd1 / r.thd0 : (r.thd1 > 0.005 ? 99.0 : 1.0);
            double dCurve = 0;
            for (size_t i = 0; i < GRID.size(); ++i) if (GRID[i] <= 18000.0)
                dCurve = std::max (dCurve, std::fabs (r.dhi.dB[i] - r.c1k.dB[i]));
            char buf[128];
            if (std::fabs (dLvl) < 0.5 && thdRatio < 1.2 && dCurve < 0.5)
                std::snprintf (buf, sizeof buf, "DEAD (lvl %+.1f dB, hot THD %.2f->%.2f%%)",
                               dLvl, 100.0 * r.thdHot0, 100.0 * r.thdHot1);
            else if (r.thd1 > 0.02 && thdRatio > 2.5)
                std::snprintf (buf, sizeof buf, "character (lvl %+.1f dB, THDx%.1f)", dLvl, thdRatio);
            else if (thdRatio < 1.5 && (std::fabs (dLvl) > 2.0 || dCurve > 2.0))
                std::snprintf (buf, sizeof buf, "VOLUME-ONLY %+.1f dB (THDx%.1f, hot %.1f->%.1f%%)",
                               dLvl, thdRatio, 100.0 * r.thdHot0, 100.0 * r.thdHot1);
            else
                std::snprintf (buf, sizeof buf, "mild (lvl %+.1f dB, THDx%.1f)", dLvl, thdRatio);
            r.driveVerdict = buf;
        }

        // CPU: ns / host sample / voice (stereo), params gated as in the voice
        {
            const int N = (int) (2.0 * FS);
            rn.resetState(); rn.setP (1000.0f, 0.5f, 0.5f);
            volatile double sink = 0;
            rngS = 0xabcdef01u;
            std::vector<float> xs ((size_t) N); for (int i = 0; i < N; ++i) xs[(size_t) i] = rnd() * 0.05f;
            auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < N; ++i)
            { rn.setP (1000.0f, 0.5f, 0.5f); float a, b; rn.step (xs[(size_t) i], xs[(size_t) i], a, b);
              sink += (double) a + (double) b; }
            auto t1 = std::chrono::high_resolution_clock::now();
            (void) sink;
            r.nsPerSample = std::chrono::duration<double, std::nano> (t1 - t0).count() / N;

            // denormal stall probe: same length of EXACT ZEROS after excitation
            auto t2 = std::chrono::high_resolution_clock::now();
            volatile double sink2 = 0;
            for (int i = 0; i < N; ++i)
            { rn.setP (1000.0f, 0.5f, 0.5f); float a, b; rn.step (0.0f, 0.0f, a, b);
              sink2 += (double) a + (double) b; }
            auto t3 = std::chrono::high_resolution_clock::now();
            (void) sink2;
            const double nsSilent = std::chrono::duration<double, std::nano> (t3 - t2).count() / N;
            r.denormRatio = nsSilent / std::max (1e-9, r.nsPerSample);
        }

        std::fprintf (stderr, "  measured %2d %-18s\r", t, kName[t]);
    }
    std::fprintf (stderr, "\n");

    // ── duplicate detection on the (100Hz,1k,8k @res0) L curves
    std::vector<std::string> dupOf ((size_t) NT);
    for (int a = 0; a < NT; ++a)
        for (int b = a + 1; b < NT; ++b)
        {
            double m = 0;
            for (size_t i = 0; i < GRID.size(); ++i)
            {
                if (GRID[i] > 18000.0) continue;
                m = std::max (m, std::fabs (R[(size_t)a].c100.dB[i] - R[(size_t)b].c100.dB[i]));
                m = std::max (m, std::fabs (R[(size_t)a].c1k .dB[i] - R[(size_t)b].c1k .dB[i]));
                m = std::max (m, std::fabs (R[(size_t)a].c8k .dB[i] - R[(size_t)b].c8k .dB[i]));
                m = std::max (m, std::fabs (R[(size_t)a].r09 .dB[i] - R[(size_t)b].r09 .dB[i]));
            }
            if (m < 0.35)
            {
                char b1[160]; std::snprintf (b1, sizeof b1, "%s(%d) max-dev %.2f dB", kName[b], b, m);
                if (! dupOf[(size_t)a].empty()) dupOf[(size_t)a] += " | ";
                dupOf[(size_t)a] += b1;
            }
        }

    // ── TABLE 1: response, slope, cutoff accuracy
    std::printf ("=== TABLE 1 — SHAPE / SLOPE / CUTOFF ACCURACY (res 0, drive 0) ===\n");
    std::printf ("   SLOPE-hi/lo = ASYMPTOTIC dB/oct, low-level probe, 2..5 oct from the corner, 2x-wrapper droop removed.\n");
    std::printf ("   NEAR = slope in the 2..8x fc band (what a 1 kHz sweep actually shows).  CORNER = -3 dB point / knob Hz.\n\n");
    std::printf ("IDX  NAME                OS SHAPE       SLOPE-hi SLOPE-lo  NEAR  PASSBAND  CORNER/fc @100/@1k/@8k   TRACK   WIDE-OPEN(cut=20k) mid/40Hz/16kHz   L!=R\n");
    for (int t = 0; t < NT; ++t)
    {
        const Res& r = R[(size_t) t];
        char cr3[64];
        auto rat = [&] (double v, double want) { return v > 0 ? v / want : 0.0; };
        std::snprintf (cr3, sizeof cr3, "%5.2f/%5.2f/%5.2f",
                       rat (r.corner[0], 100.0), rat (r.corner[1], 1000.0), rat (r.corner[2], 8000.0));
        char sh[16], sl[16];
        if (r.slopeFar   != 0.0) std::snprintf (sh, 16, "%7.1f", r.slopeFar);   else std::snprintf (sh, 16, "      -");
        if (r.slopeFarLo != 0.0) std::snprintf (sl, 16, "%8.1f", r.slopeFarLo); else std::snprintf (sl, 16, "       -");
        std::printf ("%3d  %-19s %s %-11s %s %s %5.1f  %+7.1f  %s  %5.0f%%   %+7.1f %+7.1f %+7.1f   %5.1f\n",
            t, kName[t], r.osUsed ? "2x" : "1x", r.shape.c_str(), sh, sl, r.slope,
            r.passGainPB, cr3, r.trackSpread, r.openMid, r.openBass, r.openHf, r.lrDiff);
    }

    // ── TABLE 2: resonance / self-osc / stability / drive / CPU
    std::printf ("\n=== TABLE 2 — RESONANCE / STABILITY / DRIVE / CPU ===\n");
    std::printf ("   RESsens/DRVsens = max dB change of the response curve; 'hot' = at the real bus level (rms 0.05).\n\n");
    std::printf ("IDX  NAME                RESPEAK dB @1k  r0 /r.5 /r.9 /r1.0   RESsens hot  DRVhot  SELFOSC  STABpk  THDfc  THD0%%  THD1%%  DRIVE VERDICT                        ns/smp  FLAGS\n");
    for (int t = 0; t < NT; ++t)
    {
        const Res& r = R[(size_t) t];
        std::string flags;
        if (r.nanFlag || r.stabNan)  flags += "**NAN** ";
        if (r.denormRatio > 3.0)     flags += "**DENORM-STALL** ";
        if (r.silent)                flags += "**SILENT** ";
        if (r.resSens < 1.0 && r.resSensHot < 1.0) flags += "**RES-INERT** ";
        if (r.drvSensHot < 0.3)      flags += "**DRV-INERT** ";
        if (r.lvlGain[2] - r.lvlGain[0] > 12.0) flags += "**LEVEL-GATED** ";
        if (r.stabPeak > 4.0)        flags += "**LOUD** ";
        std::printf ("%3d  %-19s %6.1f/%6.1f/%6.1f/%6.1f  %6.1f %5.1f  %6.1f  %-7s %7.2f %6.0f %6.2f %6.2f  %-36s %6.1f  %s\n",
            t, kName[t], r.resPk[0], r.resPk[1], r.resPk[2], r.resPk[3], r.resSens, r.resSensHot,
            r.drvSensHot, r.selfOsc ? "YES" : (r.soRms > -80 ? "ring" : "no"), r.stabPeak, r.thdFc,
            100.0 * r.thd0, 100.0 * r.thd1, r.driveVerdict.c_str(), r.nsPerSample, flags.c_str());
    }

    // ── LEVEL LADDER
    std::printf ("\n=== LEVEL LADDER — broadband gain (dB) vs input level, cut 1 kHz res 0.5 drv 0.5 ===\n");
    std::printf ("   A level-independent filter shows four equal numbers. A big rise with level = amplitude gating.\n");
    std::printf ("IDX  NAME                in -66dB  in -46dB  in -26dB  in -14dB   spread\n");
    for (int t = 0; t < NT; ++t)
    {
        const Res& r = R[(size_t) t];
        const double sp = *std::max_element (r.lvlGain, r.lvlGain + 4) - *std::min_element (r.lvlGain, r.lvlGain + 4);
        if (sp < 1.0) continue;
        std::printf ("%3d  %-19s %8.1f  %8.1f  %8.1f  %8.1f   %6.1f%s\n", t, kName[t],
            r.lvlGain[0], r.lvlGain[1], r.lvlGain[2], r.lvlGain[3], sp, sp > 12.0 ? "  **LEVEL-GATED**" : "");
    }

    // ── CPU ranking
    {
        std::vector<int> ord (NT); for (int i = 0; i < NT; ++i) ord[(size_t) i] = i;
        std::sort (ord.begin(), ord.end(), [&] (int a, int b) { return R[(size_t)a].nsPerSample > R[(size_t)b].nsPerSample; });
        std::printf ("\n=== CPU RANKING (ns per host sample per voice, stereo, incl. the 2x wrapper) ===\n");
        for (int i = 0; i < NT; ++i)
        { const int t = ord[(size_t) i];
          std::printf ("  %2d. %-20s %6.1f ns %s\n", i + 1, kName[t], R[(size_t)t].nsPerSample, R[(size_t)t].osUsed ? "(2x)" : ""); }
    }

    // ── DETECTOR STATUS — every detector below can legitimately find nothing.
    //    Print whether it fired so a clean run is never confused with a run that did not happen.
    {
        int nNan=0,nDen=0,nSil=0,nResInert=0,nDrvInert=0,nLvl=0,nLoud=0,nOsc=0,nRing=0;
        double denMin=1e9,denMax=-1e9;
        for (int t = 0; t < NT; ++t) { const Res& r = R[(size_t)t];
            if (r.nanFlag||r.stabNan) ++nNan;
            if (r.denormRatio>3.0) ++nDen; denMin=std::min(denMin,r.denormRatio); denMax=std::max(denMax,r.denormRatio);
            if (r.silent) ++nSil;
            if (r.resSens<1.0 && r.resSensHot<1.0) ++nResInert;
            if (r.drvSensHot<0.3) ++nDrvInert;
            // fb603 — THIS DETECTOR COULD NEVER FIRE. It read lvlGain[2]-lvlGain[0] (loud
            //   minus quiet) while the defect it hunts is the OPPOSITE sign: Bode Shifter
            //   measures +66.8 dB at -66 dBFS in and +25.5 dB at -26 dBFS in, so the old
            //   expression was -41.3, never > 12, and the status line printed "did not fire
            //   (0 types)" on the same run whose LEVEL LADDER table flagged 8 types
            //   **LEVEL-GATED**. Use the same max-min spread the table prints.
            { const double sp = *std::max_element (r.lvlGain, r.lvlGain + 4)
                              - *std::min_element (r.lvlGain, r.lvlGain + 4);
              if (sp > 12.0) ++nLvl; }
            if (r.stabPeak>4.0) ++nLoud;
            if (r.selfOsc) ++nOsc; else if (r.soRms>-80.0) ++nRing; }
        std::printf ("\n=== DETECTOR STATUS (%d types tested; each line says whether the detector FIRED) ===\n", NT);
        std::printf ("  NaN / Inf                 : %s  (%d types)\n", nNan?"FIRED":"did not fire", nNan);
        std::printf ("  denormal stall (>3x time) : %s  (%d types; ratio range %.2f..%.2f)\n",
                     nDen?"FIRED":"did not fire", nDen, denMin, denMax);
        std::printf ("  silent (<-80 dB broadband): %s  (%d types)\n", nSil?"FIRED":"did not fire", nSil);
        std::printf ("  RES inert (cold AND hot)  : %s  (%d types)\n", nResInert?"FIRED":"did not fire", nResInert);
        std::printf ("  DRV inert (hot, <0.3 dB)  : %s  (%d types)\n", nDrvInert?"FIRED":"did not fire", nDrvInert);
        std::printf ("  level-gated (>12 dB)      : %s  (%d types)\n", nLvl?"FIRED":"did not fire", nLvl);
        std::printf ("  peak > 4.0 on stress      : %s  (%d types)\n", nLoud?"FIRED":"did not fire", nLoud);
        std::printf ("  self-oscillation          : %s  (%d self-osc, %d sustained ring)\n",
                     (nOsc||nRing)?"FIRED":"did not fire", nOsc, nRing);
        std::printf ("  every type was instantiated and processed audio: %d/%d (no type failed to build)\n", NT, NT);
    }

    std::printf ("\n[DUPLICATES] identical measured response (max deviation < 0.35 dB across 4 conditions):\n");
    bool anyDup = false;
    for (int t = 0; t < NT; ++t) if (! dupOf[(size_t) t].empty())
    { std::printf ("  %-20s(%d)  ==  %s\n", kName[t], t, dupOf[(size_t) t].c_str()); anyDup = true; }
    if (! anyDup) std::printf ("  (none)\n");

    std::printf ("\n[SELF-OSC detail] tail RMS dBFS after a 100 ms burst, res=1.0, cut=1 kHz:\n");
    for (int t = 0; t < NT; ++t) if (R[(size_t)t].soRms > -80.0)
        std::printf ("  %-20s(%2d)  %+7.1f dBFS  %s\n", kName[t], t, R[(size_t)t].soRms,
                     R[(size_t)t].selfOsc ? "SELF-OSCILLATES" : "sustained ring");

    // ── STATE-LEAK PROBE: does a type's level depend on which type was selected
    //    BEFORE it? (outMakeup / poleMakeup / qMax / morph are members that
    //    FilterSlot::reset() does not clear, and several fb165 types never set them.)
    if (! quick)
    {
        std::printf ("\n=== STATE-LEAK PROBE — passband gain on a FRESH slot vs after another type ===\n");
        struct P { int probe; int before; };
        const P probes[] = {
            { (int) Type::GERMANIUM_LP, (int) Type::DIODE_LP },
            { (int) Type::FRENCH_LP,    (int) Type::DIODE_LP },
            { (int) Type::POLIVOKS,     (int) Type::DIODE_LP },
            { (int) Type::GERMAN_LP,    (int) Type::LADDER_LP6 },
            { (int) Type::ACID_SCREAM,  (int) Type::ACID_303 },
            { (int) Type::SCREAM_LP,    (int) Type::SVF_LP },
            { (int) Type::WASP,         (int) Type::OBX_SVF },
            { (int) Type::MS20_LP,      (int) Type::SEM_LP },
            { (int) Type::COMB_DAMP,    (int) Type::COMB_PLUS } };
        for (const auto& pr : probes)
        {
            auto gainOf = [&] (bool withHistory) {
                Runner rr; rr.init (pr.probe);
                if (withHistory)
                {
                    rr.f.setType (static_cast<Type> (pr.before));
                    rr.f.setParams (1000.0f, 0.5f, 0.5f, rr.coefSr);
                    for (int i = 0; i < 2000; ++i) { float a = 0.01f, b = 0.01f; rr.f.processStereo (a, b); }
                    rr.f.setType (static_cast<Type> (pr.probe));
                    rr.os = rr.f.needsOversampling(); rr.coefSr = rr.os ? FS * 2.0 : FS;
                }
                Curve cl, cr; RunFlags fl;
                noiseCurve (rr, 1000.0f, 0.0f, 0.0f, 0.002f, cl, cr, fl);
                double s2 = 0; int n = 0;
                for (size_t i = 0; i < GRID.size(); ++i) if (GRID[i] >= 20.0 && GRID[i] <= 125.0) { s2 += cl.dB[i]; ++n; }
                return n ? s2 / n : -200.0; };
            const double gFresh = gainOf (false), gAfter = gainOf (true);
            std::printf ("  %-18s fresh %+7.2f dB   after %-18s %+7.2f dB   delta %+6.2f dB %s\n",
                kName[pr.probe], gFresh, kName[pr.before], gAfter, gAfter - gFresh,
                std::fabs (gAfter - gFresh) > 0.5 ? "  **STATE LEAK**" : "");
        }
    }
    else std::printf ("\n=== STATE-LEAK PROBE — SKIPPED (--quick) ===\n");

    // CSV of every curve, for follow-up plotting
    {
        FILE* fp = std::fopen (csvPath.c_str(), "w");
        if (! fp) std::printf ("\n[CSV] could not open %s for writing\n", csvPath.c_str());
        if (fp)
        {
            std::fprintf (fp, "idx,name,cond");
            for (double f : GRID) std::fprintf (fp, ",%.1f", f);
            std::fprintf (fp, "\n");
            auto row = [&] (int t, const char* cond, const Curve& c)
            { std::fprintf (fp, "%d,\"%s\",%s", t, kName[t], cond);
              for (double v : c.dB) std::fprintf (fp, ",%.2f", v);
              std::fprintf (fp, "\n"); };
            for (int t = 0; t < NT; ++t)
            { row (t, "cut100_res0",  R[(size_t)t].c100); row (t, "cut1k_res0", R[(size_t)t].c1k);
              row (t, "cut8k_res0",   R[(size_t)t].c8k);  row (t, "cut1k_res05", R[(size_t)t].r05);
              row (t, "cut1k_res09",  R[(size_t)t].r09);  row (t, "cut1k_res10", R[(size_t)t].r10);
              row (t, "cut1k_drv1",   R[(size_t)t].dhi); row (t, "cut20k_res0", R[(size_t)t].cOpen); }
            std::fclose (fp);
            std::printf ("\n[CSV] %s written (%d types x 8 conditions x %zu grid points 20 Hz..20 kHz)\n",
                         csvPath.c_str(), NT, GRID.size());
        }
    }
    return 0;
}
