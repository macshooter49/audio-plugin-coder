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
#include <cstdlib>
#include <string>
#include <vector>

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
    check (arpBeatsPerStep (0.0f) == 4.0f,      "RATE=0 -> 1/1 (4 beats/step)");
    check (arpBeatsPerStep (1.0f) == 0.015625f, "RATE=1 -> 1/256 (near-static buzz)");
    {
        bool mono = true; float prev = 1e9f;
        for (int i = 0; i <= 20; ++i) { float b = arpBeatsPerStep (i / 20.0f); if (b > prev + 1e-6f) mono = false; prev = b; }
        check (mono, "RATE knob monotonic (faster as it climbs)");
    }
    // RATE (rich) — the ARP front-knob ladder: straight + dotted + triplet, endpoints + monotonic.
    check (arpBeatsPerStepRich (0.0f) == 4.0f,      "RICH RATE=0 -> 1/1");
    check (arpBeatsPerStepRich (1.0f) == 0.015625f, "RICH RATE=1 -> 1/256");
    check (kArpRateRichN == 19,                     "RICH ladder has 19 divisions (straight+dotted+triplet)");
    {
        bool mono = true; float prev = 1e9f;
        for (int i = 0; i < kArpRateRichN; ++i) { float b = kArpRateRich[i]; if (b >= prev) mono = false; prev = b; }
        check (mono, "RICH ladder strictly descending (every division distinct + monotonic)");
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

    // 8) engine sim — monophonic clock + robust note-offs --------------------
    std::printf ("[8] FlowArp engine sim (monophonic, free-run, no stuck notes)\n");
    auto runArp = [] (FlowArp& a, float rate, float gate, float vary, float traj, float morph,
                      bool playing, int blocks, double sr, double bpm, int block,
                      int& ons, int& offs, int& maxSound, int& soundIO, int& badOff)
    {
        ons = offs = badOff = 0; maxSound = soundIO; int sound = soundIO; double ppq = 0.0;
        const double pps = (bpm / 60.0) / sr, ppb = pps * block;
        for (int b = 0; b < blocks; ++b)
        {
            ArpEvent ev[kArpMaxEvents];
            const int n = a.process (rate, gate, vary, traj, morph, ppq, bpm, sr, block, playing, ev, kArpMaxEvents);
            for (int i = 0; i < n; ++i)
            {
                if (ev[i].sampleOffset < 0 || ev[i].sampleOffset >= block) ++badOff;
                if (ev[i].on) { ++ons; ++sound; } else { ++offs; --sound; }
                if (sound > maxSound) maxSound = sound;
            }
            if (playing) ppq += ppb;   // stopped: engine uses its own internal clock
        }
        soundIO = sound;               // persists across calls (engine note-state carries too)
    };

    {
        FlowArp arp; arp.reset();
        arp.noteOn (60,100); arp.noteOn (64,100); arp.noteOn (67,100);   // hold C major
        int ons, offs, mx, bad, sound = 0;
        runArp (arp, 0.4f, 0.5f, 0.0f, 0.0f, 0.0f, true, 400, 48000.0, 120.0, 512, ons, offs, mx, sound, bad);
        check (ons > 0,    "playing + held: emits note-ons");
        check (mx <= 1,    "monophonic: never more than one arp note sounding");
        check (bad == 0,   "all event offsets within their block");
        check (sound <= 1, "at most one note sounding while held");

        arp.noteOff (60); arp.noteOff (64); arp.noteOff (67);            // release every key
        int ons2, offs2, mx2, bad2;
        runArp (arp, 0.4f, 0.5f, 0.0f, 0.0f, 0.0f, true, 8, 48000.0, 120.0, 512, ons2, offs2, mx2, sound, bad2);
        check (bad2 == 0,  "post-release offsets in block");
        check (mx2 <= 1,   "post-release stays monophonic");
        check (sound == 0, "keys released -> no note left sounding (NO STUCK NOTE)");
        check ((ons + ons2) == (offs + offs2), "every note-on matched by a note-off");
    }
    {
        // transport STOPPED but keys held -> internal free-run clock still arps
        FlowArp arp; arp.reset(); arp.noteOn (60,100); arp.noteOn (64,100);
        int ons, offs, mx, bad, sound = 0;
        runArp (arp, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, false, 400, 48000.0, 120.0, 512, ons, offs, mx, sound, bad);
        check (ons > 0,    "transport STOPPED + held: free-run clock still arpeggiates (fixes silence)");
        check (mx <= 1,    "stopped free-run also monophonic");
        check (bad == 0,   "stopped offsets in block");
        check (sound <= 1, "stopped: at most one sounding");
    }
    {
        // latch holds the chord after keys released
        FlowArp la; la.reset(); la.setLatch (true);
        la.noteOn (60,100); la.noteOn (63,100); la.noteOn (67,100);
        la.noteOff (60); la.noteOff (63); la.noteOff (67);
        int ons, offs, mx, bad, sound = 0;
        runArp (la, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, true, 200, 48000.0, 120.0, 512, ons, offs, mx, sound, bad);
        check (ons > 0,  "LATCH holds the chord after keys released");
        check (mx <= 1,  "latch stays monophonic");
        check (bad == 0, "latch offsets in block");
    }
    {
        // turning FLOW OFF mid-note must release the sounding note (no hang on bypass)
        FlowArp arp; arp.reset(); arp.noteOn (60,100);
        int ons, offs, mx, bad, sound = 0;
        runArp (arp, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, true, 30, 48000.0, 120.0, 512, ons, offs, mx, sound, bad);
        ArpEvent rel[kArpMaxEvents];
        const int rn = arp.releaseAll (rel, kArpMaxEvents);
        int relOffs = 0; for (int i = 0; i < rn; ++i) if (! rel[i].on) ++relOffs;
        check (sound - relOffs == 0, "releaseAll() clears the sounding arp note (no hang when FLOW off)");
        const int rn2 = arp.releaseAll (rel, kArpMaxEvents);
        check (rn2 == 0, "releaseAll() idempotent — nothing left after release");
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

    // 10) EXTENSION-CARD LANE ENGINE (fb105) ------------------------------
    std::printf ("[10] extension-card lane engine\n");
    {
        auto neutralExt = [] {
            ArpExtParams x;
            x.dir = 0; x.octaves = 1; x.sorted = true;
            x.swing = 0; x.mroll = 0; x.timbre = 1; x.glide = 0;
            x.pRange = 0; x.pCurve = .5f; x.pQuant = 0; x.pSlide = 0;
            x.gLen = 0.52f; x.gCurve = .5f; x.gRand = 0; x.gSlide = 0;
            x.vRange = 0; x.vCurve = .5f; x.vRand = 0; x.vFloor = 0;
            x.oRange = 0; x.oBias = .5f; x.oRand = 0; x.oSpread = 0;
            x.rCount = 0; x.rDecay = 0; x.rCurve = .5f; x.rAmt = 0;
            x.cAmt = 0; x.cBias = .5f; x.cSeed = 0; x.cDrift = 0;
            x.wDepth = 0; x.wCurve = .5f; x.wSlide = 0; x.wRand = 0;
            return x; };
        auto neutralLanes = [] {
            ArpLaneData l; l.steps = 16;
            for (int i = 0; i < kArpLaneMax; ++i)
            { l.pitch[i]=3; l.gate[i]=1; l.vel[i]=1; l.oct[i]=0; l.ratchet[i]=1; l.prob[i]=1; l.wt[i]=.5f; }
            return l; };

        const double sr = 48000.0, bpm = 120.0;
        const float  r16 = 11.0f / 18.0f;                 // rich-ladder index 11 = 1/16
        const long long stepSmp = 6000;                   // 1/16 @120 BPM @48k
        struct Ev { bool on; int note, vel; long long t; };
        auto run = [&] (FlowArp& arp, int blockSize, int totalSamples, std::vector<Ev>& evs)
        {
            double ppq = 0.0; long long base = 0;
            for (int done = 0; done < totalSamples; done += blockSize)
            {
                const int nsmp = std::min (blockSize, totalSamples - done);
                ArpEvent ev[kArpMaxEvents];
                const int n = arp.process (r16, 0.5f, 0, 0, 0, ppq, bpm, sr, nsmp, true, ev, kArpMaxEvents);
                for (int i = 0; i < n; ++i) evs.push_back ({ ev[i].on, ev[i].note, ev[i].vel, base + ev[i].sampleOffset });
                ppq += (bpm / 60.0) / sr * nsmp; base += nsmp;
            }
        };

        // A characterful pattern: rests, a roll, a chance-kill, a soft step, swing.
        ArpExtParams X = neutralExt(); ArpLaneData L = neutralLanes();
        L.pitch[3] = -1; L.pitch[11] = -1; L.ratchet[4] = 3; L.prob[6] = 0.0f; L.vel[2] = 0.25f;
        X.rAmt = 1.0f; X.rDecay = 0.5f; X.cAmt = 1.0f; X.vRange = 1.0f; X.swing = 0.3f;

        // bin an on-time to its step: round to the NEAREST boundary — block-edge
        // rounding can land a boundary hit ±1 sample, and swing (≤45%) stays well
        // inside the half-step rounding window.
        auto stepOf = [&] (long long t) { return (int) (((t + stepSmp / 2) / stepSmp) % 16); };
        auto loopOf = [&] (long long t) { return (int) ((t + stepSmp / 2) / (stepSmp * 16)); };

        std::vector<Ev> a, b;
        {
            FlowArp arp; arp.reset(); arp.setExt (X); arp.setLanes (L);
            arp.noteOn (60, 100); arp.noteOn (64, 100); arp.noteOn (67, 100);
            run (arp, 137, 384000, a);
            ArpEvent rel[kArpMaxEvents]; const int rn = arp.releaseAll (rel, kArpMaxEvents);
            long long ons = 0, offs = rn;
            for (auto& e : a) (e.on ? ons : offs)++;
            check (ons > 20 && ons == offs, "no stuck notes: every on has a matching off (incl. releaseAll flush)");
        }
        {
            FlowArp arp; arp.reset(); arp.setExt (X); arp.setLanes (L);
            arp.noteOn (60, 100); arp.noteOn (64, 100); arp.noteOn (67, 100);
            run (arp, 480, 384000, b);
        }
        {
            bool same = a.size() > 40 && a.size() == b.size();
            for (size_t i = 0; same && i < a.size(); ++i)
                same = a[i].on == b[i].on && a[i].note == b[i].note && a[i].vel == b[i].vel
                       && std::llabs (a[i].t - b[i].t) <= 1;
            check (same, "block-size invariance: 137- vs 480-sample blocks emit the identical stream");
        }
        {
            bool rest = true, kill = true;
            for (auto& e : a) if (e.on)
            {
                const int st = stepOf (e.t);
                if (st == 3 || st == 11) rest = false;
                if (st == 6)             kill = false;
            }
            check (rest, "REST: pitch row -1 steps fire nothing");
            check (kill, "CHANCE: lane 0 at full Amount kills the step every loop");
        }
        {
            int rolls = 0, firstV = -1, lastV = -1;
            for (auto& e : a) if (e.on && e.t >= 4 * stepSmp && e.t < 5 * stepSmp)
            { ++rolls; if (firstV < 0) firstV = e.vel; lastV = e.vel; }
            check (rolls == 3,     "ROLL: ratchet-3 step fires 3 sub-hits inside the step");
            check (lastV < firstV, "ROLL: sub-hit velocity decays");
        }
        {
            int v2 = -1;
            for (auto& e : a) if (e.on && e.t >= 2 * stepSmp && e.t < 3 * stepSmp) { v2 = e.vel; break; }
            check (v2 == 25, "VEL lane: step 0.25 at full Range -> velocity 25");
        }
        {
            long long t1 = -1;
            for (auto& e : a) if (e.on && e.t >= stepSmp && e.t < 2 * stepSmp) { t1 = e.t; break; }
            check (t1 >= 0 && std::llabs (t1 - (stepSmp + 810)) <= 2, "SWING 0.3 lands odd steps 810 samples late (0.3*0.45*step)");
        }
        {
            // chance loop-lock: prob 0.5 everywhere, Drift 0 -> the SAME steps fire every loop
            ArpExtParams Xc = neutralExt(); Xc.cAmt = 1.0f; Xc.cSeed = 0.44f;
            ArpLaneData Lc = neutralLanes();
            for (int i = 0; i < kArpLaneMax; ++i) Lc.prob[i] = 0.5f;
            FlowArp arp; arp.reset(); arp.setExt (Xc); arp.setLanes (Lc);
            arp.noteOn (60, 100);
            std::vector<Ev> c; run (arp, 256, (int) (stepSmp * 64), c);   // 4 loops of 16 steps
            bool fired[16] = {}, cur[16] = {}; bool seen = false, locked = true; int loops = 0; int nf = 0;
            for (auto& e : c) if (e.on)
            {
                const int st = stepOf (e.t);
                const int lp = loopOf (e.t);
                if (lp != loops)
                {
                    if (! seen) { for (int i = 0; i < 16; ++i) fired[i] = cur[i]; seen = true; }
                    else          for (int i = 0; i < 16; ++i) locked = locked && (cur[i] == fired[i]);
                    for (int i = 0; i < 16; ++i) cur[i] = false;
                    loops = lp;
                }
                cur[st] = true;
            }
            for (int i = 0; i < 16; ++i) nf += fired[i] ? 1 : 0;
            check (seen && locked && nf > 2 && nf < 15, "CHANCE loop-lock: Drift 0 fires the same steps every loop (partial pattern)");
        }
        {
            // sorted vs as-played first-loop order
            ArpExtParams Xs = neutralExt(); Xs.sorted = false;
            FlowArp arp; arp.reset(); arp.setExt (Xs); arp.setLanes (neutralLanes());
            arp.noteOn (67, 100); arp.noteOn (60, 100); arp.noteOn (64, 100);
            std::vector<Ev> c; run (arp, 256, (int) (3 * stepSmp + 100), c);
            std::vector<int> ons; for (auto& e : c) if (e.on) ons.push_back (e.note);
            check (ons.size() >= 3 && ons[0] == 67 && ons[1] == 60 && ons[2] == 64,
                   "SORTED off: notes walk in as-played order (67,60,64)");
        }
        {
            // fb107 regression: the RATE ladder must come back DOWN — a stale
            // high-water refire guard used to eat every step after fast->slow
            FlowArp arp; arp.reset(); arp.setExt (neutralExt()); arp.setLanes (neutralLanes());
            arp.noteOn (60, 100);
            std::vector<Ev> c;
            double ppq = 0; long long base = 0;
            auto seg = [&] (float rate, int total, std::vector<Ev>* out) {
                for (int done = 0; done < total; done += 256) {
                    ArpEvent ev[kArpMaxEvents];
                    const int n = arp.process (rate, 0.5f, 0, 0, 0, ppq, bpm, sr, 256, true, ev, kArpMaxEvents);
                    if (out) for (int i = 0; i < n; ++i) out->push_back ({ ev[i].on, ev[i].note, ev[i].vel, base + ev[i].sampleOffset });
                    ppq += (bpm / 60.0) / sr * 256; base += 256; } };
            seg (16.0f / 18.0f, 48000, nullptr);     // 1/64 for a second (huge step indices)
            seg (5.0f  / 18.0f, 96000, &c);          // drop to 1/4 for two seconds
            int ons = 0; for (auto& e : c) if (e.on) ++ons;
            check (ons >= 2, "fb107: RATE fast->slow keeps firing (refire guard re-arms on grid change)");
        }
        {
            // WAVE lane: full depth/timbre, lane at 1, no slew -> +0.5 frame offset
            ArpExtParams Xw = neutralExt(); Xw.wDepth = 1.0f; Xw.timbre = 1.0f; Xw.wSlide = 0.0f;
            ArpLaneData Lw = neutralLanes();
            for (int i = 0; i < kArpLaneMax; ++i) Lw.wt[i] = 1.0f;
            FlowArp arp; arp.reset(); arp.setExt (Xw); arp.setLanes (Lw);
            arp.noteOn (60, 100);
            std::vector<Ev> c; run (arp, 256, 12000, c);
            check (std::fabs (arp.waveMod() - 0.5f) < 0.01f, "WAVE lane at 1, full Depth+Timbre -> +0.5 frame offset");
        }
    }

    std::printf ("\n");
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n\n", g_pass);
    else             std::printf ("%d passed, %d FAILED\n\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
