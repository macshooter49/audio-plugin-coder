// =============================================================================
//  SampleEngine_test.cpp — offline proof for the Sample engine's loop modes,
//  with the HEADLINE focus on PING-PONG declick via X-Fade.
//
//  g++ -std=c++17 -Wall -Wextra -ISource Source/SampleEngine_test.cpp -o /tmp/se && /tmp/se
//
//  THE BUG (reported by Max): Ping-Pong loop mode clicks on every turnaround
//  even with X-Fade up — i.e. the X-Fade does nothing in Ping-Pong. Forward /
//  Reverse / Tailed are clean. Root cause: readFrame() applies the content
//  crossfade for Forward / Tailed / Reverse but has NO Ping-Pong branch, so the
//  slope-reversal corner at each bounce is never rounded.
//
//  HOW WE MEASURE A "CLICK": a slope reversal is a corner in the waveform — a
//  spike in the discrete SECOND difference  |y[n-1] - 2 y[n] + y[n+1]|. On a
//  smooth sine read this stays at the tiny curvature floor; at a naked ping-pong
//  turnaround it jumps to ~2 * (per-sample slope). The fix must collapse that
//  turn-region spike toward the smooth floor WHEN X-Fade is up, while leaving the
//  X-Fade=0 path (naked reflection) untouched.
// =============================================================================
#include "SampleEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>

using tw::SampleEngine;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what)
{
    ++g_checks;
    if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); }
}

constexpr double SR = 48000.0;
constexpr double TWO_PI = 6.283185307179586;

// ── synthetic buffer: a clean sine whose zero-crossings (max slope, ~zero
//    curvature) land exactly on the loop boundaries, so a turnaround corner
//    stands out cleanly against the smooth-curvature floor. ───────────────────
constexpr int    PERIOD = 100;        // samples per sine cycle
constexpr int    NBUF   = 6001;       // 60 periods + 1
constexpr float  AMP    = 0.6f;

static std::vector<float> makeSine()
{
    std::vector<float> b ((size_t) NBUF);
    for (int i = 0; i < NBUF; ++i) b[(size_t) i] = AMP * (float) std::sin (TWO_PI * (double) i / (double) PERIOD);
    return b;
}

struct Run { std::vector<float> out, pos; };

// Drive the engine for `frames` mono samples with a given loop mode + xfade.
// Loop placed at [1000,5000] — both rising zero-crossings, and ≥ xfadeLen away
// from the buffer edges so the CONTENT crossfade is the one under test, not the
// buffer-edge seam-fade fallback.
static Run drive (SampleEngine::LoopMode mode, float xfade, int frames, float scan = 0.5f)
{
    static std::vector<float> buf = makeSine();
    const float* chans[1] = { buf.data() };

    SampleEngine e;
    e.prepare (SR);
    e.setSample (chans, 1, NBUF, SR);
    e.setRegion (0.0f, 1.0f);
    e.setLoop (1000.0f / (NBUF - 1), 5000.0f / (NBUF - 1));
    e.setScan (scan);                       // 0.5 = +1x forward (scanRate_ = 1.0)
    e.setXFade (xfade);
    e.setLoopMode (mode);
    e.setPitchRatio (1.0);                  // inc = 1 sample / frame
    e.noteOn (1.0, 0.0f, 0x1234u);          // no spray

    Run r; r.out.reserve ((size_t) frames); r.pos.reserve ((size_t) frames);
    for (int n = 0; n < frames; ++n)
    {
        r.pos.push_back ((float) e.positionForTesting());
        r.out.push_back (e.tickMono());
    }
    return r;
}

static inline float secondDiff (const std::vector<float>& y, int n)
{
    return std::fabs (y[(size_t) (n - 1)] - 2.0f * y[(size_t) n] + y[(size_t) (n + 1)]);
}

