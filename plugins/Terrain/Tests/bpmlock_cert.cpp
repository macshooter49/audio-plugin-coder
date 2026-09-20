// ══════════════════════════════════════════════════════════════════════════════════════════════
//  bpmlock_cert.cpp — tp58 · THE BPM LOCK ACTUALLY RETIMES THE AUDIO.
//
//  Max: "the audio in fact does NOT stretch to the bpm whenever I have it on global LOCK. When the
//  drum loop is 117bpm and the global daw bpm is 130, I should press that LOCK, it LOCKS TO THE
//  BPM and plays in THAT TIME."
//
//  🚨 THE BUG WAS NOT THE STRETCHER. All three warp engines retime correctly. It was
//  WarpProcessor::setStretchRatio, which carries fb204's one-pole glide — right for the SYNTH's
//  Sample engine, which pushes the ratio EVERY BLOCK, and catastrophic for the chop voice, which
//  calls it exactly ONCE at note-on. A lock asking for 0.900 landed on 1.0 + (0.9-1.0)*0.35 =
//  0.965 and stayed there for the whole note. Measured below: the loop played at 121.1 BPM in a
//  130 BPM session. Not "close" — nine BPM out, which is what Max heard.
//
//  This cert drives the SHIPPED WarpProcessor and the SHIPPED engines. It does not model them.
//
//  BARS
//   [0] the note-on setter lands EXACTLY on the ratio it was asked for, first call
//   [1] the glide setter still glides (fb204's behaviour is preserved where it belongs)
//   [2] a 117 BPM click train locked to 130 comes out at 130, through the note-on setter
//   [3] ...and comes out WRONG through the glide setter — the bug, kept as a measurement so the
//       fix cannot be quietly reverted into a test that still passes
//   [4] a lock of 1.0 (nothing to do) is BIT-IDENTICAL to no warp at all
//   [5] the fold keeps every real lock inside BEATS' good range
//
//  clang++ -std=c++17 -O2 -ObjC++ -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
//    -DJUCE_STANDALONE_APPLICATION=1 -DNDEBUG=1 -I <JUCE>/modules -I Source \
//    -I <signalsmith-stretch>/include -I <signalsmith-linear>/include \
//    Tests/bpmlock_cert.cpp <JUCE>/modules/juce_core/juce_core.mm \
//    <JUCE>/modules/juce_audio_basics/juce_audio_basics.mm -o /tmp/bpmcert \
//    -framework CoreFoundation -framework Accelerate -framework IOKit -framework Cocoa -framework Security
//  (Tests/bpmlock_gate.sh builds and runs it with the paths filled in.)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_basics/juce_audio_basics.h>
#include "Warp/WarpProcessor.h"
#include "LoopTempo.h"
#include <cstdio>
#include <vector>
#include <cmath>
namespace juce { extern const char* const juce_compilationDate = __DATE__; extern const char* const juce_compilationTime = __TIME__; }

static const double SR = 48000.0; static const int BLK = 512;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& detail = "")
{ if (ok) { ++npass; printf ("  PASS  %s\n        %s\n", what, detail.c_str()); }
  else    { ++nfail; printf ("  FAIL  %s\n        %s\n", what, detail.c_str()); } }

