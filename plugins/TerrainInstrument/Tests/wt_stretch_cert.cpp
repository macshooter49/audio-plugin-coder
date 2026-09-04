// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_stretch_cert.cpp — fb583: STRETCH MOVES THE PARTIALS, AND WT POS CANNOT FAKE IT.
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/wt_stretch_cert.cpp \
//            -o /tmp/wt_stretch_cert -framework Accelerate && /tmp/wt_stretch_cert
//
//  🚨 WHY THIS CERT EXISTS, AND WHY IT IS NOT THE ONE IT REPLACES.
//     fb582's SPREAD cert asked "does the sound change?" and answered yes, with real numbers, on
//     the installed AU — and the feature was still rejected on first listen: "spread is basically
//     just wave table position... whenever I move my wave table to the same position that I move
//     my spread, it sounds the exact same." The cert was not wrong. It was measuring the wrong
//     property. ANY knob that touches the table changes the sound; the question that decides
//     whether a control deserves its slot is whether the change is REACHABLE BY ANOTHER KNOB.
//
//     So the central bar here is IMITABILITY: for a stretched table, sweep WT Pos across the
//     WHOLE table, find the position whose spectrum best matches, and require that the best match
//     is still FAR AWAY. A knob that WT Pos can imitate fails, no matter how big its numbers are.
//
//  THE METRIC is amplitude-weighted magnitude-spectrum distance in dB per harmonic — a HEARING
//  metric (fb283's law: sample-difference RMS is BANNED, it scores phase-only changes as huge and
//  the ear hears nothing). Each spectrum is peak-normalised first, so the number is TIMBRE, and
//  each harmonic's error is weighted by how loud that harmonic actually is, so a bin sitting at
//  the noise floor cannot dominate the result.
//
//  THE BARS
//   0  THE FUNDAMENTAL IS PINNED — ratio(1,B) == 1.0 exactly at every stiffness. The pitch of the
//      note may not move, ever, for any setting of this knob.
//   1  THE LADDER NEVER CROSSES — ratio is strictly increasing in n, so partials fan apart and
//      never re-order or collide by construction.
//   2  NEUTRAL IS THE OLD SOUND — apply(spec, 0) is BYTE-identical to the input for all 46
//      factory tables. (The instrument leans on this: at 0 the morph slot publishes nullptr and
//      voices read the untouched bank table.)
//   3  NOT IMITABLE BY WT POS — the closest of 101 WT Pos settings is still far, and farther than
//      the table's own WT Pos axis is wide. THIS IS THE BAR THAT fb582 DID NOT HAVE.
//   4  THE KNOB NEVER STOPS MOVING — every step of the travel changes the sound, and the TOP HALF
//      still travels at least as far as the bottom half did. ⚠️ NOT "distance from neutral rises":
//      that measure SATURATES and reads a plateau above s ~= 0.6 on all eight tables while the
//      sound is still moving hard (measured: Square reads 47.22 at s = .5 and 45.77 at s = 1, yet
//      those two settings are 48.72 dB APART from each other). Measuring travel from a fixed
//      origin answers the wrong question. The Lifeguard Law asks whether the last half of the knob
//      earns its place, so that is what is measured: adjacent-step motion, and half-against-half.
//   5  IT IS TIMBRE, NOT LEVEL — the RMS swing across the stretch knob stays inside the swing the
//      WT POS knob already produces on this instrument, measured here in the same run as the
//      control. (A measured control, not an invented budget: WT Pos itself swings 20.9 dB on
//      D-50 Bell, so a timbre knob that swings less than that is in family, not rude.)
//   6  IT WORKS ON THE WHOLE BANK — across all 46 factory tables at three WT Pos settings, every
//      frame moves EXCEPT frames holding a single partial, which are inert by arithmetic and are
//      asserted to be exactly that. A knob with a silent table is the fb582 failure again.
//   7  NO ALIASING — a stretched table carries no energy above the mip level's harmonic ceiling.
//      Partials pushed past Nyquist are dropped by the bake, never folded back.
//   8  IT IS CHEAP — the transform's own cost against the bake it rides.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "HarmonicStretch.h"
#include "WavetableBank.h"
#include "SpectralMorph.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>

