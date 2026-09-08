// ══════════════════════════════════════════════════════════════════════════════════════════════
//  flt_gate.cpp — fb603 · THE FILTER SECTION'S STANDING ACCEPTANCE TEST.
//
//    c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source Tests/flt_gate.cpp \
//        -framework Accelerate -o /tmp/fltgate
//    /tmp/fltgate                      # all 94 types — 18 s, measured
//    /tmp/fltgate --quick              # a 12-type spine — 2 s, for a tight edit loop
//    TI_FLT_MUT=<bar> /tmp/fltgate     # mutation control — see MUTATION CONTROLS below
//    bash Tests/fb603_filter_gates.sh  # every gate, normal + mutated, in one run
//
//  WHY THIS FILE EXISTS.  fb603 measured all 94 shipping filter types for the first time and
//  found THIRTEEN defect classes that had shipped unnoticed — a 22.28 dB state leak between
//  types, loop gain 1.13 at ordinary knob settings, +51 dB peaks, two engines silent over most
//  of a knob, 39 engines whose DRIVE is a volume trim or nothing at all, six duplicate menu
//  slots, non-monotonic resonance, and a self-oscillation that sustains ABOVE full scale. None
//  of that was caught by anything, because nothing measured it. This is that something.
//
//  IT SHARES ONE MEASUREMENT WITH THE REPORT. Tests/flt_measure.h drives FilterSlot exactly as
//  SynthVoice does (2x wrapper and all); Tests/fltmeas.cpp prints it as tables, this file asserts
//  on it. There is no second implementation of the probe, and no number here that fltmeas.cpp
//  cannot be asked to print.
//
//  ── WHAT IT ASSERTS ───────────────────────────────────────────────────────────────────────
//   [0] every type instantiates and passes audio, with no NaN and no Inf
//   [1] NO STATE LEAK — a type's level must not depend on which type ran before it
//   [2] STRESS PEAK <= 4.0 on a res-1/drive-1 cutoff sweep at bus level
//   [3] LOOP GAIN < 1.0 across the RES x DRV plane — the tail must DECAY (below the top of RES;
//       the top of RES is bar [8]'s business, because oscillating there is the instrument)
//  [3b] SMALL-SIGNAL GAIN <= +24 dB at MODERATE settings — the other shape a loop gain over 1
//       takes. A tail-growth probe only sees an EXPONENTIAL runaway; Bode Shifter and Scream do
//       not grow forever, they have an enormous small-signal gain that a nonlinearity bounds.
//       Both probes, or the gate is blind to half of defect 2.
//   [4] NO SILENT REGION on any knob
//   [5] RESONANCE IS MONOTONIC in RES
//   [6] DRIVE IS A DRIVE — THD must move more than 1 percentage point from DRV 0 to DRV 1
//   [7] NO DUPLICATE ENGINES — every pair must differ by >= 3 dB in the LEFT channel
//   [8] SELF-OSCILLATION STAYS BELOW FULL SCALE (oscillating is wanted; +5 dBFS is not)
//   [9] WIDE OPEN IS OPEN — at the shipped 20 kHz default a lowpass must still pass 16 kHz
//
//  ── THE LAWS THESE BARS COME FROM ─────────────────────────────────────────────────────────
//  THE LIFEGUARD LAW: a knob's 100 % must be the algorithm's 100 %, unused headroom is a defect,
//  10-50 % must stay clean. Bars [3] [4] [5] [6] [8] are that law, made measurable. Note what
//  bar [3] does NOT say: it never asks for a limiter or a ceiling. It asks for a TAPER — the
//  loop gain must stay below 1 by design across the plane, not be clamped after the fact.
//  DRAMATICISM: bar [6] is "no control whose effect you can't perceive".
//  PERCEPTUAL METRICS ONLY: every number below is a magnitude spectrum, an RMS, a harmonic
//  amplitude or a growth rate. Sample-difference RMS appears nowhere.
//
//  ── WHAT IT CANNOT JUDGE, AND SAYS SO ─────────────────────────────────────────────────────
//  Several types cannot be measured by some of these metrics AT ALL. Every one of them is
//  printed as a named SKIP with its reason, on the bar that skips it — never silently passed:
//   · THD is meaningless for the 4 frequency-shifting / ring types (energy MOVES between bins,
//     so "harmonics of 220 Hz" is not a distortion measurement) -> skipped on [6].
//   · Diffusor's RES is INVISIBLE TO A MAGNITUDE METRIC BY CONSTRUCTION: an allpass chain has
//     unity magnitude, so no magnitude probe can see what its RES does. Skipped on [5], and
//     that skip is structural — it will never be fixable, and it is not a defect.
//   · The -3 dB corner is meaningless for formant / reverb / grain / karplus types (CUT is a
//     vowel shift or a tank rate there) -> skipped on [9].
//   · 'None' is a bypass -> skipped on [4] [5] [6] [9].
//   · Tilt / Low EQ / High EQ / Band EQ / Air read RES as a shelf or bell GAIN sweeping
//     cut -> flat -> boost. That curve is a V through zero: non-monotonic BY DESIGN, so
//     "monotonic" is the wrong question -> skipped on [5], with the reason printed.
//  Two things are REPORTED rather than asserted on, because the metric cannot separate a defect
//  from a design choice: the types whose RES has no magnitude signature at all (bar [5]) and the
//  types whose gain depends on input level (bar [3b] — a Moog tanh saturating and a loop that
//  only stopped growing because it clipped look identical to that number).
//
//  ── MUTATION CONTROLS ─────────────────────────────────────────────────────────────────────
//  A green bar that cannot go red is not a gate. TI_FLT_MUT=<bar> INJECTS that bar's own defect
//  into ONE nominated type that currently passes it, and the bar must name that type as a new
//  offender. The nominated type is printed on the bar's line so the runner can check it.
//      TI_FLT_MUT=leak    a 3 dB level shift survives a type change on the nominated type
//      TI_FLT_MUT=stress  the nominated type's stress output is multiplied by 4
//      TI_FLT_MUT=loop    the nominated type's tail runs through a pole-1.0004 IIR (gain > 1)
//      TI_FLT_MUT=silent  the nominated type is muted at one RES position
//      TI_FLT_MUT=mono    the nominated type's res-0.75 curve is pushed 4 dB DOWN
//      TI_FLT_MUT=thd     the nominated type's DRV-1 THD run is secretly done at DRV 0
//      TI_FLT_MUT=dup     the nominated type's curves are replaced by another type's
//      TI_FLT_MUT=osc     the nominated type's self-osc tail is multiplied by 1e5
//      TI_FLT_MUT=open    the nominated type loses 12 dB at 16 kHz wide open
//      TI_FLT_MUT=nan     the nominated type emits a NaN
//  TI_FLT_INJ=<idx> chooses the victim; Tests/fb603_filter_gates.sh sets it from the normal run's
//  own OFFENDERS: line so the victim always currently PASSES the bar. Bar [3] additionally
//  CALIBRATES its estimator two-sidedly on every run, against synthetic tails of known growth —
//  printed whether it fired or not, so "no runaway found" can never be confused with an
//  estimator that reads zero for everything.
//
//  ⚠️ NO WAIVER LIST, ON PURPOSE. This gate names offenders and exits non-zero; it does not
//  carry a frozen list of "known bad" numbers. Four agents are editing the DSP in parallel and
//  a pinned baseline written mid-flight would be wrong within the hour. To compare two runs,
//  diff the OFFENDERS: lines.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "flt_measure.h"
#include <cstdlib>
#include <set>
#include <map>

