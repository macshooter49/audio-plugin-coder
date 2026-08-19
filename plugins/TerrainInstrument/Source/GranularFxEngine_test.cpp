// ════════════════════════════════════════════════════════════════════════════
//  GranularFxEngine_test — fb362 offline proof loop (Pattern A, NOT in CMakeLists)
//
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/GranularFxEngine_test.cpp -o /tmp/gfx && /tmp/gfx
//
//  Gates are PERCEPTUAL / structural (the fb283 law — sample-diff RMS is banned as a
//  verdict). What each one is actually defending:
//    A. SAFETY      — no live grain may ever read across the write head. This is THE
//                     invariant the ring-relative design rests on; every click class
//                     the bible warns about is a violation of it.
//    B. FINITE      — no NaN/Inf escapes, at any setting, ever.
//    C. CLICK       — an isolated discontinuity shows up as an outlier in the sample
//                     difference distribution. Measured as max|Δx| / p99.9|Δx|: a clean
//                     broadband cloud sits low, one splice click sends it through the roof.
//    D. KNOB YANK   — the same detector while Size/Density/Window/Freeze are slammed
//                     mid-cloud (the fb204 norm-step lesson, on the new engine).
//    E. TYPES       — night-and-day gate (law: "types night-and-day or delete").
//    F. FREEZE      — a latched freeze holds its texture while the input changes.
//    G. LAW 6       — silence in ⇒ silence out; nothing free-runs unless latched.
//    H. FEEDBACK    — loop gain past 1.0 stays bounded (the §5.4 accounting).
// ════════════════════════════════════════════════════════════════════════════
#include "GranularFxEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>

static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("  %s  %-46s %s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}

static constexpr double FS = 48000.0;

// ── a musically dense probe: a 3-note chord + a click train, so both tonal and
//    transient behaviour are exercised (a pure sine hides splice artifacts).
struct Probe
{
    double ph1 = 0, ph2 = 0, ph3 = 0; long n = 0; bool smooth = false; bool sweep = false;
    void next (float& l, float& r)
    {
        // sweep = the input CLIMBS, which is the only way to prove a freeze is holding: a steady
        // probe is already stationary, so "is the output static?" answers yes at every setting.
        const double k = sweep ? (1.0 + 3.0 * (double) n / (FS * 5.0)) : 1.0;
        ph1 += 2 * M_PI * 220.0 * k / FS; ph2 += 2 * M_PI * 277.18 * k / FS; ph3 += 2 * M_PI * 329.63 * k / FS;
        float s = (float) (0.25 * (std::sin (ph1) + std::sin (ph2) + std::sin (ph3)));
        // The transient is what makes this probe realistic, but it is a STEP — its own click
        // ratio is enormous, and a granular that faithfully re-reads it inherits that number.
        // The click gates therefore run on the smooth variant, where any discontinuity in the
        // output can only have been made by the engine.
        if (! smooth && (n % (long) (FS * 0.5)) < 32) s += 0.5f;
        ++n; l = s; r = s * 0.92f;
    }
};

// max|Δx| / 99.9th-percentile|Δx| — a click is an isolated outlier in this distribution.
static double clickRatio (const std::vector<float>& x)
{
    if (x.size() < 64) return 0.0;
    std::vector<double> d; d.reserve (x.size());
    for (size_t i = 1; i < x.size(); ++i) d.push_back (std::fabs ((double) x[i] - (double) x[i - 1]));
    std::vector<double> s = d;
    std::sort (s.begin(), s.end());
    const double p999 = s[(size_t) ((double) s.size() * 0.999)];
    const double mx   = s.back();
    if (p999 < 1e-9) return mx < 1e-9 ? 0.0 : 1e9;
    return mx / p999;
}

static double rms (const std::vector<float>& x)
{
    double a = 0; for (float v : x) a += (double) v * v;
    return x.empty() ? 0.0 : std::sqrt (a / (double) x.size());
}

// Spectral centroid via a coarse Goertzel bank — enough to separate types by brightness.
static double centroid (const std::vector<float>& x)
{
    static const double freqs[] = { 100, 200, 400, 800, 1600, 3200, 6400, 12000 };
    double num = 0, den = 0;
    for (double f : freqs)
    {
        const double wq = 2 * M_PI * f / FS;
        const double coeff = 2 * std::cos (wq);
        double s0 = 0, s1 = 0, s2 = 0;
        for (float v : x) { s0 = (double) v + coeff * s1 - s2; s2 = s1; s1 = s0; }
        const double mag = std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2);
        num += mag * f; den += mag;
    }
    return den > 1e-12 ? num / den : 0.0;
}