// Max |2nd diff| in ±W frames around each interior turnaround (local pos extremum),
// skipping the lead-in / onset (first `skip` frames).
static float turnSpike (const Run& r, int skip = 1200, int W = 40)
{
    const int F = (int) r.out.size();
    float worst = 0.f;
    for (int n = skip + 1; n < F - 1; ++n)
    {
        const float d0 = r.pos[(size_t) n]     - r.pos[(size_t) (n - 1)];
        const float d1 = r.pos[(size_t) (n + 1)] - r.pos[(size_t) n];
        const bool turn = (d0 > 0.f && d1 < 0.f) || (d0 < 0.f && d1 > 0.f);   // direction flip
        if (! turn) continue;
        const int lo = (n - W < skip + 1) ? skip + 1 : n - W;
        const int hi = (n + W > F - 2)    ? F - 2    : n + W;
        for (int k = lo; k <= hi; ++k) { const float s = secondDiff (r.out, k); if (s > worst) worst = s; }
    }
    return worst;
}

// Max |2nd diff| over the whole steady-state body (any-mode click probe).
static float bodySpike (const Run& r, int skip = 1200)
{
    const int F = (int) r.out.size();
    float worst = 0.f;
    for (int n = skip + 1; n < F - 1; ++n) { const float s = secondDiff (r.out, n); if (s > worst) worst = s; }
    return worst;
}

static bool finiteBounded (const Run& r)
{
    for (float v : r.out) if (! std::isfinite (v) || std::fabs (v) > 1.3f) return false;
    return true;
}