// ── bar printer ───────────────────────────────────────────────────────────────────────────────
static int gPass = 0, gFail = 0;
static std::vector<std::string> gFailed;
static void bar (bool ok, const std::string& label, const std::string& detail)
{
    if (ok) ++gPass; else { ++gFail; gFailed.push_back (label); }
    std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (! detail.empty()) std::printf ("        %s\n", detail.c_str());
}
static void detector (const char* what, int n, const std::string& extra = "")
{
    std::printf ("        detector: %s  (%d %s)%s%s\n", n ? "FIRED" : "did not fire", n, what,
                 extra.empty() ? "" : "  ", extra.c_str());
}
static void skipline (const std::string& who, const char* why)
{
    if (who.empty()) return;
    std::printf ("        SKIPPED: %s\n                 %s\n", who.c_str(), why);
}
static std::string join (const std::vector<int>& v, const char* sep = ", ")
{
    std::string s;
    for (size_t i = 0; i < v.size(); ++i)
    { if (i) s += sep; s += kName[(size_t) v[i]]; s += "(" + std::to_string (v[i]) + ")"; }
    return s;
}

// ── mutation control ──────────────────────────────────────────────────────────────────────────
static const std::string MUT = std::getenv ("TI_FLT_MUT") ? std::getenv ("TI_FLT_MUT") : "";
static bool mut (const char* n) { return MUT == n; }
// Injection targets: each is a type that PASSES the bar it is nominated for on the shipping
// build, so the mutation manufactures a genuinely NEW offender rather than re-flagging a
// known one. 5 = SVF LP (stress 1.95, monotonic, not silent, not a duplicate, no leak);
// 0 = Ladder LP 24 (a real character drive, THD 0.10 -> 28.94 %); 81 = Air (flat wide open).
//  TI_FLT_INJ=<idx> overrides the target. Tests/fb603_filter_gates.sh sets it from the NORMAL
//  run's own OFFENDERS: line, so the injected type is guaranteed to be one that currently PASSES
//  the bar even as the DSP moves underneath — a hardcoded target that later starts failing on its
//  own would quietly turn the control into a tautology.
static int injOverride()
{ const char* e = std::getenv ("TI_FLT_INJ"); return e ? std::atoi (e) : -1; }
static int INJ_LEAK = 5, INJ_STRESS = 5, INJ_LOOP = 5, INJ_SILENT = 5, INJ_MONO = 5,
           INJ_THD = 0, INJ_DUP = 5, INJ_OSC = 5, INJ_OPEN = 81, INJ_NAN = 5;

// ── extra probes the report does not need, built on the same Runner ──────────────────────────
// Broadband gain (dB): rms(out)/rms(in) over `sec` seconds after a quarter-length settle.
static double bandGainDb (Runner& R, float cut, float res, float drv, float rms, double sec,
                          bool* sawNan = nullptr, bool mute = false)
{
    const int N = (int) (sec * FS);
    R.resetState(); R.setP (cut, res, drv);
    rngS = 0x5bf03635u;
    double ai = 0, ao = 0;
    for (int i = 0; i < N; ++i)
    {
        const float x = rnd() * rms * 1.7320508f;
        R.setP (cut, res, drv);
        float a, b; R.step (x, x, a, b);
        if (! std::isfinite (a) || ! std::isfinite (b)) { if (sawNan) *sawNan = true; a = b = 0.0f; }
        if (mute) a = 0.0f;
        if (i > N / 4) { ai += (double) x * x; ao += (double) a * a; }
    }
    return 20.0 * std::log10 (std::max (1e-15, std::sqrt (ao)) / std::max (1e-15, std::sqrt (ai)));
}

// LOOP GAIN, measured as the SMALL-SIGNAL GROWTH RATE of the tail after a short burst.
//   A linear system with loop gain g decays (g<1) or grows (g>1) exponentially, so the sign of
//   dB/s IS the sign of (g - 1). The burst is -80 dBFS so every tanh in every core is still in
//   its linear region: a loop that only stops growing because it saturates still reads positive
//   here, which is the point — "it settles into a limit cycle" is not "the loop gain is < 1".
//   growth = 20*log10( rms(late window) / rms(early window) ) / (their separation in seconds).
static double growthDbPerSec (Runner& R, float cut, float res, float drv, double injPole = 0.0)
{
    const int burst = (int) (0.04 * FS), total = (int) (0.55 * FS);
    const int a0 = burst + (int) (0.06 * FS), a1 = burst + (int) (0.16 * FS);
    const int b0 = burst + (int) (0.36 * FS), b1 = burst + (int) (0.46 * FS);
    R.resetState(); R.setP (cut, res, drv);
    rngS = 0x77c0ffeeu;
    double sa = 0, sb = 0, inj = 0.0; int na = 0, nb = 0;
    for (int i = 0; i < total; ++i)
    {
        const float x = (i < burst) ? rnd() * 1.0e-4f : 0.0f;
        R.setP (cut, res, drv);
        float a, b; R.step (x, x, a, b);
        if (! std::isfinite (a)) a = 0.0f;
        if (injPole > 0.0) { inj = inj * injPole + (double) a; a = (float) inj; }
        if (i >= a0 && i < a1) { sa += (double) a * a; ++na; }
        if (i >= b0 && i < b1) { sb += (double) a * a; ++nb; }
    }
    const double ra = na ? std::sqrt (sa / na) : 0.0, rb = nb ? std::sqrt (sb / nb) : 0.0;
    if (ra < 1e-18 && rb < 1e-18) return -200.0;         // silent both windows: nothing to grow
    const double dt = (double) (b0 - a0) / FS;
    return 20.0 * std::log10 (std::max (1e-30, rb) / std::max (1e-30, ra)) / dt;
}

// The SAME window arithmetic, run on a synthetic tail of KNOWN growth. Printed every run so a
// "no runaway found" line can never be confused with an estimator that reads zero for anything.
static double calibrateGrowth (double perSampleGain)
{
    const int burst = (int) (0.04 * FS), total = (int) (0.55 * FS);
    const int a0 = burst + (int) (0.06 * FS), a1 = burst + (int) (0.16 * FS);
    const int b0 = burst + (int) (0.36 * FS), b1 = burst + (int) (0.46 * FS);
    rngS = 0x77c0ffeeu;
    double sa = 0, sb = 0, st = 0; int na = 0, nb = 0;
    for (int i = 0; i < total; ++i)
    {
        const double x = (i < burst) ? (double) rnd() * 1.0e-4 : 0.0;
        st = st * perSampleGain + x;
        if (i >= a0 && i < a1) { sa += st * st; ++na; }
        if (i >= b0 && i < b1) { sb += st * st; ++nb; }
    }
    const double ra = std::sqrt (sa / na), rb = std::sqrt (sb / nb), dt = (double) (b0 - a0) / FS;
    return 20.0 * std::log10 (std::max (1e-300, rb) / std::max (1e-300, ra)) / dt;
}

