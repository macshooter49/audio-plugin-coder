// =============================================================================
//  ModCore_test.cpp  —  Terrain Instrument · Batch 1 modulation offline harness
//  Waves Crate
//
//  Standalone, no JUCE. Proves the math the audible chain depends on:
//    1. phase accumulation + wrap  (free-run frequency accuracy)
//    2. tempo-sync Hz table        (1/4 = 2 Hz @ 120, bars, dotted, triplet)
//    3. bipolar depth              (sign, invert, zero)
//    4. cutoff semitone mapping    (never 0 Hz; symmetric up/down in octaves)
//    5. clamp-once == clamp-between on a single route (no double-clamp drift)
//    6. shape sanity               (ranges, S&H stepping, polarity folds)
//
//  Build:  g++ -std=c++17 Source/ModCore_test.cpp -o /tmp/modcore && /tmp/modcore
//  Pass:   every CHECK prints OK; final line "ALL <n> CHECKS PASSED".
// =============================================================================

#include "SynthLFO.h"
#include "SynthModConfig.h"
#include <cstdio>
#include <cmath>
#include <string>

using namespace wc;

static int g_pass = 0, g_fail = 0;

static void check (bool cond, const std::string& what)
{
    if (cond) { ++g_pass; std::printf ("  OK    %s\n", what.c_str()); }
    else      { ++g_fail; std::printf ("  FAIL  %s\n", what.c_str()); }
}
static bool approx (float a, float b, float eps = 1e-4f) { return std::fabs (a - b) <= eps; }

// Measure an LFO's effective frequency by counting cycles over N seconds.
static float measureHz (LFOShape shape, float hz, double sr, double seconds)
{
    SynthLFO lfo; lfo.prepare (sr);
    LFOSettings s; s.shape = shape; s.trigger = LFOTrigger::Free; s.polarity = LFOPolarity::Bipolar;
    lfo.setSettings (s); lfo.setFrequency (hz);
    const int n = (int) (sr * seconds);
    float prevPhase = lfo.currentPhase();
    int crossings = 0;
    // Count upward crossings of phase==0.5 (mid-cycle, away from the wrap seam so
    // float drift at the boundary can't add/drop a count). One per cycle.
    for (int i = 0; i < n; ++i)
    {
        lfo.processSample();
        float ph = lfo.currentPhase();
        if (prevPhase < 0.5f && ph >= 0.5f) ++crossings;
        prevPhase = ph;
    }
    return (float) (crossings / seconds);
}

