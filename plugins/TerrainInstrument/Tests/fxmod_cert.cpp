// fxmod_cert — the FX rack's modulation destinations, gated before anything reads them.
#include <cstdio>
#include <set>
#include <map>
#include <string>
#include "SynthModConfig.h"
#include "FxModValue.h"      // fb453 — the SHIPPED per-block math (JUCE-free on purpose)
#include "fx_mod_ids.inc"
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

    std::printf ("\n[D. the generated parameter map]\n");
    // fx_mod_ids.inc is GENERATED from index.html's DEV_TEMPLATES (Tools/gen_fx_mod_ids.py), so the
    // dial and its destination are authored ONCE. These gates are the shape contract that generator
    // must keep producing: hand-edit the .inc, or point a dial somewhere new in the UI, and one of
    // them goes red.
    int live = 0, holes = 0; bool wellFormed = true;
    std::set<std::string> ids;
    for (int k = 0; k < kFxModKinds; ++k)
      for (int n = 0; n < kFxModKnobs; ++n)
      { if (kFxModLeaf[k][n] == nullptr) { ++holes; continue; }
        ++live;
        const std::string id = std::string (kFxModTag[k]) + "_" + kFxModLeaf[k][n];
        ids.insert (id);
        if (id.rfind ("SYN_", 0) != 0) wellFormed = false; }

    gate ("184 live (kind, knob) cells — 15 kinds x 12 + the Filter's 4", live == 184,
          std::to_string (live) + " live");
    gate ("the table's own count agrees with the table", live == kFxModLive);
    gate ("every id is a SYN_ parameter id", wellFormed);
    // ⚠️ NOT "no parameter is claimed by two dials" — ONE pair deliberately is. The Delay's front
    // "Time" dial and its back "Time L" knob ARE SYN_DLY_TIME (fb306-310, the L/R link), so the
    // gate is an EXACT distinct count: a NEW collision — a dial pointed at the wrong parameter —
    // drops it to 182 and fails, while the known alias stays green.
    gate ("183 distinct parameters — exactly one deliberate alias, no new collision",
          (int) ids.size() == 183 && kFxModDistinct == 183,
          std::to_string (ids.size()) + " distinct");
    gate ("that alias is exactly the Delay's Time / Time L",
          std::string (kFxModTag[1]) + "_" + kFxModLeaf[1][0] == "SYN_DLY_TIME"
       && std::string (kFxModTag[1]) + "_" + kFxModLeaf[1][10] == "SYN_DLY_TIME");
    gate ("the Filter's back panel is 8 holes, and it is the ONLY device with holes",
          holes == 8 && kFxModLeaf[5][4] == nullptr && kFxModLeaf[0][11] != nullptr
       && kFxModLeaf[15][11] != nullptr, std::to_string (holes) + " holes");
    gate ("the tag is the instance-suffix point: SYN_RVB + 3 + _ + SIZE",
          std::string (kFxModTag[0]) == "SYN_RVB" && std::string (kFxModLeaf[0][0]) == "SIZE");
    gate ("kind order is the C++ order, not the template order (5 = the Filter, 14 = Utility)",
          std::string (kFxModTag[5]) == "SYN_FLT" && std::string (kFxModTag[14]) == "SYN_UTL");

    // ═══ E. the per-block VALUE MATH — the SHIPPED wc::buildFxMod(), not a copy of it ═══════════
    // Task 3 adds no audible behaviour (nothing calls M() until Task 4), so the logic is gated
    // directly. The math lives in Source/FxModValue.h precisely so this harness can drive the
    // function the plugin runs: a second hand-typed ownership crossfade here would be exactly the
    // fb393 trap — a harness kinder than reality. The only thing the cert substitutes is what the
    // plugin's four callbacks reach for (a std::map of floats instead of the APVTS, plain floats
    // instead of the LFO bank and the mono envelopes).
    std::printf ("\n[E. the per-block modulation value]\n");
    {
        struct FakeRack
        {
            // ONE cell per parameter ID — exactly the APVTS's contract, and the reason the alias
            // needs no special case: two dials that name the same id get the same pointer here,
            // just as getRawParameterValue() hands out the same pointer twice in the plugin.
            // std::map guarantees pointer stability across inserts, so one pass is enough.
            std::map<std::string, float> param;
            float* ref[16][6][12] {};
            float  lfo[NUM_LFOS] {};
            float  env[(int) ModSource::NumSources] {};
            int    cells = 0;

            FakeRack()
            {
                for (int k = 0; k < kFxModKinds; ++k)
                  for (int i = 0; i < kFxModInsts; ++i)
                    for (int n = 0; n < kFxModKnobs; ++n)
                    { ref[k][i][n] = nullptr;
                      if (kFxModLeaf[k][n] == nullptr) continue;
                      const std::string id = std::string (kFxModTag[k])
                                           + (i == 0 ? std::string() : std::to_string (i + 1))
                                           + "_" + kFxModLeaf[k][n];
                      ref[k][i][n] = &param[id]; ++cells; }
                // An IDLE envelope: monoEnvLevelOf() returns level−1, so level 0 reads −1.0f.
                for (auto& e : env) e = -1.0f;
            }
            float& at (int k, int i, int n) { return *ref[k][i][n]; }
        };
        FakeRack R;

        auto build = [&] (FxModAccum& acc, const ModConfig& cfg)
        {
            buildFxMod (acc, cfg,
                [&] (int k, int i, int n) -> const void* { return R.ref[k][i][n]; },
                []  (const void* p) { return *static_cast<const float*> (p); },
                [&] (int si)  { return R.lfo[si]; },
                [&] (int src) { return R.env[src]; });
        };
        auto route = [] (ModConfig& c, ModSource s, int dest, float depth)
        { auto& a = c.assignments[c.numAssignments++];
          a.source = s; a.dest = (ModDest) dest; a.depth = depth; a.enabled = true; };
        auto near_ = [] (float a, float b) { return std::fabs (a - b) < 1e-5f; };

        gate ("the map resolves 1,104 cells (184 x 6) over 1,098 parameters (183 x 6)",
              R.cells == kFxModLive * kFxModInsts && (int) R.param.size() == kFxModDistinct * kFxModInsts,
              std::to_string (R.cells) + " cells / " + std::to_string (R.param.size()) + " params");

        // ── an LFO route ADDS ───────────────────────────────────────────────────────────────
        { ModConfig c; FxModAccum acc; R.at (0, 0, 0) = 0.4f; R.lfo[0] = 1.0f;
          route (c, ModSource::L1, fxModDest (0, 0, 0), 0.5f);
          build (acc, c);
          gate ("an LFO route ADDS: base 0.4 + (LFO +1 x depth 0.5) = 0.9",
                acc.count == 1 && near_ (acc.val[0], 0.9f),
                std::to_string (acc.val[0])); }

        // ── and it clamps, ONCE, at both ends ───────────────────────────────────────────────
        { ModConfig c; FxModAccum acc; R.at (0, 0, 0) = 0.9f; R.lfo[0] = 1.0f;
          route (c, ModSource::L1, fxModDest (0, 0, 0), 0.5f);
          build (acc, c);
          const float hi = acc.val[0];
          ModConfig c2; FxModAccum acc2; R.at (0, 0, 0) = 0.1f; R.lfo[0] = -1.0f;
          route (c2, ModSource::L1, fxModDest (0, 0, 0), 0.5f);
          build (acc2, c2);
          gate ("it clamps to [0,1] — 1.4 reads 1.0, -0.4 reads 0.0",
                near_ (hi, 1.0f) && near_ (acc2.val[0], 0.0f),
                std::to_string (hi) + " / " + std::to_string (acc2.val[0])); }

        // ── two LFO routes on ONE destination SUM ───────────────────────────────────────────
        { ModConfig c; FxModAccum acc; R.at (0, 0, 0) = 0.2f; R.lfo[0] = 1.0f; R.lfo[1] = 1.0f;
          route (c, ModSource::L1, fxModDest (0, 0, 0), 0.3f);
          route (c, ModSource::L2, fxModDest (0, 0, 0), 0.25f);
          build (acc, c);
          gate ("two LFO routes on one destination SUM into ONE slot: 0.2 + 0.3 + 0.25 = 0.75",
                acc.count == 1 && near_ (acc.val[0], 0.75f),
                std::to_string (acc.count) + " slot(s), " + std::to_string (acc.val[0])); }

        // ── THE ALIAS: two DESTINATIONS, one PARAMETER, one summed slot ─────────────────────
        // SYN_DLY_TIME is genuinely claimed by two dials (the Delay's front "Time" and its back
        // "Time L", fb306-310). Because the map is keyed by POINTER, routes on either land in the
        // same slot and accumulate — the correct behaviour, and it needs NO special case. This is
        // the gate that proves the pointer-keying, not a comment claiming it.
        { ModConfig c; FxModAccum acc; R.at (1, 2, 0) = 0.2f; R.lfo[0] = 1.0f;
          route (c, ModSource::L1, fxModDest (1, 2, 0),  0.3f);   // front "Time"
          route (c, ModSource::L1, fxModDest (1, 2, 10), 0.25f);  // back  "Time L" — SAME param
          build (acc, c);
          gate ("the alias: dest(1,i,0) and dest(1,i,10) are ONE slot whose value is the SUM",
                acc.count == 1 && near_ (acc.val[0], 0.75f)
             && R.ref[1][2][0] == R.ref[1][2][10],
                std::to_string (acc.count) + " slot(s), " + std::to_string (acc.val[0])); }
        // ...and two dials that are NOT aliases stay two slots, so the gate above measures the
        // alias rather than a map that collapses everything.
        { ModConfig c; FxModAccum acc; R.at (1, 2, 0) = 0.2f; R.at (1, 2, 11) = 0.2f; R.lfo[0] = 1.0f;
          route (c, ModSource::L1, fxModDest (1, 2, 0),  0.3f);
          route (c, ModSource::L1, fxModDest (1, 2, 11), 0.25f);  // TIME_R — a different parameter
          build (acc, c);
          gate ("Time and Time R are NOT aliases — two parameters, two slots",
                acc.count == 2 && near_ (acc.val[0], 0.5f) && near_ (acc.val[1], 0.45f),
                std::to_string (acc.count) + " slot(s)"); }

        // ── an ENV route OWNS: it drives from ZERO, not from the base (fb179/fb184) ─────────
        { ModConfig c; FxModAccum acc; R.at (0, 0, 1) = 0.7f;
          R.env[(int) ModSource::EnvAmp] = -1.0f;                 // envelope LEVEL 0
          route (c, ModSource::EnvAmp, fxModDest (0, 0, 1), 1.0f);
          build (acc, c);
          gate ("an ENV route OWNS: at level 0 the knob reads 0, NOT its base 0.7",
                acc.count == 1 && near_ (acc.val[0], 0.0f), std::to_string (acc.val[0])); }
        { ModConfig c; FxModAccum acc; R.at (0, 0, 1) = 0.7f;
          R.env[(int) ModSource::EnvAmp] = 0.0f;                  // envelope LEVEL 1
          route (c, ModSource::EnvAmp, fxModDest (0, 0, 1), 1.0f);
          build (acc, c);
          gate ("at env level 1, depth 1, the knob reads 1.0 — the (env + 1) term, no 0.5f",
                near_ (acc.val[0], 1.0f), std::to_string (acc.val[0])); }

        // ── depth 0.5 on an ENV = the ownership CROSSFADE, flowKnob's (base+m)(1-w) + oV ────
        { ModConfig c; FxModAccum acc; R.at (0, 0, 1) = 0.4f;
          R.env[(int) ModSource::EnvAmp] = 0.0f;                  // level 1
          route (c, ModSource::EnvAmp, fxModDest (0, 0, 1), 0.5f);
          build (acc, c);
          const float top = acc.val[0];
          ModConfig c2; FxModAccum acc2; R.env[(int) ModSource::EnvAmp] = -1.0f;   // level 0
          route (c2, ModSource::EnvAmp, fxModDest (0, 0, 1), 0.5f);
          build (acc2, c2);
          gate ("depth 0.5 crossfades: base 0.4 -> 0.4*0.5 + 0.5 = 0.7 at the top, 0.2 at rest",
                near_ (top, 0.7f) && near_ (acc2.val[0], 0.2f),
                std::to_string (top) + " / " + std::to_string (acc2.val[0])); }

        // ── an LFO and an ENV on the same knob: the LFO's sum is crossfaded, the ENV is not ──
        { ModConfig c; FxModAccum acc; R.at (0, 0, 1) = 0.4f; R.lfo[0] = 1.0f;
          R.env[(int) ModSource::EnvAmp] = 0.0f;                  // level 1
          route (c, ModSource::L1,     fxModDest (0, 0, 1), 0.2f);
          route (c, ModSource::EnvAmp, fxModDest (0, 0, 1), 0.5f);
          build (acc, c);
          gate ("LFO + ENV on one knob: (0.4 + 0.2) * (1 - 0.5) + 0.5 = 0.8",
                acc.count == 1 && near_ (acc.val[0], 0.8f), std::to_string (acc.val[0])); }

        // ── a NULL leaf resolves to no pointer and is SKIPPED ───────────────────────────────
        { ModConfig c; FxModAccum acc;
          route (c, ModSource::L1, fxModDest (5, 0, 4), 1.0f);    // the Filter's back panel: a hole
          R.lfo[0] = 1.0f;
          build (acc, c);
          gate ("a hole (the Filter's back panel) resolves to no pointer and opens NO slot",
                R.ref[5][0][4] == nullptr && acc.count == 0,
                std::to_string (acc.count) + " slot(s)"); }

        // ── the read-site lookup: an unrouted parameter reads its own value ─────────────────
        { ModConfig c; FxModAccum acc; R.at (0, 0, 0) = 0.4f; R.at (0, 0, 2) = 0.55f; R.lfo[0] = 1.0f;
          route (c, ModSource::L1, fxModDest (0, 0, 0), 0.5f);
          build (acc, c);
          gate ("lookup(): a routed parameter reads its modulated value, an unrouted one its own",
                near_ (acc.lookup (R.ref[0][0][0], R.at (0, 0, 0)), 0.9f)
             && near_ (acc.lookup (R.ref[0][0][2], R.at (0, 0, 2)), 0.55f)); }

        // ── a disabled route, and a source with no global value, both no-op ─────────────────
        { ModConfig c; FxModAccum acc; R.at (0, 0, 0) = 0.4f; R.lfo[0] = 1.0f;
          route (c, ModSource::L1, fxModDest (0, 0, 0), 0.5f);
          c.assignments[0].enabled = false;
          build (acc, c);
          gate ("a disabled route opens no slot", acc.count == 0); }
        { ModConfig c; FxModAccum acc; R.at (0, 0, 0) = 0.4f;
          route (c, ModSource::Velocity, fxModDest (0, 0, 0), 0.5f);   // no block-rate value
          build (acc, c);
          gate ("a source with no global value leaves the knob at its base — flowMod's own rule",
                acc.count == 1 && near_ (acc.val[0], 0.4f), std::to_string (acc.val[0])); }

        // ── a non-FX destination is not this map's business ─────────────────────────────────
        { ModConfig c; FxModAccum acc; R.lfo[0] = 1.0f;
          route (c, ModSource::L1, (int) ModDest::Cut1, 1.0f);
          build (acc, c);
          gate ("a legacy destination never enters the rack's map", acc.count == 0); }
    }

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n", pass, fail);
    return fail ? 1 : 0;
}