// THD of a 220 Hz sine at the real bus level, harmonics 2..10, phase-independent.
static double thdAt (Runner& R, float cut, float drv, double amp, bool forceDrv0)
{
    const int ns = (int) (0.3 * FS), nm = (int) (0.5 * FS);
    std::vector<float> y ((size_t) nm);
    const float useDrv = forceDrv0 ? 0.0f : drv;
    R.resetState(); R.setP (cut, 0.0f, useDrv);
    for (int i = 0; i < ns + nm; ++i)
    {
        const float x = (float) (amp * std::sin (2.0 * M_PI * 220.0 * i / FS));
        R.setP (cut, 0.0f, useDrv);
        float a, b; R.step (x, x, a, b);
        if (i >= ns) y[(size_t) (i - ns)] = std::isfinite (a) ? a : 0.0f;
    }
    const double h1 = ampAt (y, 220.0);
    double hs = 0;
    for (int h = 2; h <= 10; ++h) { const double a = ampAt (y, 220.0 * h); hs += a * a; }
    return (h1 > 1e-9) ? std::sqrt (hs) / h1 : 0.0;
}

static size_t gridIdx (double f)
{ size_t bi = 0; double bd = 1e9;
  for (size_t i = 0; i < GRID.size(); ++i) if (std::fabs (GRID[i] - f) < bd) { bd = std::fabs (GRID[i] - f); bi = i; }
  return bi; }
static double meanDb (const Curve& c, double f0, double f1)
{ double s = 0; int n = 0;
  for (size_t i = 0; i < GRID.size(); ++i) if (GRID[i] >= f0 && GRID[i] <= f1) { s += c.dB[i]; ++n; }
  return n ? s / n : -200.0; }
static double maxDb (const Curve& c, double f0, double f1)
{ double m = -1e9;
  for (size_t i = 0; i < GRID.size(); ++i) if (GRID[i] >= f0 && GRID[i] <= f1) m = std::max (m, c.dB[i]);
  return m; }

