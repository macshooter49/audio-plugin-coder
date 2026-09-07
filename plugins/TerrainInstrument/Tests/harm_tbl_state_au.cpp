// ══════════════════════════════════════════════════════════════════════════════════════════════
//  harm_tbl_state_au.cpp — fb601: THE HARM_TABLE MIGRATION, THROUGH THE REAL
//                                  setStateInformation, ON THE INSTALLED PLUGIN.
//
//    clang++ -std=c++17 -O2 Tests/harm_tbl_state_au.cpp -o /tmp/harm_tbl_state_au \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/harm_tbl_state_au
//    (run from plugins/TerrainInstrument — it READS Source/PluginProcessor.cpp for bar [0])
//
//  fb601 gave the Harmonics engine its OWN table parameter. The additive bank read
//  SYN_OSC_x_WT_PRESET until now; from fb601 it reads SYN_OSC_x_HARM_TABLE, whose registered
//  default is Prophet Saw (4), not Sine (0). A blob written before fb601 has no PARAM child for it
//  at all — so without a migration every already-saved Harmonic patch would come back on a
//  DIFFERENT TABLE. That is the half of fb601 that touches every project Max has already saved,
//  and the only half that can silently change a sound he approved.
//
//  🚨 WHY THIS FILE EXISTS AT ALL. PluginProcessor.cpp:15149 says
//        "fb601-MIGRATION-BEGIN — the cert slices these lines verbatim; keep the markers"
//  and until now NOTHING sliced them: `grep -rn "HARM_TABLE\|harmTableSplit" Tests/` returned
//  nothing. A comment that promises a cert is not a cert; it is a promissory note that reads
//  green to the next session. Bar [0] cashes it.
//
//  Tests/harm_table_au.cpp proves the SOUND (that HARM really renders the table HARM_TABLE names).
//  Tests/harm_tables_cert.cpp proves the fb599 mainMode pin OFFLINE. THIS proves the STATE: what
//  an old project loads as.
//
//  THE ClassInfo PLUMBING IS Tests/au_state_blob.h — dly_spread_state_au.cpp's, lifted verbatim
//  and shared rather than copied (RECYCLE). See that header for how a blob round trip works.
//
//  THE BARS
//   0  THE SHIPPED MIGRATION'S OWN LINES, SLICED between its own markers and PRINTED. Never
//      transcribed: a transcription proves arithmetic on a branch the plugin might not even reach.
//   1  A FRESH SAVE CARRIES THE MARKER — harmTableSplit="1" and four HARM_TABLE children are in
//      the root the plugin writes. Without the marker every reload would re-migrate forever.
//   2  THE REGISTERED DEFAULT IS PROPHET SAW on all four — the whole reason a migration is needed.
//   3  🚨 A PRE-fb601 BLOB COMES BACK WITH HARM_TABLE == ITS OLD WT_PRESET, per oscillator. Four
//      DIFFERENT indices, so one shared value cannot fake it.
//   4  WT_PRESET ITSELF IS UNTOUCHED — the migration SEEDS from it, it does not move it. The
//      wavetable engine must sound exactly as it did.
//   5  THE SAVED STATE CARRIES THE MARKER AGAIN — what comes back OUT can never be re-migrated.
//   6  🚨 A DELIBERATE POST-fb601 CHOICE IS NEVER RE-SEEDED — including Sine (0), which is also
//      WT_PRESET's default and would be the obvious wrong thing to treat as a "pre-fb601 tell".
//
//  MUTATION CONTROL: HARM_MIG_MUT=1 in the environment makes the pre-fb601 blob KEEP the marker.
//  The migration must then NOT fire and bar [3] MUST go red. Bars [1] [2] [4] [5] [6] stay green,
//  which is what says the mutation broke the migration and not the harness. A migration cert that
//  cannot go red is worth nothing — before the mutation there is no evidence that bar [3] reads
//  the migration rather than a leftover parameter value.
//
//  🔒 AND THE LEFTOVER TRAP, CLOSED EXPLICITLY: this AU instance is reused across bars, so a
//  HARM_TABLE that simply KEPT its previous value would look identical to a migrated one. Before
//  every pre-fb601 push the four params are parked on a SENTINEL (1/3/5/9) through a marker-
//  carrying blob, and the sentinel readback is PRINTED. Bar [3] can only pass by MOVING off it.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "au_state_blob.h"