// Frame-to-frame spectral correlation — how STATIONARY a signal is (the Freeze gate).
// Direct DFT bins rather than Goertzel: the marginally-stable Goertzel recurrence over a
// 4096-sample frame lost all its magnitude here and reported a flat zero for real audio.
static double stationarity (const std::vector<float>& x)
{
    const size_t fr = 8192;
    if (x.size() < fr * 4) return 0.0;
    auto band = [&] (size_t off) {
        static const double freqs[] = { 150, 300, 600, 1200, 2400, 4800, 9600 };
        std::vector<double> v;
        for (double f : freqs)
        {
            double re = 0, im = 0;
            for (size_t i = 0; i < fr && off + i < x.size(); ++i)
            {
                const double t = 2 * M_PI * f * (double) i / FS;
                const double win = 0.5 - 0.5 * std::cos (2 * M_PI * (double) i / (double) fr);
                re += (double) x[off + i] * win * std::cos (t);
                im -= (double) x[off + i] * win * std::sin (t);
            }
            v.push_back (std::sqrt (re * re + im * im) / (double) fr);
        }
        return v; };
    std::vector<double> a = band (x.size() - fr * 3), b = band (x.size() - fr);
    double na = 0, nb = 0, ab = 0;
    for (size_t i = 0; i < a.size(); ++i) { na += a[i] * a[i]; nb += b[i] * b[i]; ab += a[i] * b[i]; }
    return (na > 1e-18 && nb > 1e-18) ? ab / std::sqrt (na * nb) : 0.0;
}

struct RunOpts { int type = tw::GrnCloud; int chr = tw::GrnClean; int key = 0;
                 float density = .5f, size = .35f, pitch = 0, detune = 0, spray = .2f,
                       scan = 0, windowMs = 1200, width = .5f, feedback = 0, freeze = 0;
                 bool latch = false; bool silentInput = false; bool smooth = false; bool sweep = false; };

// Runs the engine and reports everything the gates need. `safeOK` is checked EVERY block.
static void run (tw::GranularFxEngine& e, const RunOpts& o, double seconds,
                 std::vector<float>& out, bool& safeOK, bool& finiteOK)
{
    tw::GranularFxParams p;
    p.type = o.type; p.character = o.chr; p.key = o.key;
    p.density = o.density; p.size = o.size; p.pitch = o.pitch; p.detune = o.detune;
    p.spray = o.spray; p.scan = o.scan; p.windowMs = o.windowMs; p.width = o.width;
    p.decay = o.feedback; p.freeze = o.freeze; p.freezeLatch = o.latch;
    e.setParams (p);

    Probe pr; pr.smooth = o.smooth; pr.sweep = o.sweep; safeOK = true; finiteOK = true;
    const long N = (long) (seconds * FS);
    out.clear(); out.reserve ((size_t) N);
    for (long i = 0; i < N; ++i)
    {
        float il = 0, ir = 0;
        if (! o.silentInput) pr.next (il, ir);
        float ol, orr; e.processSample (il, ir, ol, orr);
        if (! std::isfinite (ol) || ! std::isfinite (orr)) finiteOK = false;
        if ((i % 512) == 0 && ! e.allGrainsSafeForTesting()) safeOK = false;
        out.push_back (ol);
    }
}

