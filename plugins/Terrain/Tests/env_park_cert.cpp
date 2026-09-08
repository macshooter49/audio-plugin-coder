// env_park_cert.cpp — rs2 ITEM 1 (the stuck white follower), the C++ half.
// Proves with the REAL Source/TerrainEnvelope.h (copied beside this file; quoted include — the
// "a quoted #include beats -I" law) that SynthVoice.h's release fast-kill can leave the amp env
// non-Idle forever, so isAmpEnvActive() (SynthVoice.h:437) keeps reporting a sounding voice and
// PluginProcessor.cpp:10995-11007 keeps publishing that voice's geode read head as a follower.
//
// The simulated slot-release logic is a line-for-line model of SynthVoice.h:6273-6309:
//   releaseInaudible = stage()==Release && level() < 1e-4            (6273-6274)
//   arm an 8 ms linear fade (kFinishFadeSec = 0.008, 8104)            (6276-6282)
//   the env keeps ticking while the voice renders the fade           (3428)
//   fade reaches 0 → finishing_=false; playing_=false; clearCurrentNote()   (6304-6309)  ← NO ampEnv_.reset()
//   once playing_ is false renderNextBlock returns at 3043 → the env is never ticked again.
// The steal path resets the env (5803); the fast-kill path does not. -DFIX_RESET=1 is the fix seam.
//
//   c++ -std=c++17 -O2 env_park_cert.cpp -o /tmp/env_park && /tmp/env_park            (shipped: parked > 0)
//   c++ -std=c++17 -O2 -DFIX_RESET=1 env_park_cert.cpp -o /tmp/env_park_fix && /tmp/env_park_fix   (fix: 0)
#include "TerrainEnvelope.h"
#include <cstdio>
#include <vector>
#ifndef FIX_RESET
#define FIX_RESET 0
#endif
using E = terrain::TerrainEnvelope;
struct Case { double sus, relMs, curve; };
struct Out { bool parked; double stopMs; E::Stage stage; };
static Out run (const Case& c, double sr = 48000.0)
{
    E env; env.prepare (sr); env.setMinRelease (0.005);   // SynthVoice.h:277-278
    env.setDelay (0); env.setAttack (0.005); env.setHold (0); env.setDecay (0.200);   // the layout's canonical env (PluginProcessor.cpp:2900-2916)
    env.setSustain (c.sus); env.setRelease (c.relMs * 0.001); env.setAttackCurve (0.5); env.setDecayCurve (0.6); env.setReleaseCurve (c.curve);
    env.noteOn();
    const int holdN = (int) (0.5 * sr);
    for (int i = 0; i < holdN; ++i) env.tick();
    env.noteOff();
    bool finishing = false; float fade = 1.f, step = 0.f; const float fadeSamples = (float) (0.008 * sr);
    bool playing = true; int n = 0;
    while (playing && n < (int) (70.0 * sr))
    {
        env.tick(); ++n;
        const bool releaseInaudible = env.stage() == E::Stage::Release && env.level() < 1.0e-4;
        if ((! env.isActive() || releaseInaudible) && playing)
        {
            if (! finishing) { finishing = true; step = 1.0f / (fadeSamples > 1.f ? fadeSamples : 1.f); fade = 1.f; }
            fade -= step; if (fade < 0.f) fade = 0.f;
        }
        if (finishing && fade <= 0.0f)
        {
            finishing = false; playing = false;
#if FIX_RESET
            env.reset();
#endif
        }
    }
    return { env.isActive(), 1000.0 * n / sr, env.stage() };
}
int main()
{
    std::vector<Case> cases = {
        { 0.70,  300, 0.6 },   // the layout defaults (S 0.7, R 300 ms, release curve +0.6)
        { 0.70,  300, 0.0 },
        { 0.70, 2000, 0.6 },
        { 0.70, 3000, 0.6 },
        { 0.70, 5000, 0.6 },
        { 0.20,  300, 0.6 },
        { 0.05,  300, 0.6 },
        { 0.00,  300, 0.6 },   // a pluck: sustain 0, default release
        { 0.00,  300, 0.0 },
        { 0.00,   50, 0.6 },
        { 0.00, 1000, 0.6 },
        { 1.00,  300, 1.0 },
    };
    int parked = 0;
    std::printf ("  sus    rel_ms  curve | slot freed at   env after slot release      parked?\n");
    for (auto& c : cases)
    {
        Out o = run (c);
        const char* st = o.stage == E::Stage::Idle ? "Idle" : (o.stage == E::Stage::Release ? "Release" : "other");
        std::printf ("  %.2f  %6.0f   %+.1f  | %8.1f ms    isActive=%d stage=%-8s  %s\n", c.sus, c.relMs, c.curve, o.stopMs, (int) o.parked, st, o.parked ? "PARKED-ACTIVE" : "-");
        if (o.parked) ++parked;
    }
    std::printf ("\nFIX_RESET=%d  parked-active voices: %d / %zu\n", (int) FIX_RESET, parked, cases.size());
    return parked ? 1 : 0;
}
