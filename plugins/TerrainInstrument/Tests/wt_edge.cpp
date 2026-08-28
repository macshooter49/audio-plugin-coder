#include "WavetableBank.h"
#include "wtnames.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
// THE ONE NUMBER: when a table's content stops dead (next harmonic exactly 0),
// how LOUD was the last one? An amplitude law never lands on a loud value then jumps to silence.
int main()
{
    printf ("%-4s %-16s %6s %6s %11s  %s\n","idx","table","frame","Ntop","last h dBc","");
    printf ("%s\n", std::string(64,'-').c_str());
    for (int i = 0; i < 30; ++i)     // legacy 30 only
    {
        const auto spec = tw::WavetableBank::specForPreset ((tw::WavetableBank::Preset) i);
        double worstDbc = -999.0; int worstFr=-1, worstNtop=0;
        for (int fr = 0; fr < tw::WavetableSpec::kNumFrames; ++fr)
        {
            const auto& f = spec.frames[(size_t)fr];
            if (f.numPartials > 0) continue;                    // partial lists aren't a harmonic loop
            std::vector<double> a (tw::FrameSpec::kMaxHarmonics + 2, 0.0);
            double peak = 0.0;
            for (int h=1;h<=f.numHarmonics&&h<=tw::FrameSpec::kMaxHarmonics;++h)
            { a[(size_t)h]=std::abs((double)f.amplitudes[(size_t)(h-1)]); peak=std::max(peak,a[(size_t)h]); }
            if (peak<=0.0) continue;
            int ntop=0; for (int h=1;h<=tw::FrameSpec::kMaxHarmonics;++h) if(a[(size_t)h]>0.0) ntop=h;
            if (ntop<=1 || ntop>=1023) continue;                // a sine, or already at the ceiling
            if (a[(size_t)(ntop+1)] != 0.0) continue;           // didn't stop dead
            const double dbc = 20.0*std::log10(a[(size_t)ntop]/peak);
            if (dbc > worstDbc) { worstDbc=dbc; worstFr=fr; worstNtop=ntop; }
        }
        if (worstFr < 0) { printf("%-4d %-16s      —  (no hard stop)\n", i, kWtNames[i]); continue; }
        printf ("%-4d %-16s %6d %6d %10.1f  %s\n", i, kWtNames[i], worstFr, worstNtop, worstDbc,
                worstDbc > -40.0 ? "🔴 loud content deleted" : "");
    }
    return 0;
}
