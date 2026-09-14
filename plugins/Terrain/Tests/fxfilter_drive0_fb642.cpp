// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb642 — THE RACK FILTER AT DRIVE 0 IS CLEAN: every engine, measured.
//
//    Tests/fxfilter_drive0_fb642.sh      (build Terrain first; macOS)
//
//  Max: "the filter saturates and drives the audio signal for no damn reason even though the drive is at zero — it's
//  broken." A 220 Hz sine goes through tw::FilterFxEngine (the rack Filter, the exact per-sample routine applyFlt runs)
//  at Drive 0, resonance at its default, follower/LFO/keytrack neutral, at -18 / -12 / -6 / 0 dBFS, with the cutoff put
//  where the tone PASSES (each type runs at cut 0, 0.35 and 1.0 and keeps the setting with the loudest fundamental — an
//  HP with its cutoff at 20 kHz has no fundamental to measure against). THD is every harmonic 2..12 against the output
//  fundamental (Goertzel, 1 s steady state after 0.25 s settle).
//  THE OLD PATH, IN THE SAME BINARY: at Drive 0 the pre-fb642 engine fed the core +14 dB and dropped the output 14 dB. THD
//  does not care about the output drop, so "old" at level L is exactly the new filter model at L + 14 dB.
//  Classified by FilterFxEngine::liftRidesDrive — the plugin's own list, not a copy:
//    [1] the default engine (Ladder LP 24) at Drive 0 is clean at -12 dBFS (THD <= -60 dB);
//    [2] every filter model the old path left audibly distorted (worse than -60 dB THD at -12 dBFS) is at least 12 dB
//        cleaner now, and NO filter model is dirtier (the SVF / SEM / Multi family was already linear at -111..-119 dB —
//        the lift never saturated them, so for them "not dirtier" is the whole claim);
//    effect types keep the old voicing by construction and are listed for the record only.
//    FXF_ALL=1   print every engine
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
#include "FilterFxEngine.h"
#include "fxf_names_fb642.h"   // generated from index.html FLT_ENGINES by the .sh

static double goertzel (const std::vector<float>& x, double f, double fs)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * f / fs, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (float v : x) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (x.size() * 0.5);
}

int main (int argc, char** argv)
{
    const double fs = 48000.0, f0 = 220.0;
    const bool all = std::getenv ("FXF_ALL") != nullptr;
    const float levelsDb[] = { -18.f, -12.f, -6.f, 0.f };
    int worstType = -1; double worstThd = -200;
    int offenders12 = 0, offenders0 = 0;
    printf ("══ fb642 RACK FILTER AT DRIVE 0 — THD (dB re fundamental), 220 Hz sine, cutoff open ══\n");
    printf ("  type                           -18 dBFS  -12 dBFS   -6 dBFS    0 dBFS\n");
    auto run = [&] (int t, float cut, float levelDb, double& h1Out) -> double
    {
        tw::FilterFxEngine e; e.prepare (fs, 512);
        tw::FilterFxEngine::Params p;
        p.engine = t; p.drive = 0.0f; p.mix = 1.0f; p.cut = cut;
        p.env = 0.5f; p.sweep = 0.0f; p.track = 0.0f;
        e.noteOn (60, 1.0f);
        const float a = std::pow (10.0f, levelDb / 20.0f);
        const int settle = (int) (fs * 0.25), n = (int) fs;
        std::vector<float> out; out.reserve ((size_t) n);
        for (int i = 0; i < settle + n; ++i)
        {
            float l = a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f0 * i / fs), r = l;
            e.processSample (l, r, p);
            if (i >= settle) out.push_back (l);
        }
        const double h1 = goertzel (out, f0, fs);
        double hs = 0; for (int k = 2; k <= 12; ++k) { const double h = goertzel (out, f0 * k, fs); hs += h * h; }
        h1Out = h1;
        return (h1 > 1e-9) ? 10.0 * std::log10 (std::max (1e-30, hs) / (h1 * h1)) : 0.0;
    };
    for (int t = 0; t < tw::filters::kNumTypes; ++t)
    {
        // the cutoff where the tone passes
        float bestCut = 1.0f; double bestH1 = -1;
        for (float c : { 0.0f, 0.35f, 1.0f }) { double h1; run (t, c, -18.f, h1); if (h1 > bestH1) { bestH1 = h1; bestCut = c; } }
        double thd[4];
        for (int li = 0; li < 4; ++li) { double h1; thd[li] = run (t, bestCut, levelsDb[li], h1); }
        const bool model = tw::FilterFxEngine::liftRidesDrive (t);
        double oldM12 = 0; if (model) { double h1; oldM12 = run (t, bestCut, -12.f + 14.f, h1); }
        const std::string nm = (t < (int) (sizeof (kNames) / sizeof (kNames[0]))) ? kNames[t] : ("type " + std::to_string (t));
        const bool wasDirty = model && oldM12 > -60.0;
        const bool notBetter = model && ((wasDirty && ! (thd[1] <= oldM12 - 12.0)) || thd[1] > oldM12 + 0.5);
        offenders12 += notBetter; if (t == 0) offenders0 = (thd[1] <= -60.0) ? 0 : 1;
        if (model && thd[3] > worstThd) { worstThd = thd[3]; worstType = t; }
        if (all || notBetter)
            printf ("  %4d %-18s cut %.2f %8.1f  %8.1f  %8.1f  %8.1f   %s\n", t, nm.c_str(), bestCut, thd[0], thd[1], thd[2], thd[3],
                    model ? (juce::String::formatted ("filter model: old path at -12 dBFS was %.1f dB%s", oldM12, notBetter ? "  <- NOT cleaner enough" : "")).toRawUTF8()
                          : "effect type: old +14 dB voicing kept");
    }
    printf ("  %s [1] the default Ladder LP 24 at Drive 0 is clean at -12 dBFS (<= -60 dB THD)\n", offenders0 ? "✗" : "✓");
    printf ("  %s [2] every filter model the old path distorted is >= 12 dB cleaner at -12 dBFS, and none is dirtier (%d fail)\n", offenders12 ? "✗" : "✓", offenders12);
    printf ("  worst filter model at 0 dBFS: type %d (%.1f dB) — the model's own analog curve on a full-scale sine, as in the main filter\n", worstType, worstThd);
    printf ("fxfilter_drive0_fb642: %s\n", (offenders0 || offenders12) ? "FAIL" : "PASS");
    return (offenders0 || offenders12) ? 1 : 0;
}