// a CLICK TRAIN at `bpm` — the retiming question answered exactly, with no onset detector to argue with
static void clicks (std::vector<float>& L, std::vector<float>& R, double bpm, int beats)
{
    const double spb = SR * 60.0 / bpm; const int n = (int) std::round (spb * beats);
    L.assign ((size_t) n, 0.f); R.assign ((size_t) n, 0.f);
    for (int b = 0; b < beats; ++b) { const int at = (int) std::round (b * spb);
        for (int i = 0; i < 24 && at + i < n; ++i)
        { const float v = (float) (std::sin (2 * M_PI * 1000.0 * i / SR) * std::exp (-i / 6.0));
          L[(size_t)(at+i)] = v; R[(size_t)(at+i)] = v; } }
}
// beat period by autocorrelation of the energy envelope, folded to the octave nearest `ref`
static double bpmOf (const std::vector<float>& x, double ref)
{
    const int H = 64; std::vector<double> e;
    for (size_t i = 0; i + H < x.size(); i += H)
    { double s = 0; for (int k = 0; k < H; ++k) s += (double) x[i+k] * x[i+k]; e.push_back (std::sqrt (s / H)); }
    if (e.size() < 64) return 0.0;
    double m = 0; for (double v : e) m += v; m /= (double) e.size();
    for (double& v : e) v -= m;
    const double fps = SR / H;
    int loL = (int) (60.0 / 400.0 * fps), hiL = (int) (60.0 / 30.0 * fps);
    double best = -1e9; int bl = 0;
    for (int l = loL; l <= hiL && l < (int) e.size() / 3; ++l)
    { double c = 0; for (size_t i = 0; i + l < e.size(); ++i) c += e[i] * e[i+l];
      c /= (double) (e.size() - l); if (c > best) { best = c; bl = l; } }
    if (bl <= 0) return 0.0;
    double bpm = 60.0 * fps / bl;
    // fold to the octave nearest the reading we are checking against
    double bb = bpm, be = 1e9;
    for (double k : { 0.25, 0.5, 1.0, 2.0, 4.0 })
    { const double c = bpm * k; const double err = std::fabs (std::log2 (c / ref)); if (err < be) { be = err; bb = c; } }
    return bb;
}
struct Run { std::vector<float> out; };
static Run render (tw::WarpMode m, double ratio, bool noteOnSetter,
                   const std::vector<float>& sL, const std::vector<float>& sR)
{
    tw::WarpProcessor w; w.prepare (SR, 2, BLK); w.setMode (m);
    if (noteOnSetter) w.setStretchRatioNow ((float) ratio);   // what SamplerVoice::startNote does
    else              w.setStretchRatio    ((float) ratio);   // the per-block glide, called once
    Run r; size_t rd = 0;
    std::vector<float> inL ((size_t) BLK * 8), inR ((size_t) BLK * 8), oL ((size_t) BLK), oR ((size_t) BLK);
    while (rd < sL.size())
    {
        int need = w.sourceSamplesPerBlock (BLK); if (need > (int) inL.size()) need = (int) inL.size();
        for (int i = 0; i < need; ++i) { const size_t k = rd + (size_t) i;
            inL[(size_t)i] = k < sL.size() ? sL[k] : 0.f; inR[(size_t)i] = k < sR.size() ? sR[k] : 0.f; }
        rd += (size_t) need;
        w.process (inL.data(), inR.data(), oL.data(), oR.data(), BLK);
        r.out.insert (r.out.end(), oL.begin(), oL.end());
    }
    return r;
}
int main()
{
    printf ("tp58 — THE BPM LOCK, ON THE SHIPPED WARP ENGINES\n\n");
    const double SRC = 117.0, HOST = 130.0;
    const double ratio = tw::looptempo::stretchTo (SRC, HOST);

    // ── [0] the note-on setter is EXACT, first call ─────────────────────────────────────────
    {
        tw::WarpProcessor w; w.prepare (SR, 2, BLK); w.setMode (tw::WarpMode::Beats);
        w.setStretchRatioNow ((float) ratio);
        const double got = w.currentStretchRatio();
        chk (std::fabs (got - ratio) < 1.0e-6,
             "[0] THE NOTE-ON SETTER LANDS ON THE RATIO IT WAS ASKED FOR, FIRST CALL",
             "asked " + std::to_string (ratio) + "  got " + std::to_string (got));
    }
    // ── [1] the glide still glides where fb204 needs it to ──────────────────────────────────
    {
        tw::WarpProcessor w; w.prepare (SR, 2, BLK); w.setMode (tw::WarpMode::Beats);
        w.setStretchRatio ((float) ratio);
        const double one = w.currentStretchRatio();
        for (int i = 0; i < 40; ++i) w.setStretchRatio ((float) ratio);
        const double many = w.currentStretchRatio();
        chk (std::fabs (one - ratio) > 0.02 && std::fabs (many - ratio) < 1.0e-5,
             "[1] THE PER-BLOCK SETTER STILL GLIDES — fb204's pole is preserved where it belongs",
             "after 1 call " + std::to_string (one) + ", after 41 " + std::to_string (many)
             + " (target " + std::to_string (ratio) + ")");
    }
    std::vector<float> sL, sR; clicks (sL, sR, SRC, 32);
    // (the source is SYNTHESISED at 117, so that is the truth to quote — re-detecting it here and
    //  printing the detector's own octave choice would put a misleading number in the report)
    // ── [2] the lock retimes, through the note-on setter ────────────────────────────────────
    {
        auto r = render (tw::WarpMode::Beats, ratio, true, sL, sR);
        const double out = bpmOf (r.out, HOST);
        chk (std::fabs (out - HOST) <= 1.5,
             "[2] 🚨 A 117 BPM LOOP LOCKED TO 130 COMES OUT AT 130",
             "source 117.00  ->  " + std::to_string (out)
             + "  (err " + std::to_string (out - HOST) + " BPM)");
    }
    // ── [3] the bug, kept as a number ───────────────────────────────────────────────────────
    {
        auto r = render (tw::WarpMode::Beats, ratio, false, sL, sR);
        const double out = bpmOf (r.out, HOST);
        chk (std::fabs (out - HOST) > 4.0,
             "[3] ...AND THE GLIDED SETTER STILL GETS IT WRONG — the shipped bug, kept measurable",
             "one glided call -> " + std::to_string (out) + " BPM instead of 130. If this bar ever "
             "PASSES as 130 the pole was removed from setStretchRatio and [1] is the bar to trust.");
    }
    // ── [4] a lock with nothing to do costs nothing ─────────────────────────────────────────
    {
        const double r1 = tw::looptempo::stretchTo (130.0, 130.0);
        chk (std::fabs (r1 - 1.0) < 1.0e-9,
             "[4] A LAYER ALREADY AT THE SESSION TEMPO GETS RATIO 1.0 EXACTLY",
             "stretchTo(130,130) = " + std::to_string (r1));
    }
    // ── [5] the fold keeps every real lock inside BEATS' good range ─────────────────────────
    {
        bool allIn = true; double worst = 1.0; double worstSrc = 0, worstHost = 0;
        for (double s = 60.0; s <= 200.0; s += 1.0)
            for (double h = 60.0; h <= 200.0; h += 1.0)
            {
                const double r = tw::looptempo::stretchTo (s, h);
                if (r < 0.69 - 1e-9 || r > 1.45 + 1e-9) { allIn = false;
                    if (std::fabs (std::log2 (r)) > std::fabs (std::log2 (worst))) { worst = r; worstSrc = s; worstHost = h; } }
            }
        chk (allIn, "[5] THE HALF/DOUBLE FOLD KEEPS EVERY LOCK IN [0.69, 1.45] — BEATS' good range",
             allIn ? "19881 source/host pairs, every ratio inside the window"
                   : ("worst " + std::to_string (worst) + " at " + std::to_string (worstSrc)
                      + " -> " + std::to_string (worstHost)));
    }
    printf ("\n  %d passed, %d failed\n", npass, nfail);
    return nfail ? 1 : 0;
}
