// ══════════════════════════════════════════════════════════════════════════════════════════════
//  dly_spread_state_au.cpp — fb601: THE DELAY-SPREAD MIGRATION, THROUGH THE REAL
//                                    setStateInformation, ON THE INSTALLED PLUGIN.
//
//    clang++ -std=c++17 -O2 Tests/dly_spread_state_au.cpp -o /tmp/dly_spread_state_au \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/dly_spread_state_au
//    (run from plugins/Terrain — it READS Source/PluginProcessor.cpp for bar [0])
//
//  fb600 moved the Spread default 0.60 -> 0.05 AND added a migration that snaps an already-saved
//  0.60 down on load. Tests/ referenced neither `DLY_SPREAD` nor `dlySpreadRebased`. The default
//  is proved offline in Tests/dly_spread_cert.cpp; THIS is the other half — the half that touches
//  Max's already-saved projects, and the only half that can silently eat a deliberate setting.
//
//  WHY THE INSTALLED AU AND NOT AN OFFLINE HARNESS: the migration lives inside a 600-line
//  setStateInformation. Transcribing its predicate into a cert proves the ARITHMETIC and nothing
//  about whether the branch is reached, whether the marker gates it, or whether the pooled
//  instances are in the id list. That is the fb373/fb469 failure class — green measurements on a
//  path the plugin never takes. So this drives the real thing:
//    kAudioUnitProperty_ClassInfo -> the dict's "jucePluginState" CFData IS the getStateInformation
//    blob (JUCE copyXmlToBinary: 4-byte magic 'VC2!' + uint32 length + the XML + a NUL; NOT
//    compressed in JUCE 7/8, verified on this build). Edit that XML, push it back, read the
//    parameter out with AudioUnitGetParameter. That is a real project reload.
//
//  THE BARS
//   0  THE MIGRATION'S OWN CONSTANTS, READ OUT OF PluginProcessor.cpp — printed, never assumed.
//      Retune the 1e-4f window or the 0.05f destination and this line MOVES.
//   1  A FRESH SAVE CARRIES THE MARKER — dlySpreadRebased="1" is in the root the plugin writes.
//      Without it every reload would re-run the migration forever.
//   2  A PRE-fb600 PROJECT IS MIGRATED — marker removed, SYN_DLY_SPREAD stored 0.60 → reads 0.05.
//   3  THE POOLED INSTANCES MIGRATE TOO — SYN_DLY2..N_SPREAD, the ids the loop builds.
//   4  🚨 IT IS NOT RE-MIGRATED — the state that comes back OUT carries the marker, so a
//      DELIBERATE post-fb600 0.600 survives the next load. This is the bar that says the fix
//      cannot eat a setting Max chose on purpose.
//   5  🚨 THE KNIFE EDGE — 0.59900 KEPT · 0.60000 SNAPPED · 0.60005 SNAPPED · 0.60100 KEPT,
//      every one of them through the real branch.
//   6  NOTHING ELSE MOVED — Delay Feedback parked at a distinctive value in the same synthetic
//      blob comes back untouched. The migration is a scalpel, not a sweep.
//
//  MUTATION CONTROL (fb421): DLY_MIG_MUT=1 in the environment makes the pre-fb600 blob KEEP the
//  marker. The migration must then NOT fire, and bars [2] AND [3] must go red — which is the proof
//  that they are reading the migration and not the registered default.
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb601 — THE ClassInfo PLUMBING NOW LIVES IN Tests/au_state_blob.h. It was lifted out of this
//  file verbatim when Tests/harm_tbl_state_au.cpp turned out to need the identical 200 lines;
//  RECYCLE says reuse it, not fork it. Everything specific to the Spread migration is still here.
#include "au_state_blob.h"

// ── the migration's own constants, read out of the processor (never hardcoded here) ────────────
struct Mig { bool found = false; double from = 0, tol = 0, to = 0; std::string line; };
static Mig migrationConstants (const char* path)
{
    Mig m; std::ifstream f (path); if (! f) return m;
    std::stringstream ss; ss << f.rdbuf(); const std::string src = ss.str();
    // if (std::abs (v - 0.60f) < 1e-4f) ch.setProperty ("value", 0.05f, nullptr);
    const size_t anchor = src.find ("dlySpreadRebased\"))");
    if (anchor == std::string::npos) return m;
    const size_t at = src.find ("std::abs (v - ", anchor);
    if (at == std::string::npos) return m;
    const size_t eol = src.find ('\n', at);
    m.line = src.substr (src.rfind ('\n', at) + 1, eol - src.rfind ('\n', at) - 1);
    const size_t s0 = m.line.find ("v - ") + 4;
    m.from = std::atof (m.line.c_str() + s0);
    const size_t s1 = m.line.find ("< ", s0) + 2;
    m.tol  = std::atof (m.line.c_str() + s1);
    const size_t s2 = m.line.find ("\"value\", ") + 9;
    m.to   = std::atof (m.line.c_str() + s2);
    m.found = (m.from > 0 && m.tol > 0 && m.to >= 0);
    while (! m.line.empty() && (m.line[0] == ' ' || m.line[0] == '\t')) m.line.erase (0, 1);
    return m;
}

