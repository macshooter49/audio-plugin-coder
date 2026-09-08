// fb413 — THE SYNC LAW, cross-device. Max: "whichever thing doesn't switch to a time signature
// whenever it's synced, that's a hard rule." A readout is only true if the ENGINE resolves the
// same division from the same knob value, in all three devices AND in the UI's own formatter.
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
float FS = 48000.0f;
#include "TerrainChorusFx.h"
#include "TerrainFlangerFx.h"
#include "TerrainPhaserFx.h"

// index.html: grnDivIdx(v) = 1 + round(v/100*(N-2)), clamped 1..N-1. This is the readout's index.
static int uiIdx (double v01) { const int N = 20;
    int k = 1 + (int) std::lround (v01 * (N - 2)); return std::max (1, std::min (N - 1, k)); }

int main()
{
    const char* D[20] = { "Free","4 bar","2 bar","1 bar","1/2","1/2D","1/2T","1/4","1/4D","1/4T",
                          "1/8","1/8D","1/8T","1/16","1/16D","1/16T","1/32","1/64","1/128","1/256" };
    int bad = 0, n = 0;
    std::printf ("  knob   UI label   chorus Hz   flanger Hz   phaser Hz   (120 BPM)\n");
    for (int i = 0; i <= 20; ++i)
    {
        const double v = i / 20.0;
        const int k = uiIdx (v);
        // the beats the UI's label means, from the house table (all three engines publish it)
        const float beats = tw::TerrainChorusFx::divBeats (k);
        const double want = (beats > 0.0f) ? (120.0 / 60.0) / beats : 0.0;

        tw::TerrainChorusFx  c; tw::TerrainFlangerFx f; tw::TerrainPhaserFx p;
        c.prepare (48000.0, 128); f.prepare (48000.0, 128); p.prepare (48000.0, 128);
        tw::TerrainChorusFx::Params cp;  cp.rate=(float)v; cp.tempoSync=true; cp.bpm=120.0; cp.type=1; cp.character=0;
        tw::TerrainFlangerFx::Params fp; fp.rate=(float)v; fp.tempoSync=true; fp.bpm=120.0; fp.type=1; fp.character=0;
        tw::TerrainPhaserFx::Params  pp; pp.rate=(float)v; pp.tempoSync=true; pp.bpm=120.0; pp.type=0; pp.character=0;
        c.setParams (cp); f.setParams (fp); p.setParams (pp);
        float L[8]={0},R[8]={0}; c.processStereo(L,R,8); f.processStereo(L,R,8); p.processStereo(L,R,8);
        const double hc = c.liveRateHz(), hf = f.liveRateHz(), hp = p.liveRateHz();
        const bool ok = std::fabs (hc-want) < want*0.02 + 1e-3
                     && std::fabs (hf-want) < want*0.02 + 1e-3
                     && std::fabs (hp-want) < want*0.02 + 1e-3;
        if (! ok) ++bad;  ++n;
        std::printf ("  %4.2f   %-8s   %9.3f   %10.3f   %9.3f   want %8.3f  %s\n",
                     v, D[k], hc, hf, hp, want, ok ? "" : "  <-- MISMATCH");
    }
    std::printf ("\n  %d of %d knob positions agree across all three engines and the UI label\n",
                 n - bad, n);
    return bad == 0 ? 0 : 1;
}
