// =============================================================================
//  FlowArp_test.cpp — Terrain Instrument · FLOW ARP offline harness (no JUCE)
//  Waves Crate
//
//  Proves the front-panel performance math the audible arp depends on:
//    1. RATE  knob -> division table (endpoints, monotonic)
//    2. GATE  knob -> note-length fraction (range, monotonic)
//    3. TRAJ  knob -> direction + octave ramp (endpoints)
//    4. MORPH knob -> voicing spread (identity at 0, widening, monotonic)
//    5. sequence builders (Up/Down/UpDown/Converge, octave expansion, counts)
//    6. per-step pick (VARY=0 deterministic ordered; VARY=1 stays in valid range)
//    7. PPQ step scheduling: correct count, NO DRIFT, bar-anchored over 2000 blocks
//    8. FlowArp engine sim: note-ons at step rate, gated offs, pending flush, latch
//
//  Build:  g++ -std=c++17 Source/FlowArp_test.cpp -o /tmp/flowarp && /tmp/flowarp
//  Pass:   every CHECK prints OK; final line "ALL <n> CHECKS PASSED".
// =============================================================================

#include "FlowArp.h"
#include "SynthModConfig.h"   // real routeContribution + kDestInfo (FLOW dests)
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
static int width (const NoteSet& s) { int lo = 999, hi = -999; for (int i=0;i<s.count;++i){ lo=std::min(lo,s.n[i]); hi=std::max(hi,s.n[i]); } return hi-lo; }