int main (int argc, char** argv)
{
    bool quick = false;
    for (int i = 1; i < argc; ++i) if (std::string (argv[i]) == "--quick") quick = true;
    if (injOverride() >= 0)
    { const int v = injOverride();
      INJ_LEAK = INJ_STRESS = INJ_LOOP = INJ_SILENT = INJ_MONO = INJ_THD = INJ_DUP = INJ_OSC
               = INJ_OPEN = INJ_NAN = v; }

    // A 12-type spine for the tight loop: one of every DSP core family, plus the types the
    // fb603 measurement found at the extremes of each bar.
    static const int SPINE[] = { 0, 3, 5, 9, 10, 23, 43, 48, 75, 83, 85, 91 };
    std::vector<int> T;
    if (quick) for (int i : SPINE) T.push_back (i);
    else       for (int i = 0; i < kNumTypes; ++i) T.push_back (i);
    const int NT = (int) T.size();

    std::printf ("\n══ fb603 · FILTER SECTION ACCEPTANCE GATE ══  %d of %d types%s\n",
                 NT, kNumTypes, quick ? "  (--quick spine)" : "");
    std::printf ("   MUTATION CONTROL TI_FLT_MUT=%s\n\n", MUT.empty() ? "(none)" : MUT.c_str());

    // ═════ MEASURE ════════════════════════════════════════════════════════════════════════════
    struct Res
    {
        int idx = 0;
        bool nan = false, built = false;
        double leak = 0; int leakPrior = (int) Type::DIODE_LP;   // [1] (never -1: it indexes kName)
        double stress = 0;                                   // [2]
        double loopMax = -200; float loopRes = 0, loopDrv = 0, loopCut = 0;   // [3a]
        double smallSigMax = -200, lvlSpread = 0; float ssRes = 0, ssDrv = 0;  // [3b]
        double knobMin = 0; std::string knobWhere;           // [4]
        double resPk[6] = { 0,0,0,0,0,0 }; double monoDrop = 0;               // [5]
        double thd0 = 0, thd1 = 0;                           // [6]
        double soRms = -200;                                 // [8]
        double openHf = 0, openMid = 0, openHfCore = 0;      // [9]
        bool osUsed = false;
        std::string shape;
        Curve cond[10];                                      // [7] + [5] + [9]
    };
    std::vector<Res> R ((size_t) NT);

    // The voice's own 2x wrapper (linear-interp up / box decimate) droops at the top whether a
    // filter is in it or not. Measure it ONCE with an identity filter so bar [9] can judge the
    // CORE rather than re-reporting the wrapper 26 times. The wrapper's cost is a real finding —
    // it is printed on [9] as its own line, attributed to the wrapper, not to the filter.
    double WRAPHF = 0.0;
    {
        Runner w; w.init ((int) Type::LADDER_LP24); w.bypassIdentity = true;
        Curve cl, cr; RunFlags fl;
        noiseCurve (w, 20000.0f, 0.0f, 0.0f, 0.002f, cl, cr, fl, 4, true);
        WRAPHF = meanDb (cl, 14000.0, 17000.0) - meanDb (cl, 700.0, 1400.0);
    }

    // the ten measured conditions — the SAME set Tests/flt_curves.csv carries, plus the two
    // extra RES steps the monotonicity bar needs.
    struct Cond { const char* key; float cut, res, drv; };
    static const Cond CD[10] = {
        { "cut100_res0",  100.0f, 0.0f,  0.0f }, { "cut1k_res0",   1000.0f, 0.0f,  0.0f },
        { "cut8k_res0",  8000.0f, 0.0f,  0.0f }, { "cut20k_res0", 20000.0f, 0.0f,  0.0f },
        { "cut1k_res025",1000.0f, 0.25f, 0.0f }, { "cut1k_res05",  1000.0f, 0.50f, 0.0f },
        { "cut1k_res075",1000.0f, 0.75f, 0.0f }, { "cut1k_res09",  1000.0f, 0.90f, 0.0f },
        { "cut1k_res10", 1000.0f, 1.00f, 0.0f }, { "cut1k_drv1",   1000.0f, 0.0f,  1.0f } };
    static const int RESCOND[6] = { 1, 4, 5, 6, 7, 8 };      // res 0, .25, .5, .75, .9, 1.0

    for (int ti = 0; ti < NT; ++ti)
    {
        const int t = T[(size_t) ti];
        Res& r = R[(size_t) ti]; r.idx = t;
        Runner rn; rn.init (t); r.built = true; r.osUsed = rn.os;
        RunFlags fl; Curve dummy;

        // ── the ten curves (LEFT only, 4 Welch segments — thresholds here are 0.5-3 dB) ──
        for (int c = 0; c < 10; ++c)
            noiseCurve (rn, CD[c].cut, CD[c].res, CD[c].drv, 0.002f, r.cond[c], dummy, fl, 4, true);
        r.nan = fl.nan;
        if (mut ("nan") && t == INJ_NAN) r.nan = true;
        if (mut ("mono") && t == INJ_MONO)
            for (auto& v : r.cond[6].dB) v -= 60.0;           // res 0.75 pushed below res 0.5.
            //  60 dB, not 6: a ladder climbs 45.7 dB in that ONE step (measured), so a small
            //  nudge leaves the curve still rising and the control silently reads "broken".
        if (mut ("open") && t == INJ_OPEN)
            for (size_t i = 0; i < GRID.size(); ++i) if (GRID[i] > 12000.0) r.cond[3].dB[i] -= 12.0;

        // shape, from the res-0 1 kHz curve — used to pick the THD cutoff and the [9] skip set
        {
            const double lo = meanDb (r.cond[1],   62.0,  126.0);
            const double at = meanDb (r.cond[1],  840.0, 1190.0);
            const double hi = meanDb (r.cond[1], 8000.0,16000.0);
            if      (lo - hi > 12.0) r.shape = "LP";
            else if (hi - lo > 12.0) r.shape = "HP";
            else if (at > lo + 6 && at > hi + 6) r.shape = "BP";
            else if (at < lo - 6 && at < hi - 6) r.shape = "NOTCH";
            else r.shape = "flat";
        }

        // ── [1] STATE LEAK: fresh vs after five priors, at two operating points ──────────────
        //    The known leak (DiodeLP::outMakeup, +22.28 dB) is a LEVEL leak, so a broadband gain
        //    probe sees it. Two settings are used so a leak that only shows in the shaping (a
        //    stale qMax / poleMakeup / morph) also moves at least one of them.
        {
            static const int PRIOR[5] = { (int) Type::DIODE_LP, (int) Type::LADDER_LP24,
                                          (int) Type::COMB_PLUS, (int) Type::SVF_LP24,
                                          (int) Type::REVERB_FILT };
            struct OP { float cut, res, drv, rms; };
            static const OP ops[2] = { { 1000.0f, 0.5f, 0.5f, 0.02f }, { 8000.0f, 0.9f, 0.0f, 0.02f } };
            for (int o = 0; o < 2; ++o)
            {
                Runner fresh; fresh.init (t);
                const double gF = bandGainDb (fresh, ops[o].cut, ops[o].res, ops[o].drv, ops[o].rms, 0.35);
                for (int p = 0; p < 5; ++p)
                {
                    if (PRIOR[p] == t) continue;
                    Runner rr; rr.init (PRIOR[p]);
                    rr.f.setParams (1000.0f, 0.5f, 0.5f, rr.coefSr);
                    for (int i = 0; i < 3000; ++i) { float a = 0.02f, b = 0.02f; rr.f.processStereo (a, b); }
                    rr.f.setType (static_cast<Type> (t));
                    rr.os = rr.f.needsOversampling(); rr.coefSr = rr.os ? FS * 2.0 : FS;
                    double gA = bandGainDb (rr, ops[o].cut, ops[o].res, ops[o].drv, ops[o].rms, 0.35);
                    if (mut ("leak") && t == INJ_LEAK && p == 0) gA += 3.0;
                    const double d = std::fabs (gA - gF);
                    if (d > r.leak) { r.leak = d; r.leakPrior = PRIOR[p]; }
                }
            }
        }

        // ── [2] STRESS PEAK: the report's own stability sweep, at the real bus level ─────────
        {
            const float lvl = mut ("stress") ? 0.05f : 0.05f;
            rngS = 0x1234567u; rn.resetState();
            double pk = 0;
            const int total = (int) (4.0 * FS);
            for (int i = 0; i < total; ++i)
            {
                const double ph = std::fmod (6.0 * i / FS, 1.0);
                const double c01 = 0.5 - 0.5 * std::cos (2.0 * M_PI * ph);
                rn.setP ((float) (20.0 * std::pow (1000.0, c01)), 1.0f, 1.0f);
                const float x = rnd() * lvl;
                float a, b; rn.step (x, x, a, b);
                if (! std::isfinite (a) || ! std::isfinite (b)) { r.nan = true; a = b = 0.0f; }
                pk = std::max (pk, (double) std::max (std::fabs (a), std::fabs (b)));
            }
            for (float cut : { 20.0f, 20000.0f })
            {
                rn.resetState(); rn.setP (cut, 1.0f, 1.0f);
                for (int i = 0; i < (int) (2.0 * FS); ++i)
                {
                    rn.setP (cut, 1.0f, 1.0f);
                    const float x = rnd() * lvl;
                    float a, b; rn.step (x, x, a, b);
                    if (! std::isfinite (a) || ! std::isfinite (b)) { r.nan = true; a = b = 0.0f; }
                    pk = std::max (pk, (double) std::max (std::fabs (a), std::fabs (b)));
                }
            }
            if (mut ("stress") && t == INJ_STRESS) pk *= 100.0;   // must clear 4.0 from ANY clean victim
            r.stress = pk;
        }

        // ── [3] LOOP GAIN over the RES x DRV plane, at two cutoffs ───────────────────────────
        {
            static const float RV[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 0.9f };
            static const float DV[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            for (float cut : { 200.0f, 4000.0f })
                for (float rv : RV)
                    for (float dv : DV)
                    {
                        const bool inj = mut ("loop") && t == INJ_LOOP && rv == 0.5f && dv == 0.5f;
                        const double g = growthDbPerSec (rn, cut, rv, dv, inj ? 1.0004 : 0.0);
                        if (g > r.loopMax) { r.loopMax = g; r.loopRes = rv; r.loopDrv = dv; r.loopCut = cut; }
                    }
        }

        // ── [3b] SMALL-SIGNAL GAIN — the OTHER shape a loop gain over 1 takes ───────────────
        //    A tail-growth probe only sees an EXPONENTIAL runaway. Bode Shifter and Scream do not
        //    grow forever; they have an enormous small-signal gain that a nonlinearity then bounds
        //    (+66.8 dB at -66 dBFS in, +25.5 dB at -26 dBFS in — measured). That is still a loop
        //    gain over 1, and it is still a filter that screams when you play quietly. So the bar
        //    also holds the SMALL-SIGNAL gain, and holds the filter to being LEVEL-INDEPENDENT:
        //    the same knobs must give the same gain whether the note is soft or loud.
        {
            //  ⚠️ MODERATE SETTINGS ONLY (res <= 0.5, drv <= 0.5). A ladder at RES 0.9 measures
            //  +62 dB of small-signal gain and that is the INSTRUMENT, not a runaway — the top
            //  of the RES knob is bar [8]'s business. The defect this bar exists for lives in
            //  the MIDDLE of both knobs: Bode Shifter +66.8 dB and Scream LP +59.8 dB at RES 0.5
            //  / DRV 0.5, where 10-50 % is supposed to be clean and musical. A threshold that
            //  also flagged every resonant ladder would be a bar nobody could ever make green.
            for (float rv : { 0.0f, 0.25f, 0.5f })
                for (float dv : { 0.0f, 0.25f, 0.5f })
                {
                    const double gq = bandGainDb (rn, 1000.0f, rv, dv, 0.0005f, 0.25);   // -66 dBFS
                    if (gq > r.smallSigMax) { r.smallSigMax = gq; r.ssRes = rv; r.ssDrv = dv; }
                }
            //  Level dependence is MEASURED and PRINTED but never asserted on: it cannot tell a
            //  Moog tanh saturating (which is the sound) from a loop that only stops growing
            //  because it clipped. Reported so a human can look, not used to fail anyone.
            {
                const float LV[4] = { 0.0005f, 0.005f, 0.05f, 0.2f };
                double lo = 1e9, hi = -1e9;
                for (int q = 0; q < 4; ++q)
                { const double g = bandGainDb (rn, 1000.0f, 0.5f, 0.5f, LV[q], 0.25);
                  lo = std::min (lo, g); hi = std::max (hi, g); }
                r.lvlSpread = hi - lo;
            }
            if (mut ("loop") && t == INJ_LOOP) r.smallSigMax += 40.0;
        }

        // ── [4] NO SILENT REGION ON ANY KNOB ─────────────────────────────────────────────────
        //    CUT is a frequency selector, so a low broadband number at an extreme cutoff is the
        //    SHAPE doing its job — it gets a true-silence floor (-90 dB) only. RES and DRV are
        //    not frequency selectors: at any position, with the cutoff open, sound must come
        //    out. That asymmetry is deliberate and is what catches Bit-Crush and Radio.
        {
            r.knobMin = 1e9;
            auto note = [&] (double g, const std::string& where)
            { if (g < r.knobMin) { r.knobMin = g; r.knobWhere = where; } };
            for (float cut : { 20.0f, 60.0f, 200.0f, 1000.0f, 4000.0f, 12000.0f, 20000.0f })
            {
                const double g = bandGainDb (rn, cut, 0.5f, 0.5f, 0.02f, 0.25);
                if (g < -90.0) note (g + 30.0, "CUT " + std::to_string ((int) cut) + " Hz (true-silence floor)");
            }
            for (float base : { 1000.0f, 20000.0f })
                for (float rv : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    const bool inj = mut ("silent") && t == INJ_SILENT && rv == 0.75f && base == 20000.0f;
                    const double g = bandGainDb (rn, base, rv, 0.5f, 0.02f, 0.25, nullptr, inj);
                    note (g, "RES " + std::to_string (rv).substr (0, 4) + " @ cut " + std::to_string ((int) base));
                }
            for (float base : { 1000.0f, 20000.0f })
                for (float dv : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                    note (bandGainDb (rn, base, 0.5f, dv, 0.02f, 0.25),
                          "DRV " + std::to_string (dv).substr (0, 4) + " @ cut " + std::to_string ((int) base));
        }

        // ── [5] RESONANCE MONOTONIC: peak gain in 0.5..2 x fc across the RES steps ───────────
        {
            //  Whole band, not 0.5..2 x fc. A formant's peak sits wherever the vowel puts it
            //  (Formant A resonates at 5.4 kHz with CUT at 1 kHz), so a window pinned to the
            //  cutoff measures the skirt of the wrong peak and reports a fold-back that is not
            //  there. The peak is the peak; find it where it is.
            for (int q = 0; q < 6; ++q) r.resPk[q] = maxDb (r.cond[RESCOND[q]], 20.0, 16000.0);
            for (int q = 0; q + 1 < 6; ++q)
                r.monoDrop = std::max (r.monoDrop, r.resPk[q] - r.resPk[q + 1]);
        }

        // ── [6] DRIVE IS A DRIVE ─────────────────────────────────────────────────────────────
        {
            const float fc = (r.shape == "HP")    ? 20.0f
                           : (r.shape == "BP")    ? 220.0f
                           : (r.shape == "NOTCH") ? 4000.0f : 20000.0f;
            r.thd0 = thdAt (rn, fc, 0.0f, 0.25, false);
            r.thd1 = thdAt (rn, fc, 1.0f, 0.25, mut ("thd") && t == INJ_THD);
        }

        // ── [8] SELF-OSCILLATION LEVEL ───────────────────────────────────────────────────────
        {
            rn.resetState(); rn.setP (1000.0f, 1.0f, 0.0f);
            const int burst = (int) (0.10 * FS), total = (int) (3.0 * FS);
            double acc = 0; int n = 0;
            rngS = 0x2545f491u;
            for (int i = 0; i < total; ++i)
            {
                const float x = (i < burst) ? rnd() * 0.05f : 0.0f;
                rn.setP (1000.0f, 1.0f, 0.0f);
                float a, b; rn.step (x, x, a, b);
                if (! std::isfinite (a)) { r.nan = true; a = 0.0f; }
                if (i > total - (int) (0.5 * FS)) { acc += (double) a * a; ++n; }
            }
            double rms = n ? std::sqrt (acc / n) : 0.0;
            if (mut ("osc") && t == INJ_OSC) rms *= 1e5;
            r.soRms = 20.0 * std::log10 (std::max (1e-12, rms));
        }

        // ── [9] WIDE OPEN ────────────────────────────────────────────────────────────────────
        r.openMid = meanDb (r.cond[3],   700.0,  1400.0);
        r.openHf  = meanDb (r.cond[3], 14000.0, 17000.0) - r.openMid;

        std::fprintf (stderr, "  measured %2d/%d %-20s\r", ti + 1, NT, kName[t]);
    }
    std::fprintf (stderr, "%-46s\r", "");

    //  The duplicate injection runs HERE, not inside the measurement loop, so its source is
    //  guaranteed to be a type this run actually measured — otherwise no PAIR can ever match and
    //  the control reads "broken" for a reason that has nothing to do with the detector.
    if (mut ("dup"))
    {
        int vict = -1, src = -1;
        for (int i = 0; i < NT; ++i) if (R[(size_t) i].idx == INJ_DUP) vict = i;
        for (int i = 0; i < NT; ++i) if (i != vict) { src = i; break; }
        if (vict >= 0 && src >= 0)
            for (int c = 0; c < 10; ++c) R[(size_t) vict].cond[c] = R[(size_t) src].cond[c];
    }

    // ═════ ASSERT ═════════════════════════════════════════════════════════════════════════════
    auto nm = [&] (const Res& r) { return std::string (kName[(size_t) r.idx]) + "(" + std::to_string (r.idx) + ")"; };

    // ── [0] ───────────────────────────────────────────────────────────────────────────────────
    {
        std::vector<int> bad;
        for (const auto& r : R) if (! r.built || r.nan) bad.push_back (r.idx);
        bar (bad.empty(), "[0] EVERY TYPE INSTANTIATES AND PASSES AUDIO — no NaN, no Inf",
             bad.empty() ? std::to_string (NT) + "/" + std::to_string (NT) + " types built and processed clean"
                         : "NaN / failed to build: " + join (bad));
        detector ("types", (int) bad.size());
        std::printf ("        mutation target: %s (TI_FLT_MUT=nan)\n", kName[INJ_NAN]);
    }

    // ── [1] ───────────────────────────────────────────────────────────────────────────────────
    {
        std::vector<int> bad; std::string worst; double wv = -1;
        for (const auto& r : R)
        {
            if (r.leak > 0.5) bad.push_back (r.idx);
            if (r.leak > wv) { wv = r.leak; worst = nm (r) + " " + std::to_string (r.leak).substr (0, 5)
                                    + " dB after " + kName[(size_t) r.leakPrior]; }
        }
        bar (bad.empty(), "[1] NO STATE LEAK — a type's level does not depend on what ran before it",
             bad.empty() ? "largest carry-over across 94 x 5 priors x 2 operating points: " + worst
                         : "LEAKING (> 0.5 dB): " + join (bad) + "   worst " + worst);
        detector ("types", (int) bad.size());
        std::printf ("        mutation target: %s (TI_FLT_MUT=leak)\n", kName[INJ_LEAK]);
    }

    // ── [2] ───────────────────────────────────────────────────────────────────────────────────
    {
        std::vector<int> bad; double wv = 0; std::string worst;
        for (const auto& r : R)
        { if (r.stress > 4.0) bad.push_back (r.idx);
          if (r.stress > wv) { wv = r.stress; worst = nm (r); } }
        char d[240];
        std::snprintf (d, sizeof d, "%s   highest peak %.2f (%s) on a 6 Hz cutoff sweep, res 1 / drive 1, in-rms 0.05",
                       bad.empty() ? "every type stayed under 4.0" : ("OVER 4.0: " + join (bad)).c_str(),
                       wv, worst.c_str());
        bar (bad.empty(), "[2] STRESS PEAK <= 4.0 — no engine can hand the voice bus +12 dBFS", d);
        detector ("types", (int) bad.size());
        std::printf ("        mutation target: %s (TI_FLT_MUT=stress)\n", kName[INJ_STRESS]);
    }

    // ── [3] ───────────────────────────────────────────────────────────────────────────────────
    {
        const double calHot = calibrateGrowth (1.00005), calCold = calibrateGrowth (0.9999);
        std::vector<int> bad; double wv = -200; std::string worst;
        for (const auto& r : R)
        {
            if (r.loopMax > 3.0) bad.push_back (r.idx);
            if (r.loopMax > wv)
            { wv = r.loopMax; char b[160];
              std::snprintf (b, sizeof b, "%s %+.1f dB/s at RES %.2f / DRV %.2f / cut %.0f Hz",
                             nm (r).c_str(), r.loopMax, r.loopRes, r.loopDrv, r.loopCut);
              worst = b; }
        }
        char d[300];
        std::snprintf (d, sizeof d, "%s   worst: %s",
                       bad.empty() ? "every type decays at every point of the 5 x 5 x 2 plane"
                                   : ("LOOP GAIN >= 1 (tail GROWS at -80 dBFS): " + join (bad)).c_str(),
                       worst.c_str());
        bar (bad.empty(), "[3] LOOP GAIN < 1.0 ACROSS THE RES x DRV PLANE (res <= 0.9) — taper, not a limiter", d);
        detector ("types", (int) bad.size());
        std::printf ("        estimator calibration (same window arithmetic, synthetic tails):\n"
                     "          pole 1.00005 (gain > 1) reads %+.1f dB/s — must be POSITIVE: %s\n"
                     "          pole 0.9999  (gain < 1) reads %+.1f dB/s — must be NEGATIVE: %s\n",
                     calHot, calHot > 3.0 ? "ok" : "*** ESTIMATOR IS BLIND ***",
                     calCold, calCold < -3.0 ? "ok" : "*** ESTIMATOR IS BLIND ***");
        if (! (calHot > 3.0 && calCold < -3.0))
            bar (false, "[3c] THE LOOP-GAIN ESTIMATOR ITSELF IS CALIBRATED", "the two-sided synthetic check failed");
        std::printf ("        mutation target: %s (TI_FLT_MUT=loop)\n", kName[INJ_LOOP]);
    }
    {
        std::vector<int> bad; double wv = -200; std::string worst;
        for (const auto& r : R)
        {
            if (r.smallSigMax > 24.0) bad.push_back (r.idx);
            if (r.smallSigMax > wv)
            { wv = r.smallSigMax; char b[220];
              std::snprintf (b, sizeof b, "%s %+.1f dB at RES %.1f / DRV %.1f, and %.1f dB of level dependence",
                             nm (r).c_str(), r.smallSigMax, r.ssRes, r.ssDrv, r.lvlSpread);
              worst = b; }
        }
        bar (bad.empty(), "[3b] SMALL-SIGNAL GAIN <= +24 dB AT MODERATE SETTINGS — the other shape of loop gain > 1",
             (bad.empty() ? "highest small-signal gain on the plane: "
                          : ("RUNS AWAY AT LOW LEVEL: " + join (bad) + "   worst: ")) + worst);
        detector ("types", (int) bad.size());
        {   // the report's own LEVEL-GATED list, printed beside the bar so the two agree
            std::vector<int> lg;
            for (const auto& r : R) if (r.lvlSpread > 12.0) lg.push_back (r.idx);
            std::printf ("        level dependence > 12 dB across a 52 dB input range, REPORTED NOT ASSERTED\n"
                         "        (a saturating ladder and a runaway look the same to this number):\n          %s\n",
                         lg.empty() ? "(none)" : join (lg).c_str());
        }
        std::printf ("        note: a tail-growth probe alone cannot see this. Bode Shifter / Scream do not\n"
                     "              grow without bound - they have a huge small-signal gain that a\n"
                     "              nonlinearity then bounds, so the tail looks stable while a soft note\n"
                     "              comes out 50 dB louder than a loud one. Both probes, or neither.\n");
    }

    // ── [4] ───────────────────────────────────────────────────────────────────────────────────
    {
        std::vector<int> bad, sk; double wv = 1e9; std::string worst;
        for (const auto& r : R)
        {
            if (r.idx == (int) Type::NONE) { sk.push_back (r.idx); continue; }
            if (r.knobMin < -60.0) bad.push_back (r.idx);
            if (r.knobMin < wv) { wv = r.knobMin; char b[200];
                std::snprintf (b, sizeof b, "%s %.1f dB at %s", nm (r).c_str(), r.knobMin, r.knobWhere.c_str());
                worst = b; }
        }
        bar (bad.empty(), "[4] NO SILENT REGION ON ANY KNOB — RES and DRV never mute the engine",
             (bad.empty() ? "quietest knob position anywhere: " : ("SILENT (< -60 dB): " + join (bad) + "   worst: "))
             + worst);
        detector ("types", (int) bad.size());
        skipline (join (sk), "'None' is a bypass — it has no knobs to be silent on.");
        std::printf ("        note: CUT is a frequency selector, so it is held only to a -90 dB true-silence\n"
                     "              floor; RES and DRV are held to -60 dB with the cutoff open.\n");
        std::printf ("        mutation target: %s (TI_FLT_MUT=silent)\n", kName[INJ_SILENT]);
    }

    // ── [5] ───────────────────────────────────────────────────────────────────────────────────
    {
        std::vector<int> bad, inert, sk; double wv = 0; std::string worst;
        for (const auto& r : R)
        {
            //  The shelf/bell family reads RES as a GAIN that sweeps cut -> flat -> boost. A
            //  gain that passes through zero is a V, and a V is non-monotonic BY DESIGN. That is
            //  a statement about what the knob IS on those engines, not about the DSP.
            static const std::set<int> RES_IS_NOT_RESONANCE = {
                (int) Type::TILT, (int) Type::LOW_EQ, (int) Type::HIGH_EQ,
                (int) Type::BAND_EQ, (int) Type::AIR };
            if (r.idx == (int) Type::NONE || r.idx == (int) Type::DIFFUSOR
                || RES_IS_NOT_RESONANCE.count (r.idx)) { sk.push_back (r.idx); continue; }
            double span = -1e9, lo = 1e9;
            for (double v : r.resPk) { span = std::max (span, v); lo = std::min (lo, v); }
            if (span - lo < 1.0) { inert.push_back (r.idx); continue; }     // no RES signature: see below
            if (r.monoDrop > 1.0) bad.push_back (r.idx);
            if (r.monoDrop > wv)
            { wv = r.monoDrop; char b[220];
              std::snprintf (b, sizeof b, "%s  %.1f / %.1f / %.1f / %.1f / %.1f / %.1f dB (RES 0 .25 .5 .75 .9 1.0)",
                             nm (r).c_str(), r.resPk[0], r.resPk[1], r.resPk[2], r.resPk[3], r.resPk[4], r.resPk[5]);
              worst = b; }
        }
        bar (bad.empty(), "[5] RESONANCE IS MONOTONIC IN RES — turning it up never turns it down",
             (bad.empty() ? "every type rises or holds at every step.  largest single step-down: "
                          : ("FOLDS BACK (> 1.0 dB step down): " + join (bad) + "   worst: ")) + worst);
        detector ("types", (int) bad.size());
        skipline (join (sk),
                  "None is a bypass. Diffusor is a STRUCTURAL blind spot: an allpass chain has unity "
                  "magnitude by construction, so no magnitude metric can see what its RES does — that "
                  "skip is permanent and is not a defect. Tilt / Low EQ / High EQ / Band EQ / Air read "
                  "RES as a shelf or bell GAIN that sweeps cut -> flat -> boost, so their curve is a V "
                  "through zero: non-monotonic by design, and 'monotonic' is the wrong question there.");
        if (! inert.empty())
            std::printf ("        RES HAS NO MAGNITUDE SIGNATURE (< 1.0 dB across the whole knob) — reported,\n"
                         "        not passed, because 'monotonic' is vacuous for a knob that does nothing:\n"
                         "          %s\n", join (inert).c_str());
        std::printf ("        mutation target: %s (TI_FLT_MUT=mono)\n", kName[INJ_MONO]);
    }

    // ── [6] ───────────────────────────────────────────────────────────────────────────────────
    {
        static const std::set<int> THD_BLIND = { (int) Type::RING_MOD, (int) Type::BODE_SHIFT,
                                                 (int) Type::BODE_DOWN, (int) Type::RING_X2 };
        std::vector<int> bad, sk; double wv = 1e9; std::string best;
        for (const auto& r : R)
        {
            if (THD_BLIND.count (r.idx) || r.idx == (int) Type::NONE) { sk.push_back (r.idx); continue; }
            const double d = 100.0 * (r.thd1 - r.thd0);
            if (d <= 1.0) bad.push_back (r.idx);
            if (d < wv) { wv = d; char b[200];
                std::snprintf (b, sizeof b, "%s  THD %.2f%% -> %.2f%%", nm (r).c_str(), 100.0 * r.thd0, 100.0 * r.thd1);
                best = b; }
        }
        bar (bad.empty(), "[6] DRIVE IS A DRIVE — THD moves more than 1 percentage point from DRV 0 to DRV 1",
             (bad.empty() ? "every character type distorts more with the knob up.  weakest: "
                          : ("DRIVE DOES NOT DISTORT: " + join (bad) + "   weakest: ")) + best);
        detector ("types", (int) bad.size());
        skipline (join (sk),
                  "THD is MEANINGLESS for the frequency-shifting / ring types: they move energy between "
                  "bins, so 'harmonics of 220 Hz' measures the shift, not distortion. 'None' is a bypass.");
        std::printf ("        note: a DRIVE that is really a Q or a level trim fails here on purpose —\n"
                     "              +12.0 dB with THD flat at 0.00%% is a volume knob wearing a drive's label.\n");
        std::printf ("        mutation target: %s (TI_FLT_MUT=thd)\n", kName[INJ_THD]);
    }

    std::set<int> dupFlagged;
    // ── [7] ───────────────────────────────────────────────────────────────────────────────────
    {
        //  Two rules, because a duplicate can hide behind a level offset: Xpd BP 12 and Xpd BP 6
        //  differ by 4.08 dB with a 0.01 dB spread — a pure "max deviation" test calls that
        //  distinct and it is the SAME FILTER with a trim on it.
        std::vector<std::string> dups; std::set<int> flagged;
        double closest = 1e9; std::string closestPair;
        for (int a = 0; a < NT; ++a)
            for (int b = a + 1; b < NT; ++b)
            {
                double absDev = 0, hi = -1e9, lo = 1e9;
                for (int c = 0; c < 10; ++c)
                    for (size_t i = 0; i < GRID.size(); ++i)
                    {
                        if (GRID[i] > 16000.0) continue;
                        const double d = R[(size_t) a].cond[c].dB[i] - R[(size_t) b].cond[c].dB[i];
                        absDev = std::max (absDev, std::fabs (d));
                        hi = std::max (hi, d); lo = std::min (lo, d);
                    }
                const double shapeDev = 0.5 * (hi - lo);         // minimax offset removed
                if (absDev < closest) { closest = absDev;
                    closestPair = std::string (kName[(size_t) R[(size_t)a].idx]) + "(" + std::to_string (R[(size_t)a].idx)
                                + ") vs " + kName[(size_t) R[(size_t)b].idx] + "(" + std::to_string (R[(size_t)b].idx) + ")"; }
                if (absDev < 3.0 || shapeDev < 0.5)
                {
                    char s[220];
                    std::snprintf (s, sizeof s, "%s(%d) == %s(%d)   max-dev %.2f dB, %.2f dB after level-match%s",
                        kName[(size_t) R[(size_t)a].idx], R[(size_t)a].idx,
                        kName[(size_t) R[(size_t)b].idx], R[(size_t)b].idx, absDev, shapeDev,
                        (absDev >= 3.0) ? "  (SAME FILTER + A TRIM)" : "");
                    dups.emplace_back (s);
                    flagged.insert (R[(size_t)a].idx); flagged.insert (R[(size_t)b].idx);
                    dupFlagged.insert (R[(size_t)a].idx); dupFlagged.insert (R[(size_t)b].idx);
                }
            }
        char d[280];
        std::snprintf (d, sizeof d, "%s   closest surviving pair: %s at %.2f dB",
                       dups.empty() ? "every one of the pairs differs by 3 dB or more"
                                    : (std::to_string (dups.size()) + " duplicate pair(s)").c_str(),
                       closestPair.c_str(), closest);
        bar (dups.empty(), "[7] NO DUPLICATE ENGINES — every pair differs by >= 3 dB in the LEFT channel", d);
        for (const auto& s : dups) std::printf ("          %s\n", s.c_str());
        detector ("types involved", (int) flagged.size());
        std::printf ("        note: LEFT channel by design — the Comb Wide/Octave/Fifth retunes touch the\n"
                     "              RIGHT channel only, so in mono they collapse onto Comb + and a\n"
                     "              stereo-averaged test would never see it.\n");
        std::printf ("        mutation target: %s (TI_FLT_MUT=dup)\n", kName[INJ_DUP]);
    }

    // ── [8] ───────────────────────────────────────────────────────────────────────────────────
    {
        std::vector<int> bad; double wv = -200; std::string worst; int nOsc = 0;
        for (const auto& r : R)
        {
            if (r.soRms > -60.0) ++nOsc;
            if (r.soRms > 0.0) bad.push_back (r.idx);
            if (r.soRms > wv) { wv = r.soRms; char b[160];
                std::snprintf (b, sizeof b, "%s %+.1f dBFS", nm (r).c_str(), r.soRms); worst = b; }
        }
        bar (bad.empty(), "[8] SELF-OSCILLATION STAYS BELOW FULL SCALE — oscillating is wanted, clipping is not",
             (bad.empty() ? "loudest sustained tail: " : ("ABOVE 0 dBFS PER VOICE: " + join (bad) + "   loudest: "))
             + worst + "   (" + std::to_string (nOsc) + " types sustain at all)");
        detector ("types", (int) bad.size());
        std::printf ("        note: this bar does NOT forbid self-oscillation. A ladder that screams at\n"
                     "              RES 1.0 is the instrument; one that screams at +5.1 dBFS per voice is\n"
                     "              a taper that ran out of room.\n");
        std::printf ("        mutation target: %s (TI_FLT_MUT=osc)\n", kName[INJ_OSC]);
    }

    // ── [9] ───────────────────────────────────────────────────────────────────────────────────
    {
        //  The shipped default cutoff is 20 kHz. A lowpass or flat engine parked there is what
        //  the user hears on a FRESH PATCH before touching anything, so it has to be open.
        static const std::set<int> NOT_A_CORNER = {
            14,15,16,17,68,69,70,71,      // formant — CUT is a vowel shift
            18,26,92,93,                  // reverb  — CUT is a tank rate
            13,66,67,                     // karplus — CUT is a pitch
            25,                           // grain   — CUT is a grain rate
            21,22,76,90,                  // freq shift / ring
            27 };                         // None
        std::vector<int> bad, sk; double wv = 0; std::string worst;
        for (const auto& r : R)
        {
            if (NOT_A_CORNER.count (r.idx)) { sk.push_back (r.idx); continue; }
            if (r.shape == "HP" || r.shape == "BP" || r.shape == "NOTCH") { sk.push_back (r.idx); continue; }
            const double core = r.openHf - (r.osUsed ? WRAPHF : 0.0);
            if (core < -3.0) bad.push_back (r.idx);
            if (core < wv) { wv = core; char b[200];
                std::snprintf (b, sizeof b, "%s %.1f dB at 14-17 kHz%s", nm (r).c_str(), core,
                               r.osUsed ? " (2x-wrapper droop already removed)" : ""); worst = b; }
        }
        bar (bad.empty(), "[9] WIDE OPEN IS OPEN — at the shipped 20 kHz default, 16 kHz survives",
             (bad.empty() ? "flattest-to-worst at the top: " : ("ROLLED OFF AT THE DEFAULT CUTOFF: " + join (bad)
                            + "   worst: ")) + worst);
        detector ("types", (int) bad.size());
        skipline (join (sk),
                  "HP / BP / NOTCH shapes are SUPPOSED to cut at 16 kHz when parked at 20 kHz, and for the "
                  "formant / reverb / karplus / grain / freq-shift families CUT is not a corner at all.");
        std::printf ("        the voice's 2x wrapper costs a further %.2f dB at 14-17 kHz on EVERY oversampled\n"
                     "        type, filter or not (measured with an identity filter in the same path). That is\n"
                     "        subtracted above so this bar judges the FILTER; it is still %.2f dB the user hears.\n",
                     WRAPHF, WRAPHF);
        std::printf ("        mutation target: %s (TI_FLT_MUT=open)\n", kName[INJ_OPEN]);
    }

    // ═════ SUMMARY ════════════════════════════════════════════════════════════════════════════
    std::printf ("\n  %d passed, %d FAILED\n", gPass, gFail);
    if (! gFailed.empty())
    {
        std::printf ("  FAILED BARS:\n");
        for (const auto& b : gFailed) std::printf ("    - %s\n", b.c_str());
    }
    // machine-readable, for fb603_filter_gates.sh's mutation check
    std::printf ("\nBARS pass=%d fail=%d\n", gPass, gFail);
    //  The machine line has to honour the SAME skips the bars do, or the runner reads a skipped
    //  type as an offender and the two halves of this file disagree with each other.
    static const std::set<int> SK_MONO = { (int) Type::NONE, (int) Type::DIFFUSOR, (int) Type::TILT,
                                           (int) Type::LOW_EQ, (int) Type::HIGH_EQ,
                                           (int) Type::BAND_EQ, (int) Type::AIR };
    static const std::set<int> SK_THD  = { (int) Type::RING_MOD, (int) Type::BODE_SHIFT,
                                           (int) Type::BODE_DOWN, (int) Type::RING_X2,
                                           (int) Type::NONE };
    static const std::set<int> SK_OPEN = { 14,15,16,17,68,69,70,71, 18,26,92,93, 13,66,67, 25,
                                           21,22,76,90, 27 };
    std::printf ("OFFENDERS:");
    for (const auto& r : R)
    {
        std::string f;
        if (r.nan)            f += "nan,";
        if (r.leak > 0.5)     f += "leak,";
        if (r.stress > 4.0)   f += "stress,";
        if (r.loopMax > 3.0)  f += "loop,";
        if (r.smallSigMax > 24.0) f += "loop,";
        if (r.knobMin < -60 && r.idx != (int) Type::NONE) f += "silent,";
        if (r.monoDrop > 1.0 && ! SK_MONO.count (r.idx)) f += "mono,";
        if (100.0 * (r.thd1 - r.thd0) <= 1.0 && ! SK_THD.count (r.idx)) f += "thd,";
        if (r.soRms > 0.0)    f += "osc,";
        if (dupFlagged.count (r.idx)) f += "dup,";
        if (r.openHf - (r.osUsed ? WRAPHF : 0.0) < -3.0 && ! SK_OPEN.count (r.idx)
            && r.shape != "HP" && r.shape != "BP" && r.shape != "NOTCH") f += "open,";
        if (! f.empty()) std::printf (" %d:%s", r.idx, f.c_str());
    }
    std::printf ("\n");
    //  Which types each bar SKIPS, so Tests/fb603_filter_gates.sh never injects a fault into a
    //  type that bar ignores by design. (It did: the [9] control landed on Ladder HP 24, an HP
    //  shape [9] skips because an HP is SUPPOSED to cut at 16 kHz, and read "broken" for a
    //  reason that had nothing to do with the detector.)
    std::printf ("SKIPPED:");
    for (const auto& r : R)
    {
        std::string f;
        if (r.idx == (int) Type::NONE) f += "silent,thd,mono,open,";
        else {
            if (SK_MONO.count (r.idx)) f += "mono,";
            if (SK_THD.count  (r.idx)) f += "thd,";
            if (SK_OPEN.count (r.idx) || r.shape == "HP" || r.shape == "BP" || r.shape == "NOTCH")
                f += "open,";
        }
        if (! f.empty()) std::printf (" %d:%s", r.idx, f.c_str());
    }
    std::printf ("\n\n");
    return gFail ? 1 : 0;
}
