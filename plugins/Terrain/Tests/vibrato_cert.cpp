// ══════════════════════════════════════════════════════════════════════════════════════════════
//  vibrato_cert.cpp — tp55 · THE SHIPPED VIBRATO MATH, MEASURED.
//
//  Max: "jitter isn't broken, it's the wrong feature."  It was a ONE-SHOT random detune sampled at
//  note-on — a de-phaser for stacked layers, silent on a lone one-shot. tp55 makes it a periodic
//  pitch LFO on the playback rate.
//
//  🚨 THIS GATE INCLUDES Source/Vibrato.h — THE HEADER THE PLUGIN RUNS. The math was lifted out of
//  SamplerVoice.h for exactly that reason: tp54's lesson is that a gate which drives a HELPER, or
//  asserts appearance instead of behaviour, proves nothing. Every number below comes out of the
//  shipped code. The two CALL SITES in SamplerVoice.h are asserted by shape in [9].
//
//  Build: clang++ -std=c++17 -O2 -I Source Tests/vibrato_cert.cpp -o /tmp/vibcert && /tmp/vibcert
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "Vibrato.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

static int pass = 0, fail = 0;
static void ok (bool c, const char* label, const std::string& detail = {})
{
    // A passing bar prints its measurement too: a number nobody can read is a number nobody
    // can sanity-check when the next change moves it.
    if (c) { ++pass; printf ("  PASS  %s\n", label); if (! detail.empty()) printf ("        %s\n", detail.c_str()); }
    else   { ++fail; printf ("  FAIL  %s\n        %s\n", label, detail.c_str()); }
}
static std::string f (double v, int d = 4) { char b[64]; snprintf (b, sizeof b, "%.*f", d, v); return b; }

// Render one block's worth of the ramp into `out`, the way SamplerVoice's NONE path does.
static void rampBlock (const tw::Vibrato::Block& b, int n, std::vector<double>& out)
{
    const int mask = (1 << b.segShift) - 1;
    for (int i = 0; i < n; ++i)
        out.push_back (b.active ? (b.mul[i >> b.segShift] + b.step[i >> b.segShift] * (double) (i & mask)) : 1.0);
}
// ⚠️ COUNTING UNITY CROSSINGS: a (prev-1)*(cur-1) < 0 test SILENTLY LOSES a quarter of them here.
//    The phase grid divides the period exactly — at 12 Hz / 48 kHz a period is 4000 samples and a
//    segment is 64, so every other period lands a segment boundary on phase 0, where the
//    multiplier is exp2(0) = EXACTLY 1.0; the product is then 0, not negative, and the crossing is
//    missed. It read 9.00 Hz for a 12 Hz vibrato — a bar failing for a reason that had nothing to
//    do with the code under test. Track the SIGN and ignore the zeros instead.
static int countCrossings (const std::vector<double>& mul)
{
    int cr = 0, sign = 0;
    for (double m : mul)
    {
        const int s = (m > 1.0) ? 1 : (m < 1.0 ? -1 : 0);
        if (s == 0) continue;
        if (sign != 0 && s != sign) ++cr;
        sign = s;
    }
    return cr;
}
// A crossing count over T seconds is short by at most one crossing at each end, so the estimate is
// biased LOW by up to 1/(2T) Hz — which is why every window below is seconds long, not one cycle.
static double crossHz (int crossings, double seconds) { return crossings / (2.0 * seconds); }
// cents away from unity for a rate multiplier
static double cents (double mul) { return 1200.0 * std::log2 (mul); }