int main (int argc, char** argv)
{
    const char* proc = (argc > 1) ? argv[1] : "Source/PluginProcessor.cpp";
    const bool  MUT  = (std::getenv ("DLY_MIG_MUT") != nullptr);

    std::printf ("\n══ dly_spread_state_au — fb601 ══  the installed AU, real setStateInformation\n\n");
    if (MUT) std::printf ("  ⚠️  MUTATION ACTIVE: DLY_MIG_MUT set — bar [2] keeps the marker and is EXPECTED to go red.\n\n");

    AU a;
    if (! a.open()) { std::printf ("\n  ❌ could not open the AU — nothing asserted\n\n"); return 1; }

    // ── bar 0 ─────────────────────────────────────────────────────────────────────────────────
    const Mig mig = migrationConstants (proc);
    { char d[640];
      if (mig.found) std::snprintf (d, sizeof d, "from PluginProcessor.cpp — snap |v - %.5f| < %g  ->  %.5f\n        %s",
                                    mig.from, mig.tol, mig.to, mig.line.c_str());
      else           std::snprintf (d, sizeof d, "COULD NOT READ the migration out of %s — run from plugins/Terrain", proc);
      chk (mig.found, "[0] THE MIGRATION'S OWN CONSTANTS — read out of the processor, not assumed", d); }
    if (! mig.found) { a.close(); std::printf ("\n  ❌ refusing to assert against guessed constants\n\n"); return 1; }

    const std::string SPREAD_ID = "SYN_DLY_SPREAD";
    const std::string SPREAD_NM = "Delay Spread";
    if (! a.has (SPREAD_NM)) { std::printf ("  !! no '%s' parameter on this AU\n", SPREAD_NM.c_str()); a.close(); return 1; }

    // the pooled instance ids the migration's loop builds (SYN_DLY2_SPREAD .. up to kFxInstances)
    std::vector<std::string> pool;
    { std::string why; const std::string x0 = a.readXml (why);
      for (int n = 2; n <= 12; ++n)
      { const std::string id = "SYN_DLY" + std::to_string (n) + "_SPREAD";
        if (x0.find ("<PARAM id=\"" + id + "\"") != std::string::npos) pool.push_back (id); } }

    // ── bar 1 — a fresh save carries the marker ───────────────────────────────────────────────
    std::string why, base = a.readXml (why);
    if (base.empty()) { std::printf ("  !! could not read the state blob: %s\n", why.c_str()); a.close(); return 1; }
    { char d[420]; std::snprintf (d, sizeof d,
        "blob %zu bytes of XML, magic 'VC2!'; root carries dlySpreadRebased=%s, and the pool this build "
        "exposes is %zu more Spread ids (%s ...)",
        base.size(), hasProperty (base, "dlySpreadRebased") ? "YES" : "NO", pool.size(),
        pool.empty() ? "-" : pool.front().c_str());
      chk (hasProperty (base, "dlySpreadRebased"), "[1] A FRESH SAVE CARRIES THE MARKER — without it every reload re-migrates", d); }

    // ── the pre-fb600 blob factory ────────────────────────────────────────────────────────────
    auto preFb600 = [&] (double spread, bool keepMarker) -> std::string
    {
        std::string x = base;
        if (! keepMarker) dropProperty (x, "dlySpreadRebased");
        setParam (x, SPREAD_ID, spread);
        for (const auto& id : pool) setParam (x, id, spread);
        return x;
    };

    // ── bar 2 — a pre-fb600 project is migrated ───────────────────────────────────────────────
    {
        const std::string x = preFb600 (0.60, MUT);       // MUT keeps the marker: the branch must NOT fire
        const bool ok = a.writeXml (x);
        const float got = a.get (SPREAD_NM);
        char d[512]; std::snprintf (d, sizeof d,
            "stored %.5f with the marker %s  ->  '%s' reads %.6f (want %.5f). push ok=%d",
            0.60, MUT ? "KEPT (mutation)" : "REMOVED", SPREAD_NM.c_str(), got, mig.to, (int) ok);
        chk (ok && std::fabs (got - mig.to) < 1e-5f,
             "[2] A PRE-fb600 PROJECT IS MIGRATED — 0.600 in the blob, 0.050 on the parameter", d);
    }

    // ── bar 3 — the pooled instances too ──────────────────────────────────────────────────────
    {
        std::string bad; int seen = 0, good = 0;
        for (const auto& id : pool)
        {
            const int n = std::atoi (id.c_str() + 7);
            const std::string nm = "Delay " + std::to_string (n) + " Spread";
            if (! a.has (nm)) continue;
            ++seen;
            const float got = a.get (nm);
            if (std::fabs (got - mig.to) < 1e-5f) ++good;
            else bad += nm + "=" + std::to_string (got) + " ";
        }
        char d[512]; std::snprintf (d, sizeof d, "%d/%d pooled Spread parameters snapped to %.5f%s%s",
                                    good, seen, mig.to, bad.empty() ? "" : "  — missed: ", bad.c_str());
        chk (seen > 0 && good == seen, "[3] THE POOLED INSTANCES MIGRATE TOO — not just instance 1", d);
    }

    // ── bar 4 — not re-migrated ───────────────────────────────────────────────────────────────
    {
        std::string out = a.readXml (why);                       // the blob the plugin just WROTE
        const bool markerBack = hasProperty (out, "dlySpreadRebased");
        setParam (out, SPREAD_ID, 0.60);                         // a DELIBERATE post-fb600 0.600
        const bool ok = a.writeXml (out);
        const float got = a.get (SPREAD_NM);
        char d[512]; std::snprintf (d, sizeof d,
            "the saved blob carries the marker=%s; a deliberate 0.600 pushed through it reads back %.6f "
            "(0.600 = kept, %.3f = eaten)", markerBack ? "YES" : "NO", got, mig.to);
        chk (ok && markerBack && std::fabs (got - 0.60f) < 1e-4f,
             "[4] 🚨 IT IS NOT RE-MIGRATED — a Spread Max parked at 0.600 on purpose survives the reload", d);
    }

    // ── bar 5 — the knife edge ────────────────────────────────────────────────────────────────
    {
        struct Case { double in; bool snap; const char* what; };
        const Case cases[] = {
            { 0.59900, false, "0.59900 — 10x outside the window" },
            { 0.60000, true,  "0.60000 — the exact old default"  },
            { 0.60005, true,  "0.60005 — just inside 1e-4"       },
            { 0.60100, false, "0.60100 — 10x outside the window" } };
        std::string detail; int good = 0;
        for (const auto& c : cases)
        {
            a.writeXml (preFb600 (c.in, false));
            const float got  = a.get (SPREAD_NM);
            const bool snapped = std::fabs (got - mig.to) < 1e-5f;
            const bool kept    = std::fabs (got - (float) c.in) < 2e-5f;
            const bool ok      = c.snap ? snapped : kept;
            if (ok) ++good;
            char line[256]; std::snprintf (line, sizeof line, "\n          %-34s -> %.6f  %-7s %s",
                                           c.what, got, c.snap ? "SNAP" : "KEEP", ok ? "ok" : "❌");
            detail += line;
        }
        chk (good == 4, "[5] 🚨 THE KNIFE EDGE — only |v-0.600| < 1e-4 is touched", detail);
    }

    // ── bar 6 — nothing else moved ────────────────────────────────────────────────────────────
    //  ⚠️ CHECKED IN THE BLOB, NOT ON THE AU PARAMETER. The AU exposes every float param on a
    //  0..1 scale, and only Spread's own range happens to BE 0..1 — Delay Feedback stored 0.600
    //  reads back 0.409091 on the AU scale, which is a units mismatch and not a migration. The
    //  round trip out of getStateInformation is the range-independent question: did the number in
    //  the blob survive?
    {
        std::string x = preFb600 (0.60, false);
        const double park = 0.60;                          // the SAME number, on a DIFFERENT param
        const bool wrote = setParam (x, "SYN_DLY_FEEDBACK", park);
        a.writeXml (x);
        const std::string back = a.readXml (why);
        const double nb = getParam (back, "SYN_DLY_FEEDBACK"), sp = getParam (back, SPREAD_ID);
        char d[640]; std::snprintf (d, sizeof d,
            "SYN_DLY_FEEDBACK parked at %.4f in the SAME pre-fb600 blob (written=%d) -> saved back as %.6f "
            "(untouched), while SYN_DLY_SPREAD went %.4f -> %.6f in the same load. The migration is scoped "
            "to the Spread id list, not to the value 0.60.",
            park, (int) wrote, nb, 0.60, sp);
        chk (wrote && std::fabs (nb - park) < 2e-5 && std::fabs (sp - mig.to) < 1e-5,
             "[6] NOTHING ELSE MOVED — 0.600 on a neighbouring delay param is NOT snapped", d);
    }

    a.close();
    std::printf ("\n  %s %d passed, %d failed\n\n", fail == 0 ? "✅ OK" : "❌ FAILED", pass, fail);
    return fail == 0 ? 0 : 1;
}