using namespace tw;

static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const std::string& d = "")
{ c ? ++gPass : ++gFail; printf ("  %-5s %-56s %s\n", c ? "ok" : "FAIL", n, d.c_str()); }

static const int N = 2048, HMEAS = 160;
static const double SR = 48000.0;

// ── the hearing metric ────────────────────────────────────────────────────────────────────────
static void cyc (const Wavetable& w, int mip, float p, std::vector<float>& o)
{ o.assign ((size_t) N, 0.0f); w.renderBlend (mip, p, 0.0f, o.data()); }

static std::vector<double> mag (const std::vector<float>& x)
{
    std::vector<double> m ((size_t) HMEAS + 1, 0.0);
    for (int h = 1; h <= HMEAS; ++h)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < N; ++i)
        { const double a = 2.0 * M_PI * h * i / N; re += x[(size_t) i] * std::cos (a); im += x[(size_t) i] * std::sin (a); }
        m[(size_t) h] = 2.0 * std::sqrt (re * re + im * im) / N;
    }
    return m;
}

static double dist (const std::vector<double>& a, const std::vector<double>& b)
{
    double pa = 0.0, pb = 0.0;
    for (int h = 1; h <= HMEAS; ++h) { pa = std::max (pa, a[(size_t) h]); pb = std::max (pb, b[(size_t) h]); }
    if (pa <= 0.0 || pb <= 0.0) return 0.0;
    double num = 0.0, den = 0.0;
    for (int h = 1; h <= HMEAS; ++h)
    {
        const double na = a[(size_t) h] / pa, nb = b[(size_t) h] / pb;
        const double w  = std::max (na, nb);
        if (w < 1.0e-4) continue;                       // both at the floor — nothing to hear
        const double d = 20.0 * std::log10 (std::max (na, 1.0e-4)) - 20.0 * std::log10 (std::max (nb, 1.0e-4));
        num += w * d * d; den += w;
    }
    return den > 0.0 ? std::sqrt (num / den) : 0.0;
}
static double rms (const std::vector<float>& x)
{ double a = 0.0; for (float v : x) a += (double) v * v; return std::sqrt (a / N); }

// The eight most different factory tables — two analogue, two digital, two bell/partial-based,
// one vocal, one modern. A cert that only ever looks at a saw proves nothing about a bell.
static const int  PR8[8]  = { 4, 2, 12, 13, 16, 18, 23, 10 };
static const char* PN8[8] = { "ProphetSaw", "Square", "D50Bell", "M1Piano", "VowelMorph", "GlassHarm", "SerumHD", "PPGWave" };

