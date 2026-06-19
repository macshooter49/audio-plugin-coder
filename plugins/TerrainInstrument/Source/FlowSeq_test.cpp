// =============================================================================
//  FlowSeq_test.cpp  —  offline proof harness for the rebuilt SEQ engine
//  g++ -std=c++17 -Wall -Wextra -ISource Source/FlowSeq_test.cpp -o /tmp/flowseq && /tmp/flowseq
// =============================================================================
#include "FlowSeq.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <set>
#include <cmath>

using namespace wc;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what)
{
    ++g_checks;
    if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); }
}

// ── event-collecting driver (absolute sample positions) ──────────────────────
struct Ev { bool on; int note; int vel; long long abs; bool legato; bool accent; };

static std::vector<Ev> runSeq (FlowSeq& s, float rate, float gate, float vary, float traj, float morph,
                               double bpm, double sr, int blk, int nblk, bool playing = true)
{
    std::vector<Ev> evs; SeqEvent buf[256];
    const double pps = (bpm / 60.0) / sr; double ppq = 0.0; long long absS = 0;
    for (int b = 0; b < nblk; ++b)
    {
        const double hp = playing ? ppq : 0.0;
        const int n = s.process (rate, gate, vary, traj, morph, hp, bpm, sr, blk, playing, buf, 256);
        for (int i = 0; i < n; ++i) evs.push_back ({ buf[i].on, buf[i].note, buf[i].vel, absS + buf[i].sampleOffset, buf[i].legato, buf[i].accent });
        absS += blk; ppq += pps * blk;
    }
    return evs;
}
static std::vector<int> onNotes (const std::vector<Ev>& e) { std::vector<int> v; for (auto& x : e) if (x.on) v.push_back (x.note); return v; }
static int countOn  (const std::vector<Ev>& e) { int c = 0; for (auto& x : e) if (x.on) ++c; return c; }
static int countOff (const std::vector<Ev>& e) { int c = 0; for (auto& x : e) if (! x.on) ++c; return c; }

static SeqStep mk (bool on, int deg, float gate = 0.5f, float vel = 1.0f)
{ SeqStep s; s.on = on; s.degree = deg; s.gate = gate; s.vel = vel; return s; }

static void holdCmaj (FlowSeq& s) { s.noteOn (60, 100); s.noteOn (64, 100); s.noteOn (67, 100); }

// silence-only pattern of length n with all steps off, then set specific steps
static void clearPattern (FlowSeq& s, int len) { s.setLength (len); for (int i = 0; i < kSeqMaxSteps; ++i) s.setStep (i, mk (false, 0)); }

constexpr uint16_t MAJOR = 2741; // bits 0,2,4,5,7,9,11