int main()
{
    const double SR = 48000.0;
    printf ("\n══ tp55 — THE VIBRATO (Source/Vibrato.h, the shipped header) ══\n\n");

    // ── [0] PARKED IS THE EXACT IDENTITY ─────────────────────────────────────────────────────
    //  A vibrato at depth 0 may not cost the render a single different bit — this is what lets
    //  every existing null test stay green.
    {
        tw::Vibrato v; v.reset();
        bool clean = true; std::string why;
        for (int k = 0; k < 400; ++k)
        {
            auto& b = v.advance (0.0f, 5.0f, SR, 512);
            if (b.active || b.mul[0] != 1.0 || b.step[0] != 0.0 || b.semisMid != 0.0f)
            { clean = false; why = "block " + std::to_string (k) + " active=" + std::to_string (b.active)
                                 + " mul0=" + f (b.mul[0], 17); break; }
        }
        ok (clean, "[0] AT DEPTH 0 THE BLOCK IS THE EXACT IDENTITY — active=false, mul=1.0, step=0, offset=0", why);
    }

    // ── [1] IT ACTUALLY WOBBLES, AND TO THE DEPTH ON THE KNOB ────────────────────────────────
    {
        tw::Vibrato v; v.reset();
        const float D = 50.0f, R = 5.0f;
        for (int k = 0; k < 40; ++k) v.advance (D, R, SR, 512);   // let the depth smoother settle
        std::vector<double> mul;
        const int blocks = (int) std::ceil (SR / R / 512.0) + 2;  // just over one period
        for (int k = 0; k < blocks; ++k) { auto& b = v.advance (D, R, SR, 512); rampBlock (b, 512, mul); }
        double hi = -1e9, lo = 1e9;
        for (double m : mul) { hi = std::max (hi, cents (m)); lo = std::min (lo, cents (m)); }
        const bool good = std::fabs (hi - D) < 1.0 && std::fabs (lo + D) < 1.0;
        ok (good, "[1] IT SWINGS THE DEPTH THE KNOB SAYS — +/-50 cents, measured over a full cycle",
            "peak +" + f (hi, 2) + " / " + f (lo, 2) + " cents");
    }

    // ── [2] 🚨 AND IT IS PERIODIC AT THE RATE — the whole point Max was making ───────────────
    //  A ONE-SHOT detune (what jitter was) would show ZERO zero-crossings no matter how long you
    //  render. A vibrato at R Hz crosses unity 2R times a second. This is the bar the old feature
    //  fails and the new one must pass.
    {
        tw::Vibrato v; v.reset();
        const float D = 40.0f, R = 6.0f;
        for (int k = 0; k < 40; ++k) v.advance (D, R, SR, 512);
        std::vector<double> mul;
        const int blocks = (int) (SR * 8.0 / 512.0);              // eight seconds: the crossing
        for (int k = 0; k < blocks; ++k) { auto& b = v.advance (D, R, SR, 512); rampBlock (b, 512, mul); }
        const int crossings = countCrossings (mul);               // estimate's own bias is 1/(2T),
        const double seconds = (double) mul.size() / SR;           // so the window is 8 s, not one cycle
        const double measured = crossHz (crossings, seconds);
        ok (std::fabs (measured - R) < 0.1,
            "[2] 🚨 IT IS PERIODIC AT THE RATE — a one-shot detune would never cross unity at all",
            "counted " + std::to_string (crossings) + " unity crossings in " + f (seconds, 2)
            + " s = " + f (measured, 3) + " Hz, knob said " + f (R, 2));
    }

    // ── [3] 🚨 THE RAMP IS HONEST — the block chord vs the true sine ─────────────────────────
    //  The cheap part of the design is approximating a sine by a straight line across each block.
    //  Measure the error at the WORST case the knobs allow: the fastest rate and the deepest
    //  swing, on the longest block a host is likely to hand us.
    {
        for (int bs : { 64, 128, 512, 1024, 2048, 4096 })
        {
            tw::Vibrato v; v.reset();
            const float D = tw::Vibrato::kMaxDepthCents, R = tw::Vibrato::kMaxRateHz;
            for (int k = 0; k < 60; ++k) v.advance (D, R, SR, bs);
            double worst = 0.0;
            const double phase0 = v.phase;
            (void) phase0;
            const int blocks = (int) std::ceil (SR / R / bs) + 2;
            double t = v.phase;                                    // track the true phase alongside
            for (int k = 0; k < blocks; ++k)
            {
                auto& b = v.advance (D, R, SR, bs);
                for (int i = 0; i < bs; ++i)
                {
                    const double approx = b.mul[i >> b.segShift]
                                        + b.step[i >> b.segShift] * (double) (i & ((1 << b.segShift) - 1));
                    const double truePh = t + (double) R / SR * (double) i;
                    const double exact  = std::exp2 ((D * 0.01) * std::sin (truePh * tw::Vibrato::kTwoPi) / 12.0);
                    worst = std::max (worst, std::fabs (cents (approx) - cents (exact)));
                }
                t += (double) R / SR * (double) bs;
            }
            ok (worst < 0.25,
                (std::string ("[3] 🚨 THE SEGMENT GRID TRACKS THE SINE at the worst case the knobs allow — block ")
                 + std::to_string (bs)).c_str(),
                "12 Hz / 100 cents, block " + std::to_string (bs) + ": worst error " + f (worst, 4) + " cents");
        }
    }

    // ── [4] NO SEAM AT THE BLOCK BOUNDARY ────────────────────────────────────────────────────
    //  A ramp that restarts each block instead of chaining would put a step at every boundary —
    //  93 clicks a second at 512/48k. The end of block K must meet the start of block K+1.
    {
        tw::Vibrato v; v.reset();
        const float D = 80.0f, R = 9.0f; const int N = 512;
        for (int k = 0; k < 40; ++k) v.advance (D, R, SR, N);
        std::vector<double> mul;
        for (int k = 0; k < 400; ++k) { auto& b = v.advance (D, R, SR, N); rampBlock (b, N, mul); }
        double worst = 0.0;
        for (size_t i = 1; i < mul.size(); ++i)
            worst = std::max (worst, std::fabs (cents (mul[i]) - cents (mul[i-1])));
        // the honest ceiling for a smooth ramp: one sample's worth of the steepest slope the
        // knobs allow, 2*pi*R*depth/SR cents, plus a hair.
        const double ceiling = tw::Vibrato::kTwoPi * R * D / SR + 0.002;
        ok (worst < ceiling,
            "[4] EVERY STEP IS ONE SAMPLE'S WORTH — no seam at a segment boundary and none at a block boundary",
            "worst sample-to-sample step " + f (worst, 6) + " cents over " + std::to_string (mul.size())
            + " samples; the slope's own ceiling is " + f (ceiling, 6));
    }

    // ── [5] A NOTE STARTS AT ITS TRUE PITCH ──────────────────────────────────────────────────
    //  Phase 0 is the sine's zero crossing. Arming vibrato must never step a note-on.
    {
        tw::Vibrato v;
        for (int k = 0; k < 200; ++k) v.advance (100.0f, 7.0f, SR, 512);   // run it somewhere random
        v.reset();
        auto& b = v.advance (100.0f, 7.0f, SR, 512);
        ok (std::fabs (cents (b.mul[0])) < 1e-9,
            "[5] A NOTE STARTS AT ITS TRUE PITCH — reset() lands on the zero crossing, so note-on never steps",
            "first sample of the first block = " + f (cents (b.mul[0]), 10) + " cents");
    }

    // ── [6] DETERMINISTIC ────────────────────────────────────────────────────────────────────
    //  The old jitter pulled juce::Random at note-on, so two renders of one patch could not null.
    //  Two Vibratos given the same calls must agree bit for bit.
    {
        tw::Vibrato a, c; a.reset(); c.reset();
        bool same = true;
        for (int k = 0; k < 500 && same; ++k)
        {
            auto& ba = a.advance (63.0f, 4.3f, SR, 480);
            auto& bc = c.advance (63.0f, 4.3f, SR, 480);
            same = (ba.semisMid == bc.semisMid && ba.segShift == bc.segShift);
            for (int sgi = 0; sgi <= tw::Vibrato::kMaxSegs && same; ++sgi)
                same = (ba.mul[sgi] == bc.mul[sgi] && ba.step[sgi] == bc.step[sgi]);
        }
        ok (same, "[6] DETERMINISTIC — two voices given the same calls agree BIT FOR BIT (the old jitter could not)");
    }

    // ── [7] A KNOB MOVE SLIDES, IT DOES NOT STEP ─────────────────────────────────────────────
    {
        tw::Vibrato v; v.reset();
        for (int k = 0; k < 60; ++k) v.advance (0.0f, 5.0f, SR, 512);
        double worstJump = 0.0, prev = 0.0;
        for (int k = 0; k < 120; ++k)
        {
            v.advance (100.0f, 5.0f, SR, 512);                   // slam 0 -> 100 cents
            const double amp = std::fabs ((double) v.depthSmoothed);
            worstJump = std::max (worstJump, std::fabs (amp - prev));
            prev = amp;
        }
        const bool arrived = std::fabs ((double) v.depthSmoothed - 100.0) < 0.01;
        ok (worstJump <= 25.01 && arrived,
            "[7] A KNOB SLAM SLIDES IN — the depth one-pole caps the first block's move and still arrives",
            "worst single-block move " + f (worstJump, 2) + " cents, settled at " + f (v.depthSmoothed, 3));
    }

    // ── [8] CLAMPS ───────────────────────────────────────────────────────────────────────────
    {
        tw::Vibrato v; v.reset();
        for (int k = 0; k < 60; ++k) v.advance (1e9f, 1e9f, SR, 512);
        const bool depthOk = std::fabs ((double) v.depthSmoothed - tw::Vibrato::kMaxDepthCents) < 0.01;
        tw::Vibrato w; w.reset();
        for (int k = 0; k < 60; ++k) w.advance (-5.0f, -5.0f, SR, 512);
        const bool floorOk = w.depthSmoothed == 0.0f;
        // rate clamp: run at an absurd rate and count crossings — it must land on the ceiling
        tw::Vibrato x; x.reset();
        for (int k = 0; k < 40; ++k) x.advance (50.0f, 1e6f, SR, 512);
        std::vector<double> mul;
        for (int k = 0; k < (int) (SR * 8.0 / 512.0); ++k) { auto& b = x.advance (50.0f, 1e6f, SR, 512); rampBlock (b, 512, mul); }
        const double hz = crossHz (countCrossings (mul), (double) mul.size() / SR);
        ok (depthOk && floorOk && std::fabs (hz - tw::Vibrato::kMaxRateHz) < 0.1,
            "[8] A WILD VALUE IS CLAMPED — depth to 100 cents, rate to 12 Hz, and a negative is 0",
            "depth=" + f (v.depthSmoothed, 2) + " floor=" + f (w.depthSmoothed, 2) + " rate=" + f (hz, 2) + " Hz");
    }

    // ── [9] 🚨 THE VOICE ACTUALLY USES IT — both call sites, in the shipped source ────────────
    //  A perfect LFO nobody calls is a perfect LFO nobody hears. These are the two places the
    //  block has to land: the NONE path's per-sample playback rate, and the warp path's per-block
    //  pitch offset (a stretched voice cannot varispeed — the stretcher owns the read rate).
    {
        std::ifstream in ("Source/SamplerVoice.h");
        std::string src ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const bool hasInclude = src.find ("#include \"Vibrato.h\"")          != std::string::npos;
        const bool hasAdvance = src.find ("vib_.advance (vibDepth, vibRate") != std::string::npos;
        const bool hasReset   = src.find ("vib_.reset();")                   != std::string::npos;
        const bool hasRamp    = src.find ("pitchInc = pitchRatio * (vib.mul[sgi] + vib.step[sgi]") != std::string::npos;
        const bool hasWarp    = src.find ("activeConfig.pitchSemitones + vib.semisMid")          != std::string::npos;
        const bool noJitter   = src.find ("pitchJitterCents") == std::string::npos
                             && src.find ("Random::getSystemRandom")  == std::string::npos;
        ok (hasInclude && hasAdvance && hasReset && hasRamp && hasWarp && noJitter,
            "[9] 🚨 SamplerVoice USES IT ON BOTH PATHS — the NONE path's playback rate and the warp path's pitch, and the old jitter is GONE",
            std::string ("include=") + (hasInclude?"y":"n") + " advance=" + (hasAdvance?"y":"n")
            + " reset=" + (hasReset?"y":"n") + " ramp=" + (hasRamp?"y":"n") + " warp=" + (hasWarp?"y":"n")
            + " jitterGone=" + (noJitter?"y":"n"));
    }

    // ── [10] THE WARP PATH'S OFFSET IS THE SAME WOBBLE ───────────────────────────────────────
    {
        tw::Vibrato v; v.reset();
        const float D = 60.0f, R = 5.0f;
        for (int k = 0; k < 40; ++k) v.advance (D, R, SR, 512);
        double hi = -1e9, lo = 1e9; int cr = 0; float prevS = 0.0f; bool have = false;
        const int blocks = (int) (SR * 8.0 / 512.0);
        for (int k = 0; k < blocks; ++k)
        {
            auto& b = v.advance (D, R, SR, 512);
            const double c = (double) b.semisMid * 100.0;
            hi = std::max (hi, c); lo = std::min (lo, c);
            if (have && (double) prevS * (double) b.semisMid < 0.0) ++cr;
            prevS = b.semisMid; have = true;
        }
        const double hz = crossHz (cr, (double) blocks * 512.0 / SR);
        ok (std::fabs (hi - D) < 2.0 && std::fabs (lo + D) < 2.0 && std::fabs (hz - R) < 0.15,
            "[10] THE WARP PATH GETS THE SAME WOBBLE — per-block pitch offset, same depth, same rate",
            "peak +" + f (hi, 2) + " / " + f (lo, 2) + " cents at " + f (hz, 3) + " Hz");
    }

    printf ("\n  %d passed, %d failed\n\n", pass, fail);
    return fail ? 1 : 0;
}