// ── fb427 — TWO MORE FEATURES, because the old vector could not SEE the mechanisms.
//    The type-distinctness gate scored on spectral centroid and level ONLY — two scalars for
//    eight types — and went red at fb416 when the duty-cycle fix removed the stutter that had
//    been carrying the difference by accident. `Rewind` plays its grains BACKWARDS and
//    `Pulverize` shreds them into fragments; those sound nothing alike, and neither a centroid
//    nor an RMS can tell them apart. That is "geometry is not hearing" inverted: a metric blind
//    to a difference that is obvious to an ear.
//    The honest fix is a feature per mechanism, NOT a lower bar (CONTRACT §3.2 — tuning the
//    constant just moves the failure, which is exactly what happened when it was tried on Monday).
//
//  envRate — how fast the amplitude envelope fluctuates, in crossings of its own mean per second.
//            A grain engine's envelope moves at its GRAIN RATE, so `Pulverize` (short grains,
//            high spawn rate) and `Rewind` (ordinary grains) are far apart on it.
static double envRate (const std::vector<float>& y)
{
    if (y.size() < 4096) return 0.0;
    std::vector<double> env; env.reserve (y.size());
    double e = 0.0;
    const double a = 1.0 - std::exp (-1.0 / (0.002 * FS));      // 2 ms rectified follower
    for (float v : y) { const double r = std::fabs ((double) v); e += a * (r - e); env.push_back (e); }
    const size_t s0 = env.size() / 4;                            // skip the fill-in
    double mean = 0.0; for (size_t i = s0; i < env.size(); ++i) mean += env[i];
    mean /= (double) (env.size() - s0);
    int cross = 0;
    for (size_t i = s0 + 1; i < env.size(); ++i)
        if ((env[i - 1] < mean) != (env[i] < mean)) ++cross;
    return (double) cross * FS / (double) (env.size() - s0);
}

//  envSkew — TEMPORAL ASYMMETRY of the envelope: mean rising slope over mean falling slope.
//            A grain played FORWARD attacks faster than it decays; played BACKWARD the shape is
//            mirrored. This is the one feature that is directly sensitive to `Rewind`'s mechanism,
//            and nothing in the old vector could respond to a time reversal at all.
static double envSkew (const std::vector<float>& y)
{
    if (y.size() < 4096) return 1.0;
    double e = 0.0, up = 0.0, dn = 0.0; size_t nu = 0, nd = 0;
    const double a = 1.0 - std::exp (-1.0 / (0.002 * FS));
    std::vector<double> env; env.reserve (y.size());
    for (float v : y) { const double r = std::fabs ((double) v); e += a * (r - e); env.push_back (e); }
    for (size_t i = env.size() / 4 + 1; i < env.size(); ++i)
    { const double d = env[i] - env[i - 1];
      if (d > 0) { up += d; ++nu; } else { dn -= d; ++nd; } }
    const double u = (nu ? up / (double) nu : 0.0), v = (nd ? dn / (double) nd : 0.0);
    return (v > 1e-12) ? u / v : 1.0;
}

