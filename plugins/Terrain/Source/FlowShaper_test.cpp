// =============================================================================
//  FlowShaper_test.cpp — offline proof for the TERRAIN SHAPER (tp71)
//  g++ -std=c++17 -O2 -Wall -Wextra -ISource Source/FlowShaper_test.cpp -o /tmp/fs && /tmp/fs
//
//  Headline: DAW-LOCKED — the shape is read at the transport's phase from the FIRST sample, a play
//  pressed mid-cycle reads the shape mid-cycle, a loop wrap lands on the shape. Then every lane is
//  night and day by a hearing metric (level, zero-crossing rate, harmonic ratio, notch depth), and
//  the Time lane's jumps never click.
// =============================================================================
#include "FlowShaper.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>
using namespace wc;
static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what) { ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }
constexpr double BPM = 120, SR = 48000; constexpr int BLK = 512;
static const double FPB = SR * 60.0 / BPM;   // 24000 frames per beat

static float rampF (double p) { return (float) p; }
static std::shared_ptr<ShaperState> state() { auto s = std::make_shared<ShaperState>(); for (auto& L : s->lanes) L.fill (rampF); return s; }
static void fill (ShaperLane& L, float (*f) (double)) { L.fill (f); }
static float gate16 (double p) { return ((int) std::floor (p * 16.0) & 1) ? 0.f : 1.f; }   // a 1/16 gate over a 1-bar cycle, exactly on the grid
static float one (double) { return 1.f; }
static float zero (double) { return 0.f; }
static float ramp (double p) { return (float) p; }
static float half (double p) { return (float) (0.5 * p); }
static float stut (double p) { return (float) (std::fmod (p * 16.0, 1.0) / 16.0); }   // the first SIXTEENTH, sixteen times
static float sine (double p) { return 0.5f - 0.5f * (float) std::cos (2 * 3.14159265 * p); }

struct Run { std::vector<float> L, R; };
// drive the engine from ppq0 for `beats`, with a generator per sample; optional ppq jump at a beat
static Run run (FlowShaper& g, const std::shared_ptr<ShaperState>& st, double ppq0, double beats, float (*gen) (long long), double jumpAtBeat = -1, double jumpTo = 0)
{
    g.setState (st); Run r; const long long N = (long long) (beats * FPB); double ppq = ppq0; long long n = 0; bool jumped = false;
    std::vector<float> L (BLK), R (BLK);
    while (n < N)
    {
        if (! jumped && jumpAtBeat >= 0 && (double) n / FPB >= jumpAtBeat) { ppq = jumpTo; jumped = true; }
        for (int i = 0; i < BLK; ++i) { const float v = gen (n + i); L[(size_t) i] = v; R[(size_t) i] = v; }
        g.process (L.data(), R.data(), BLK, ppq, BPM, true);
        r.L.insert (r.L.end(), L.begin(), L.end()); r.R.insert (r.R.end(), R.begin(), R.end());
        ppq += BLK / FPB; n += BLK;
    }
    return r;
}
static float sig440 (long long n) { return 0.5f * std::sin (2 * 3.14159265f * 440.f * (float) n / (float) SR); }
static float sig5k (long long n) { return 0.5f * std::sin (2 * 3.14159265f * 5000.f * (float) n / (float) SR); }
static float clicks8 (long long n) { const long long per = (long long) (FPB / 2); return (n % per) < 96 ? 0.9f : 0.0f; }   // an 1/8-note click train (2 ms clicks)
static float chirp (long long n) { const double t = n / SR; return 0.5f * (float) std::sin (2 * 3.14159265 * (200.0 + 400.0 * t) * t); }
static unsigned g_ns = 1; static float noise (long long) { g_ns ^= g_ns << 13; g_ns ^= g_ns >> 17; g_ns ^= g_ns << 5; return ((g_ns & 0xffff) / 32768.0f - 1.0f) * 0.4f; }
static double rms (const std::vector<float>& x, size_t a, size_t b) { double e = 0; size_t n = 0; for (size_t i = a; i < b && i < x.size(); ++i) { e += (double) x[i] * x[i]; ++n; } return std::sqrt (e / std::max<size_t> (1, n)); }
static double db (double v) { return 20 * std::log10 (v + 1e-12); }
static int zeroCross (const std::vector<float>& x, size_t a, size_t b) { int c = 0; for (size_t i = a + 1; i < b && i < x.size(); ++i) if ((x[i] >= 0) != (x[i - 1] >= 0)) ++c; return c; }
static double maxJump (const std::vector<float>& x, size_t a, size_t b) { double m = 0; for (size_t i = a + 1; i < b && i < x.size(); ++i) m = std::max (m, (double) std::fabs (x[i] - x[i - 1])); return m; }
// harmonic ratio of a 440 Hz sine: energy at 2f..8f vs f (Goertzel)
static double goertzel (const std::vector<float>& x, size_t a, size_t n, double hz) { const double w = 2 * 3.14159265358979 * hz / SR; double s1 = 0, s2 = 0; for (size_t i = 0; i < n; ++i) { const double s0 = x[a + i] + 2 * std::cos (w) * s1 - s2; s2 = s1; s1 = s0; } return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - 2 * std::cos (w) * s1 * s2)) / n; }
static double nonFundDb (const std::vector<float>& x, size_t a) { const size_t n = 24000; const double f = goertzel (x, a, n, 440) * 2.0; const double tot = rms (x, a, a + n); const double nf = std::sqrt (std::max (0.0, tot * tot - f * f * 0.5)); return db (nf) - db (f / std::sqrt (2.0)); }
static double harmRatioDb (const std::vector<float>& x, size_t a) { const size_t n = 24000; const double f = goertzel (x, a, n, 440); double h = 0; for (int k = 2; k <= 8; ++k) h += std::pow (goertzel (x, a, n, 440.0 * k), 2); return db (std::sqrt (h)) - db (f); }
// 256-bin magnitude of a window (for the phaser's notches)
static std::vector<double> spectrum (const std::vector<float>& x, size_t a) { const size_t N = 4096; std::vector<double> m (N / 2); for (size_t k = 4; k < N / 2; k += 1) { double re = 0, im = 0; for (size_t i = 0; i < N; ++i) { const double v = x[a + i] * (0.5 - 0.5 * std::cos (2 * 3.14159265 * i / N)); re += v * std::cos (2 * 3.14159265 * k * i / N); im -= v * std::sin (2 * 3.14159265 * k * i / N); } m[k] = std::sqrt (re * re + im * im); } return m; }