int main()
{
    const int F = 20000;   // lead-in (~1000) + ~4 interior turnarounds

    // ── HEADLINE: Ping-Pong turnaround corner, X-Fade off vs on ──────────────
    Run ppOff = drive (SampleEngine::LoopMode::PingPong, 0.0f,  F);
    Run ppOn  = drive (SampleEngine::LoopMode::PingPong, 0.25f, F);

    const float spikeOff = turnSpike (ppOff);
    const float spikeOn  = turnSpike (ppOn);
    std::printf ("ping-pong turn spike:  xfade=0 -> %.5f   xfade=0.25 -> %.5f   (ratio %.2fx)\n",
                 spikeOff, spikeOn, (spikeOn > 1e-9f ? spikeOff / spikeOn : 999.f));

    // The test must actually expose a real corner with X-Fade OFF (else it proves nothing).
    check (spikeOff > 0.02f, "naked ping-pong has an audible turnaround corner (test exposes the bug)");
    // THE FIX: X-Fade must round that corner — at least a 3x reduction of the turn spike...
    check (spikeOn < spikeOff * 0.34f, "X-Fade rounds the ping-pong turnaround corner (>=3x reduction)");
    // ...and bring it down near the smooth-curvature floor (no audible click).
    check (spikeOn < 0.012f, "X-Fade ping-pong corner reduced to near the smooth-curvature floor");

    check (finiteBounded (ppOff) && finiteBounded (ppOn), "ping-pong output finite & bounded");

    // Reverse-SCAN ping-pong (playhead starts heading backward): the mirror crossfade is
    // purely position-based, so it must declick the turnarounds regardless of travel direction.
    Run ppRevOff = drive (SampleEngine::LoopMode::PingPong, 0.0f,  F, -0.5f);
    Run ppRevOn  = drive (SampleEngine::LoopMode::PingPong, 0.25f, F, -0.5f);
    const float revOff = turnSpike (ppRevOff), revOn = turnSpike (ppRevOn);
    std::printf ("ping-pong (rev scan) turn spike:  xfade=0 -> %.5f   xfade=0.25 -> %.5f\n", revOff, revOn);
    check (revOff > 0.02f, "reverse-scan ping-pong also has a naked turnaround corner");
    check (revOn < revOff * 0.34f, "X-Fade declicks reverse-scan ping-pong too (>=3x reduction)");
    check (finiteBounded (ppRevOn), "reverse-scan ping-pong output finite & bounded");

    // ── SWELL GUARD (adversarial-review finding) ─────────────────────────────
    // At the turn the mirror read == the actual read (perfectly correlated). The crossfade must
    // use EQUAL-GAIN (gA+gM==1), NOT equal-power (0.707/0.707) — else two correlated reads sum to
    // 1.414x = +3 dB, an audible level PUMP on every bounce (worst on peak/DC loop endpoints).
    // The earlier sine tests hide this because their loop ends land on ZERO-crossings (f(turn)=0).
    {
        std::vector<float> dc ((size_t) NBUF, 0.5f);                       // pure DC = maximal, unmissable swell
        const float* chans[1] = { dc.data() };
        SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
        e.setRegion (0.0f, 1.0f); e.setLoop (1000.0f / (NBUF - 1), 5000.0f / (NBUF - 1));
        e.setScan (0.5f); e.setXFade (0.25f); e.setLoopMode (SampleEngine::LoopMode::PingPong);
        e.setPitchRatio (1.0); e.noteOn (1.0, 0.0f, 0x9u);
        float worst = 0.f;
        for (int n = 0; n < F; ++n) { const float v = e.tickMono(); if (n > 1500 && std::fabs (v) > worst) worst = std::fabs (v); }
        std::printf ("DC ping-pong turn level (want ~0.5; equal-power swell bug = ~0.707): %.4f\n", worst);
        check (worst < 0.55f, "ping-pong X-Fade does NOT swell DC content at the turn (no +3 dB pump)");
    }
    {
        // Pure tone whose loop ENDPOINTS land on PEAKS (cos at i=1000,5000) — the worst phase for
        // the correlated-read swell, and the phase where the naked reflection has NO corner
        // (slope=0 at a peak), so this isolates the SWELL, not the click. Body max == AMP; the
        // equal-power bug would push the bounce to 1.414*AMP.
        std::vector<float> pk ((size_t) NBUF);
        for (int i = 0; i < NBUF; ++i) pk[(size_t) i] = AMP * (float) std::cos (TWO_PI * (double) i / (double) PERIOD);
        const float* chans[1] = { pk.data() };
        SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
        e.setRegion (0.0f, 1.0f); e.setLoop (1000.0f / (NBUF - 1), 5000.0f / (NBUF - 1));
        e.setScan (0.5f); e.setXFade (0.25f); e.setLoopMode (SampleEngine::LoopMode::PingPong);
        e.setPitchRatio (1.0); e.noteOn (1.0, 0.0f, 0xAu);
        float worst = 0.f;
        for (int n = 0; n < F; ++n) { const float v = e.tickMono(); if (n > 1500 && std::fabs (v) > worst) worst = std::fabs (v); }
        std::printf ("peak-endpoint ping-pong max level (want ~%.3f; equal-power swell = ~%.3f): %.4f\n", AMP, 1.4142f * AMP, worst);
        check (worst < AMP * 1.06f, "ping-pong X-Fade does NOT swell a peak-endpoint pure tone at the turn");
    }

    // ── REGRESSION: the X-Fade=0 path must stay the naked reflection (unchanged) ─
    // (Re-driving with xfade=0 must reproduce the same clicky corner, proving the
    //  fix is gated and does not alter the no-crossfade behaviour.)
    Run ppOff2 = drive (SampleEngine::LoopMode::PingPong, 0.0f, F);
    check (std::fabs (turnSpike (ppOff2) - spikeOff) < 1e-6f, "X-Fade=0 ping-pong path is unchanged (gated fix)");

    // ── REGRESSION: other loop modes stay click-free with X-Fade up ──────────
    Run fwd = drive (SampleEngine::LoopMode::Forward, 0.25f, F);
    check (bodySpike (fwd) < 0.02f, "Forward loop stays click-free at the seam with X-Fade");
    check (finiteBounded (fwd), "Forward output finite & bounded");

    Run rev = drive (SampleEngine::LoopMode::Reverse, 0.25f, F);
    check (bodySpike (rev) < 0.02f, "Reverse loop stays click-free at the seam with X-Fade");
    check (finiteBounded (rev), "Reverse output finite & bounded");

    // ── REGRESSION: One-Shot plays through and terminates without a tail click ─
    {
        static std::vector<float> buf = makeSine();
        const float* chans[1] = { buf.data() };
        SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
        e.setRegion (0.0f, 1.0f); e.setScan (0.5f); e.setLoopMode (SampleEngine::LoopMode::OneShot);
        e.setPitchRatio (1.0); e.noteOn (1.0, 0.0f, 0x55u);
        std::vector<float> o; float l, r; bool everActive = false;
        for (int n = 0; n < NBUF + 2000; ++n) { bool a = e.tick (l, r); if (a) everActive = true; o.push_back (0.5f * (l + r)); if (! a) break; }
        check (everActive && ! e.isActive(), "One-Shot terminates at region end");
        float worst = 0.f; for (int n = 1; n < (int) o.size() - 1; ++n) { float s = std::fabs (o[(size_t)(n-1)] - 2*o[(size_t)n] + o[(size_t)(n+1)]); if (s > worst) worst = s; }
        check (worst < 0.02f, "One-Shot has no terminal click");
    }

    // ── DOORWAY / LOOP-ENTRY CLICK (Max's 2nd report) ────────────────────────
    // No click when the playhead first ENTERS the purple loop zone (the lead-in -> 'caught'
    // transition). Fixture loop [1025,4975] deliberately puts loopStart on a PEAK (+0.6) and
    // loopEnd on a TROUGH (-0.6), so ANY entry value-jump (e.g. a Reverse snap-to-loopEnd, or an
    // equal-power crossfade step) is loud and unmistakable. Normal per-sample step on this sine at
    // inc=1 is ~0.038; a doorway click is many times larger.
    auto catchJump = [] (SampleEngine::LoopMode mode, float xfade) -> float
    {
        static std::vector<float> buf = makeSine();
        const float* chans[1] = { buf.data() };
        SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
        e.setRegion (0.0f, 1.0f); e.setLoop (1025.0f / (NBUF - 1), 4975.0f / (NBUF - 1));
        e.setScan (0.5f); e.setXFade (xfade); e.setLoopMode (mode); e.setPitchRatio (1.0);
        e.noteOn (1.0, 0.0f, 0x33u);
        std::vector<float> o; std::vector<char> cg;
        for (int n = 0; n < 8000; ++n) { cg.push_back (e.caught() ? 1 : 0); o.push_back (e.tickMono()); }
        int k = -1; for (int n = 1; n < (int) cg.size(); ++n) if (! cg[(size_t)(n-1)] && cg[(size_t) n]) { k = n; break; }
        if (k < 3) return -1.f;                              // never caught / caught at frame 0
        float worst = 0.f; const int hi = (k + 6 > (int) o.size() - 1) ? (int) o.size() - 1 : k + 6;
        for (int n = k - 2; n <= hi; ++n) { const float j = std::fabs (o[(size_t) n] - o[(size_t)(n-1)]); if (j > worst) worst = j; }
        return worst;
    };
    {
        const float smoothMax = 0.05f;                       // ~0.038 normal step; doorway click >> this
        const char* nm[5] = { "OneShot", "Forward", "Reverse", "PingPong", "Tailed" };
        for (int m = 1; m <= 4; ++m)
            std::printf ("catch jump %-8s: xfade=0 -> %.4f   xfade=0.25 -> %.4f\n",
                         nm[m], catchJump ((SampleEngine::LoopMode) m, 0.0f), catchJump ((SampleEngine::LoopMode) m, 0.25f));
        check (catchJump (SampleEngine::LoopMode::Forward,  0.25f) < smoothMax, "Forward: no click entering the loop zone");
        check (catchJump (SampleEngine::LoopMode::PingPong, 0.25f) < smoothMax, "Ping-Pong: no click entering the loop zone (X-Fade up)");
        check (catchJump (SampleEngine::LoopMode::PingPong, 0.0f)  < smoothMax, "Ping-Pong: no click entering the loop zone (X-Fade off)");
        check (catchJump (SampleEngine::LoopMode::Reverse,  0.25f) < smoothMax, "Reverse: no click entering the loop zone");
        check (catchJump (SampleEngine::LoopMode::Tailed,   0.25f) < smoothMax, "Tailed: no click entering the loop zone");
    }
    {
        // Reverse DOORWAY (the entry crossfade I added) must not SWELL correlated content —
        // equal-gain, like ping-pong. Measure ONLY the ~3 ms entry window (isolated from the
        // steady-state reverse SEAM, which is a SEPARATE, PRE-EXISTING equal-power path further
        // into the loop). DC = maximal correlation.
        std::vector<float> dc ((size_t) NBUF, 0.5f);
        const float* chans[1] = { dc.data() };
        SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
        e.setRegion (0.0f, 1.0f); e.setLoop (1025.0f / (NBUF - 1), 4975.0f / (NBUF - 1));
        e.setScan (0.5f); e.setXFade (0.25f); e.setLoopMode (SampleEngine::LoopMode::Reverse);
        e.setPitchRatio (1.0); e.noteOn (1.0, 0.0f, 0x44u);
        std::vector<float> o; std::vector<char> cg;
        for (int n = 0; n < 8000; ++n) { cg.push_back (e.caught() ? 1 : 0); o.push_back (e.tickMono()); }
        int k = -1; for (int n = 1; n < (int) cg.size(); ++n) if (! cg[(size_t)(n-1)] && cg[(size_t) n]) { k = n; break; }
        float door = 0.f; const int hi = (k + 400 > (int) o.size()) ? (int) o.size() : k + 400;   // ~3 ms fade ≈ 144 frames
        for (int n = k; n < hi; ++n) if (std::fabs (o[(size_t) n]) > door) door = std::fabs (o[(size_t) n]);
        float seam = 0.f; for (int n = k + 400; n < (int) o.size(); ++n) if (std::fabs (o[(size_t) n]) > seam) seam = std::fabs (o[(size_t) n]);
        std::printf ("DC reverse DOORWAY level (want ~0.5): %.4f   |   steady-state SEAM level (want ~0.5): %.4f\n", door, seam);
        check (door < 0.55f, "Reverse doorway entry crossfade does NOT swell DC content (equal-gain)");
        check (seam < 0.55f, "Reverse steady-state seam does NOT swell DC content (equal-gain)");
    }
    {
        // Forward loop seam must not swell correlated content either — equal-gain (Max-approved).
        std::vector<float> dc ((size_t) NBUF, 0.5f);
        const float* chans[1] = { dc.data() };
        SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
        e.setRegion (0.0f, 1.0f); e.setLoop (1000.0f / (NBUF - 1), 5000.0f / (NBUF - 1));
        e.setScan (0.5f); e.setXFade (0.25f); e.setLoopMode (SampleEngine::LoopMode::Forward);
        e.setPitchRatio (1.0); e.noteOn (1.0, 0.0f, 0x55u);
        float worst = 0.f;
        for (int n = 0; n < 12000; ++n) { const float v = e.tickMono(); if (n > 1500 && std::fabs (v) > worst) worst = std::fabs (v); }
        std::printf ("DC forward seam level (want ~0.5; equal-power swell = ~0.707): %.4f\n", worst);
        check (worst < 0.55f, "Forward loop seam does NOT swell DC content (equal-gain)");
    }

    // ── PING-PONG ENTRY CLICK vs X-FADE SWEEP (Max: clicks on loop ENTRY only when X-Fade > ~40%) ──
    // Loop [1000,3500] on a COSINE buffer puts loopStart on a non-zero PEAK and makes
    // loopStart/loopLen = 1000/2500 = 0.4, so the old contentValid gate (loopStart >= xfade*loopLen)
    // flipped FALSE above xfade=0.4 — disabling the ping-pong corner crossfade and letting the
    // seam-fade fallback slam the entry to ~0 = the click. After the fix: clean at EVERY X-Fade.
    {
        std::vector<float> cb ((size_t) NBUF);
        for (int i = 0; i < NBUF; ++i) cb[(size_t) i] = AMP * (float) std::cos (TWO_PI * (double) i / (double) PERIOD);
        auto entryClick = [&] (SampleEngine::LoopMode mode, float xfade) -> float {
            const float* chans[1] = { cb.data() };
            SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
            e.setRegion (0.0f, 1.0f); e.setLoop (1000.0f / (NBUF - 1), 3500.0f / (NBUF - 1));
            e.setScan (0.5f); e.setXFade (xfade); e.setLoopMode (mode);
            e.setPitchRatio (1.0); e.noteOn (1.0, 0.0f, 0x77u);
            std::vector<float> o; std::vector<char> cg;
            for (int n = 0; n < 6000; ++n) { cg.push_back (e.caught() ? 1 : 0); o.push_back (e.tickMono()); }
            int k = -1; for (int n = 1; n < (int) cg.size(); ++n) if (! cg[(size_t)(n-1)] && cg[(size_t) n]) { k = n; break; }
            if (k < 3) return -1.f;
            float worst = 0.f; const int hi = (k + 8 > (int) o.size() - 1) ? (int) o.size() - 1 : k + 8;
            for (int n = k - 2; n <= hi; ++n) { const float j = std::fabs (o[(size_t) n] - o[(size_t)(n-1)]); if (j > worst) worst = j; }
            return worst;
        };
        const float xf[7] = { 0.20f, 0.30f, 0.38f, 0.40f, 0.42f, 0.45f, 0.50f };
        struct ME { SampleEngine::LoopMode m; const char* nm; };
        const ME modes[3] = { { SampleEngine::LoopMode::Forward, "Forward " },
                              { SampleEngine::LoopMode::Reverse, "Reverse " },
                              { SampleEngine::LoopMode::PingPong, "PingPong" } };
        for (int mi = 0; mi < 3; ++mi)
        {
            float worstAll = 0.f;
            for (int i = 0; i < 7; ++i) { const float c = entryClick (modes[mi].m, xf[i]); std::printf ("  %s entry click @ xfade %.2f -> %.4f\n", modes[mi].nm, xf[i], c); if (c > worstAll) worstAll = c; }
            check (worstAll < 0.05f, "Loop ENTRY is click-free at ALL X-Fade values incl. > 40% (per mode)");
        }
    }
    {
        // BUFFER-EDGE loops: the adaptive content crossfade has NO room on the edge side, so the
        // seam-amplitude fallback must declick the wrap/turn. Sine → the edge index is a steep
        // zero-crossing = maximal artefact. PingPong/Forward use loopStart==0; Reverse uses
        // loopEnd==N-1 (its opposite-seam read is at the buffer end). Guards against over-excluding
        // any mode from the fallback.
        static std::vector<float> buf = makeSine();
        const float* chans[1] = { buf.data() };
        struct EC { SampleEngine::LoopMode m; float ls, le; const char* nm; };
        const EC cases[3] = {
            { SampleEngine::LoopMode::PingPong, 0.0f,                       3000.0f / (NBUF - 1), "ping-pong loopStart=0" },
            { SampleEngine::LoopMode::Forward,  0.0f,                       3000.0f / (NBUF - 1), "forward   loopStart=0" },
            { SampleEngine::LoopMode::Reverse,  3000.0f / (NBUF - 1),       1.0f,                 "reverse   loopEnd=N-1" },
        };
        for (int c = 0; c < 3; ++c)
        {
            SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
            e.setRegion (0.0f, 1.0f); e.setLoop (cases[c].ls, cases[c].le);
            e.setScan (0.5f); e.setXFade (0.25f); e.setLoopMode (cases[c].m);
            e.setPitchRatio (1.0); e.noteOn (1.0, 0.0f, 0x88u);
            std::vector<float> o; for (int n = 0; n < 12000; ++n) o.push_back (e.tickMono());
            float worst = 0.f; for (int n = 1501; n < (int) o.size() - 1; ++n) { const float s = secondDiff (o, n); if (s > worst) worst = s; }
            std::printf ("buffer-edge %s worst 2nd-diff: %.4f\n", cases[c].nm, worst);
            check (worst < 0.02f && finiteBounded (Run{ o, {} }), "Buffer-edge loop stays click-free (seam fallback)");
        }
    }

    // ── SMALL-ROOM DECLICK GAP (adversarial-review HIGH finding) ─────────────
    // A loop edge a few samples from a buffer boundary makes the adaptive crossfade window tiny —
    // too short to bridge, and its Hermite fringe clamps to DC. The crossfade (room ≥ xfReq) and the
    // seam-amplitude fallback (room < xfReq) must be COMPLEMENTARY so EVERY room is declicked. Use a
    // MISMATCHED loop (endpoints at different sine phases → a real seam value jump) and sweep the
    // edge distance through the danger band. makeSine endpoints are picked to NOT be zero-crossings.
    {
        static std::vector<float> buf = makeSine();
        const float* chans[1] = { buf.data() };
        const int rooms[9] = { 2, 3, 6, 9, 12, 16, 20, 24, 40 };
        float worstFwd = 0.f, worstRev = 0.f, worstPP = 0.f;
        for (int ri = 0; ri < 9; ++ri)
        {
            const int rm = rooms[ri];
            // Forward / Ping-Pong: loopStart = rm (near buffer start), loopEnd mid-buffer at a
            // different phase (4937 → not a multiple of PERIOD from loopStart).
            auto run = [&] (SampleEngine::LoopMode m, float ls01, float le01) -> float {
                SampleEngine e; e.prepare (SR); e.setSample (chans, 1, NBUF, SR);
                e.setRegion (0.0f, 1.0f); e.setLoop (ls01, le01);
                e.setScan (0.5f); e.setXFade (0.25f); e.setLoopMode (m); e.setPitchRatio (1.0);
                e.noteOn (1.0, 0.0f, 0x99u);
                std::vector<float> o; for (int n = 0; n < 14000; ++n) o.push_back (e.tickMono());
                if (! finiteBounded (Run{ o, {} })) return 9.f;
                float w = 0.f; for (int n = 1501; n < (int) o.size() - 1; ++n) { const float s = secondDiff (o, n); if (s > w) w = s; }
                return w;
            };
            const float fw = run (SampleEngine::LoopMode::Forward,  (float) rm / (NBUF - 1), 4937.0f / (NBUF - 1));
            const float pp = run (SampleEngine::LoopMode::PingPong, (float) rm / (NBUF - 1), 4937.0f / (NBUF - 1));
            const float rv = run (SampleEngine::LoopMode::Reverse,  1063.0f / (NBUF - 1), (float) (NBUF - 1 - rm) / (NBUF - 1));
            if (fw > worstFwd) worstFwd = fw; if (pp > worstPP) worstPP = pp; if (rv > worstRev) worstRev = rv;
            std::printf ("  small-room=%2d  fwd %.4f  rev %.4f  pp %.4f\n", rm, fw, rv, pp);
        }
        // Threshold 0.03 (vs 0.02 elsewhere): for room < kMinXfadeRoom the seam-AMPLITUDE fallback
        // declicks the wrap, leaving a small residual notch (~0.012-0.026) at the BOTTOM of its
        // fade-to-zero — its per-sample step (~0.013) is below the signal's own normal slope (~0.038),
        // i.e. inaudible. The point of this guard is that the CROSSFADE's DC-clamp THUMP (up to 0.214,
        // the adversarial-review regression) is GONE — every room is now declicked, none thumps.
        check (worstFwd < 0.03f, "Forward: no declick gap for loop edges 2-40 samples from the buffer start");
        check (worstRev < 0.03f, "Reverse: no declick gap for loop edges 2-40 samples from the buffer end");
        check (worstPP  < 0.03f, "Ping-Pong: no declick gap for loop edges 2-40 samples from a buffer edge");
    }

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
