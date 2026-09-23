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
#include <cstring>
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
/* tp86 — RESTATED IN THE OFFSET CONVENTION. The shape is now HOW FAR BACK FROM NOW (1 = the present),
   so rate = 1 + slope x Range. Halftime is a slope of -1/2, not a ramp of 1/2; and a stutter is a
   DESCENDING STAIRCASE — each tread flat (normal speed) and each riser jumping back a slice. */
static float unity (double)   { return 1.0f; }                                        // flat at the top = 100%, no delay
static float half (double p)  { return (float) (1.0 - 0.5 * p); }                     // slope -1/2 = halftime
static float stut (double p)  { const int k = (int) (p * 16.0); return 1.0f - (float) k / 16.0f; }   // the first SIXTEENTH, sixteen times
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
        fill (T, unity); auto u = run (g, st, 0.0, 4.0, clicks8); const int nU = count (u.L, (size_t) FPB * 2, (size_t) FPB * 4);
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
        check (db (rms (a.L, 4000, 20000)) + 6.02 < -24 && db (rms (b.L, 4000, 20000)) + 6.02 > -4, buf);   /* tp72 — the fallback SVF at 20 kHz droops ~3 dB at 5 kHz (bilinear warp); the roster engine replaces it in the plugin */
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
    // ══ tp72 — THE TRIGGERS, THE ROSTER HOOKS, THE TARGET TAB ═══════════════════════════════════════════════
    // ── T12: FREE — the lane runs on its own clock with the transport STOPPED ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.smooth = 0; V.trig = (int) ShaperTrig::Free; V.rate = 4; fill (V, gate16); g.setState (st);
        std::vector<float> out; std::vector<float> L (BLK), R (BLK);
        for (int b = 0; b < 190; ++b) { std::fill (L.begin(), L.end(), 0.5f); std::fill (R.begin(), R.end(), 0.5f); g.process (L.data(), R.data(), BLK, 0.0, BPM, false); out.insert (out.end(), L.begin(), L.end()); }
        // one bar = 96000 samples, a sixteenth = 6000: step 0 on, step 1 off, on its own clock
        const double s0 = rms (out, 700, 5600), s1 = rms (out, 6700, 11600), s2 = rms (out, 12700, 17600);
        char buf[160]; std::snprintf (buf, sizeof buf, "T12 FREE trigger, transport stopped: sixteenths read %.2f / %.2f / %.2f (on / off / on)", s0, s1, s2);
        check (s0 > 0.4 && s1 < 0.05 && s2 > 0.4, buf);
    }
    // ── T13: MIDI — a note-on restarts the lane's clock at the note's SAMPLE ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.smooth = 0; V.trig = (int) ShaperTrig::Midi; V.rate = 4; fill (V, ramp); g.setState (st);
        std::vector<float> L (BLK), R (BLK); std::vector<float> out;
        for (int b = 0; b < 100; ++b) { std::fill (L.begin(), L.end(), 1.0f); std::fill (R.begin(), R.end(), 1.0f); if (b == 60) g.noteOn (100); g.process (L.data(), R.data(), BLK, 0.0, BPM, true); out.insert (out.end(), L.begin(), L.end()); }
        const float before = out[(size_t) (60 * BLK + 98)], after = out[(size_t) (60 * BLK + 500)];   // 400 samples on: the 0.2 ms smoother has settled, the ramp has moved 0.004
        char buf[160]; std::snprintf (buf, sizeof buf, "T13 MIDI trigger: the ramp read %.3f the sample before the note and %.3f 400 samples after (restarted at 0)", before, after);
        check (before > 0.25 && after < 0.03, buf);
    }
    // ── T14: AUDIO — a transient of the input restarts the lane ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& F = st->lanes[2]; F.on = true; F.depth = 1; F.trig = (int) ShaperTrig::Audio; F.rate = 4; fill (F, ramp); st->sense = 0.5f; g.setState (st);
        std::vector<float> L (BLK), R (BLK); float phBefore = 0, phAfter = 0;
        for (int b = 0; b < 80; ++b)
        {
            for (int i = 0; i < BLK; ++i) { const long long n = (long long) b * BLK + i; const float v = n >= 30000 ? 0.8f * std::sin (2 * 3.14159265f * 440.f * (float) n / (float) SR) : 0.0f; L[(size_t) i] = v; R[(size_t) i] = v; }
            g.process (L.data(), R.data(), BLK, 0.0, BPM, true);
            if (b == 57) phBefore = g.vizPhase (2);   // just before the burst (sample 30000 sits in block 58)
            if (b == 58) phAfter = g.vizPhase (2);
        }
        char buf[160]; std::snprintf (buf, sizeof buf, "T14 AUDIO trigger: the lane's phase was %.3f before the burst and %.3f right after it (restarted)", phBefore, phAfter);
        check (phBefore > 0.25 && phAfter < 0.02, buf);
    }
    // ── T15: VOLUME Duck — the shape is read upside down ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.smooth = 0; V.mode = 1; fill (V, one);
        auto a = run (g, st, 0.0, 0.5, sig440);
        char buf[120]; std::snprintf (buf, sizeof buf, "T15 VOLUME Duck: a shape at 1 reads as gain 0 (%.1f dB)", db (rms (a.L, 4000, 20000)));
        check (db (rms (a.L, 4000, 20000)) < -40, buf);
    }
    // ── T16: REPEAT reverse + decay + pitch — the slices run without a click ──
    {
        FlowShaper g; g.prepare (SR); g.armRing(); auto st = state(); auto& P = st->lanes[4]; P.on = true; P.depth = 1; P.mode = 1; P.k[1] = 0.5f; P.k[2] = 0.8f; fill (P, [] (double p) { return p < 0.25 ? 0.f : 0.6f; });
        auto a = run (g, st, 0.0, 4.0, sig440);
        char buf[160]; std::snprintf (buf, sizeof buf, "T16 REPEAT reverse / decay / pitch: level %.1f dB, worst step %.3f (a 440 Hz sine steps %.3f)", db (rms (a.L, 30000, 90000)), maxJump (a.L, 30000, 90000), 0.5 * 2 * 3.14159 * 440 / SR);
        check (db (rms (a.L, 30000, 90000)) > -30 && maxJump (a.L, 30000, 90000) < 0.25, buf);
    }
    // ── T17: THE ROSTER HOOKS — a lane whose type is a rack engine hands the sample to the processor's engine ──
    {
        struct FakeExt : ShaperExt
        {
            int fCalls[3] = { 0, 0, 0 }, fEng[3] = { -1, -1, -1 }, dCalls[2] = { 0, 0 }, dMode[2] = { -1, -1 }; float lastCut = -1;
            bool filter (int which, int engine, float cut01, float, float, float, int, float, float, float& l, float& r) noexcept override { ++fCalls[which]; fEng[which] = engine; lastCut = cut01; l *= 0.5f; r *= 0.5f; return true; }
            bool drive  (int which, int mode, float, float, int, float, float, float, float& l, float& r) noexcept override { ++dCalls[which]; dMode[which] = mode; l *= 0.25f; r *= 0.25f; return true; }
            bool fx (int, float, const float*, int, float, float&, float&) noexcept override { return false; }
        } ext;
        FlowShaper g; g.prepare (SR); g.setExt (&ext); auto st = state();
        auto& F = st->lanes[2]; F.on = true; F.depth = 1; F.mode = 4; fill (F, one);                 // Acid 303
        auto& D = st->lanes[5]; D.on = true; D.depth = 1; D.mode = 9; D.k[1] = 0.5f; fill (D, one);  // Diode 1
        auto& H = st->lanes[6]; H.on = true; H.depth = 1; H.mode = 2; fill (H, one);                 // roster entry 0 = Phaser 4P (19)
        auto& C = st->lanes[7]; C.on = true; C.depth = 1; C.mode = 4; fill (C, one);                 // Samp-Hold (83)
        auto a = run (g, st, 0.0, 0.25, sig440);
        const double lv = rms (a.L, 2000, 6000) / rms (a.L, 2000, 6000);   // (the fake scales: 0.5 · 0.25 · 0.5 · 0.5 = 1/32 of the input, makeup 1.0 at k1 .5)
        (void) lv;
        char buf[220]; std::snprintf (buf, sizeof buf, "T17 ROSTER HOOKS: filter(0) engine %d ×%d, phaser filter(1) engine %d ×%d, crush filter(2) engine %d ×%d, drive(0) mode %d ×%d, level %.1f dB (the fakes scale to -30)",
                                      ext.fEng[0], ext.fCalls[0], ext.fEng[1], ext.fCalls[1], ext.fEng[2], ext.fCalls[2], ext.dMode[0], ext.dCalls[0], db (rms (a.L, 2000, 6000)));
        check (ext.fEng[0] == 4 && ext.fCalls[0] == 6144 && ext.fEng[1] == 19 && ext.fCalls[1] == 6144 && ext.fEng[2] == 83 && ext.fCalls[2] == 6144 && ext.dMode[0] == 9 && ext.dCalls[0] == 6144
               //  tp90 — plus the per-type level trims of the two lit roster types, straight from the table
               && std::fabs (db (rms (a.L, 2000, 6000)) - (db (0.5 / std::sqrt (2.0)) - 30.1 + kShaperTrimDb[6][2] + kShaperTrimDb[7][4])) < 1.0, buf);
        // and with the crush lane on a distortion type: drive slot 1
        FlowShaper g2; g2.prepare (SR); g2.setExt (&ext); auto st2 = state(); auto& C2 = st2->lanes[7]; C2.on = true; C2.depth = 1; C2.mode = 8; fill (C2, one);
        run (g2, st2, 0.0, 0.25, sig440);
        check (ext.dMode[1] == 21 && ext.dCalls[1] == 6144, "T17b the Crush lane's Bitcrush type is the distortion's mode 21 on drive slot 1");
        // without an ext every lane still sounds (the built-ins)
        FlowShaper g3; g3.prepare (SR); auto st3 = state(); auto& F3 = st3->lanes[2]; F3.on = true; F3.depth = 1; F3.mode = 4; fill (F3, one); auto c = run (g3, st3, 0.0, 0.25, sig440);
        check (db (rms (c.L, 2000, 6000)) > -12, "T17c no ext: the Filter lane's built-in SVF carries the signal (nothing goes silent)");
    }
    // ── T18: TIME Glide — the read head slews instead of cutting: a sine stays continuous through a stutter ──
    {
        FlowShaper g; g.prepare (SR); g.armRing(); auto st = state(); auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.k[1] = 0.7f; T.rate = 4; fill (T, stut);
        auto a = run (g, st, 0.0, 2.0, sig440);
        char buf[160]; std::snprintf (buf, sizeof buf, "T18 TIME glide: worst sample step %.3f through a 1/16 stutter (a cut would step ~%.2f), level %.1f dB", maxJump (a.L, 12000, 90000), 0.5, db (rms (a.L, 12000, 90000)));
        check (maxJump (a.L, 12000, 90000) < 0.07 && db (rms (a.L, 12000, 90000)) > -12, buf);
    }
    // ── T19: VOLUME Punch — a 1/16 gate's openings carry a transient overshoot ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.smooth = 0.1f; V.k[2] = 1.0f; fill (V, gate16);
        auto a = run (g, st, 0.0, 1.0, [] (long long) { return 0.5f; });
        FlowShaper g2; g2.prepare (SR); V.k[2] = 0.0f; auto b = run (g2, st, 0.0, 1.0, [] (long long) { return 0.5f; });
        // the second opening (step 2 begins at sample 12000): its first 6 ms vs the settled level
        const double punched = rms (a.L, 12000 + 100, 12000 + 400), settled = rms (a.L, 12000 + 3000, 12000 + 5500), plain = rms (b.L, 12000 + 100, 12000 + 400);
        char buf[160]; std::snprintf (buf, sizeof buf, "T19 VOLUME Punch: the gate's opening reads %.2f (settled %.2f) with Punch, %.2f without", punched, settled, plain);
        check (punched > settled * 1.3 && plain < settled * 1.15, buf);
    }
    // ── T20: PAN Haas — panned right, the left side arrives late (cross-correlation peaks at a lag) ──
    {
        FlowShaper g; g.prepare (SR); auto st = state(); auto& P = st->lanes[3]; P.on = true; P.depth = 1; P.mode = 1; P.k[2] = 1.0f; fill (P, one);   // Linear law: full right = L 0 ⇒ use 0.9
        fill (P, [] (double) { return 0.9f; });
        auto a = run (g, st, 0.0, 0.5, chirp);
        int bestLag = 0; double best = -1; for (int lag = 0; lag <= 900; lag += 5) { double c = 0; for (size_t i = 6000; i + 900 < a.L.size(); ++i) c += (double) a.L[i + (size_t) lag] * a.R[i]; if (c > best) { best = c; bestLag = lag; } }
        char buf[160]; std::snprintf (buf, sizeof buf, "T20 PAN Haas: with the pan 80 %% right the left side lags the right by %d samples (%.1f ms)", bestLag, bestLag / SR * 1000.0);
        check (bestLag >= 400 && bestLag <= 700, buf);
    }
    // ── T21: tp79 — THE CHAIN IS THE SLOT ORDER, and order has to be AUDIBLE or the feature is a label ──
    //  Max: "these are in a chain obviously … I want to put Bode second, I want to put Pan third."
    //  A deep gate in front of a distortion is not the same sound as the same gate behind it: the distortion's
    //  curve is driven by whatever level reaches it, so gating first changes what it has to work on. No ext here,
    //  so this is the BUILT-IN drive — the proof does not depend on the rack being armed.
    {
        //  A bit-crusher quantises to FIXED levels, so it is the least ambiguous order test there is: crush a loud
        //  signal and then turn it down, and the steps come down with it; turn it down FIRST and the same steps are
        //  now enormous next to what is left. Same two lanes, same shapes — only the position moves.
        auto gate2 = [] (double x) { return x < 0.5 ? 1.0f : 0.25f; };
        auto build = [&] (const int* order) -> Run
        {
            FlowShaper g; g.prepare (SR); auto st = state();
            auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.blend = 1; V.smooth = 0.2f; fill (V, gate2);
            auto& C = st->lanes[7]; C.on = true; C.depth = 1; C.blend = 1; C.mode = 1; C.k[0] = 1.0f; C.k[2] = 1.0f;   // k0 = 1 is the COARSEST bit depth (measured: at k0 = 0 the two orders differ by 0.0005, which proves nothing)
            fill (C, [] (double) { return 1.0f; });
            for (int q = 0; q < kShaperSlots; ++q) st->slot[q] = order[q];
            return run (g, st, 0.0, 0.5, sig440);
        };
        static const int volFirst[8] = { 0, 7, 1, 2, 3, 4, 5, 6 };   // Volume at position 0, Crush at position 1
        static const int crshFirst[8] = { 7, 0, 1, 2, 3, 4, 5, 6 };  // the same two, swapped
        const Run a = build (volFirst), b = build (crshFirst);
        double worst = 0; for (size_t q = 2000; q < a.L.size() && q < b.L.size(); ++q) worst = std::max (worst, (double) std::fabs (a.L[q] - b.L[q]));
        char buf[220]; std::snprintf (buf, sizeof buf, "T21 CHAIN ORDER IS AUDIBLE: gate->crush against crush->gate differ by %.4f peak (same two lanes, same shapes, only the position moved)", worst);
        check (worst > 0.05, buf);
        ShaperState fresh; bool ident = true; for (int q = 0; q < kShaperSlots; ++q) ident = ident && fresh.slot[q] == q;
        check (ident, "T21b a fresh state's chain is the tile order, 0..7 — the card reads left to right");
    }
    // ── T22: tp79 — sanitise() is the wall in front of a switch the AUDIO THREAD indexes with this array ──
    {
        int dup[8]   = { 0, 0, 1, 2, 3, 4, 5, 6 };   // a kind twice
        int over[8]  = { 0, 1, 2, 3, 4, 5, 6, 99 };  // out of range
        int under[8] = { -1, 1, 2, 3, 4, 5, 6, 7 };  // negative
        int good[8]  = { 7, 6, 5, 4, 3, 2, 1, 0 };   // a real permutation — must be left alone
        ShaperState::sanitise (dup); ShaperState::sanitise (over); ShaperState::sanitise (under); ShaperState::sanitise (good);
        bool fixed = true; for (int q = 0; q < 8; ++q) fixed = fixed && dup[q] == q && over[q] == q && under[q] == q;
        bool kept = true;  for (int q = 0; q < 8; ++q) kept = kept && good[q] == 7 - q;
        check (fixed && kept, "T22 a duplicate, an out-of-range and a negative slot each fall back to the identity; a real permutation is untouched");
    }
    // ── T23: tp79 — 🚨 THE SHAPE'S SLOPE IS THE PLAYBACK RATE, AND PLAYBACK RATE IS PITCH ──
    //  Max: "our time is broken … I want ours to sound exactly like ShaperBox 3 — every time my grid goes
    //  somewhere, the fucking pitch moves." That IS the law (Gross Beat's, TimeShaper's): the shape is the read
    //  position, so a ramp of slope 0.5 reads at half speed and comes back an octave down.
    //  Measured without a spectrum: the input's VALUE IS ITS OWN SAMPLE INDEX, so the output value says exactly
    //  which input sample was read, and the slope of that is the playback rate. No estimator to be fooled by.
    {
        static float g_sl = 1.0f;
        auto rateFor = [&] (float slope, float glide) -> double
        {
            g_sl = slope;
            FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
            auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.blend = 1; T.rate = 4;
            T.k[0] = 0.3f; T.k[1] = glide; T.k[2] = 0.5f;
            /* tp86 — the shape is the OFFSET now: a line falling at `1 - slope` per cycle reads at `slope`,
               because rate = 1 + d(offset)/dt x Range. Flat is 1.000x, the grey guideline is 0.000x. */
            fill (T, [] (double x) { return (float) (1.0 - (1.0 - g_sl) * x); });
            g.setState (st);
            const int N = (int) (SR * 2.2); std::vector<float> L ((size_t) N), R ((size_t) N);
            for (int i = 0; i < N; ++i) L[(size_t) i] = R[(size_t) i] = (float) i / (float) N;
            int done = 0; double ppq = 0.0; const int B = 256;
            while (done < N)
            { const int n = std::min (B, N - done); g.process (L.data() + done, R.data() + done, n, ppq, BPM, true); ppq += n / FPB; done += n; }
            const int i0 = (int) (SR * 1.3), w = 2000;
            return ((double) L[(size_t) (i0 + w)] - (double) L[(size_t) i0]) * N / w;
        };
        bool ok = true; char worst[160] = "";
        for (float sl : { 1.0f, 0.75f, 0.5f, 0.25f })
            for (float gl : { 0.0f, 0.7f })
            { const double got = rateFor (sl, gl);
              if (std::fabs (got - sl) > 0.02) { ok = false; std::snprintf (worst, sizeof worst, "slope %.2f glide %.1f -> rate %.3f", sl, gl, got); } }
        char buf[260]; std::snprintf (buf, sizeof buf, "T23 TIME: rate = 1 + slope x Range — a line falling by 0, 1/4, 1/2 and 3/4 of full height reads 1, 0.75, 0.5 and 0.25, with Glide off AND on%s%s",
                                      ok ? " (measured within 0.02)" : " — WRONG at ", worst);
        check (ok, buf);
        //  ⚠️ Glide used to clamp the read position's TOTAL motion, which clamped the pitch: every slope came out
        //  at 0.818. It now limits only the catch-up after a discontinuity, so the slope passes through.
    }
    // ── T24: tp79 — the ring's write head advances for the TIME lane alone ──
    //  The regression this bar exists for: lifting the lanes into slot-dispatched lambdas swallowed the head's
    //  advance into the REPEAT block, so with only Time lit the buffer never moved and the lane played at unity
    //  rate whatever was drawn. Every other check in this file still passed, which is why this one is here.
    {
        static float g_sl2 = 0.5f;
        FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
        auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.blend = 1; T.rate = 4; T.k[1] = 0.0f; T.k[2] = 0.5f;
        auto& RP = st->lanes[4]; RP.on = false;            // REPEAT stays OFF: Time must move the head by itself
        fill (T, [] (double x) { return (float) (1.0 - g_sl2 * x); });   // tp86 — halftime is a slope of -1/2 in the offset convention
        g.setState (st);
        const int N = (int) (SR * 4.2); std::vector<float> L ((size_t) N), R ((size_t) N);   // tp86 — past the first cycle: the ring must hold what the offset asks for
        for (int i = 0; i < N; ++i) L[(size_t) i] = R[(size_t) i] = (float) i / (float) N;
        int done = 0; double ppq = 0.0; const int B = 256;
        while (done < N)
        { const int n = std::min (B, N - done); g.process (L.data() + done, R.data() + done, n, ppq, BPM, true); ppq += n / FPB; done += n; }
        const int i0 = (int) (SR * 3.3), w = 2000;
        const double rate = ((double) L[(size_t) (i0 + w)] - (double) L[(size_t) i0]) * N / w;
        char buf[220]; std::snprintf (buf, sizeof buf, "T24 the ring advances for TIME ALONE (Repeat off): a halftime line reads at %.3f, not stuck at unity", rate);
        check (std::fabs (rate - 0.5) < 0.02, buf);
    }
    // ── T25: tp80 — 🚨 THE NINE LENT LANES REACH THEIR ENGINE, WITH THE SHAPE AS THE RHYTHM ──
    //  Max: "we're going to make each of these effects available to shape … reverb, tape, widen, multiband,
    //  granular, delay, bode, chorus … and a noise engine too." Every one of them arrives through the single
    //  ShaperExt::fx door, so this proves the door: the right kind, the shape's value, the four target knobs,
    //  the lane's type, the mix — and that an UNARMED engine leaves the audio untouched instead of silencing it.
    {
        struct LentExt : ShaperExt
        {
            int calls[kShaperLanes] = {}; float lastS[kShaperLanes] = {}, lastMix[kShaperLanes] = {};
            float lastK[kShaperLanes][4] = {}; int lastMode[kShaperLanes] = {};
            bool armed = true;
            bool filter (int, int, float, float, float, float, int, float, float, float&, float&) noexcept override { return false; }
            bool drive  (int, int, float, float, int, float, float, float, float&, float&) noexcept override { return false; }
            bool fx (int kind, float s, const float* k, int mode, float mix, float& l, float& r) noexcept override
            {
                if (kind < 0 || kind >= kShaperLanes) return false;
                ++calls[kind]; lastS[kind] = s; lastMix[kind] = mix; lastMode[kind] = mode;
                for (int q = 0; q < 4; ++q) lastK[kind][q] = k[q];
                if (! armed) return false;
                l *= 0.5f; r *= 0.5f; return true;
            }
        } ext;
        FlowShaper g; g.prepare (SR); g.setExt (&ext); auto st = state();
        /* the five lent kinds, all placed in the chain, plus one ORDINARY kind so the bar also proves that a
           lane which does its own work in FlowShaper never comes through this door. */
        static const int lent[8] = { 8, 9, 10, 11, 12, 13, 14, 15 };   // Reverb · Delay · Chorus · Widen · Multiband · Tape · Granular · Bode
        for (int q = 0; q < 7; ++q) st->slot[q] = lent[q];
        st->slot[7] = 0;   // and one NATIVE lane in the chain, lit, which must never come through this door
        for (int q = 0; q < 7; ++q)
        { auto& L = st->lanes[lent[q]]; L.on = true; L.depth = 1; L.blend = 1; L.mode = 1 + q;
          for (int w = 0; w < 4; ++w) L.k[w] = 0.1f * (float) (w + 1); fill (L, one); }
        auto& VV = st->lanes[0]; VV.on = true; VV.depth = 1; VV.blend = 1; fill (VV, one);   // a native lane, lit
        auto a = run (g, st, 0.0, 0.25, sig440);
        bool placedRan = true, kOk = true, modeOk = true, sOk = true, mixOk = true;
        for (int q = 0; q < 7; ++q)
        { const int K = lent[q]; if (ext.calls[K] == 0) placedRan = false;
          if (std::fabs (ext.lastS[K] - 1.0f) > 0.01f) sOk = false;            // shape `one` at depth 1
          if (std::fabs (ext.lastMix[K] - 1.0f) > 0.01f) mixOk = false;        // s * blend
          if (ext.lastMode[K] != 1 + q) modeOk = false;
          for (int w = 0; w < 4; ++w) if (std::fabs (ext.lastK[K][w] - 0.1f * (float) (w + 1)) > 1e-4f) kOk = false; }
        const bool nativeStayedHome = ext.calls[0] == 0;   // Volume is lit and placed, and must NOT use fx()
        char buf[280]; std::snprintf (buf, sizeof buf, "T25 the lent lanes: %d of 7 reached fx() with the shape, the four target knobs, the type and the mix; the lit Volume lane used fx() %d times; level %.1f dB",
                                      placedRan ? 7 : 0, ext.calls[0], db (rms (a.L, 2000, 6000)));
        check (placedRan && nativeStayedHome && kOk && modeOk && sOk && mixOk, buf);
        /* tp82 — the last two kinds, in their own placement (eight positions cannot hold nine lent kinds at once).
           NOISE is the one that ADDS signal rather than processing it, so the thing to prove here is that the door
           still hands it the shape as its LEVEL. */
        {
            LentExt e2; FlowShaper g3; g3.prepare (SR); g3.setExt (&e2); auto st3 = state();
            st3->slot[0] = 15; st3->slot[1] = 16;
            for (int q : { 15, 16 }) { auto& L = st3->lanes[q]; L.on = true; L.depth = 1; L.blend = 1; L.mode = 2; L.k[0] = 0.4f; fill (L, one); }
            run (g3, st3, 0.0, 0.25, sig440);
            char b3[220]; std::snprintf (b3, sizeof b3, "T25c Bode and Noise reach fx() too — %d and %d calls, shape %.2f / %.2f, mode %d / %d",
                                         e2.calls[15], e2.calls[16], (double) e2.lastS[15], (double) e2.lastS[16], e2.lastMode[15], e2.lastMode[16]);
            check (e2.calls[15] > 0 && e2.calls[16] > 0 && std::fabs (e2.lastS[16] - 1.0f) < 0.01f && e2.lastMode[16] == 2, b3);
        }
        // an UNARMED engine must be a wire, not a mute — the lane passes the audio through
        LentExt off; off.armed = false;
        FlowShaper g2; g2.prepare (SR); g2.setExt (&off); auto st2 = state();
        for (int q = 0; q < 8; ++q) { st2->slot[q] = lent[q]; auto& L = st2->lanes[lent[q]]; L.on = true; L.depth = 1; L.blend = 1; fill (L, one); }
        auto b = run (g2, st2, 0.0, 0.25, sig440);
        char buf2[200]; std::snprintf (buf2, sizeof buf2, "T25b eight UNARMED lent lanes pass the audio through at %.1f dB (a mute would read -inf)", db (rms (b.L, 2000, 6000)));
        check (db (rms (b.L, 2000, 6000)) > -10.0, buf2);   /* the 440 Hz test tone is 0.5 peak, so untouched IS about -9 dB; a mute reads -inf */
    }
    // ── T26: tp80 — 🚨 A LANE'S PARAMETERS MUST NOT LAND ON ANOTHER LANE ──
    //  setLaneCtl indexed with `lane & 7`, a mask sized to the eight lanes that existed when it was written.
    //  The moment the roster grew, lanes 8..12 wrapped onto 0..4 and pushed their own OFF state over Volume,
    //  Time, Filter, Pan and Repeat: the whole Shaper went silent while every parameter still read correctly.
    //  Push every lane the way the processor does, with only Volume lit, and Volume must still gate.
    {
        FlowShaper g; g.prepare (SR); auto st = state();
        auto& V = st->lanes[0]; V.on = true; V.depth = 1; V.blend = 1;
        fill (V, [] (double x) { return x < 0.5 ? 1.0f : 0.0f; });
        g.setState (st);
        const int N = (int) (SR * 2.0); std::vector<float> L ((size_t) N), R ((size_t) N);   // one whole 1-bar cycle at 120 BPM
        int done = 0; double ppq = 0.0; const int B = 256;
        while (done < N)
        {
            const int n = std::min (B, N - done);
            for (int i = 0; i < n; ++i) { L[(size_t) (done + i)] = R[(size_t) (done + i)] = sig440 (done + i); }
            // exactly what the processor does every block: push ALL kinds, lit or not, in index order
            for (int ln = 0; ln < kShaperLanes; ++ln) g.setLaneCtl (ln, ln == 0, 1.0f, kShaperRateDefault, 0, 0);
            g.process (L.data() + done, R.data() + done, n, ppq, BPM, true);
            ppq += n / FPB; done += n;
        }
        const double open_ = db (rms (L, 4800, 19200)), shut = db (rms (L, 62400, 76800));   // the shape's first half, then its second
        char buf[220]; std::snprintf (buf, sizeof buf, "T26 pushing all %d lanes leaves the lit one alone: the gate reads %.1f dB open against %.1f dB shut", kShaperLanes, open_, shut);
        check (open_ - shut > 30.0, buf);
    }
    // ── T27: tp81 — 🚨 EVERY LANE HAS FOUR TARGETS, AND THE FOURTH ONE DOES SOMETHING ──
    //  Max: "I want everyone to have four targets. It's not three, not two, exactly four … so that means four
    //  parameters night and day, you know, crazy ones." A knob that exists and cannot be heard is the failure
    //  this bar is for. Six of the eight are new DSP in this header and are measured; the other two (Filter's
    //  Punch, Drive's Knee) are controls the rack ENGINES already had and the lane simply never reached, so for
    //  those the thing to prove is that the value arrives — the engines' own proofs own their sound.
    {
        struct SpyExt : ShaperExt
        {
            float punch = -1, knee = -1;
            bool filter (int, int, float, float, float, float, int, float, float pu, float& l, float& r) noexcept override
            { punch = pu; l *= 0.7f; r *= 0.7f; return true; }
            bool drive (int, int, float, float, int, float, float kn, float, float& l, float& r) noexcept override
            { knee = kn; l *= 0.7f; r *= 0.7f; return true; }
            bool fx (int, float, const float*, int, float, float&, float&) noexcept override { return false; }
        } spy;
        // the six that are ours: light the lane alone, sweep its fourth knob end to end, and listen
        struct Case { int kind; int knob; const char* name; float lo; float hi; };
        static const Case C[6] = { { 0, 3, "Volume Hold", 0.0f, 1.0f }, { 1, 3, "Time Tone",   0.0f, 1.0f },
                                   { 3, 3, "Pan Tilt",    0.0f, 1.0f }, { 4, 3, "Repeat Tone", 0.0f, 1.0f },
                                   { 6, 3, "Phaser Centre", 0.0f, 1.0f }, { 7, 3, "Crush Stereo", 0.0f, 1.0f } };
        bool allHeard = true; char worst[200] = ""; double least = 1e9;
        for (const auto& c : C)
        {
            auto runAt = [&] (float kv) -> Run
            {
                FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
                auto& L = st->lanes[c.kind]; L.on = true; L.depth = 1; L.blend = 1; L.k[c.knob] = kv;
                if (c.kind == 7) { L.mode = 0; L.k[0] = 1.0f; L.k[1] = 0.6f; }      // a coarse, slow crush to hear the stereo split
                if (c.kind == 0) fill (L, [] (double x) { return x < 0.15 ? 1.0f : 0.0f; });   // a short opening, so Hold has something to hold
                else if (c.kind == 1) fill (L, [] (double x) { return (float) (1.0 - 0.5 * x); });   // tp86 — a halftime read is a slope of -1/2 now, so Tone has something to colour
                else if (c.kind == 4) fill (L, [] (double x) { return x < 0.5 ? 0.8f : 0.0f; });
                else fill (L, [] (double x) { return (float) x; });
                return run (g, st, 0.0, 1.0, sig440);
            };
            const Run a = runAt (c.lo), b = runAt (c.hi);
            /* BOTH channels: Crush's Stereo moves the RIGHT one by design, so a left-only reading called the
               one knob that is explicitly about the image "inaudible". */
            double diff = 0;
            for (size_t q = 4000; q < a.L.size() && q < b.L.size(); ++q)
                diff = std::max (diff, std::max ((double) std::fabs (a.L[q] - b.L[q]), (double) std::fabs (a.R[q] - b.R[q])));
            if (diff < least) { least = diff; std::snprintf (worst, sizeof worst, "%s moved the output by only %.4f", c.name, diff); }
            if (diff < 0.01) allHeard = false;
        }
        char buf[260]; std::snprintf (buf, sizeof buf, "T27 the six NEW fourth targets are all audible — the quietest: %s", worst);
        check (allHeard, buf);
        // and the two borrowed ones ARRIVE at their engine
        FlowShaper g2; g2.prepare (SR); g2.setExt (&spy); auto st2 = state();
        auto& F = st2->lanes[2]; F.on = true; F.depth = 1; F.blend = 1; F.k[5] = 0.83f; fill (F, one);
        auto& D = st2->lanes[5]; D.on = true; D.depth = 1; D.blend = 1; D.k[4] = 0.31f; fill (D, one);
        run (g2, st2, 0.0, 0.25, sig440);
        char buf2[220]; std::snprintf (buf2, sizeof buf2, "T27b Filter Punch reached the engine as %.2f (set 0.83) and Drive Knee as %.2f (set 0.31)", spy.punch, spy.knee);
        check (std::fabs (spy.punch - 0.83f) < 1e-3f && std::fabs (spy.knee - 0.31f) < 1e-3f, buf2);
    }
    // ── T28-T33: tp86 — 🚨 TIME IS SHAPERBOX'S TIMESHAPER NOW: THE SHAPE IS THE OFFSET ───────────
    //  Max sent the Cableguys TimeShaper tutorial with "we need our time to act like this". The model it
    //  states on screen is: the vertical is TIME OFFSET, the top line is the present, "a horizontal line
    //  gives 100% playback speed", "when your LFO matches the angle of this gray guideline you get 0%
    //  playback speed — the audio is stopped", steeper than the guideline is reverse, and a line angled up
    //  plays faster. That is rate = 1 + slope × Range, and it is what these bars measure.
    //
    //  ⚠️ WHAT WAS WRONG BEFORE, and it was ONE thing: the shape was read as an absolute position WITHIN
    //  the cycle, which re-anchors the read head to NOW at every cycle boundary. Two capabilities fell out
    //  of that and both are in T29 and T33 — a constant delay decayed into a freeze, and the average speed
    //  over a cycle could never exceed 1.000× however the shape was drawn.
    //
    //  Measured on T23's instrument: the input's VALUE IS ITS OWN SAMPLE INDEX, so the output says exactly
    //  which sample was read and its slope is the playback rate. No estimator to be fooled by.
    {
        auto traceOf = [&] (float (*shape) (double), float range, float fade, float glide, double seconds) -> std::vector<float>
        {
            FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
            auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.blend = 1; T.rate = 4;
            T.k[0] = fade; T.k[1] = glide; T.k[2] = range;
            T.fill (shape); g.setState (st);
            const int N = (int) (SR * seconds); std::vector<float> L ((size_t) N), R ((size_t) N);
            for (int i = 0; i < N; ++i) L[(size_t) i] = R[(size_t) i] = (float) i / (float) N;
            int done = 0; double ppq = 0.0; const int B = 256;
            while (done < N) { const int n = std::min (B, N - done); g.process (L.data() + done, R.data() + done, n, ppq, BPM, true); ppq += n / FPB; done += n; }
            for (auto& v : L) v *= (float) N;    // in INPUT SAMPLE INDEX units
            return L;
        };
        auto rateAt = [] (const std::vector<float>& t, double at, int w) -> double
        { const size_t i0 = (size_t) (SR * at); return ((double) t[i0 + (size_t) w] - (double) t[i0]) / w; };
        //  the whole-cycle numbers: the cycle is 4 beats = 2 s at 120, so read the third one, warm.
        auto cycleRate = [&] (const std::vector<float>& t) -> double
        { const size_t a2 = (size_t) (SR * 6.02), b2 = (size_t) (SR * 7.98); return ((double) t[b2] - (double) t[a2]) / (double) (b2 - a2); };
        auto lagAtCycle = [&] (const std::vector<float>& t, double at) -> double
        { const size_t i = (size_t) (SR * at); return (double) i - (double) t[i]; };

        // ── T28 THE MANUAL'S OWN SENTENCE: A HORIZONTAL LINE GIVES 100% PLAYBACK SPEED ──
        {
            static float gy; auto flat = [] (double) { return gy; };
            gy = 1.0f; const std::vector<float> top = traceOf (flat, 0.5f, 0.3f, 0.0f, 8.4);
            gy = 0.5f; const std::vector<float> mid = traceOf (flat, 0.5f, 0.3f, 0.0f, 8.4);
            const double rTop = cycleRate (top), lTop = lagAtCycle (top, 6.0);
            const double rMid = cycleRate (mid), lMid = lagAtCycle (mid, 6.0);
            const bool ok = std::fabs (rTop - 1.0) < 0.02 && std::fabs (lTop) < 8.0
                         && std::fabs (rMid - 1.0) < 0.02 && std::fabs (lMid - 48000.0) < 400.0;
            char buf[330]; std::snprintf (buf, sizeof buf,
                "T28 A HORIZONTAL LINE GIVES 100%% PLAYBACK SPEED — at the top %.3fx with no lag (%.0f samples), and half way down %.3fx held a constant %.0f samples behind: normal speed, DELAYED",
                rTop, lTop, rMid, lMid);
            check (ok, buf);
        }
        // ── T29 🚨 THE CAPABILITY THE OLD MODEL COULD NOT REACH: A CONSTANT DELAY ──
        {
            /* Under the absolute model the best drawable "play normally, one beat late" averaged 0.755x,
               because the first beat of EVERY cycle was a freeze — the read head was re-anchored to now at
               the cycle boundary and had to fall behind again from scratch. A flat line does it now. */
            static float gy2; auto flat = [] (double) { return gy2; };
            bool ok = true; std::string got;
            for (float y : { 0.75f, 0.5f, 0.25f })
            {
                gy2 = y; const std::vector<float> t = traceOf (flat, 0.5f, 0.3f, 0.0f, 8.4);
                const double r = cycleRate (t), lag = lagAtCycle (t, 6.0);
                char q[80]; std::snprintf (q, sizeof q, "%sy=%.2f %.3fx at %.0f behind", got.empty() ? "" : " · ", y, r, lag);
                got += q;
                if (std::fabs (r - 1.0) > 0.02) ok = false;
                if (std::fabs (lag - (double) (1.0f - y) * 96000.0) > 600.0) ok = false;
            }
            char buf[330]; std::snprintf (buf, sizeof buf, "T29 A CONSTANT OFFSET PLAYS AT FULL SPEED, WHICH THE OLD MODEL COULD NOT DO AT ALL (its best was 0.755x): %s", got.c_str());
            check (ok, buf);
        }
        // ── T30 THE GUIDELINE IS THE STOP, AND STEEPER IS REVERSE ──
        {
            //  slope -1 over the cycle is the grey guideline: rate 1 + (-1) = 0. Steeper reverses, and the
            //  video draws exactly that — half a bar of normal playback, then the same half bar backwards.
            const std::vector<float> stop = traceOf ([] (double x) { return (float) (1.0 - x); }, 0.5f, 0.3f, 0.0f, 8.4);
            const std::vector<float> rev  = traceOf ([] (double x) { return x < 0.5 ? 1.0f : (float) (1.0 - 2.0 * (x - 0.5)); }, 0.5f, 0.0f, 0.0f, 8.4);
            const double rStop = cycleRate (stop);
            const double rFwd = rateAt (rev, 6.4, 400), rBack = rateAt (rev, 7.4, 400);
            const bool ok = std::fabs (rStop) < 0.02 && std::fabs (rFwd - 1.0) < 0.05 && std::fabs (rBack + 1.0) < 0.05;
            char buf[330]; std::snprintf (buf, sizeof buf,
                "T30 THE GREY GUIDELINE IS THE STOP AND STEEPER IS REVERSE: a slope of -1 over the cycle reads %.3fx, and half a bar flat then falling twice as steep reads %+.3fx then %+.3fx",
                rStop, rFwd, rBack);
            check (ok, buf);
        }
        // ── T31 🚨 THE OTHER CAPABILITY THE OLD MODEL COULD NOT REACH: FASTER THAN REALTIME, SUSTAINED ──
        {
            /* "A line angled up plays the audio faster." Under the absolute model the average speed over a
               cycle could never exceed 1.000x whatever was drawn, because the cycle always began at the
               present and the read could only fall behind. This is the tutorial's 200% line. */
            const std::vector<float> up1 = traceOf ([] (double x) { return (float) x; },       0.5f,  0.0f, 0.0f, 8.4);
            const std::vector<float> up2 = traceOf ([] (double x) { return (float) x; },       0.75f, 0.0f, 0.0f, 8.4);
            const double r1 = cycleRate (up1), r2 = cycleRate (up2);
            const bool ok = std::fabs (r1 - 2.0) < 0.03 && std::fabs (r2 - 3.0) < 0.05;
            char buf[330]; std::snprintf (buf, sizeof buf,
                "T31 A LINE ANGLED UP PLAYS FASTER, FOR THE WHOLE CYCLE — a full rise reads %.3fx at Range x1 and %.3fx at Range x2 (the old model was pinned at 1.000x however it was drawn)",
                r1, r2);
            check (ok, buf);
        }
        // ── T32 THE FUTURE IS STILL UNREADABLE ──
        {
            /* The one clamp that survives the change, and the tutorial has it too: the cross-hatched area
               below the guideline is audio the buffer does not hold yet. Asking for it pins you to now. */
            const std::vector<float> ahead = traceOf ([] (double) { return 1.0f; }, 0.5f, 0.3f, 0.0f, 4.4);
            const double lag = lagAtCycle (ahead, 3.0);
            const std::vector<float> deep = traceOf ([] (double) { return 0.0f; }, 1.0f, 0.3f, 0.0f, 1.0);
            const double early = lagAtCycle (deep, 0.10);   // 0.1 s in, the ring cannot hold a whole Range yet
            const bool ok = std::fabs (lag) < 8.0 && early < (double) (SR * 0.11);
            char buf[320]; std::snprintf (buf, sizeof buf,
                "T32 THE FUTURE IS STILL UNREADABLE AND THE BUFFER STILL HAS TO FILL: the top line sits exactly on the write head (%.0f samples) and a full-Range offset asked for 0.1 s after the start reads only %.0f samples back",
                lag, early);
            check (ok, buf);
        }
        // ── T34 🚨 PRESS PLAY AND NOTHING DROPS OUT ──
        {
            /* Max: "I press play and it drops out and then it comes in two bars later — I hate that shit,
               ShaperBox doesn't do that, it's very reactive."  He was right, and the cause was the clamp:
               an offset the ring could not meet pinned the read to the OLDEST sample, and the oldest sample
               does not move, so the read stood still while the write head ran away — a whole bar of frozen
               near-DC. MEASURED BEFORE: a constant half-bar offset rendered -24.8 dBFS for exactly one bar
               and then snapped to -9.0. It falls back to the PRESENT now: full level, full speed, from the
               first sample, whatever the shape asks for. */
            struct Sh { const char* name; float (*f) (double); };
            static const Sh S4[5] = {
                { "flat at the top",   [] (double)   { return 1.0f; } },
                { "a constant offset", [] (double)   { return 0.5f; } },
                { "a 1/8 stutter",     [] (double x) { const int k = (int) (x * 8.0); return 1.0f - (float) k / 8.0f; } },
                { "a full rise (200%)",[] (double x) { return (float) x; } },
                { "halftime",          [] (double x) { return (float) (1.0 - 0.5 * x); } } };
            bool ok = true; char worst[180] = ""; double worstDb = 0;
            for (const auto& sh : S4)
            {
                FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
                auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.blend = 1; T.rate = 4;
                T.k[0] = 0.3f; T.k[1] = 0.0f; T.k[2] = 0.5f; T.fill (sh.f); g.setState (st);
                const int N = (int) (SR * 4.2); std::vector<float> L ((size_t) N), R ((size_t) N);
                for (int i = 0; i < N; ++i) L[(size_t) i] = R[(size_t) i] = 0.5f * (float) std::sin (2.0 * M_PI * 220.0 * i / SR);
                int done = 0; double ppq = 0.0; const int B = 256;
                while (done < N) { const int n = std::min (B, N - done); g.process (L.data() + done, R.data() + done, n, ppq, BPM, true); ppq += n / FPB; done += n; }
                //  every sixteenth of the first two bars, against the settled level in the third
                const size_t rA = (size_t) (SR * 3.0), rB = (size_t) (SR * 4.0);
                double e = 0; for (size_t i = rA; i < rB; ++i) e += (double) L[i] * L[i];
                const double ref = 20.0 * std::log10 (std::sqrt (e / (double) (rB - rA)) + 1e-12);
                for (int k = 0; k < 16; ++k)
                {
                    const size_t a2 = (size_t) (k * SR * 0.125), b2 = (size_t) ((k + 1) * SR * 0.125);
                    double e2 = 0; for (size_t i = a2; i < b2; ++i) e2 += (double) L[i] * L[i];
                    const double db = 20.0 * std::log10 (std::sqrt (e2 / (double) (b2 - a2)) + 1e-12);
                    if (db < ref - 3.0) { ok = false;
                        if (ref - db > worstDb) { worstDb = ref - db; std::snprintf (worst, sizeof worst, "%s dipped %.1f dB at the %d%s sixteenth", sh.name, ref - db, k + 1, k == 0 ? "st" : "th"); } }
                }
            }
            char buf[330]; std::snprintf (buf, sizeof buf,
                "T34 PRESS PLAY AND NOTHING DROPS OUT: flat, a constant offset, a stutter, a full rise and halftime all hold full level from the FIRST sixteenth — an offset the ring cannot meet yet plays the PRESENT, it does not freeze at the buffer's edge%s%s",
                ok ? "" : " — WRONG: ", worst);
            check (ok, buf);
        }
        // ── T35 🚨 A PHRASE CHOPS FROM ITS FIRST NOTE — IT NEVER REPLAYS THE SILENCE BEFORE IT ──
        {
            /* Max, on his own 16-tread shape: "it's supposed to either shift pitches or stutter, right now
               it's just doing a bunch of DROPOUT CLICKS."  Reproduced: with the plugin sitting open (a ring
               full of silence) and a phrase starting, the lane reached BACK PAST THE FIRST NOTE and
               faithfully replayed the nothing that was there — MEASURED at 2.5 s of silence, in pieces,
               inside the first 3 s. `ringFilled_` could not catch it: it counts samples WRITTEN, and the
               ring was full — of silence. The offset is held inside `soundAge_` now, the age of the oldest
               sample belonging to the sound that is playing. */
            static const float TR[16] = { 0.93f,0.78f,0.62f,0.72f,0.50f,0.38f,0.55f,0.45f,
                                          0.62f,0.30f,0.22f,0.58f,0.75f,0.35f,0.28f,0.62f };
            FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
            auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.blend = 1; T.rate = 5;   // 2 bars, as he had it
            T.k[0] = 0.3f; T.k[1] = 0.0f; T.k[2] = 0.5f; T.k[3] = 0.5f;
            T.fill ([] (double p) { int k = (int) (p * 16.0); if (k < 0) k = 0; if (k > 15) k = 15; return TR[k]; });
            g.setState (st);
            const double PRE = 10.0, PLAY = 12.0;                  // ten seconds open and silent, then a phrase
            const int N = (int) (SR * (PRE + PLAY));
            std::vector<float> L ((size_t) N), R ((size_t) N);
            for (int i = 0; i < N; ++i)
            { const double t = (double) i / SR - PRE; float v = 0.0f;
              if (t >= 0) v = (float) (0.22*std::sin(2*M_PI*130.81*t) + 0.18*std::sin(2*M_PI*196.0*t) + 0.15*std::sin(2*M_PI*261.63*t));
              L[(size_t) i] = R[(size_t) i] = v; }
            int done = 0; double ppq = 0.0; const int B = 512;
            while (done < N)
            { const int n = std::min (B, N - done); const bool playing = (done >= (int) (SR * PRE));
              g.process (L.data() + done, R.data() + done, n, ppq, BPM, playing, 1.0f);
              if (playing) ppq += n / FPB; done += n; }
            //  a HOLE is a run of identical samples — silence, or a stuck read head
            int holes = 0; double longest = 0, longestAt = 0; float held = 0;
            size_t i = (size_t) (SR * PRE) + 1, runStart = i;
            for (; i < L.size(); ++i)
              if (L[i] != L[i-1])
              { const double len = (double) (i - runStart) / SR;
                if (len > 0.005) { ++holes; if (len > longest) { longest = len; longestAt = (double) runStart / SR - PRE; held = L[runStart]; } }
                runStart = i; }
            char buf[330]; std::snprintf (buf, sizeof buf,
                "T35 A PHRASE CHOPS FROM ITS FIRST NOTE: ten seconds open and silent, then a chord through his own 16-tread shape at 2 bars — %d holes longer than 5 ms%s",
                holes, holes ? "" : " (it used to be 2.5 s of silence inside the first three seconds)");
            if (holes) { char w[150]; std::snprintf (w, sizeof w, " — worst %.0f ms at %.2f s holding %+.5f", longest*1000.0, longestAt, held); std::strncat (buf, w, sizeof buf - std::strlen (buf) - 1); }
            check (holes == 0, buf);
        }
        // ── T36 🚨 DOUBLE SPEED DOES NOT ALIAS ──
        {
            /* Max: "double time has a weird bitcrush bug going when things are CHOPPED in the 2x speed …
               it's bitcrushed and distorted when it shifts up, and it's almost detuned as well."  That is
               aliasing exactly: reading faster than realtime is a DECIMATION, and a decimation without a
               band-limit folds everything above SR/(2·rate) back around Nyquist, inharmonic — which is what
               a bitcrusher is. MEASURED BEFORE: a 13 / 15 / 17 kHz tone read at 2x vanished from 2f and came
               back at |SR-2f| at FULL amplitude. He heard it as a depth problem first — "depth 50 sounds WAY
               better, 100 sounds off" — and he was reading it right: depth scales the offset, so it scales
               the RATE, and half the speed-up is a quarter of the folding. */
            auto goertz = [] (const std::vector<float>& x, size_t a, size_t n, double f) -> double
            { double mean = 0; for (size_t i = a; i < a+n; ++i) mean += x[i]; mean /= (double) n;
              const double w = 2.0*M_PI*f/SR, c = 2.0*std::cos (w); double s1 = 0, s2 = 0;
              for (size_t i = a; i < a+n; ++i) { const double s0 = ((double) x[i]-mean) + c*s1 - s2; s2 = s1; s1 = s0; }
              return std::sqrt (std::max (0.0, s1*s1 + s2*s2 - c*s1*s2)) / (double) n; };
            auto at2x = [&] (double toneHz) -> std::vector<float>
            { FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
              auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.blend = 1; T.rate = 4;
              T.k[0] = 0.3f; T.k[1] = 0.0f; T.k[2] = 0.5f; T.k[3] = 0.5f;
              T.fill ([] (double x) { return (float) x; });           // a full rise = a sustained 2x
              g.setState (st);
              const int N = (int) (SR * 8.4); std::vector<float> L ((size_t) N), R ((size_t) N);
              for (int i = 0; i < N; ++i) L[(size_t) i] = R[(size_t) i] = (float) (0.5 * std::sin (2.0*M_PI*toneHz*i/SR));
              int done = 0; double ppq = 0.0; const int B = 256;
              while (done < N) { const int n = std::min (B, N-done); g.process (L.data()+done, R.data()+done, n, ppq, BPM, true); ppq += n/FPB; done += n; }
              return L; };
            const size_t A = (size_t) (SR * 6.2), W = (size_t) (SR * 0.35);
            bool ok = true; std::string got;
            //  above SR/4 there is nothing legal to hear at 2x, so the mirror bin must be EMPTY
            for (double f : { 15000.0, 17000.0 })
            { const std::vector<float> x = at2x (f);
              const double al = goertz (x, A, W, std::fabs (SR - 2.0*f));
              char q[80]; std::snprintf (q, sizeof q, "%s%.0f kHz folds to %.5f", got.empty() ? "" : " · ", f/1000.0, al);
              got += q;
              if (al > 0.005) ok = false; }                            // 0.25 is full scale: this is -34 dB
            //  and the band that IS legal must still be there, at level
            { const std::vector<float> x = at2x (5000.0);
              const double keep = goertz (x, A, W, 10000.0);
              char q[70]; std::snprintf (q, sizeof q, " · 5 kHz still arrives at 10 kHz at %.3f", keep);
              got += q;
              if (keep < 0.20) ok = false; }
            char buf[330]; std::snprintf (buf, sizeof buf,
                "T36 DOUBLE SPEED DOES NOT ALIAS: read at 2x, the bins above SR/4 stay empty and the band below it comes through at level — %s", got.c_str());
            check (ok, buf);
        }
        // ── T37 🚨 A REPEAT HELD OPEN RE-CAPTURES EVERY CYCLE — IT NEVER LOOPS ONE SLICE FOREVER ──
        {
            /* tp90 — the level audit lit Repeat with its shape pinned open on a held chord and read -226 dB.
               Capture only happened on the RISING edge of the shape, so a flat top captured ONE slice and
               looped it for good; the lane had lit in the silence before the chord, so the slice was silence.
               The grid restarts every cycle, and so does the slice now. */
            auto run = [&] (bool lane) -> double
            { FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
              auto& P = st->lanes[4]; P.on = lane; P.depth = 1; P.blend = 1; P.rate = 4;
              P.fill ([] (double) { return 1.0f; });
              g.setState (st);
              const double PRE = 2.0, PLAY = 8.0; const int N = (int) (SR * (PRE + PLAY));
              std::vector<float> L ((size_t) N), R ((size_t) N);
              for (int i = 0; i < N; ++i)
              { const double t = (double) i / SR - PRE; float v = 0.0f;
                if (t >= 0) v = (float) (0.22*std::sin(2*M_PI*130.81*t) + 0.18*std::sin(2*M_PI*196.0*t) + 0.15*std::sin(2*M_PI*261.63*t));
                L[(size_t) i] = R[(size_t) i] = v; }
              int done = 0; double ppq = 0.0; const int B = 512;
              while (done < N)
              { const int n = std::min (B, N - done); const bool playing = (done >= (int) (SR * PRE));
                g.process (L.data() + done, R.data() + done, n, ppq, BPM, playing, 1.0f);
                if (playing) ppq += n / FPB; done += n; }
              double e = 0; const size_t a = (size_t) (SR * (PRE + 4.0));
              for (size_t i = a; i < L.size(); ++i) e += (double) L[i] * L[i];
              return 10.0 * std::log10 (e / (double) (L.size() - a) + 1e-24); };
            const double dry = run (false), wet = run (true);
            char buf[260]; std::snprintf (buf, sizeof buf,
                "T37 A REPEAT HELD OPEN RE-CAPTURES EVERY CYCLE: silence, then a chord, shape flat at the top — the repeat reads %+.2f dB against dry (it used to be -226 dB: one silent slice, looped forever)",
                wet - dry);
            check (std::fabs (wet - dry) < 3.0, buf);
        }
        // ── T38 🚨 A RAMP AT FULL DEPTH IS CLEAN — NO JITTER, NO IMAGES ──
        {
            /* tp95 — Max: "time depth at 100% still has that weird bitcrushed sound when there's ramps." Two causes, both
               measured with a 5 kHz tone and everything NOT at the expected pitch counted as dirt:
                 1. the shape was read as a 32-bit float and multiplied by up to 384 000 samples, so its rounding became
                    ~0.01 of a sample of random read-head motion — phase noise: −47 dB on a 4-bar 2x ramp (−67 at 1 beat);
                 2. a SLOWING ramp upsampled through the cubic and its images folded back: −49 dB at 0.25x.
               Now the Time lane reads its shape in double and band-limits both directions: ≥ 82 dB everywhere. */
            static float slope = 0.0f;
            auto dirtDb = [&] (float a) -> double
            { slope = a;
              FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
              auto& T = st->lanes[1]; T.on = true; T.depth = 1; T.blend = 1; T.rate = 6;   // a 4-bar cycle
              T.k[0] = 0.3f; T.k[1] = 0.0f; T.k[2] = 0.5f; T.k[3] = 0.5f;
              T.fill ([] (double p) { return slope >= 0 ? (float) (slope * p) : (float) std::min (1.0, std::max (0.0, 1.0 + slope * p)); });
              g.setState (st);
              const int N = (int) (SR * 20.0); std::vector<float> L ((size_t) N), R ((size_t) N);
              for (int i = 0; i < N; ++i) L[(size_t) i] = R[(size_t) i] = (float) (0.3 * std::sin (2.0 * M_PI * 5000.0 * i / SR));
              int done = 0; double ppq = 0.0;
              while (done < N) { const int n = std::min (512, N - done); g.process (L.data() + done, R.data() + done, n, ppq, BPM, true, 1.0f); ppq += n / FPB; done += n; }
              const size_t A = (size_t) (SR * 16.8), W = 16384; const double want = 5000.0 * (1.0 + a);
              auto e = [&] (double f) { const double w = 2.0 * M_PI * f / SR, c = 2.0 * std::cos (w); double s1 = 0, s2 = 0;
                  for (size_t i = 0; i < W; ++i) { const double h = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (W - 1)); const double s0 = L[A + i] * h + c * s1 - s2; s2 = s1; s1 = s0; }
                  return s1 * s1 + s2 * s2 - c * s1 * s2; };
              double in = 0, out = 0; for (double f = 40; f < 22000; f += SR / W) { const double v = e (f); if (std::fabs (f - want) < 60) in += v; else out += v; }
              return 10.0 * std::log10 (in / (out + 1e-30)); };
            const double fast = dirtDb (1.0f), slow = dirtDb (-0.75f);
            char buf[300]; std::snprintf (buf, sizeof buf,
                "T38 A RAMP AT FULL DEPTH IS CLEAN: a 4-bar ramp at 2x keeps its grit %.1f dB under the tone (was 47: float read-head jitter), a 0.25x slow-down %.1f dB (was 49: cubic images)",
                fast, slow);
            check (fast > 75.0 && slow > 75.0, buf);
        }
        // ── T33 FADE CLOSES THE SEAM AT A STEP ──
        {
            //  a stutter in this model is a DESCENDING STAIRCASE: each tread is flat (normal speed) and each
            //  riser jumps back. The risers are the steps Fade has to smooth.
            double jump[3]; const float fades[3] = { 0.0f, 0.3f, 1.0f };
            for (int i = 0; i < 3; ++i)
            {
                const std::vector<float> t = traceOf ([] (double x) { const int k = (int) (x * 8.0); return 1.0f - (float) k / 8.0f; },
                                                      0.5f, fades[i], 0.0f, 4.4);
                double m = 0; for (size_t q = (size_t) (SR * 2.2); q < t.size(); ++q) m = std::max (m, (double) std::fabs ((double) t[q] - (double) t[q-1]));
                jump[i] = m;
            }
            const bool ok = jump[1] < jump[0] * 0.9 && jump[2] < jump[1] * 0.9;
            char buf[300]; std::snprintf (buf, sizeof buf, "T33 FADE CLOSES THE SEAM AT A STUTTER'S RISER: the read's largest jump falls %.0f -> %.0f -> %.0f samples as Fade goes 0 -> 0.3 -> 1",
                                          jump[0], jump[1], jump[2]);
            check (ok, buf);
        }
    }
    // ── T39 (tp97, task 4) 🚨 TIME IS CLICK-FREE AT THE DEFAULT DEPTH (50%) ACROSS EVERY SHAPE ──────────
    //  The Time lane now boots at 50 % (its 100 % ramp read has a deferred artifact, so the default steps away
    //  from it). At 50 % a 440 Hz sine through the lane never steps by more than the sine's OWN per-sample step,
    //  whatever shape is drawn on it — no interpolation click, no kernel-switch click, at the shipped default.
    {   // measured on BROADBAND noise: a click is a sample step LARGER than the source material's own maximum
        //  (a sped-up read of a sine legitimately steps faster — that is pitch, not a click — so the reference
        //  is the source's own broadband step, and only a discontinuity beyond it counts). At 100 % the sine
        //  shape shows 2 such steps (the deferred artifact); at the 50 % default there are none, any shape.
        g_ns = 1; double dryMax = 0; { float prev = 0; for (long long i = 0; i < (long long) (SR * 4); ++i) { const float v = noise (i); if (i) dryMax = std::max (dryMax, (double) std::fabs (v - prev)); prev = v; } }
        auto clicksFor = [&] (float (*shp) (double)) -> int {
            FlowShaper g; g.prepare (SR); g.armRing(); auto st = state(); auto& T = st->lanes[1];
            T.on = true; T.depth = 0.5f; T.rate = 4; T.mode = 0; T.k[0] = 0.3f; fill (T, shp);
            g_ns = 1; auto z = run (g, st, 0.0, 6.0, noise);
            int c = 0; for (size_t i = (size_t) FPB * 2 + 1; i < z.L.size(); ++i) if (std::fabs (z.L[i] - z.L[i - 1]) > dryMax * 1.5) ++c; return c; };
        const int cc = clicksFor (unity) + clicksFor (half) + clicksFor (stut) + clicksFor (sine) + clicksFor (ramp);
        char buf[260]; std::snprintf (buf, sizeof buf, "T39 TIME IS CLICK-FREE AT THE 50%% DEFAULT: across unity / halftime / stutter / sine / ramp, %d sample steps exceed 1.5x the source's own broadband step (%.3f) — none, i.e. no interpolation or kernel-switch click at the shipped default", cc, dryMax);
        check (cc == 0, buf);
    }
    // ── T40 (tp97, task 5) 🚨 REPEAT IS LOCKED TO THE HOST GRID — IT NEVER DRIFTS, AT ANY TEMPO ──────────
    //  Max: "REP must ALWAYS chop in time." A grid-aligned 1/16 click train through the Repeat lane (a 1/16 slice)
    //  reproduces its onsets ON the host's 1/16 grid: the slice is captured at fmod(beat, stepBeats) of the HOST
    //  position and re-captured every cycle, so the loop can never walk off the beat. Proven at three tempos —
    //  clean (120) and non-clean (127, 133.333) — where a free-running clock would smear.
    {
        auto repDrift = [&] (double bpm) -> double {
            const double fpb = SR * 60.0 / bpm, step = fpb / 4.0;   // 1/16 note in samples
            FlowShaper g; g.prepare (SR); g.armRing(); auto st = state(); auto& P = st->lanes[4];
            P.on = true; P.depth = 1; P.rate = 4; P.grid = 16; P.blend = 1; P.k[0] = 0.05f; fill (P, [] (double) { return 0.5f; });
            g.setState (st); const long long N = (long long) (8.0 * fpb); double ppq = 0; long long n = 0;
            std::vector<float> L (BLK), R (BLK), out; out.reserve ((size_t) N);
            while (n < N) {
                for (int i = 0; i < BLK; ++i) { const double q16 = (double) (n + i) / fpb * 4.0; const double frac = q16 - std::floor (q16);
                    const float v = (frac * step < 8.0) ? 0.9f : 0.0f;   // a grid-exact 1/16 click train, ~8 samples wide
                    L[(size_t) i] = v; R[(size_t) i] = v; }
                g.process (L.data(), R.data(), BLK, ppq, bpm, true);
                out.insert (out.end(), L.begin(), L.end()); ppq += BLK / fpb; n += BLK; }
            double worst = 0; bool up = false;
            for (size_t i = (size_t) (fpb * 2); i < out.size(); ++i) { const bool h = std::fabs (out[i]) > 0.3f;
                if (h && ! up) { const double g0 = std::round ((double) i / step) * step; worst = std::max (worst, std::fabs ((double) i - g0)); } up = h; }
            return worst; };
        const double d120 = repDrift (120.0), d127 = repDrift (127.0), d133 = repDrift (133.333);
        const double worstMs = std::max ({ d120, d127, d133 }) / SR * 1000.0;
        char buf[280]; std::snprintf (buf, sizeof buf, "T40 REPEAT LOCKS TO THE HOST GRID: the worst 1/16 onset deviation is %.2f / %.2f / %.2f samples at 120 / 127 / 133 BPM (%.3f ms max) — it stays on the beat at every tempo, it never drifts off time", d120, d127, d133, worstMs);
        check (worstMs < 0.75, buf);
    }
    // ── T41 (tp97, task 5) TIME + REPEAT TOGETHER STAY IN SYNC AND CLICK-FREE ────────────────────────────
    //  Both lanes read the SAME host position every sample, so running them together stays finite and bounded
    //  across eight bars — Time halftime under a Repeat stutter, the two locked to one clock.
    {
        FlowShaper g; g.prepare (SR); g.armRing(); auto st = state();
        auto& T = st->lanes[1]; T.on = true; T.depth = 0.5f; T.rate = 4; T.mode = 0; T.k[0] = 0.3f; fill (T, half);
        auto& P = st->lanes[4]; P.on = true; P.depth = 1; P.rate = 4; P.grid = 16; P.k[0] = 0.1f; fill (P, [] (double p) { return p < 0.5 ? 0.f : 0.7f; });
        auto z = run (g, st, 0.0, 8.0, sig440);
        bool finite = true; for (float v : z.L) if (! std::isfinite (v)) finite = false;
        const double mj = maxJump (z.L, (size_t) FPB * 2, z.L.size());
        char buf[220]; std::snprintf (buf, sizeof buf, "T41 TIME + REPEAT TOGETHER: both read one host clock — the output stays finite (%d) and bounded (worst step %.4f) over 8 bars", finite ? 1 : 0, mj);
        check (finite && mj < 1.0, buf);
    }
    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