int main()
{
    std::printf ("FlowShaper — the Terrain Shaper, offline (DAW-locked, night and day, click-free)\n");
    // ── T1: DAW LOCK — the first sample reads the shape at the transport's phase ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.smooth = 0; fill (V, gate16); V.rate = 4;   // 1 bar, 1/16 gate: on for 1/16, off for 1/16
        auto a = run (g, st, 0.0, 1.0, sig440);                       // play from bar 1
        const double onDb = db (rms (a.L, 200, 2000)), offDb = db (rms (a.L, 6200, 8000));   // step 0 on (0..6000), step 1 off (6000..12000)
        check (onDb > -10 && offDb < -50, "T1a a play from the bar line: the gate's first step is ON from the first samples, the second is silent");
        FlowShaper g2; g2.prepare (SR);
        auto b = run (g2, st, 0.25 + 0.125, 0.5, sig440);            // play pressed 1.5 sixteenths in: step 1 (OFF) is playing, then step 2 (ON) at 0.5 beats
        check (db (rms (b.L, 100, 2800)) < -50 && db (rms (b.L, 3200, 5500)) > -10, "T1b a play pressed mid-cycle reads the shape mid-cycle: silent now, on at the next step — no wait, no anchor");
        FlowShaper g3; g3.prepare (SR);
        auto c = run (g3, st, 0.0, 2.0, sig440, 1.03, 8.0);          // a loop wrap: ppq jumps 1.03 → 8.0 (a bar line)
        const size_t jw = (size_t) std::ceil (1.03 * FPB / BLK) * BLK;   // the block the wrap lands in (run() jumps at the first block start past 1.03)
        check (db (rms (c.L, jw + 100, jw + 2000)) > -10 && db (rms (c.L, jw + 6200, jw + 8000)) < -50, "T1c a loop wrap onto a bar line lands on the shape's first step at once");
    }
    // ── T2: VOLUME is night and day, and a square edge does not click ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.smooth = 0.25f; fill (V, gate16);
        auto a = run (g, st, 0.0, 2.0, sig440);
        char buf[120]; std::snprintf (buf, sizeof buf, "T2 VOLUME gate: on %.1f dB, off %.1f dB (the last half of the off step, past the 10 ms smoothing)", db (rms (a.L, 1000, 5000)), db (rms (a.L, 9000, 11800)));
        check (db (rms (a.L, 1000, 5000)) > -10 && db (rms (a.L, 9000, 11800)) < -50, buf);
        check (maxJump (a.L, 0, a.L.size()) < 0.09, "T2b the gate's edges are smoothed: no sample-to-sample slam (the 440 Hz sine itself steps 0.03)");
    }
    // ── T3/T4: TIME — halftime doubles the click spacing, stutter quarters it, and nothing clicks on a sine ──
    {
        auto count = [] (const std::vector<float>& x, size_t a, size_t b) { int c = 0; bool up = false; for (size_t i = a; i < b; ++i) { const bool h = std::fabs (x[i]) > 0.3f; if (h && ! up) ++c; up = h; } return c; };
        FlowShaper g; g.prepare (SR); g.armRing(); auto st = state(); auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.rate = 4; T.mode = 0; T.k[0] = 0.3f;
        fill (T, ramp); auto u = run (g, st, 0.0, 4.0, clicks8); const int nU = count (u.L, (size_t) FPB * 2, (size_t) FPB * 4);
        FlowShaper g2; g2.prepare (SR); g2.armRing(); fill (T, half); auto h = run (g2, st, 0.0, 4.0, clicks8); const int nH = count (h.L, (size_t) FPB * 2, (size_t) FPB * 4);
        FlowShaper g3; g3.prepare (SR); g3.armRing(); fill (T, stut); auto s = run (g3, st, 0.0, 4.0, clicks8); const int nS = count (s.L, (size_t) FPB * 2, (size_t) FPB * 4);
        char buf[260]; std::snprintf (buf, sizeof buf, "T3 TIME: unity keeps the 1/8 clicks (%d in 2 beats), HALFTIME halves them (%d, the restart's own click may add one), a 1/16 stutter repeats the first sixteenth (%d)", nU, nH, nS);
        check (nU == 4 && nH >= 2 && nH <= 3 && nS == 8, buf);
        FlowShaper g4; g4.prepare (SR); g4.armRing(); fill (T, stut); auto z = run (g4, st, 0.0, 4.0, sig440);
        size_t at = 0; { double m = 0; for (size_t i = (size_t) FPB * 2 + 1; i < (size_t) FPB * 4; ++i) { const double j = std::fabs (z.L[i] - z.L[i - 1]); if (j > m) { m = j; at = i; } } }
        std::snprintf (buf, sizeof buf, "T4 TIME jumps are crossfaded: max jump %.4f at sample %zu (beat %.3f) on a sine under a stutter (the sine's own step is 0.029) [%.3f %.3f %.3f | %.3f %.3f %.3f]", maxJump (z.L, (size_t) FPB * 2, (size_t) FPB * 4), at, at / FPB, z.L[at-3], z.L[at-2], z.L[at-1], z.L[at], z.L[at+1], z.L[at+2]);
        check (maxJump (z.L, (size_t) FPB * 2, (size_t) FPB * 4) < 0.12, buf);
    }
    // ── T5: FILTER — shape 0 closes it 24 dB+ on 5 kHz, shape 1 is open ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& F = st->lanes[2]; F.on = true; F.depth = 1; F.mode = 0; F.k[0] = 0.2f; fill (F, zero);
        auto a = run (g, st, 0.0, 1.0, sig5k); FlowShaper g2; g2.prepare (SR); fill (F, one); auto b = run (g2, st, 0.0, 1.0, sig5k);
        char buf[120]; std::snprintf (buf, sizeof buf, "T5 FILTER LP: shape 0 = %.1f dB, shape 1 = %.1f dB on 5 kHz", db (rms (a.L, 4000, 20000)) + 6.02, db (rms (b.L, 4000, 20000)) + 6.02);
        check (db (rms (a.L, 4000, 20000)) + 6.02 < -24 && db (rms (b.L, 4000, 20000)) + 6.02 > -2, buf);
    }
    // ── T6: PAN — shape 0 is left, shape 1 is right ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& P = st->lanes[3]; P.on = true; P.depth = 1; fill (P, zero); auto a = run (g, st, 0.0, 1.0, sig440);
        FlowShaper g2; g2.prepare (SR); fill (P, one); auto b = run (g2, st, 0.0, 1.0, sig440);
        check (db (rms (a.R, 2000, 20000)) - db (rms (a.L, 2000, 20000)) < -30 && db (rms (b.L, 2000, 20000)) - db (rms (b.R, 2000, 20000)) < -30, "T6 PAN: shape 0 = hard left, shape 1 = hard right (the other side 30 dB down)");
    }
    // ── T7: REPEAT — a chirp becomes a loop: the second slice equals the first ──
    {
        FlowShaper g; g.prepare (SR); g.armRing(); auto st = state(); auto& Rp = st->lanes[4]; Rp.on = true; Rp.depth = 1; Rp.rate = 4; Rp.grid = 16; fill (Rp, one);
        Rp.fill ([] (double) { return 0.5f; });   // height 0.5 → 1/16 slices (div 16)
        auto a = run (g, st, 0.0, 2.0, chirp);
        const size_t len = (size_t) (4.0 * FPB / 16.0);   // a 1/16 of the bar
        double num = 0, d1 = 0, d2 = 0; const size_t s0 = (size_t) FPB * 1;   // compare slice k with slice k+1 in beat 2
        for (size_t i = 0; i < len; ++i) { const double x = a.L[s0 + i], y = a.L[s0 + len + i]; num += x * y; d1 += x * x; d2 += y * y; }
        const double corr = num / std::sqrt (d1 * d2 + 1e-12);
        FlowShaper g2; g2.prepare (SR); g2.armRing(); fill (Rp, zero); auto b = run (g2, st, 0.0, 2.0, chirp);
        double num2 = 0, e1 = 0, e2 = 0; for (size_t i = 0; i < len; ++i) { const double x = b.L[s0 + i], y = b.L[s0 + len + i]; num2 += x * y; e1 += x * x; e2 += y * y; }
        char buf[160]; std::snprintf (buf, sizeof buf, "T7 REPEAT: consecutive 1/16 slices correlate %.3f with the shape up (a chirp does not repeat itself: %.3f with the shape down)", corr, num2 / std::sqrt (e1 * e2 + 1e-12));
        check (corr > 0.97 && num2 / std::sqrt (e1 * e2 + 1e-12) < 0.9, buf);
    }
    // ── T8: DRIVE — harmonics appear with the shape up ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& D = st->lanes[5]; D.on = true; D.depth = 1; D.mode = 0; fill (D, one); auto a = run (g, st, 0.0, 2.0, sig440);
        FlowShaper g2; g2.prepare (SR); fill (D, zero); auto b = run (g2, st, 0.0, 2.0, sig440);
        char buf[120]; std::snprintf (buf, sizeof buf, "T8 DRIVE: harmonics %.1f dB (shape 1) vs %.1f dB (shape 0) below the fundamental", harmRatioDb (a.L, 12000), harmRatioDb (b.L, 12000));
        check (harmRatioDb (a.L, 12000) > -12 && harmRatioDb (b.L, 12000) < -40 && harmRatioDb (a.L, 12000) - harmRatioDb (b.L, 12000) > 25, buf);
    }
    // ── T9: PHASER — notches in white noise ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& P = st->lanes[6]; P.on = true; P.depth = 1; P.k[0] = 0.5f; fill (P, sine); auto a = run (g, st, 0.0, 1.0, noise);
        auto m = spectrum (a.L, 20000); double mx = 0, mn = 1e9; for (size_t k = 20; k < 600; ++k) { mx = std::max (mx, m[k]); mn = std::min (mn, m[k]); }
        char buf[120]; std::snprintf (buf, sizeof buf, "T9 PHASER: %.1f dB between the deepest notch and the loudest peak in white noise (flat noise is ~6)", db (mx) - db (mn));
        check (db (mx) - db (mn) > 18, buf);
    }
    // ── T10: CRUSH — the noise floor rises with the shape ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& C = st->lanes[7]; C.on = true; C.depth = 1; C.k[0] = 1; C.k[1] = 0.5f; fill (C, one); auto a = run (g, st, 0.0, 2.0, sig440);
        FlowShaper g2; g2.prepare (SR); fill (C, zero); auto b = run (g2, st, 0.0, 2.0, sig440);
        auto residual = [] (const std::vector<float>& y, size_t a, size_t b) { double e = 0, ei = 0; for (size_t i = a; i < b; ++i) { const double d = y[i] - sig440 ((long long) i); e += d * d; ei += (double) sig440 ((long long) i) * sig440 ((long long) i); } return db (std::sqrt (e / (b - a))) - db (std::sqrt (ei / (b - a))); };
        char buf[200]; std::snprintf (buf, sizeof buf, "T10 CRUSH: what the lane ADDS to the tone %.1f dB (shape 1) vs %.1f dB (shape 0, a wire)", residual (a.L, 12000, 36000), residual (b.L, 12000, 36000));
        check (residual (a.L, 12000, 36000) > -12 && residual (b.L, 12000, 36000) < -60, buf);
    }
    // ── T11: STOPPED transport — every lane holds phase 0, nothing runs away ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.smooth = 0; fill (V, gate16); g.setState (st);
        std::vector<float> L (BLK, 0.5f), R (BLK, 0.5f); for (int b = 0; b < 40; ++b) { std::fill (L.begin(), L.end(), 0.5f); std::fill (R.begin(), R.end(), 0.5f); g.process (L.data(), R.data(), BLK, 7.77, BPM, false); }
        check (std::fabs (L[100] - 0.5f) < 1e-3, "T11 stopped transport: the gate holds its first step (unity), whatever ppq says");
    }
    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