int main()
{
    std::printf ("\nfb362 — GranularFxEngine proof loop\n");

    // ── A + B: THE SAFETY INVARIANT, swept ────────────────────────────────────
    std::printf ("\n[A/B] safety invariant + finiteness, swept over the whole control space\n");
    {
        int cases = 0; bool allSafe = true, allFinite = true;
        std::string firstBad;
        const float pitches[]  = { -24, -12, 0, 7, 12, 24 };
        const float sizes[]    = { 0.f, 0.35f, 1.f };
        const float windows[]  = { 50.f, 1200.f, 16000.f };
        const float detunes[]  = { 0.f, 1.f };
        for (int t = 0; t < 8; ++t)
          for (float pit : pitches)
            for (float sz : sizes)
              for (float wm : windows)
                for (float dt : detunes)
                {
                    tw::GranularFxEngine e; e.prepare (FS);
                    RunOpts o; o.type = t; o.pitch = pit; o.size = sz; o.windowMs = wm;
                    o.detune = dt; o.density = 0.8f; o.key = (t == tw::GrnRise) ? 1 : 0;
                    std::vector<float> y; bool s, f;
                    run (e, o, 0.35, y, s, f);
                    ++cases;
                    if (! s && firstBad.empty())
                        firstBad = "type=" + std::to_string (t) + " pitch=" + std::to_string ((int) pit)
                                 + " size=" + std::to_string (sz) + " win=" + std::to_string ((int) wm);
                    allSafe   &= s;
                    allFinite &= f;
                }
        check (allSafe,   "no grain ever reads across the write head",
               allSafe ? (std::to_string (cases) + " cases clean") : ("first violation: " + firstBad));
        check (allFinite, "output finite in every case", std::to_string (cases) + " cases");
    }

    // ── C: CLICK — steady state ───────────────────────────────────────────────
    std::printf ("\n[C] click detector — steady cloud (max|dx| / p99.9|dx|)\n");
    {
        bool ok = true; std::string worst; double worstR = 0;
        for (int t = 0; t < 8; ++t)
        {
            tw::GranularFxEngine e; e.prepare (FS);
            RunOpts o; o.type = t; o.density = 0.7f; o.smooth = true; o.key = (t == tw::GrnRise) ? 1 : 0;
            std::vector<float> y; bool s, f; run (e, o, 3.0, y, s, f);
            const double r = clickRatio (y);
            if (r > worstR) { worstR = r; worst = "type " + std::to_string (t); }
            if (r > 12.0) ok = false;
        }
        check (ok, "steady cloud is click-free (all 8 types)",
               "worst " + worst + " ratio " + std::to_string (worstR).substr (0, 5) + " (gate 12.0)");
    }

    // ── D: CLICK — knob yanks mid-cloud ──────────────────────────────────────
    std::printf ("\n[D] click detector — knobs SLAMMED mid-cloud\n");
    {
        struct Yank { const char* name; float RunOpts::*field; float a, b; };
        const Yank yanks[] = {
            { "Size 0 -> 1",      &RunOpts::size,     0.f,   1.f    },
            { "Density 0 -> 1",   &RunOpts::density,  0.f,   1.f    },
            { "Window 16s -> 50ms", &RunOpts::windowMs, 16000.f, 50.f },
            { "Freeze 0 -> 1",    &RunOpts::freeze,   0.f,   1.f    },
            { "Scan -1 -> +1",    &RunOpts::scan,    -1.f,   1.f    },
            { "Detune 0 -> 1",    &RunOpts::detune,   0.f,   1.f    },
        };
        for (const auto& yk : yanks)
        {
            tw::GranularFxEngine e; e.prepare (FS);
            RunOpts o; o.density = 0.7f; o.size = 0.4f; o.smooth = true;
            o.*(yk.field) = yk.a;
            std::vector<float> warm; bool s1, f1; run (e, o, 1.2, warm, s1, f1);
            o.*(yk.field) = yk.b;                          // the slam
            std::vector<float> after; bool s2, f2; run (e, o, 1.2, after, s2, f2);
            // look at the seam: the tail of the warm-up joined to the head of the new setting
            std::vector<float> seam (warm.end() - 4096, warm.end());
            seam.insert (seam.end(), after.begin(), after.begin() + 8192);
            const double r = clickRatio (seam);
            check (r <= 14.0 && s2 && f2, std::string ("no click on ") + yk.name,
                   "ratio " + std::to_string (r).substr (0, 5) + " (gate 14.0)");
        }
    }

    // ── E: TYPES are night-and-day ───────────────────────────────────────────
    std::printf ("\n[E] the 8 types are measurably distinct\n");
    {
        double cen[8], lvl[8], rat[8], skw[8];
        for (int t = 0; t < 8; ++t)
        {
            tw::GranularFxEngine e; e.prepare (FS);
            RunOpts o; o.type = t; o.density = 0.6f; o.size = 0.35f; o.key = (t == tw::GrnRise) ? 1 : 0;
            std::vector<float> y; bool s, f; run (e, o, 2.5, y, s, f);
            cen[t] = centroid (y); lvl[t] = rms (y);
            rat[t] = envRate (y);  skw[t] = envSkew (y);
        }
        int twins = 0; std::string worst; double worstD = 1e9;
        for (int a = 0; a < 8; ++a) for (int b = a + 1; b < 8; ++b)
        {
            const double dc = std::fabs (cen[a] - cen[b]) / std::max (1.0, std::max (cen[a], cen[b]));
            const double dl = std::fabs (lvl[a] - lvl[b]) / std::max (1e-6, std::max (lvl[a], lvl[b]));
            const double dr = std::fabs (rat[a] - rat[b]) / std::max (1.0,  std::max (rat[a], rat[b]));
            const double ds = std::fabs (skw[a] - skw[b]) / std::max (0.05, std::max (skw[a], skw[b]));
            // ANY ONE axis clearing the bar makes the pair distinct — two machines that differ
            // only in HOW THEY MOVE are still two machines. The old vector had no motion axis.
            const double d  = std::max (std::max (dc, dl), std::max (dr, ds));
            if (d < worstD) { worstD = d; worst = std::to_string (a) + "/" + std::to_string (b); }
            if (d < 0.06) ++twins;
        }
        check (twins == 0, "no two types are twins",
               "closest pair " + worst + " delta " + std::to_string (worstD).substr (0, 5) + " (gate 0.06)");
        bool alive = true; std::string levels;
        static const char* TN[8] = {"Cloud","Rise","Swarm","Suspend","Scatter","Rewind","Stretch","Pulverize"};
        for (int t = 0; t < 8; ++t)
        {
            if (lvl[t] < 1e-4) alive = false;
            levels += std::string (TN[t]) + " lvl " + std::to_string (lvl[t]).substr (0, 6)
                    + " rate " + std::to_string (rat[t]).substr (0, 5)
                    + " skew " + std::to_string (skw[t]).substr (0, 5) + "\n        ";
        }
        check (alive, "every type actually makes sound", levels);
    }

    // ── F: FREEZE holds ──────────────────────────────────────────────────────
    std::printf ("\n[F] latched freeze suspends the archive\n");
    {
        tw::GranularFxEngine e; e.prepare (FS);
        RunOpts o; o.density = 0.7f; o.size = 0.5f; o.windowMs = 800;
        std::vector<float> warm; bool s1, f1; run (e, o, 2.0, warm, s1, f1);
        o.latch = true; o.silentInput = true;              // freeze, then take the input away
        std::vector<float> held; bool s2, f2; run (e, o, 4.0, held, s2, f2);
        check (rms (held) > 1e-3, "frozen cloud sustains with no input",
               "rms " + std::to_string (rms (held)).substr (0, 6));
        check (stationarity (held) > 0.90, "frozen texture is stationary",
               "corr " + std::to_string (stationarity (held)).substr (0, 5) + " (gate 0.90)");
        check (s2 && f2, "frozen cloud stays safe + finite", "");
    }

    // ── G: LAW 6 — nothing free-runs ─────────────────────────────────────────
    std::printf ("\n[G] law 6 — silence in, silence out (unlatched)\n");
    {
        tw::GranularFxEngine e; e.prepare (FS);
        RunOpts o; o.density = 0.8f; o.feedback = 1.0f; o.freeze = 0.7f;
        std::vector<float> warm; bool s1, f1; run (e, o, 2.0, warm, s1, f1);
        o.silentInput = true;
        std::vector<float> tail; bool s2, f2; run (e, o, 8.0, tail, s2, f2);
        std::vector<float> last (tail.end() - (size_t) FS, tail.end());
        check (rms (last) < 1e-3, "regeneration dies with the input",
               "final-second rms " + std::to_string (rms (last)).substr (0, 8));
        check (s2 && f2, "decay stays safe + finite", "");
    }

    // ── H: FEEDBACK stability ────────────────────────────────────────────────
    std::printf ("\n[H] feedback past unity stays bounded\n");
    {
        tw::GranularFxEngine e; e.prepare (FS);
        RunOpts o; o.density = 0.9f; o.feedback = 1.10f; o.size = 0.5f;
        std::vector<float> y; bool s, f; run (e, o, 12.0, y, s, f);
        double pk = 0; for (float v : y) pk = std::max (pk, (double) std::fabs (v));
        check (pk <= 1.02 && f, "peak bounded at max feedback",
               "peak " + std::to_string (pk).substr (0, 5));
        check (s, "safe at max feedback", "");
    }

    // ── I: THE KNOBS MAX SAYS FEEL DEAD ──────────────────────────────────────
    // Structural probes, not ear-metrics: a perceptual delta can be masked by the material, a
    // census or a head-position readout cannot. This is what "night and day" has to mean.
    std::printf ("\n[I] Scan / Spray / Detune / Freeze actually do something\n");
    {
        // ── SCAN. ⚠️ The head FOLDS inside the window (it loops, by design), so raw start-minus-end
        //    is meaningless — the first draft of this probe read a wrap as "wrong direction". Sum
        //    the per-step deltas and unwrap instead: that measures the true velocity.
        auto headVel = [] (float scan) {
            tw::GranularFxEngine e; e.prepare (FS);
            RunOpts o; o.scan = scan; o.density = 0.5f; o.windowMs = 4000; o.spray = 0.f;
            std::vector<float> y; bool s1, f1;
            run (e, o, 0.30, y, s1, f1);                        // let the window glide settle
            double prev = e.headAgeForTesting(), total = 0.0;
            const double span = 4.0 * FS;
            for (int k = 0; k < 40; ++k)
            {
                run (e, o, 0.01, y, s1, f1);
                double now = e.headAgeForTesting(), d = now - prev;
                if (d >  span * 0.5) d -= span;                  // unwrap a fold
                if (d < -span * 0.5) d += span;
                total += d; prev = now;
            }
            return total / 0.40; };                              // samples of drift per second
        const double vBack = headVel (-1.f), vHold = headVel (0.f), vFwd = headVel (1.f);
        check (vBack > 1.5 * FS, "Scan < 0 dives into the past (2x)",
               std::to_string ((int) (vBack / FS)).substr (0, 4) + "x realtime");
        check (std::fabs (vHold) < 0.05 * FS, "Scan centre holds a fixed age",
               std::to_string (vHold / FS).substr (0, 5) + "x");
        check (vFwd < -1.5 * FS, "Scan > 0 races toward the present (2x)",
               std::to_string ((int) (vFwd / FS)).substr (0, 5) + "x realtime");

        // ── SPRAY, by census. ⚠️ needs real OVERLAP or there are never 2 live grains to measure:
        //    the first draft used size 0.2 (6.8 ms) at 79 g/s = 0.54 overlap and read 0.000 for both.
        auto spread = [] (float spray) {
            tw::GranularFxEngine e; e.prepare (FS);
            RunOpts o; o.spray = spray; o.density = 0.8f; o.size = 0.6f; o.windowMs = 2000;
            std::vector<float> y; bool s1, f1; run (e, o, 1.2, y, s1, f1);
            return e.birthSpreadForTesting(); };
        const double s0 = spread (0.f), s1v = spread (1.f);
        check (s1v > s0 * 3.0 && s1v > 0.25, "Spray widens the grain scatter",
               "spread " + std::to_string (s0).substr (0, 5) + " -> " + std::to_string (s1v).substr (0, 5));

        // ── DETUNE is audible, and is a WOBBLE (nearby grains agree) rather than a scatter.
        {
            tw::GranularFxEngine e0, e1; e0.prepare (FS); e1.prepare (FS);
            RunOpts a; a.detune = 0.f; a.density = 0.9f; a.size = 0.5f;
            RunOpts b2 = a; b2.detune = 1.f;
            std::vector<float> y0, y1; bool s1, f1;
            run (e0, a,  1.5, y0, s1, f1);
            run (e1, b2, 1.5, y1, s1, f1);
            const double c0 = centroid (y0), c1 = centroid (y1);
            check (std::fabs (c1 - c0) / std::max (1.0, c0) > 0.02, "Detune is audible",
                   "centroid " + std::to_string ((int) c0) + " -> " + std::to_string ((int) c1));
        }

        // ── FREEZE, with the input STILL PLAYING. Max's complaint ("it's not freezing") is about
        //    the knob's travel, so measure what the knob does to STATIONARITY while audio runs —
        //    not the tail after silence, which the law-6 gate mutes on purpose and which made the
        //    first draft of this probe read 0.0000 at every setting.
        //    ⚠️ WARM UP FIRST. The real gesture is "play, then turn Freeze up" — starting at
        //    freeze=1 means the ring never recorded anything, so the probe measured silence and
        //    reported 0.000 for the most-frozen setting. (That trap is real for users too, which
        //    is what the priming guard in the engine now prevents.)
        auto stat = [] (float fz) {
            tw::GranularFxEngine e; e.prepare (FS);
            RunOpts o; o.density = 0.7f; o.size = 0.5f; o.windowMs = 600; o.sweep = true;
            std::vector<float> warm; bool s1, f1; run (e, o, 2.0, warm, s1, f1);   // capture material
            o.freeze = fz;
            std::vector<float> y; run (e, o, 5.0, y, s1, f1);                       // input climbs away
            std::vector<float> last (y.end() - (size_t) FS, y.end());
            return centroid (last); };   // frozen ⇒ stays with the OLD material, so the centroid lags
        const double f0 = stat (0.f), f50 = stat (0.5f), f100 = stat (1.0f);
        check (f100 < f0 * 0.92, "Freeze at 100 % holds the OLD material as the input climbs",
               "centroid free " + std::to_string ((int) f0) + " -> frozen " + std::to_string ((int) f100));
        check (f50 < f0 && f50 > f100 * 0.95, "Freeze is progressive across the travel",
               "0/50/100 = " + std::to_string ((int) f0) + " / " + std::to_string ((int) f50)
                             + " / " + std::to_string ((int) f100));
    }

    std::printf ("\n%s — %d failure(s)\n\n", failures ? "FAILED" : "ALL GATES PASSED", failures);
    return failures ? 1 : 0;
}
