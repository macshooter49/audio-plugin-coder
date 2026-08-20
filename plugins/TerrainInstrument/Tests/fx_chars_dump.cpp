// fb438 — dump every rack engine's per-Type CHARACTER names as JSON. The card's Character dropdown
// must show THE TYPE'S names (the engines relabel per Type), and a name that lives only in C++ is
// a name the card cannot print — so index.html carries a mirrored literal (FX-CHARS markers) and
// the UI gate re-runs this dump and diffs it against the page. One source, one gate.
//   clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/fx_chars_dump.cpp -o /tmp/fx_chars_dump && /tmp/fx_chars_dump
#include <cstdio>
#include "TerrainChorusFx.h"
#include "TerrainFlangerFx.h"
#include "TerrainPhaserFx.h"
#include "TerrainEqualizerFx.h"
#include "TerrainWidenFx.h"
#include "TerrainCompressFx.h"
#include "TerrainOttFx.h"
template <class E> static void dump (const char* core, int nTypes, int nChars, bool last)
{
    std::printf ("\"%s\":[", core);
    for (int t = 0; t < nTypes; ++t)
    {
        std::printf ("%s[", t ? "," : "");
        const char* const* n = E::charNames (t);
        for (int c = 0; c < nChars; ++c) std::printf ("%s\"%s\"", c ? "," : "", n[c]);
        std::printf ("]");
    }
    std::printf ("]%s", last ? "" : ",");
}
int main()
{
    std::printf ("{");
    dump<tw::TerrainChorusFx>    ("cho", tw::TerrainChorusFx::kNumTypes,    tw::TerrainChorusFx::kNumChars,    false);
    dump<tw::TerrainFlangerFx>   ("fla", tw::TerrainFlangerFx::kNumTypes,   tw::TerrainFlangerFx::kNumChars,   false);
    dump<tw::TerrainPhaserFx>    ("pha", tw::TerrainPhaserFx::kNumTypes,    tw::TerrainPhaserFx::kNumChars,    false);
    dump<tw::TerrainEqualizerFx> ("eqz", tw::TerrainEqualizerFx::kNumTypes, tw::TerrainEqualizerFx::kNumChars, false);
    dump<tw::TerrainWidenFx>     ("wid", tw::TerrainWidenFx::kNumTypes,     tw::TerrainWidenFx::kNumChars,     false);
    dump<tw::TerrainCompressFx>  ("cmp", tw::TerrainCompressFx::kNumTypes,  tw::TerrainCompressFx::kNumChars,  false);
    dump<tw::TerrainOttFx>       ("ott", tw::TerrainOttFx::kNumTypes,       tw::TerrainOttFx::kNumChars,       true);
    std::printf ("}\n");
    return 0;
}