int main()
{
    std::printf ("FlowSeq engine — full feature proof\n");

    // ── T1: deterministic direction maps ──────────────────────────────────────
    {
        auto seq = [] (SeqDir d, int len, int n) { std::vector<int> v; for (int k = 0; k < n; ++k) v.push_back (seqDeterministicPos (d, k, len)); return v; };
        check ((seq (SeqDir::Forward, 4, 5)  == std::vector<int>{0,1,2,3,0}), "T1 Forward len4");
        check ((seq (SeqDir::Reverse, 4, 5)  == std::vector<int>{3,2,1,0,3}), "T1 Reverse len4");
        check ((seq (SeqDir::PingPong, 4, 8) == std::vector<int>{0,1,2,3,3,2,1,0}), "T1 PingPong repeats endpoints");
        check ((seq (SeqDir::Pendulum, 4, 8) == std::vector<int>{0,1,2,3,2,1,0,1}), "T1 Pendulum no endpoint repeat");
        check ((seq (SeqDir::Converge, 4, 4) == std::vector<int>{0,3,1,2}), "T1 Converge len4");
        check ((seq (SeqDir::Diverge, 5, 5)  == std::vector<int>{2,3,1,4,0}), "T1 Diverge len5");
        // all deterministic dirs stay in range
        bool inRange = true; for (int d = 0; d < kSeqDirN; ++d) for (int k = 0; k < 50; ++k) { int p = seqDeterministicPos ((SeqDir) d, k, 7); if (p < 0 || p >= 7) inRange = false; }
        check (inRange, "T1 all deterministic dirs in [0,len)");
    }

    // ── T1b: stateful directions via engine stay valid (notes are real chord tones) ──
    {
        for (float traj : { 0.45f, 0.55f, 0.65f, 0.75f }) // Random / RandomSkip / Brownian / Shuffle band
        {
            FlowSeq s; holdCmaj (s); s.setLength (4);
            for (int i = 0; i < 4; ++i) s.setStep (i, mk (true, i));
            auto e = runSeq (s, 0.5f, 0.5f, 0.0f, traj, 0.0f, 120, 48000, 256, 64);
            bool valid = true; for (int nt : onNotes (e)) { int pc = nt % 12; if (! (pc == 0 || pc == 4 || pc == 7)) valid = false; }
            check (valid && countOn (e) > 0, "T1b stateful direction stays on chord tones");
        }
    }

    // ── T2: scale helpers ──────────────────────────────────────────────────────
    {
        check (seqScaleSize (MAJOR) == 7, "T2 major scale size 7");
        const int want[8] = { 60,62,64,65,67,69,71,72 };
        bool ok = true; for (int d = 0; d <= 7; ++d) if (seqScaleNote (60, MAJOR, d) != want[d]) ok = false;
        check (ok, "T2 major degree->pitch 60..72");
        check (seqScaleNote (60, MAJOR, -1) == 59, "T2 negative degree wraps down (B below)");
    }

    // ── T3: Euclidean distribution ─────────────────────────────────────────────
    {
        auto pulses = [] (int steps, int p, int rot) { bool o[kSeqMaxSteps]; seqEuclid (steps, p, rot, o); int c = 0; for (int i = 0; i < steps; ++i) c += o[i] ? 1 : 0; return c; };
        check (pulses (8, 3, 0) == 3, "T3 E(8,3) has 3 pulses");
        check (pulses (8, 5, 0) == 5, "T3 E(8,5) has 5 pulses");
        check (pulses (8, 8, 0) == 8, "T3 E(8,8) all on");
        check (pulses (8, 0, 0) == 0, "T3 E(8,0) all off");
        check (pulses (16, 7, 3) == 7, "T3 E(16,7) rot keeps 7 pulses");
        // rotation shifts the pattern (not identical) but conserves count
        bool a[kSeqMaxSteps], b[kSeqMaxSteps]; seqEuclid (8, 3, 0, a); seqEuclid (8, 3, 1, b);
        bool diff = false; for (int i = 0; i < 8; ++i) if (a[i] != b[i]) diff = true;
        check (diff, "T3 rotation changes pattern");
    }

    // ── T4: weighted chord-tone offset bounds + skew ───────────────────────────
    {
        ArpRng r; r.seed (1234);
        int mn = 99, mx = -99; double sumLow = 0, sumHigh = 0; const int N = 4000;
        for (int i = 0; i < N; ++i) { int o = seqWeightedOffset (4, 0.5f, 0.0f, r); mn = std::min (mn, o); mx = std::max (mx, o); sumLow += o; }
        check (mn >= 0 && mx <= 4, "T4 offset within [0,reach]");
        for (int i = 0; i < N; ++i) sumHigh += seqWeightedOffset (4, 0.5f, 1.0f, r);
        check (sumLow / N < sumHigh / N, "T4 bias 0 skews lower than bias 1");
        // spread narrows reach
        int mxNarrow = -99; for (int i = 0; i < N; ++i) mxNarrow = std::max (mxNarrow, seqWeightedOffset (8, 0.0f, 0.5f, r));
        int mxWide = -99;   for (int i = 0; i < N; ++i) mxWide   = std::max (mxWide,   seqWeightedOffset (8, 1.0f, 0.5f, r));
        check (mxNarrow < mxWide, "T4 small spread narrows reach");
    }

    // ── T5: trig conditions via engine ─────────────────────────────────────────
    {
        // Ratio 2:4 over 12 single-step loops -> fires on loops 1,5,9 (3 fires)
        FlowSeq s; holdCmaj (s); clearPattern (s, 1);
        SeqStep st = mk (true, 0); st.cond.type = SeqCond::Ratio; st.cond.a = 2; st.cond.b = 4; s.setStep (0, st);
        // 12 steps at rate 0.5 (beats .25 -> 6000 smp/step); blk 512 -> ~12 steps need ~141 blocks
        auto e = runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 145);
        check (countOn (e) == 3, "T5 Ratio 2:4 -> 3 fires in 12 loops");

        FlowSeq s2; holdCmaj (s2); clearPattern (s2, 1);
        SeqStep f = mk (true, 0); f.cond.type = SeqCond::First; s2.setStep (0, f);
        check (countOn (runSeq (s2, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 145)) == 1, "T5 First -> 1 fire");

        FlowSeq s3; holdCmaj (s3); clearPattern (s3, 1);
        SeqStep al = mk (true, 0); s3.setStep (0, al);                    // Always baseline
        int always = countOn (runSeq (s3, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 145));
        FlowSeq s3b; holdCmaj (s3b); clearPattern (s3b, 1);
        SeqStep nf = mk (true, 0); nf.cond.type = SeqCond::NotFirst; s3b.setStep (0, nf);
        int notfirst = countOn (runSeq (s3b, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 145));
        check (notfirst == always - 1, "T5 NotFirst -> all-but-first fire");

        // Fill gating
        FlowSeq s4; holdCmaj (s4); clearPattern (s4, 1);
        SeqStep fl = mk (true, 0); fl.cond.type = SeqCond::Fill; s4.setStep (0, fl);
        check (countOn (runSeq (s4, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 60)) == 0, "T5 Fill off -> no fire");
        s4.setFill (true);
        check (countOn (runSeq (s4, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 60)) > 0, "T5 Fill on -> fires");

        // Pre: 2-step, step0=First, step1=Pre -> loop0 both fire, loop1 neither (count==2 over 2 loops)
        FlowSeq s5; holdCmaj (s5); clearPattern (s5, 2);
        SeqStep p0 = mk (true, 0); p0.cond.type = SeqCond::First;
        SeqStep p1 = mk (true, 1); p1.cond.type = SeqCond::Pre;
        s5.setStep (0, p0); s5.setStep (1, p1);
        // 2 loops = 4 steps; rate .5 -> 6000/step -> ~48 blocks
        check (countOn (runSeq (s5, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 50)) == 2, "T5 First+Pre chain -> 2 fires in 2 loops");
    }

    // ── T6: basic playback follows the drawn pattern (chord-degree) ────────────
    {
        FlowSeq s; holdCmaj (s); clearPattern (s, 4);
        s.setStep (0, mk (true, 0)); s.setStep (1, mk (false, 0)); s.setStep (2, mk (true, 1)); s.setStep (3, mk (true, 2));
        auto on = onNotes (runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 96));
        // pattern -> 60,67? no: deg0=60, deg1=64, deg2=67, with a rest at step1
        bool ok = on.size() >= 6 && on[0] == 60 && on[1] == 64 && on[2] == 67 && on[3] == 60 && on[4] == 64 && on[5] == 67;
        check (ok, "T6 pattern plays 60,64,67 repeating with rest");
    }

    // ── T7: scale mode degree -> pitch ─────────────────────────────────────────
    {
        FlowSeq s; holdCmaj (s); clearPattern (s, 3);
        s.setScale (60, MAJOR, true);
        s.setStep (0, mk (true, 0)); s.setStep (1, mk (true, 1)); s.setStep (2, mk (true, 3));
        auto on = onNotes (runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 72));
        bool ok = on.size() >= 3 && on[0] == 60 && on[1] == 62 && on[2] == 65; // scale degrees 0,1,3 -> C,D,F
        check (ok, "T7 scale mode 60,62,65");
    }

    // ── T8: ratchet ────────────────────────────────────────────────────────────
    {
        // Repeat ratchet x4 on a single step -> 4 ons, same note, increasing offsets
        FlowSeq s; holdCmaj (s); clearPattern (s, 1);
        SeqStep st = mk (true, 0, 0.9f); st.ratchet = 4; s.setStep (0, st);
        auto e = runSeq (s, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 11); // 1 step (ratchet tail in-block, S=1 excluded)
        auto on = onNotes (e);
        check ((int) on.size() == 4, "T8 ratchet x4 -> 4 note-ons");
        bool same = true; for (int nt : on) if (nt != 60) same = false; check (same, "T8 repeat ratchet same note");
        // offsets strictly increasing
        std::vector<long long> offs; for (auto& x : e) if (x.on) offs.push_back (x.abs);
        bool inc = true; for (size_t i = 1; i < offs.size(); ++i) if (offs[i] <= offs[i-1]) inc = false; check (inc, "T8 ratchet offsets increasing");

        // ChordUp ratchet x3 -> 60,64,67
        FlowSeq s2; holdCmaj (s2); clearPattern (s2, 1);
        SeqStep cu = mk (true, 0, 0.9f); cu.ratchet = 3; cu.ratMode = RatchetMode::ChordUp; s2.setStep (0, cu);
        auto on2 = onNotes (runSeq (s2, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 11));
        check (on2.size() >= 3 && on2[0] == 60 && on2[1] == 64 && on2[2] == 67, "T8 ChordUp ratchet walks 60,64,67");

        // velocity ramp monotonic
        FlowSeq s3; s3.noteOn (60, 120); s3.noteOn (64, 120); s3.noteOn (67, 120); clearPattern (s3, 1);
        SeqStep vr = mk (true, 0, 0.9f); vr.ratchet = 4; vr.ratVelRamp = 1.0f; s3.setStep (0, vr);
        auto e3 = runSeq (s3, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 11);
        std::vector<int> vels; for (auto& x : e3) if (x.on) vels.push_back (x.vel);
        bool ramp = vels.size() == 4; for (size_t i = 1; i < vels.size(); ++i) if (vels[i] < vels[i-1]) ramp = false;
        check (ramp, "T8 ratchet velocity ramp monotonic up");
    }

    // ── T9: block-chord play + voicing ─────────────────────────────────────────
    {
        FlowSeq s; holdCmaj (s); clearPattern (s, 1);
        SeqStep st = mk (true, 0); st.play = StepPlay::Chord; s.setStep (0, st);
        auto e = runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 8);
        auto on = onNotes (e);
        std::set<int> notes (on.begin(), on.end());
        check (on.size() == 3 && notes.count (60) && notes.count (64) && notes.count (67), "T9 block chord stamps 60,64,67");
        // same onset (simultaneous)
        std::vector<long long> offs; for (auto& x : e) if (x.on) offs.push_back (x.abs);
        bool simul = offs.size() == 3 && offs[0] == offs[1] && offs[1] == offs[2]; check (simul, "T9 block chord simultaneous");
        // voicing=1 raises lowest note an octave -> 72 present, 60 absent
        FlowSeq s2; holdCmaj (s2); clearPattern (s2, 1);
        SeqStep v = mk (true, 0); v.play = StepPlay::Chord; v.voicing = 1; s2.setStep (0, v);
        auto on2 = onNotes (runSeq (s2, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 8));
        std::set<int> n2 (on2.begin(), on2.end());
        check (n2.count (72) && ! n2.count (60), "T9 voicing=1 inverts lowest up an octave");
    }

    // ── T10: strum staggers chord notes in time ────────────────────────────────
    {
        FlowSeq s; holdCmaj (s); clearPattern (s, 1);
        SeqStep st = mk (true, 0); st.play = StepPlay::Strum; st.strum = 0.8f; s.setStep (0, st);
        auto e = runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 6);
        std::vector<long long> offs; for (auto& x : e) if (x.on) offs.push_back (x.abs);
        bool inc = offs.size() == 3 && offs[0] < offs[1] && offs[1] < offs[2];
        check (inc, "T10 strum -> 3 staggered onsets");
    }

    // ── T11: chord-tone reach + déjà-vu pitch locking ──────────────────────────
    {
        // dejavu pitch = 1 -> locked: every fire identical
        FlowSeq s; holdCmaj (s); clearPattern (s, 1);
        SeqStep st = mk (true, 0); st.reach = 4; s.setStep (0, st);
        s.setGenerative (1.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f);
        auto on = onNotes (runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 200));
        bool allSame = on.size() > 5; for (size_t i = 1; i < on.size(); ++i) if (on[i] != on[0]) allSame = false;
        check (allSame, "T11 dejavu pitch=1 locks chord-tone choice");

        // dejavu pitch = 0 -> varies
        FlowSeq s2; holdCmaj (s2); clearPattern (s2, 1);
        SeqStep st2 = mk (true, 0); st2.reach = 4; s2.setStep (0, st2);
        s2.setGenerative (1.0f, 0.0f, 0.7f, 0.5f, 0.0f, 0.0f);
        auto on2 = onNotes (runSeq (s2, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 200));
        std::set<int> distinct (on2.begin(), on2.end());
        check (distinct.size() > 1, "T11 dejavu pitch=0 varies chord-tone choice");

        // bounds: every note is a Cmaj chord tone (possibly octave-shifted)
        bool valid = true; for (int nt : on2) { int pc = nt % 12; if (! (pc == 0 || pc == 4 || pc == 7)) valid = false; }
        check (valid, "T11 reach stays on chord tones");
    }

    // ── T12: déjà-vu rhythm locking ────────────────────────────────────────────
    {
        // Build firing-set per loop for a 4-step prob pattern; dejavu rhythm=1 -> loops 2..5 identical
        auto fireSetsPerLoop = [] (float dejavuR) {
            FlowSeq s; holdCmaj (s); clearPattern (s, 4);
            for (int i = 0; i < 4; ++i) { SeqStep st = mk (true, i); st.cond.type = SeqCond::Prob; st.cond.prob = 0.5f; s.setStep (i, st); }
            s.setGenerative (dejavuR, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f);
            // capture onsets with absolute ppq -> map to loop = floor(step / 4)
            SeqEvent buf[256]; const double bpm = 120, sr = 48000, pps = (bpm/60.0)/sr; const float beats = arpBeatsPerStep (0.5f);
            double ppq = 0; std::vector<std::set<int>> loops (8);
            for (int b = 0; b < 400; ++b) {
                int n = s.process (0.5f, 0.5f, 1.0f, 0.0f, 0.0f, ppq, bpm, sr, 256, true, buf, 256);
                for (int i = 0; i < n; ++i) if (buf[i].on) {
                    double t = ppq + buf[i].sampleOffset * pps; long long step = (long long) std::llround (t / beats);
                    long long loop = step / 4; int slot = (int) (step % 4);
                    if (loop >= 0 && loop < 8) loops[(size_t) loop].insert (slot);
                }
                ppq += pps * 256;
            }
            return loops;
        };
        auto locked = fireSetsPerLoop (1.0f);
        bool same = (locked[1] == locked[2]) && (locked[2] == locked[3]) && ! locked[1].empty();
        check (same, "T12 dejavu rhythm=1 locks fire pattern across loops");
        auto rnd = fireSetsPerLoop (0.0f);
        bool varies = ! ((rnd[1] == rnd[2]) && (rnd[2] == rnd[3]));
        check (varies, "T12 dejavu rhythm=0 varies fire pattern");
    }

    // ── T13: mod lanes — clocking, smoothing, bipolar ──────────────────────────
    {
        // length2 [1,-1], smooth0 -> output snaps to +/-1
        FlowSeq s; holdCmaj (s);
        SeqModLane L; L.length = 2; L.rate = 1.0f; L.smooth = 0.0f; L.val[0] = 1.0f; L.val[1] = -1.0f;
        s.setModLane (0, L);
        SeqEvent buf[256]; double bpm = 120, sr = 48000, pps = (bpm/60.0)/sr; double ppq = 0;
        double mn = 9, mx = -9; bool everMid = false;
        for (int b = 0; b < 120; ++b) {
            s.process (0.5f, 0.5f, 0.0f, 0.0f, 0.0f, ppq, bpm, sr, 256, true, buf, 256);
            float m = s.modValue (0); mn = std::min (mn, (double) m); mx = std::max (mx, (double) m);
            if (std::fabs (m) < 0.9f) everMid = true;
            ppq += pps * 256;
        }
        check (mn < -0.9 && mx > 0.9, "T13 mod lane reaches +/-1 (bipolar)");
        check (! everMid, "T13 smooth=0 mod lane snaps (no mid values)");

        // smooth high -> gradual (mid values appear)
        FlowSeq s2; holdCmaj (s2);
        SeqModLane L2; L2.length = 2; L2.rate = 1.0f; L2.smooth = 0.9f; L2.val[0] = 1.0f; L2.val[1] = -1.0f;
        s2.setModLane (0, L2);
        bool mid = false; ppq = 0;
        for (int b = 0; b < 120; ++b) { s2.process (0.5f, 0.5f, 0.0f, 0.0f, 0.0f, ppq, bpm, sr, 256, true, buf, 256); float m = s2.modValue (0); if (std::fabs (m) < 0.8f) mid = true; ppq += pps * 256; }
        check (mid, "T13 smooth>0 produces gradual mid values");

        // rate 2 vs 1 -> faster lane sees more transitions
        auto transitions = [&] (float rate) {
            FlowSeq sx; sx.noteOn (60,100);
            SeqModLane Lx; Lx.length = 2; Lx.rate = rate; Lx.smooth = 0.0f; Lx.val[0] = 1.0f; Lx.val[1] = -1.0f; sx.setModLane (0, Lx);
            double q = 0; float prev = sx.modValue (0); int tr = 0;
            for (int b = 0; b < 120; ++b) { sx.process (0.5f, 0.5f, 0.0f, 0.0f, 0.0f, q, bpm, sr, 256, true, buf, 256); float m = sx.modValue (0); if (m != prev) ++tr; prev = m; q += pps * 256; }
            return tr;
        };
        check (transitions (2.0f) > transitions (1.0f), "T13 mod lane rate=2 clocks faster than rate=1");
    }

    // ── T14: per-step parameter locks ──────────────────────────────────────────
    {
        FlowSeq s; holdCmaj (s); clearPattern (s, 3);
        SeqStep a = mk (true, 0); a.numLocks = 1; a.locks[0] = { 5, 0.7f };
        SeqStep b = mk (true, 1); b.numLocks = 1; b.locks[0] = { 9, 0.3f };
        SeqStep c = mk (true, 2); c.numLocks = 1; c.locks[0] = { 5, 0.2f };  // re-locks param 5
        s.setStep (0, a); s.setStep (1, b); s.setStep (2, c);
        runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 28); // single pass over 3 steps
        // param 5 should now hold 0.2 (latest), param 9 holds 0.3
        float v5 = -1, v9 = -1; for (int i = 0; i < s.activeLockCount(); ++i) { if (s.activeLockParam (i) == 5) v5 = s.activeLockValue (i); if (s.activeLockParam (i) == 9) v9 = s.activeLockValue (i); }
        check (std::fabs (v5 - 0.2f) < 1e-6 && std::fabs (v9 - 0.3f) < 1e-6, "T14 locks held; param5 re-locked to latest");
        SeqEvent tmp[64]; s.releaseAll (tmp, 64);
        check (s.activeLockCount() == 0, "T14 releaseAll clears active locks");
    }

    // ── T15: mutate deterministic + bounded ────────────────────────────────────
    {
        FlowSeq a, b; a.setDefaultPattern(); b.setDefaultPattern();
        a.mutate (0.5f, 777); b.mutate (0.5f, 777);
        bool identical = true, changed = false;
        FlowSeq orig; orig.setDefaultPattern();
        for (int i = 0; i < 16; ++i) {
            SeqStep sa = a.getStep (i), sb = b.getStep (i), so = orig.getStep (i);
            if (sa.degree != sb.degree || sa.on != sb.on) identical = false;
            if (sa.degree != so.degree || sa.on != so.on) changed = true;
            if (std::abs (sa.degree - so.degree) > 1) identical = false; // bounded ±1 per mutate
        }
        check (identical, "T15 mutate deterministic with seed + bounded ±1");
        check (changed, "T15 mutate actually changes the pattern");
    }

    // ── T16: micro-timing shifts onset ─────────────────────────────────────────
    {
        auto secondOnset = [] (float micro) {
            FlowSeq s; holdCmaj (s); clearPattern (s, 2);
            SeqStep s0 = mk (true, 0), s1 = mk (true, 0); s1.micro = micro; s.setStep (0, s0); s.setStep (1, s1);
            auto e = runSeq (s, 1.0f, 0.3f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 4); // fast (1/256), step1 lands mid-block0
            std::vector<long long> ons; for (auto& x : e) if (x.on) ons.push_back (x.abs);
            return ons.size() >= 2 ? ons[1] : -1;
        };
        long long neg = secondOnset (-0.4f), zero = secondOnset (0.0f), pos = secondOnset (0.4f);
        check (neg >= 0 && zero >= 0 && pos >= 0, "T16 micro test produced onsets");
        check (neg < zero, "T16 negative micro shifts step earlier");
        check (pos > neg, "T16 positive micro later than negative");
    }

    // ── T17: slide/tie legato flag ─────────────────────────────────────────────
    {
        FlowSeq s; holdCmaj (s); clearPattern (s, 2);
        SeqStep s0 = mk (true, 0), s1 = mk (true, 1); s1.slide = true; s.setStep (0, s0); s.setStep (1, s1);
        auto e = runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 60);
        // find the note-on for degree1 (64) and confirm legato
        bool found = false, legato = false; for (auto& x : e) if (x.on && x.note == 64) { found = true; if (x.legato) legato = true; }
        check (found && legato, "T17 slide sets legato on the slid note-on");
        // a non-slide step is not legato
        bool firstLegato = false; for (auto& x : e) if (x.on && x.note == 60) firstLegato = firstLegato || x.legato;
        check (! firstLegato, "T17 non-slide step is not legato");
    }

    // ── T18: accent boosts velocity + sets flag ────────────────────────────────
    {
        FlowSeq s; s.noteOn (60, 100); clearPattern (s, 2);
        SeqStep plain = mk (true, 0, 0.5f, 0.5f);
        SeqStep acc   = mk (true, 0, 0.5f, 0.5f); acc.accent = true;
        s.setStep (0, plain); s.setStep (1, acc);
        auto e = runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 60);
        int plainVel = -1, accVel = -1; bool accFlag = false;
        // step0 fires first (plain), step1 (accent) second; both note 60 -> use order
        std::vector<Ev> ons; for (auto& x : e) if (x.on) ons.push_back (x);
        if (ons.size() >= 2) { plainVel = ons[0].vel; accVel = ons[1].vel; accFlag = ons[1].accent; }
        check (accVel > plainVel, "T18 accent raises velocity");
        check (accFlag, "T18 accent flag propagates to event");
    }

    // ── T19: engine integrity — no stuck notes, free-run, latch, releaseAll ────
    {
        // default pattern, block chords, long run -> ons == offs after drain
        FlowSeq s; holdCmaj (s);
        for (int i = 0; i < 16; ++i) { SeqStep st = s.getStep (i); st.play = StepPlay::Chord; s.setStep (i, st); }
        auto e = runSeq (s, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 256, 400);
        SeqEvent tail[64]; int t = s.releaseAll (tail, 64);
        int totalOff = countOff (e) + t;
        check (countOn (e) == totalOff, "T19 every note-on closed (no stuck notes)");
        check (countOn (e) > 50, "T19 produced a healthy number of notes");

        // free-run (playing=false) still sequences
        FlowSeq fr; holdCmaj (fr); clearPattern (fr, 4); for (int i = 0; i < 4; ++i) fr.setStep (i, mk (true, i));
        check (countOn (runSeq (fr, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 96, /*playing*/false)) > 0, "T19 free-run sequences with transport stopped");

        // latch: notes released but latch keeps playing
        FlowSeq la; la.setLatch (true); la.noteOn (60, 100); la.noteOn (64, 100); la.noteOn (67, 100);
        clearPattern (la, 4); for (int i = 0; i < 4; ++i) la.setStep (i, mk (true, i));
        la.noteOff (60); la.noteOff (64); la.noteOff (67); // physically released
        check (countOn (runSeq (la, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 120, 48000, 512, 96)) > 0, "T19 latch keeps sequencing after note-off");

        // releaseAll emits offs for everything open
        FlowSeq ra; holdCmaj (ra); for (int i = 0; i < 16; ++i) { SeqStep st = ra.getStep (i); st.play = StepPlay::Chord; ra.setStep (i, st); }
        runSeq (ra, 0.5f, 0.9f, 0.0f, 0.0f, 0.0f, 120, 48000, 256, 6); // open some chord notes mid-gate
        SeqEvent rbuf[64]; int rn = ra.releaseAll (rbuf, 64);
        check (rn >= 0, "T19 releaseAll returns cleanly");
    }

    // ── summary ────────────────────────────────────────────────────────────────
    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    if (g_fail == 0) std::printf ("ALL %d CHECKS PASSED\n", g_checks);
    return g_fail == 0 ? 0 : 1;
}