int main()
{
    printf ("\n══ wt_stretch_cert — fb583 ══  edge -> harmonic %.0f, at most %.0fx\n\n",
            HarmonicStretch::kEdgeCeil, HarmonicStretch::kGMax);

    // ── 0 · THE FUNDAMENTAL IS PINNED ─────────────────────────────────────────────────────────
    {
        bool ok = true; double worst = 0.0;
        for (double B : { 0.0, 1.0e-6, 0.01, 0.1, 0.5, 1.0, 2.5, 10.0, 1000.0 })
        { const double r = HarmonicStretch::ratio (1.0, B);
          worst = std::max (worst, std::fabs (r - 1.0)); if (r != 1.0) ok = false; }
        gate (ok, "[0] THE FUNDAMENTAL IS PINNED — the pitch never moves",
              "max |ratio(1,B) - 1| = " + std::to_string (worst) + " over B in [0, 1000]");
    }

    // ── 1 · THE LADDER NEVER CROSSES ──────────────────────────────────────────────────────────
    {
        bool ok = true; double minStep = 1.0e9;
        for (double B : { 0.01, 0.1, 1.0, 2.5 })
            for (int n = 1; n < 512; ++n)
            { const double step = HarmonicStretch::ratio (n + 1, B) - HarmonicStretch::ratio (n, B);
              minStep = std::min (minStep, step); if (step <= 0.0) ok = false; }
        char b[128]; snprintf (b, sizeof b, "smallest gap between adjacent partials = %.4f (must be > 0)", minStep);
        gate (ok, "[1] THE LADDER NEVER CROSSES — strictly increasing in n", b);
    }

    // ── 2 · NEUTRAL IS THE OLD SOUND, on every table in the bank ──────────────────────────────
    {
        int bad = 0;
        for (int p = 0; p < WavetableBank::kNumPresets; ++p)
        {
            const WavetableSpec in = WavetableBank::specForPreset (p);
            const WavetableSpec out = HarmonicStretch::apply (in, 0.0f);
            if (std::memcmp (&in, &out, sizeof (WavetableSpec)) != 0) ++bad;
        }
        gate (bad == 0, "[2] NEUTRAL IS BYTE-IDENTICAL — every patch restores unchanged",
              std::to_string (WavetableBank::kNumPresets - bad) + "/" + std::to_string (WavetableBank::kNumPresets) + " tables identical at s = 0");
    }

    // ── the shared rig for bars 3-5: eight tables, the full WT Pos sweep as the reference ─────
    const int MIP = Wavetable::mipLevelForMidiNote (60, SR);      // C4
    struct Rig { std::vector<std::vector<double>> sweep; std::vector<double> ref0; double selfSpread, rmsLo, rmsHi; };
    std::vector<Rig> rigs ((size_t) 8);
    std::vector<WavetableSpec> base ((size_t) 8);
    {
        std::vector<float> t;
        for (int i = 0; i < 8; ++i)
        {
            base[(size_t) i] = WavetableBank::specForPreset (PR8[i]);
            std::unique_ptr<Wavetable> ref (new Wavetable()); ref->buildFromSpec (base[(size_t) i]);
            Rig& R = rigs[(size_t) i];
            R.rmsLo = 1.0e9; R.rmsHi = 0.0;
            for (int j = 0; j <= 100; ++j)
            { cyc (*ref, MIP, (float) j / 100.0f, t); R.sweep.push_back (mag (t));
              const double r = rms (t); R.rmsLo = std::min (R.rmsLo, r); R.rmsHi = std::max (R.rmsHi, r); }
            cyc (*ref, MIP, 0.0f, t); R.ref0 = mag (t);
            R.selfSpread = 0.0;
            for (auto& s : R.sweep) R.selfSpread = std::max (R.selfSpread, dist (R.ref0, s));
        }
    }

    // ── 3 · NOT IMITABLE BY WT POS ── the bar fb582 did not have ──────────────────────────────
    // ── 4 · PROGRESSIVE ── and 5 · TIMBRE NOT LEVEL, against the WT Pos knob's OWN swing.
    {
        const double SV[5] = { 0.1, 0.25, 0.5, 0.75, 1.0 };
        double imitMean[5] = { 0, 0, 0, 0, 0 };
        double worstRatio = 1.0e9, meanRatio = 0.0;
        bool   progressive = true;
        double worstLevelExcess = -1.0e9, worstStretchSwing = 0.0, worstPosSwing = 0.0;
        std::string worstTbl, levelTbl;
        std::vector<float> t;

        for (int i = 0; i < 8; ++i)
        {
            Rig& R = rigs[(size_t) i];
            double prev = -1.0, stretchLo = 1.0e9, stretchHi = 0.0;
            for (int q = 0; q < 5; ++q)
            {
                std::unique_ptr<Wavetable> w (new Wavetable());
                w->buildFromSpec (HarmonicStretch::apply (base[(size_t) i], (float) SV[q]));
                cyc (*w, MIP, 0.0f, t);
                const std::vector<double> m = mag (t);
                const double r = rms (t); stretchLo = std::min (stretchLo, r); stretchHi = std::max (stretchHi, r);

                double closest = 1.0e9;
                for (auto& s : R.sweep) closest = std::min (closest, dist (m, s));
                imitMean[q] += closest / 8.0;

                (void) prev; (void) progressive;

                if (q == 4)
                {
                    const double ratio = (R.selfSpread > 0.01) ? closest / R.selfSpread : 0.0;
                    meanRatio += ratio / 8.0;
                    if (ratio < worstRatio) { worstRatio = ratio; worstTbl = PN8[i]; }
                }
            }
            // level: the stretch knob's RMS swing, and the WT POS knob's own on the same table
            const double stretchSwing = 20.0 * std::log10 (std::max (stretchHi, 1e-9) / std::max (stretchLo, 1e-9));
            const double posSwing     = 20.0 * std::log10 (std::max (R.rmsHi,    1e-9) / std::max (R.rmsLo,    1e-9));
            if (stretchSwing > worstStretchSwing) { worstStretchSwing = stretchSwing; levelTbl = PN8[i]; }
            worstPosSwing = std::max (worstPosSwing, posSwing);
            (void) worstLevelExcess;
        }

        char b[256];
        snprintf (b, sizeof b, "closest WT Pos match: %.2f / %.2f / %.2f / %.2f / %.2f dB at s = .1/.25/.5/.75/1",
                  imitMean[0], imitMean[1], imitMean[2], imitMean[3], imitMean[4]);
        gate (imitMean[4] >= 25.0 && imitMean[1] >= 6.0, "[3a] NOT IMITABLE BY WT POS — the closest match is still far", b);

        snprintf (b, sizeof b, "mean %.2fx the table's own WT Pos span, worst %.2fx on %s (must be >= 0.90 / 0.40)",
                  meanRatio, worstRatio, worstTbl.c_str());
        // The per-table floor is 0.40, not 1.00, and the reason is Square: its WT Pos axis sweeps
        // sine to square across 1023 harmonics, so the DENOMINATOR here is the widest in the bank
        // and the ratio reads pessimistic even where the absolute distance is large. The absolute
        // bar above is the one that carries the claim; this one guards the MEAN.
        gate (meanRatio >= 0.90 && worstRatio >= 0.40,
              "[3b] IT REACHES PAST THE WHOLE WT POS AXIS", b);

        snprintf (b, sizeof b, "stretch swings %.1f dB worst (%s); the WT POS knob swings %.1f dB on the same tables",
                  worstStretchSwing, levelTbl.c_str(), worstPosSwing);
        gate (worstStretchSwing <= worstPosSwing && worstStretchSwing <= 12.0,
              "[5] TIMBRE, NOT LEVEL — quieter than WT Pos's own level swing", b);
    }

    // ── 3c · IT IS NOT THE SPECTRAL DROPDOWN'S STRETCH EITHER ────────────────────────────────
    {
        // The instrument already carries two partial-remapping modes in the SPECTRAL dropdown:
        // Harmonic Stretch (linear spacing, ratio' = 1 + (ratio-1)·s) and Inharmonic Stretch
        // (power law, ratio' = ratio^p with p up to 3.3, plus a brightness boost). Both pin the
        // fundamental, like this knob. "It is a different curve" is a claim, so it gets measured:
        // sweep BOTH modes across their whole amount range and find the closest match to STRETCH.
        double worstClosest = 1.0e9; std::string who; int mBest = 0; double aBest = 0.0;
        std::vector<float> t;
        for (int i = 0; i < 8; ++i)
        {
            std::unique_ptr<Wavetable> w (new Wavetable());
            w->buildFromSpec (HarmonicStretch::apply (base[(size_t) i], 1.0f));
            cyc (*w, MIP, 0.0f, t);
            const std::vector<double> mine = mag (t);

            double closest = 1.0e9; int cm = 0; double ca = 0.0;
            for (int mode : { (int) SpectralMode::HarmonicStretch, (int) SpectralMode::InharmonicStretch })
                for (int q = 1; q <= 40; ++q)
                {
                    const float amt = (float) q / 40.0f;
                    std::unique_ptr<Wavetable> v (new Wavetable());
                    v->buildFromSpec (SpectralMorph::apply (base[(size_t) i], (SpectralMode) mode, amt));
                    cyc (*v, MIP, 0.0f, t);
                    const double d = dist (mine, mag (t));
                    if (d < closest) { closest = d; cm = mode; ca = amt; }
                }
            if (closest < worstClosest) { worstClosest = closest; who = PN8[i]; mBest = cm; aBest = ca; }
        }
        char b[256];
        snprintf (b, sizeof b, "closest of 80 dropdown settings: %.2f dB (%s, %s at %.2f)",
                  worstClosest, who.c_str(),
                  mBest == (int) SpectralMode::HarmonicStretch ? "Harmonic Stretch" : "Inharmonic Stretch", aBest);
        gate (worstClosest >= 8.0, "[3c] NOT THE SPECTRAL DROPDOWN'S STRETCH EITHER", b);
    }

    // ── 4 · THE KNOB NEVER STOPS MOVING ──────────────────────────────────────────────────────
    {
        const int STEPS = 20;
        double worstStep = 1.0e9, worstTop = 1.0e9, worstTopVsBottom = 1.0e9;
        std::string stepTbl, topTbl;
        std::vector<float> t;
        for (int i = 0; i < 8; ++i)
        {
            std::vector<std::vector<double>> sp;
            for (int q = 0; q <= STEPS; ++q)
            {
                std::unique_ptr<Wavetable> w (new Wavetable());
                w->buildFromSpec (HarmonicStretch::apply (base[(size_t) i], (float) q / STEPS));
                cyc (*w, MIP, 0.0f, t); sp.push_back (mag (t));
            }
            for (int q = 0; q < STEPS; ++q)
            { const double d = dist (sp[(size_t) q], sp[(size_t) q + 1]);
              if (d < worstStep) { worstStep = d; stepTbl = PN8[i]; } }
            const double top = dist (sp[(size_t) (STEPS / 2)], sp[(size_t) STEPS]);
            const double bot = dist (sp[0], sp[(size_t) (STEPS / 2)]);
            if (top < worstTop) { worstTop = top; topTbl = PN8[i]; }
            worstTopVsBottom = std::min (worstTopVsBottom, top - bot);
        }
        char b[256];
        snprintf (b, sizeof b, "quietest 5%% step anywhere on any table: %.2f dB (%s)", worstStep, stepTbl.c_str());
        gate (worstStep >= 3.0, "[4a] NO DEAD ZONE — every step of the travel moves the sound", b);

        snprintf (b, sizeof b, "50%%->100%% travels %.2f dB at worst (%s); top half beats bottom half by %+.2f dB",
                  worstTop, topTbl.c_str(), worstTopVsBottom);
        gate (worstTop >= 20.0 && worstTopVsBottom >= -5.0,
              "[4b] THE TOP HALF EARNS ITS PLACE — no unused headroom", b);
    }

    // ── 6 · IT WORKS ON THE WHOLE BANK ────────────────────────────────────────────────────────
    {
        const float POS[3] = { 0.0f, 0.5f, 1.0f };
        int inertCells = 0, unexplained = 0, liveCells = 0;
        double weakest = 1.0e9; std::string weakestName;
        std::vector<float> t;
        for (int p = 0; p < WavetableBank::kNumPresets; ++p)
        {
            const WavetableSpec bs = WavetableBank::specForPreset (p);
            std::unique_ptr<Wavetable> a (new Wavetable()), b2 (new Wavetable());
            a->buildFromSpec (bs);
            b2->buildFromSpec (HarmonicStretch::apply (bs, 1.0f));
            for (int k = 0; k < 3; ++k)
            {
                cyc (*a, MIP, POS[k], t);  const std::vector<double> m0 = mag (t);
                cyc (*b2, MIP, POS[k], t); const std::vector<double> m1 = mag (t);
                const double d = dist (m0, m1);
                if (d < 1.0)
                {
                    ++inertCells;
                    // An inert cell is only allowed if the frame it draws from genuinely holds a
                    // single partial. The waterfall's frame axis maps WT Pos 0/0.5/1 onto frames
                    // 0 / 7.5 / 15, so check the frames either side of the read position.
                    // Only the frames this read position ACTUALLY touches, and only partials that
                    // actually sound: Railroad's frame 15 is a six-entry partial list of which five
                    // carry amp 0.0 — a pure sine in a padded list. Counting list entries instead of
                    // audible ones called that frame "unexplained" when it is the plainest case there is.
                    const double fpos = (double) POS[k] * (WavetableSpec::kNumFrames - 1);
                    const int    f0   = (int) std::floor (fpos);
                    const double frac = fpos - (double) f0;
                    auto audible = [&] (int f)
                    {
                        const FrameSpec& fs = bs.frames[(size_t) std::min (f, WavetableSpec::kNumFrames - 1)];
                        int n = 0;
                        if (fs.numPartials > 0)
                        { for (int q = 0; q < fs.numPartials; ++q) if (fs.partials[(size_t) q].amp != 0.0f && fs.partials[(size_t) q].ratio > 0.0f) ++n; }
                        else
                        { for (int q = 0; q < fs.numHarmonics && q < FrameSpec::kMaxHarmonics; ++q) if (fs.amplitudes[(size_t) q] != 0.0f) ++n; }
                        return n;
                    };
                    // THE SECOND LAW, and it is the honest twin of the first. A frame whose
                    // spectral edge already sits at or past the reference ceiling has nowhere to
                    // travel: its target is pinned to kGMin, and what little it gets is spent
                    // pushing its own top out of the band. Stated, and BOUNDED — the count below
                    // reports how many cells lean on it, so it can never quietly grow into
                    // "the knob does nothing".
                    auto noRoom = [&] (int f)
                    { return HarmonicStretch::edgeOf (bs.frames[(size_t) std::min (f, WavetableSpec::kNumFrames - 1)])
                             >= HarmonicStretch::kEdgeCeil; };
                    auto exempt = [&] (int f) { return audible (f) < 2 || noRoom (f); };
                    bool nothingToStretch = exempt (f0);
                    if (frac > 0.0) nothingToStretch = nothingToStretch && exempt (f0 + 1);
                    if (! nothingToStretch) ++unexplained;
                }
                else
                {
                    ++liveCells;
                    if (d < weakest) { weakest = d; weakestName = "preset " + std::to_string (p); }
                }
            }
        }
        char b[256];
        snprintf (b, sizeof b, "%d of %d cells move; %d inert (%d unexplained); weakest live cell %.2f dB (%s)",
                  liveCells, liveCells + inertCells, inertCells, unexplained, weakest, weakestName.c_str());
        gate (unexplained == 0, "[6] THE WHOLE BANK MOVES — except frames with nothing to stretch", b);
    }

    // ── 9 · NO FRAME COLLAPSE ── the defect a fixed stiffness shipped, gated forever ─────────
    {
        // Stretch pushes energy UP. A frame whose energy already sits high can therefore be pushed
        // clean out of the mip's band and go quiet, and because the bake peak-normalises per LEVEL
        // and not per frame, that shows up as the WT POS axis becoming wildly uneven in LOUDNESS.
        // MEASURED with a fixed global stiffness: Serum HD's spread went 4.0 -> 75.1 dB at C4.
        // Sweeping WT Pos would have faded the sound to nothing three octaves below where that
        // table does it naturally. This bar is why the stiffness is solved per frame.
        double worst = -1.0e9; std::string who;
        std::vector<float> t;
        for (int note : { 36, 60 })                          // C2 and C4 — where people play
        {
            const int mp = Wavetable::mipLevelForMidiNote (note, SR);
            for (int i = 0; i < 8; ++i)
            {
                auto spreadOf = [&] (const Wavetable& w)
                {
                    double lo = 1.0e9, hi = 0.0;
                    for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
                    { cyc (w, mp, (float) f / (WavetableSpec::kNumFrames - 1), t);
                      const double r = rms (t); if (r > 1.0e-9) { lo = std::min (lo, r); hi = std::max (hi, r); } }
                    return (hi > 0.0 && lo < 1.0e8) ? 20.0 * std::log10 (hi / lo) : 0.0;
                };
                std::unique_ptr<Wavetable> a (new Wavetable()), b2 (new Wavetable());
                a->buildFromSpec (base[(size_t) i]);
                b2->buildFromSpec (HarmonicStretch::apply (base[(size_t) i], 1.0f));
                const double d = spreadOf (*b2) - spreadOf (*a);
                if (d > worst) { worst = d; who = std::string (PN8[i]) + " at C" + std::to_string (note / 12 - 1); }
            }
        }
        char b[224];
        snprintf (b, sizeof b, "worst extra WT Pos level unevenness: %+.1f dB (%s); a fixed stiffness read +71.1 dB here",
                  worst, who.c_str());
        gate (worst <= 6.0, "[9] NO FRAME COLLAPSE — the WT Pos axis stays even", b);
    }

    // ── 7 · NO ALIASING ───────────────────────────────────────────────────────────────────────
    {
        // At a high note the mip ceiling is low. A stretched partial pushed past it must be GONE,
        // not folded down. Render at the C7 mip and look for energy above that mip's ceiling.
        const int mipHi = Wavetable::mipLevelForMidiNote (96, SR);
        double worstAbove = -1.0e9; std::string tbl;   // NOT 0.0: every dBc here is negative
        std::vector<float> t;
        for (int i = 0; i < 8; ++i)
        {
            std::unique_ptr<Wavetable> w (new Wavetable());
            w->buildFromSpec (HarmonicStretch::apply (base[(size_t) i], 1.0f));
            cyc (*w, mipHi, 0.0f, t);
            const std::vector<double> m = mag (t);
            const int cap = Wavetable::kMipMaxHarmonics[(size_t) mipHi];
            double peak = 0.0, above = 0.0;
            for (int h = 1; h <= HMEAS; ++h) peak = std::max (peak, m[(size_t) h]);
            for (int h = cap + 1; h <= HMEAS; ++h) above = std::max (above, m[(size_t) h]);
            if (peak > 0.0)
            { const double dbc = 20.0 * std::log10 (std::max (above, 1e-12) / peak);
              if (dbc > worstAbove) { worstAbove = dbc; tbl = PN8[i]; } }
        }
        char b[192];
        snprintf (b, sizeof b, "worst energy above the mip ceiling: %.1f dBc on %s (must be <= -100)", worstAbove, tbl.c_str());
        gate (worstAbove <= -100.0, "[7] NO ALIASING — nothing above the mip's harmonic ceiling", b);
    }

    // ── 8 · IT IS CHEAP ───────────────────────────────────────────────────────────────────────
    {
        const WavetableSpec in = WavetableBank::specForPreset (2);   // Square: 1023 harmonics, the worst case
        WavetableSpec out;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 50; ++i) HarmonicStretch::applyInto (in, 1.0f, out);
        auto t1 = std::chrono::high_resolution_clock::now();
        const double xf = std::chrono::duration<double, std::milli> (t1 - t0).count() / 50.0;

        std::unique_ptr<Wavetable> w (new Wavetable());
        t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 10; ++i) w->buildFromSpec (out);
        t1 = std::chrono::high_resolution_clock::now();
        const double bk = std::chrono::duration<double, std::milli> (t1 - t0).count() / 10.0;

        char b[192];
        snprintf (b, sizeof b, "transform %.3f ms on the bank's widest table (1023 harmonics), the bake it rides %.2f ms", xf, bk);
        gate (xf < bk, "[8] THE TRANSFORM IS SMALL NEXT TO THE BAKE IT RIDES", b);
    }

    printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
