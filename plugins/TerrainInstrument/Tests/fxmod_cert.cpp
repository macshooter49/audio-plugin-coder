// fxmod_cert — the FX rack's modulation destinations, gated before anything reads them.
#include <cstdio>
#include <set>
#include "SynthModConfig.h"
static int pass = 0, fail = 0;
static void gate (const char* what, bool ok, const std::string& d = {})
{ (ok ? pass : fail)++; std::printf ("  %-5s %s%s%s\n", ok ? "ok" : "FAIL", what,
    d.empty() ? "" : "   ", d.c_str()); }

int main()
{
    using namespace wc;
    std::printf ("\n[A. the destination block]\n");
    gate ("FxModBase is 694 — the NumDests the rack was appended after",
          (int) ModDest::FxModBase == 694,
          "FxModBase=" + std::to_string ((int) ModDest::FxModBase));
    gate ("NumDests is 1846 (694 + 16*6*12)", (int) ModDest::NumDests == 1846,
          "NumDests=" + std::to_string ((int) ModDest::NumDests));
    gate ("the first FX dest is FxModBase", fxModDest (0, 0, 0) == (int) ModDest::FxModBase);
    gate ("the last FX dest is NumDests-1", fxModDest (15, 5, 11) == (int) ModDest::NumDests - 1);

    std::printf ("\n[B. every one of the 1,152 round-trips and is unique]\n");
    std::set<int> seen; bool rt = true, uniq = true, inRange = true;
    for (int k = 0; k < kFxModKinds; ++k)
      for (int i = 0; i < kFxModInsts; ++i)
        for (int n = 0; n < kFxModKnobs; ++n)
        { const int d = fxModDest (k, i, n);
          if (! seen.insert (d).second) uniq = false;
          if (! isFxModDest (d)) inRange = false;
          const auto a = fxModDecode (d);
          if (a.kind != k || a.inst != i || a.knob != n) rt = false; }
    gate ("1,152 destinations, all unique", uniq && (int) seen.size() == 1152,
          std::to_string (seen.size()) + " distinct");
    gate ("every one decodes back to its own (kind, instance, knob)", rt);
    gate ("every one answers isFxModDest()", inRange);
    gate ("no legacy destination is inside the FX range", ! isFxModDest ((int) ModDest::DstMorph));

    std::printf ("\n[C. the DestInfo rows exist — the zero-fill trap]\n");
    // kDestInfo is sized by NumDests. Growing the enum WITHOUT growing the table zero-fills the new
    // rows (fullScale 0.0f) and every FX route silently does nothing. This is that gate.
    bool allLinear = true; float worst = 1.0f;
    for (int d = (int) ModDest::FxModBase; d < (int) ModDest::NumDests; ++d)
    { const auto& in = kDestInfo[d];
      if (in.domain != ModDomain::Linear01 || in.fullScale != 1.0f) allLinear = false;
      worst = std::min (worst, in.fullScale); }
    gate ("every FX dest is {Linear01, 1.0} — not a zero-filled hole", allLinear,
          "worst fullScale " + std::to_string (worst));
    gate ("the legacy rows are untouched (Cut1 is still Semitone 48)",
          kDestInfo[(int) ModDest::Cut1].domain == ModDomain::Semitone
          && kDestInfo[(int) ModDest::Cut1].fullScale == 48.0f);
    gate ("routeContribution on an FX dest: source +1 at depth 1 == +1.0",
          std::fabs (routeContribution (kDestInfo[(int) ModDest::FxModBase], 1.0f, 1.0f) - 1.0f) < 1e-6f);

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n", pass, fail);
    return fail ? 1 : 0;
}