int main()
{
    const double SR = 48000.0;

    std::printf ("== 1. phase accumulation + wrap ==\n");
    {
        // 2 Hz free LFO over 2 s should complete ~4 cycles -> ~2 crossings/s.
        float f = measureHz (LFOShape::Sine, 2.0f, SR, 2.0);
        check (approx (f, 2.0f, 0.05f), "free 2 Hz measures 2.0 cycles/s (got " + std::to_string (f) + ")");

        // phase stays in [0,1)
        SynthLFO lfo; lfo.prepare (SR);
        LFOSettings s; s.shape = LFOShape::SawUp; lfo.setSettings (s); lfo.setFrequency (1000.0f);
        bool inRange = true;
        for (int i = 0; i < 5000; ++i) { lfo.processSample(); float p = lfo.currentPhase(); if (p < 0.0f || p >= 1.0f) inRange = false; }
        check (inRange, "phase always in [0,1) even at 1 kHz");
    }

    std::printf ("== 2. tempo-sync Hz table (BPM 120) ==\n");
    {
        const float bpm = 120.0f;
        auto idxOf = [] (const char* nm) { for (int i = 0; i < kNumSyncDivisions; ++i) if (std::string (kSyncDivisions[i].name) == nm) return i; return -1; };
        check (approx (syncedHz (idxOf ("1/4"),  bpm), 2.0f),  "1/4  = 2.0 Hz");
        check (approx (syncedHz (idxOf ("1/8"),  bpm), 4.0f),  "1/8  = 4.0 Hz");
        check (approx (syncedHz (idxOf ("1/16"), bpm), 8.0f),  "1/16 = 8.0 Hz");
        check (approx (syncedHz (idxOf ("1/2"),  bpm), 1.0f),  "1/2  = 1.0 Hz");
        check (approx (syncedHz (idxOf ("1 bar"),bpm), 0.5f),  "1 bar= 0.5 Hz");
        check (approx (syncedHz (idxOf ("2 bar"),bpm), 0.25f), "2 bar= 0.25 Hz");
        // dotted 1/4. = 1.5 beats -> slower than 1/4
        check (syncedHz (idxOf ("1/4."), bpm) < syncedHz (idxOf ("1/4"), bpm), "dotted 1/4. slower than 1/4");
        check (approx (syncedHz (idxOf ("1/4."), bpm), 2.0f/1.5f), "1/4. = 1/4 / 1.5 (dotted)");
        // triplet 1/8T = faster than 1/8
        check (syncedHz (idxOf ("1/8T"), bpm) > syncedHz (idxOf ("1/8"), bpm), "triplet 1/8T faster than 1/8");
        check (approx (syncedHz (idxOf ("1/8T"), bpm), 4.0f * 1.5f), "1/8T = 1/8 * 3/2 (triplet)");
        // scales with tempo
        check (approx (syncedHz (idxOf ("1/4"), 60.0f), 1.0f), "1/4 @ 60 BPM = 1.0 Hz");
        check (approx (syncedHz (idxOf ("1/4"), 140.0f), 140.0f/60.0f), "1/4 @ 140 BPM tracks tempo");
    }

    std::printf ("== 3. bipolar depth (sign / invert / zero) ==\n");
    {
        const DestInfo& cut = kDestInfo[(int) ModDest::Cut1];
        // source +1 (LFO peak), depth +0.5 -> +24 ST ; depth -0.5 -> -24 ST ; depth 0 -> 0
        check (approx (routeContribution (cut, +1.0f, +0.5f), +24.0f), "src +1, depth +0.5 -> +24 ST");
        check (approx (routeContribution (cut, +1.0f, -0.5f), -24.0f), "depth negative inverts (-24 ST)");
        check (approx (routeContribution (cut, +1.0f,  0.0f),   0.0f), "depth 0 -> no modulation");
        // source -1 with +depth mirrors source +1 with -depth
        check (approx (routeContribution (cut, -1.0f, +0.5f), routeContribution (cut, +1.0f, -0.5f)), "src sign and depth sign are interchangeable");
    }

    std::printf ("== 4. cutoff semitone mapping (octave-correct, never 0 Hz) ==\n");
    {
        float base = 1000.0f;
        check (approx (applySemitones (base, +12.0f), 2000.0f, 0.5f), "+12 ST doubles 1000 -> 2000 Hz");
        check (approx (applySemitones (base, -12.0f),  500.0f, 0.5f), "-12 ST halves 1000 -> 500 Hz");
        check (approx (applySemitones (base, +24.0f), 4000.0f, 1.0f), "+24 ST = +2 oct -> 4000 Hz");
        // symmetric in octaves: up then down returns to base
        check (approx (applySemitones (applySemitones (base, +7.0f), -7.0f), base, 0.5f), "up 7 then down 7 returns to base");
        // extreme negative never reaches 0 (exp can't hit 0)
        float deep = applySemitones (base, -200.0f);
        check (deep > 0.0f, "huge negative ST stays > 0 Hz (got " + std::to_string (deep) + ")");
    }

    std::printf ("== 5. clamp-once == clamp-between (single route, summed sources) ==\n");
    {
        // Two sources onto Cut1, in semitone space, then clamp the SUM once vs clamping each add.
        const DestInfo& cut = kDestInfo[(int) ModDest::Cut1];
        const float loST = -96.0f, hiST = +96.0f;  // the voice's cutoff mod headroom
        float a = routeContribution (cut, +1.0f, 0.9f);   // +43.2 ST
        float b = routeContribution (cut, +1.0f, 0.9f);   // +43.2 ST  (sum 86.4, under +96)
        float clampOnce    = clampRange (0.0f + a + b, loST, hiST);
        float clampBetween = clampRange (clampRange (0.0f + a, loST, hiST) + b, loST, hiST);
        check (approx (clampOnce, clampBetween), "under the ceiling, clamp-once == clamp-between");
        // Now push over the ceiling: clamp-once is the correct (single) saturation.
        float c = routeContribution (cut, +1.0f, 1.0f);   // +48
        float d = routeContribution (cut, +1.0f, 1.0f);   // +48 (sum 96, exactly ceiling)
        float e = routeContribution (cut, +1.0f, 1.0f);   // +48 (sum 144, over)
        float once = clampRange (c + d + e, loST, hiST);
        check (approx (once, hiST), "over the ceiling, clamp-once saturates exactly at +96 ST");
    }

    std::printf ("== 6. shape + polarity sanity ==\n");
    {
        // bipolar shapes stay within [-1.01, 1.01]
        for (int sh = 0; sh < (int) LFOShape::NumShapes; ++sh)
        {
            SynthLFO lfo; lfo.prepare (SR);
            LFOSettings s; s.shape = (LFOShape) sh; s.polarity = LFOPolarity::Bipolar; lfo.setSettings (s); lfo.setFrequency (3.0f);
            float mn = 1e9f, mx = -1e9f;
            for (int i = 0; i < 20000; ++i) { float v = lfo.processSample(); mn = std::min (mn, v); mx = std::max (mx, v); }
            check (mn >= -1.01f && mx <= 1.01f, std::string ("shape ") + std::to_string (sh) + " stays in [-1,1]");
        }
        // UniPlus folds sine into [0,1]
        {
            SynthLFO lfo; lfo.prepare (SR);
            LFOSettings s; s.shape = LFOShape::Sine; s.polarity = LFOPolarity::UniPlus; lfo.setSettings (s); lfo.setFrequency (3.0f);
            float mn = 1e9f, mx = -1e9f;
            for (int i = 0; i < 20000; ++i) { float v = lfo.processSample(); mn = std::min (mn, v); mx = std::max (mx, v); }
            check (mn >= -0.01f && mx <= 1.01f, "UniPlus polarity folds sine into [0,1]");
        }
        // S&H holds a value for a whole cycle (no per-sample change mid-cycle)
        {
            SynthLFO lfo; lfo.prepare (SR);
            LFOSettings s; s.shape = LFOShape::SampleHold; s.trigger = LFOTrigger::Free; lfo.setSettings (s); lfo.setFrequency (10.0f);
            float first = lfo.processSample();
            bool heldWithinCycle = true;
            // at 10 Hz on 48k, a cycle is 4800 samples; check the next 1000 are identical
            for (int i = 0; i < 1000; ++i) { if (! approx (lfo.processSample(), first, 1e-6f)) { heldWithinCycle = false; break; } }
            check (heldWithinCycle, "S&H holds its value within a cycle");
        }
        // Trig mode resets phase on noteOn
        {
            SynthLFO lfo; lfo.prepare (SR);
            LFOSettings s; s.shape = LFOShape::SawUp; s.trigger = LFOTrigger::Trig; s.startPhase = 0.0f; lfo.setSettings (s); lfo.setFrequency (1.0f);
            for (int i = 0; i < 12000; ++i) lfo.processSample();  // advance ~1/4 cycle
            lfo.noteOn();
            check (approx (lfo.currentPhase(), 0.0f), "Trig mode resets phase to startPhase on noteOn");
        }
    }

    std::printf ("== 7. integrated voice cutoff (the audible default route) ==\n");
    {
        // Reproduce SynthVoice's exact per-sample path for the default route:
        //   L1 sine @ 2 Hz  ->  Cut1, depth +0.35   (base cutoff 1000 Hz)
        // Confirms the filter cutoff actually oscillates, stays bounded, and is
        // octave-symmetric around the base — i.e. it will *breathe*, not click or die.
        const double SR2 = 48000.0;
        const float  baseHz = 1000.0f;
        const float  depth  = 0.35f;
        SynthLFO l1; l1.prepare (SR2);
        LFOSettings s; s.shape = LFOShape::Sine; s.trigger = LFOTrigger::Free;
        s.polarity = LFOPolarity::Bipolar; l1.setSettings (s); l1.setFrequency (2.0f);
        const DestInfo& cut = kDestInfo[(int) ModDest::Cut1];

        float minHz = 1e9f, maxHz = -1e9f;
        const float fmax = 20000.0f, fmin = 20.0f;
        bool boundedAlways = true;
        const int n = (int) (SR2 * 1.0);   // 1 s = 2 full LFO cycles
        for (int i = 0; i < n; ++i)
        {
            float v = l1.processSample();                          // bipolar
            float semis = routeContribution (cut, v, depth);       // +/- (0.35*48)=16.8 ST
            float cutHz = applySemitones (baseHz, semis);          // base * 2^(st/12)
            cutHz = clampRange (cutHz, fmin, fmax);                // clamp ONCE
            if (cutHz < fmin || cutHz > fmax) boundedAlways = false;
            minHz = std::min (minHz, cutHz);
            maxHz = std::max (maxHz, cutHz);
        }
        // Expected swing: +/-16.8 ST -> base*2^(±16.8/12)
        float expHi = baseHz * std::exp2 ( 16.8f / 12.0f);   // ~2641 Hz
        float expLo = baseHz * std::exp2 (-16.8f / 12.0f);   // ~ 379 Hz
        check (boundedAlways, "cutoff stays within [20, 20000] Hz every sample");
        check (maxHz > baseHz + 50.0f && minHz < baseHz - 50.0f, "cutoff actually moves above AND below base (it breathes)");
        check (approx (maxHz, expHi, 25.0f), "peak cutoff matches +16.8 ST (~2641 Hz, got " + std::to_string (maxHz) + ")");
        check (approx (minHz, expLo, 25.0f), "trough cutoff matches -16.8 ST (~379 Hz, got " + std::to_string (minHz) + ")");
        // octave symmetry: peak/base == base/trough (ratios equal)
        check (approx (maxHz / baseHz, baseHz / minHz, 0.05f), "swing is octave-symmetric (equal ratio up/down)");
        // depth 0 => no movement at all
        {
            float mn = 1e9f, mx = -1e9f;
            for (int i = 0; i < 4800; ++i) { float v = l1.processSample(); float st = routeContribution (cut, v, 0.0f); float hz = applySemitones (baseHz, st); mn = std::min (mn, hz); mx = std::max (mx, hz); }
            check (approx (mn, mx) && approx (mn, baseHz), "depth 0 => cutoff sits exactly at base (no breathing)");
        }
    }

    std::printf ("\n");
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_pass);
    else             std::printf ("%d PASSED, %d FAILED\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