static const char* WTID[4]   = { "SYN_OSC_A_WT_PRESET","SYN_OSC_B_WT_PRESET","SYN_OSC_C_WT_PRESET","SYN_OSC_D_WT_PRESET" };
static const char* HMID[4]   = { "SYN_OSC_A_HARM_TABLE","SYN_OSC_B_HARM_TABLE","SYN_OSC_C_HARM_TABLE","SYN_OSC_D_HARM_TABLE" };
static const char* HMNAME[4] = { "Synth OSC A Harm Table","Synth OSC B Harm Table","Synth OSC C Harm Table","Synth OSC D Harm Table" };
static const char* WTNAME[4] = { "Synth OSC A WT Preset","Synth OSC B WT Preset","Synth OSC C WT Preset","Synth OSC D WT Preset" };

// four DIFFERENT tables a pre-fb601 patch might have been saved on — one shared number would let
// a single stuck parameter pass all four checks at once.
static const int WANT[4] = { 7, 16, 2, 31 };
// and four values that are none of the above, and none of the 4 = Prophet Saw default either
static const int SENT[4] = { 1, 3, 5, 9 };

int main (int argc, char** argv)
{
    const char* proc = (argc > 1) ? argv[1] : "Source/PluginProcessor.cpp";
    const bool  MUT  = (std::getenv ("HARM_MIG_MUT") != nullptr);

    std::printf ("\n══ harm_tbl_state_au — fb601 ══  HARM_TABLE migration, installed AU, real setStateInformation\n\n");
    if (MUT) std::printf ("  ⚠️  MUTATION ACTIVE: HARM_MIG_MUT set — the pre-fb601 blob KEEPS the marker, so the\n"
                          "      migration must NOT fire and bar [3] is EXPECTED to go red.\n\n");

    AU a;
    if (! a.open()) { std::printf ("\n  ❌ could not open the AU — nothing asserted\n\n"); return 1; }

    // ── [0] the shipped migration's own lines ─────────────────────────────────────────────────
    {
        const std::string block = sliceMarked (proc, "fb601-MIGRATION-BEGIN", "fb601-MIGRATION-END");
        std::string d;
        if (block.empty())
            d = "COULD NOT SLICE the fb601-MIGRATION markers out of " + std::string (proc)
              + " — either the markers were removed (the comment at PluginProcessor.cpp:15149 asks that they be"
                " kept for exactly this reason) or this was not run from plugins/TerrainInstrument";
        else
        {
            int lines = 1; for (char c : block) if (c == '\n') ++lines;
            d = "sliced " + std::to_string (lines) + " lines verbatim between fb601-MIGRATION-BEGIN/END:\n"
              + block + "\n        ^ the branch every bar below drives for real";
        }
        chk (! block.empty(), "[0] THE SHIPPED MIGRATION, SLICED OUT OF THE PROCESSOR — printed, never transcribed", d);
        if (block.empty()) { a.close(); std::printf ("\n  ❌ refusing to certify a migration it cannot even find\n\n"); return 1; }
    }

    std::string why, base = a.readXml (why);
    if (base.empty()) { std::printf ("  !! could not read the state blob: %s\n", why.c_str()); a.close(); return 1; }

    // ── [1] a fresh save carries the marker AND the four new children ─────────────────────────
    {
        std::string d = "blob " + std::to_string (base.size()) + " bytes of XML, magic 'VC2!'; ";
        bool all = true;
        for (int o = 0; o < 4; ++o) { all &= hasParam (base, HMID[o]);
            d += std::string (HMID[o]).substr (8) + "=" + std::to_string ((int) getParam (base, HMID[o])) + " "; }
        d += "| root marker harmTableSplit=" + std::string (hasProperty (base, "harmTableSplit") ? "YES" : "NO");
        chk (all && hasProperty (base, "harmTableSplit"),
             "[1] A FRESH SAVE CARRIES THE MARKER AND THE FOUR NEW PARAMS", d);
    }

    // ── [2] the registered default ────────────────────────────────────────────────────────────
    {
        std::string d; bool ok = true;
        for (int o = 0; o < 4; ++o) { const float v = a.get (HMNAME[o]);
            ok &= (std::fabs (v - 4.0f) < 0.01f);
            d += std::string (HMNAME[o]).substr (10) + "=" + std::to_string ((int) v) + "  "; }
        d += "(4 = Prophet Saw; WT_PRESET's own default is 0 = Sine, which is exactly why an old blob needs seeding)";
        chk (ok, "[2] THE REGISTERED DEFAULT IS PROPHET SAW ON ALL FOUR", d);
    }

    // ── park the four params on a sentinel, so nothing below can pass by standing still ───────
    auto parkSentinel = [&] () -> std::string
    {
        std::string s = base;                                   // base HAS the marker
        for (int o = 0; o < 4; ++o) setParam (s, HMID[o], SENT[o]);
        a.writeXml (s);
        std::string r;
        for (int o = 0; o < 4; ++o) r += std::to_string ((int) a.get (HMNAME[o])) + "/" + std::to_string (SENT[o]) + " ";
        return r;
    };

    // ── [3] THE BAR — a pre-fb601 project seeds HARM_TABLE from its own WT_PRESET ─────────────
    {
        const std::string seeded = parkSentinel();
        std::string x = base;
        if (! MUT) dropProperty (x, "harmTableSplit");           // MUT keeps it: the branch must NOT fire
        int dropped = 0;
        for (int o = 0; o < 4; ++o) { dropped += dropParam (x, HMID[o]) ? 1 : 0; setParam (x, WTID[o], WANT[o]); }
        const bool wrote = a.writeXml (x);
        std::string d = "sentinel parked first (read back " + seeded + "), then dropped "
                      + std::to_string (dropped) + "/4 HARM_TABLE children, marker "
                      + (MUT ? "KEPT (mutation)" : "REMOVED") + ", WT_PRESET written 7/16/2/31. push ok="
                      + std::to_string ((int) wrote) + "  ->  ";
        bool ok = wrote;
        for (int o = 0; o < 4; ++o) { const int got = (int) a.get (HMNAME[o]); ok &= (got == WANT[o]);
            d += std::string (HMNAME[o]).substr (10) + " " + std::to_string (got) + "/" + std::to_string (WANT[o]) + "  "; }
        chk (ok, "[3] 🚨 A PRE-fb601 BLOB COMES BACK WITH HARM_TABLE == ITS OLD WT_PRESET (all four)", d);
    }

    // ── [4] the wavetable engine's own preset is untouched ────────────────────────────────────
    {
        std::string d; bool ok = true;
        for (int o = 0; o < 4; ++o) { const int got = (int) a.get (WTNAME[o]); ok &= (got == WANT[o]);
            d += std::string (WTNAME[o]).substr (10) + " " + std::to_string (got) + "/" + std::to_string (WANT[o]) + "  "; }
        d += " — the migration SEEDS from WT_PRESET, it never moves it";
        chk (ok, "[4] THE WAVETABLE ENGINE'S OWN PRESET IS UNTOUCHED", d);
    }

    // ── [5] the state that comes back OUT carries the marker again ────────────────────────────
    {
        std::string w2; const std::string out = a.readXml (w2);
        std::string d = "re-read " + std::to_string (out.size()) + " bytes; harmTableSplit="
                      + (hasProperty (out, "harmTableSplit") ? "YES" : "NO") + ", HARM_TABLE children back: ";
        bool all = true;
        for (int o = 0; o < 4; ++o) { all &= hasParam (out, HMID[o]);
            d += std::to_string ((int) getParam (out, HMID[o])) + " "; }
        chk (hasProperty (out, "harmTableSplit") && all,
             "[5] THE SAVED STATE CARRIES THE MARKER AGAIN — a reload cannot re-migrate", d);
    }

    // ── [6] a deliberate post-fb601 choice survives, INCLUDING Sine ───────────────────────────
    {
        parkSentinel();                                          // off the answer before we ask
        std::string x = base;                                    // base HAS the marker
        for (int o = 0; o < 4; ++o) { setParam (x, HMID[o], 0.0); setParam (x, WTID[o], 22.0); }
        const bool wrote = a.writeXml (x);
        std::string d = "marker present; HARM_TABLE deliberately 0 = Sine while WT_PRESET is 22. push ok="
                      + std::to_string ((int) wrote) + "  ->  ";
        bool ok = wrote;
        for (int o = 0; o < 4; ++o) { const int got = (int) a.get (HMNAME[o]); ok &= (got == 0);
            d += std::string (HMNAME[o]).substr (10) + " " + std::to_string (got) + "/0  "; }
        d += " (a re-seed would read 22 — Sine is a CHOICE, not a 'pre-fb601 tell')";
        chk (ok, "[6] 🚨 A DELIBERATE POST-fb601 CHOICE IS NEVER RE-SEEDED", d);
    }

    a.close();
    std::printf ("\n  %s %d passed, %d failed\n\n", fail == 0 ? "✅ OK" : "❌ FAILED", pass, fail);
    return fail == 0 ? 0 : 1;
}