int main()
{
    std::printf ("\n=== FLOW · ARP math harness ===\n\n");

    // 1) RATE -------------------------------------------------------------
    std::printf ("[1] RATE -> division\n");
    check (arpBeatsPerStep (0.0f) == 4.0f,   "RATE=0 -> 1/1 (4 beats/step)");
    check (arpBeatsPerStep (1.0f) == 0.125f, "RATE=1 -> 1/32 (0.125 beats/step)");
    {
        bool mono = true; float prev = 1e9f;
        for (int i = 0; i <= 20; ++i) { float b = arpBeatsPerStep (i / 20.0f); if (b > prev + 1e-6f) mono = false; prev = b; }
        check (mono, "RATE knob monotonic (faster as it climbs)");
    }

    // 2) GATE -------------------------------------------------------------
    std::printf ("[2] GATE -> note length fraction\n");
    check (std::fabs (arpGateFrac (0.0f) - 0.05f) < 1e-6f, "GATE=0 -> 0.05 (staccato)");
    check (std::fabs (arpGateFrac (1.0f) - 0.98f) < 1e-6f, "GATE=1 -> 0.98 (near-legato)");
    check (arpGateFrac (0.5f) > arpGateFrac (0.1f),        "GATE monotonic up");

    // 3) TRAJ -------------------------------------------------------------
    std::printf ("[3] TRAJ -> direction + octaves\n");
    { auto t0 = arpTraj (0.0f); check (t0.dir == ArpDir::Up && t0.octaves == 1, "TRAJ=0 -> Up / 1 octave"); }
    { auto t1 = arpTraj (1.0f); check (t1.dir == ArpDir::Random && t1.octaves == 4, "TRAJ=1 -> Random / 4 octaves"); }

    // 4) MORPH voicing ----------------------------------------------------
    std::printf ("[4] MORPH -> voicing spread\n");
    const int cmaj[3] = { 60, 64, 67 };
    { NoteSet v; arpVoicing (cmaj, 3, 0.0f, v);
      check (v.count == 3 && v.n[0]==60 && v.n[1]==64 && v.n[2]==67, "MORPH=0 -> chord unchanged"); }
    { NoteSet v; arpVoicing (cmaj, 3, 1.0f, v);
      check (v.n[0]==60 && v.n[1]==76 && v.n[2]==91, "MORPH=1 -> fully spread (+0,+12,+24 oct)"); }
    { NoteSet a,b,c; arpVoicing (cmaj,3,0.0f,a); arpVoicing (cmaj,3,0.5f,b); arpVoicing (cmaj,3,1.0f,c);
      check (width(a) <= width(b) && width(b) <= width(c), "voicing width grows monotonically with MORPH"); }

    // 5) sequence builders ------------------------------------------------
    std::printf ("[5] sequence builders\n");
    NoteSet v1; arpVoicing (cmaj, 3, 0.0f, v1);   // {60,64,67}
    { NoteSet s; arpSequence (v1, ArpDir::Up, 1, s);
      check (s.count==3 && s.n[0]==60 && s.n[2]==67, "Up: ascending, 3 steps"); }
    { NoteSet s; arpSequence (v1, ArpDir::Down, 1, s);
      check (s.count==3 && s.n[0]==67 && s.n[2]==60, "Down: descending"); }
    { NoteSet s; arpSequence (v1, ArpDir::UpDown, 1, s);
      check (s.count==4 && s.n[0]==60 && s.n[1]==64 && s.n[2]==67 && s.n[3]==64, "UpDown: 60,64,67,64 (no endpoint repeat)"); }
    { NoteSet s; arpSequence (v1, ArpDir::Converge, 1, s);
      check (s.count==3 && s.n[0]==60 && s.n[1]==67 && s.n[2]==64, "Converge: outside-in 60,67,64"); }
    { NoteSet s; arpSequence (v1, ArpDir::Up, 2, s);
      check (s.count==6 && s.n[0]==60 && s.n[5]==79, "octave range x2 -> 6 steps, top +12"); }

    // 6) per-step pick ----------------------------------------------------
    std::printf ("[6] per-step pick (VARY)\n");
    { NoteSet s; arpSequence (v1, ArpDir::Up, 1, s); ArpRng r; r.seed (1);
      int n0 = arpPickNote (s, ArpDir::Up, 0, 0.0f, 1, r);
      int n1 = arpPickNote (s, ArpDir::Up, 1, 0.0f, 1, r);
      int n2 = arpPickNote (s, ArpDir::Up, 2, 0.0f, 1, r);
      int n3 = arpPickNote (s, ArpDir::Up, 3, 0.0f, 1, r);
      check (n0==60 && n1==64 && n2==67 && n3==60, "VARY=0 -> deterministic ordered (60,64,67,60)"); }
    { NoteSet s; arpSequence (v1, ArpDir::Up, 2, s); ArpRng r; r.seed (99);
      bool inRange = true; int lo = 60, hi = 79 + 12*2; // top of 2-oct set + max octave jump
      for (int k = 0; k < 4000; ++k) { int nn = arpPickNote (s, ArpDir::Up, k, 1.0f, 2, r); if (nn < lo || nn > hi || nn < 0 || nn > 127) inRange = false; }
      check (inRange, "VARY=1 -> 4000 picks all within valid clamped range"); }

    // 7) PPQ scheduling: count, no drift, bar-anchored ---------------------
    std::printf ("[7] PPQ step scheduling\n");
    { StepHit h[64]; int n = arpStepsInBlock (0.0, 120.0, 44100.0, 44100, 1.0f, h, 64);
      check (n==2 && h[0].sampleOffset==0 && h[1].sampleOffset==22050, "120BPM 1/4, 1s block -> 2 steps @ 0 & 22050"); }
    {
        // 2000 blocks of 512 @ 130 BPM, 1/16 — awkward fractional samples-per-step
        const double sr = 48000.0, bpm = 130.0; const float beats = 0.25f; const int block = 512, blocks = 2000;
        const double bps = bpm / 60.0, ppqPerSample = bps / sr, ppqPerBlock = ppqPerSample * block;
        double ppq = 0.0; long long expectIdx = 0; bool contiguous = true, aligned = true, inBlock = true; int total = 0;
        for (int b = 0; b < blocks; ++b)
        {
            StepHit h[64]; int hn = arpStepsInBlock (ppq, bpm, sr, block, beats, h, 64);
            for (int i = 0; i < hn; ++i)
            {
                if (h[i].stepIndex != expectIdx) contiguous = false;
                expectIdx++; total++;
                if (h[i].sampleOffset < 0 || h[i].sampleOffset >= block) inBlock = false;
                if (std::fabs (h[i].ppq - (double) h[i].stepIndex * beats) > 1e-9) aligned = false;
            }
            ppq += ppqPerBlock;
        }
        const double ppqTotal = ppqPerBlock * blocks;
        const long long expTotal = (long long) std::floor (ppqTotal / beats + 1e-9);
        check (contiguous, "no drift: step indices contiguous over 2000 blocks (re-derived from ppq)");
        check (aligned,    "bar-anchored: every step ppq == idx * beatsPerStep");
        check (inBlock,    "every step offset within its block");
        check (std::llabs ((long long) total - expTotal) <= 1, "total step count matches ppq span");
    }

    // 8) engine sim -------------------------------------------------------
    std::printf ("[8] FlowArp engine sim\n");
    {
        FlowArp arp; arp.reset();
        arp.noteOn (60, 100); arp.noteOn (64, 100); arp.noteOn (67, 100); // hold C major
        const double sr = 48000.0, bpm = 120.0; const int block = 512;
        const double ppqPerSample = (bpm/60.0)/sr, ppqPerBlock = ppqPerSample*block;
        // RATE=0.4 -> ~1/8 (0.5 beats); GATE=0.5; VARY=0; TRAJ=0 (Up,1oct); MORPH=0
        int ons = 0, offs = 0; double ppq = 0.0;
        for (int b = 0; b < 400; ++b)   // 400*512/48000 ≈ 4.27 s
        {
            ArpEvent ev[kArpMaxEvents];
            int n = arp.process (0.4f, 0.5f, 0.0f, 0.0f, 0.0f, ppq, bpm, sr, block, true, ev, kArpMaxEvents);
            for (int i = 0; i < n; ++i) { if (ev[i].on) ++ons; else ++offs; if (ev[i].sampleOffset < 0 || ev[i].sampleOffset >= block) check(false,"event offset in block"); }
            ppq += ppqPerBlock;
        }
        check (ons > 0, "engine emits note-ons while held + playing");
        check (std::abs (ons - offs) <= 2, "every note-on gets a matching note-off (within flush latency)");

        // latch: notes off, but arp keeps playing the latched chord
        FlowArp la; la.reset(); la.setLatch (true);
        la.noteOn (60,100); la.noteOn (63,100); la.noteOn (67,100);
        la.noteOff (60); la.noteOff (63); la.noteOff (67);   // hands off the keys
        int latchOns = 0; double p2 = 0.0;
        for (int b = 0; b < 200; ++b) { ArpEvent ev[kArpMaxEvents]; int n = la.process (0.5f,0.5f,0.f,0.f,0.f,p2,bpm,sr,block,true,ev,kArpMaxEvents); for (int i=0;i<n;++i) if (ev[i].on) ++latchOns; p2 += ppqPerBlock; }
        check (latchOns > 0, "LATCH holds the chord after keys released");

        // stopped transport: no new note-ons
        FlowArp sa; sa.reset(); sa.noteOn (60,100);
        ArpEvent ev[kArpMaxEvents]; int n = sa.process (0.5f,0.5f,0.f,0.f,0.f, 0.0, bpm, sr, block, false, ev, kArpMaxEvents);
        int stoppedOns = 0; for (int i=0;i<n;++i) if (ev[i].on) ++stoppedOns;
        check (stoppedOns == 0, "transport stopped -> no note-ons emitted");
    }

    // 9) FLOW-knob modulation accumulation (the processBlock MOD HOOK math) ----
    //    effective = clamp01( base + Σ routeContribution(info, globalLfoValue, depth) ).
    //    Mirrors exactly what processBlock will do with the dedicated FLOW LFO bank.
    std::printf ("[9] FLOW-knob LFO modulation (base + sum, clamp once)\n");
    {
        auto flowEff = [] (float base, wc::ModDest dest, const wc::Assignment* as, int n, const float* lfo) -> float
        {
            float sum = 0.0f; const auto& info = wc::kDestInfo[(int) dest];
            for (int a = 0; a < n; ++a)
            {
                if (! as[a].enabled || as[a].dest != dest) continue;
                const int si = (int) as[a].source - (int) wc::ModSource::L1;
                if (si < 0 || si >= wc::NUM_LFOS) continue;       // only LFO sources carry a global value
                sum += wc::routeContribution (info, lfo[si], as[a].depth);
            }
            return std::max (0.0f, std::min (1.0f, base + sum));
        };

        float lfo[wc::NUM_LFOS] = {0};
        wc::Assignment as[4];

        // one route L1 -> FlowMorph, depth +0.5
        as[0] = {}; as[0].source = wc::ModSource::L1; as[0].dest = wc::ModDest::FlowMorph; as[0].depth = 0.5f; as[0].enabled = true;
        lfo[0] = 1.0f;  check (std::fabs (flowEff (0.5f, wc::ModDest::FlowMorph, as, 1, lfo) - 1.0f) < 1e-6f, "LFO=+1, depth+0.5, base0.5 -> 1.0 (clamped at ceiling)");
        lfo[0] = -1.0f; check (std::fabs (flowEff (0.5f, wc::ModDest::FlowMorph, as, 1, lfo) - 0.0f) < 1e-6f, "LFO=-1 inverts -> 0.0 (clamped at floor)");
        lfo[0] = 0.5f;  check (std::fabs (flowEff (0.5f, wc::ModDest::FlowMorph, as, 1, lfo) - 0.75f) < 1e-6f, "LFO=+0.5 -> 0.75 (mid)");

        // two routes onto the same dest sum BEFORE the single clamp
        as[1] = {}; as[1].source = wc::ModSource::L2; as[1].dest = wc::ModDest::FlowMorph; as[1].depth = 0.5f; as[1].enabled = true;
        lfo[0] = 0.4f; lfo[1] = 0.4f;
        check (std::fabs (flowEff (0.1f, wc::ModDest::FlowMorph, as, 2, lfo) - (0.1f + 0.2f + 0.2f)) < 1e-6f, "two routes sum (0.1+0.2+0.2=0.5)");
        lfo[0] = 1.0f; lfo[1] = 1.0f; // 0.5 + 0.5 + 0.5 = 1.5 -> clamp ONCE to 1.0
        check (std::fabs (flowEff (0.5f, wc::ModDest::FlowMorph, as, 2, lfo) - 1.0f) < 1e-6f, "sum past ceiling clamps once to 1.0 (no double-clamp)");

        // a route to a different dest must not bleed into this knob
        as[2] = {}; as[2].source = wc::ModSource::L3; as[2].dest = wc::ModDest::FlowGate; as[2].depth = 1.0f; as[2].enabled = true;
        lfo[2] = 1.0f; lfo[0] = 0.0f; lfo[1] = 0.0f;
        check (std::fabs (flowEff (0.3f, wc::ModDest::FlowMorph, as, 3, lfo) - 0.3f) < 1e-6f, "FlowGate route does not affect FlowMorph");
        check (std::fabs (flowEff (0.3f, wc::ModDest::FlowGate,  as, 3, lfo) - 1.0f) < 1e-6f, "FlowGate route drives FlowGate (0.3+1.0 -> clamp 1.0)");

        // non-LFO source (Velocity) has no global value -> skipped
        as[3] = {}; as[3].source = wc::ModSource::Velocity; as[3].dest = wc::ModDest::FlowVary; as[3].depth = 1.0f; as[3].enabled = true;
        check (std::fabs (flowEff (0.2f, wc::ModDest::FlowVary, as, 4, lfo) - 0.2f) < 1e-6f, "non-LFO source skipped at block rate (base unchanged)");
    }

    std::printf ("\n");
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n\n", g_pass);
    else             std::printf ("%d passed, %d FAILED\n\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
