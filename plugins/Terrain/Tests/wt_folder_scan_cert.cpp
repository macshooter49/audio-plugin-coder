// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_folder_scan_cert.cpp — fb606: POINT TERRAIN AT A MASTER FOLDER AND THE TABLES INSIDE ITS
//  SUBFOLDERS MUST APPEAR.
//
//  Max, with the Import Wavetable browser open on 120 tables he cannot reach:
//    "it's hard to open them up because it's subfolders in the MASTER folder, and for some reason
//     Terrain can't open them unless it's DIRECTLY INSIDE OF THE SUB FOLDER. I should be able to
//     open the MASTER FOLDER and see all of the tables in the SUB FOLDERS too."
//
//  THE DEFECT, in one boolean (Source/PluginProcessor.cpp, getImportsJson):
//        d.findChildFiles (juce::File::findFiles, false, kImportWild);
//                                                 ^^^^^ searchRecursively
//  Register a master folder and the browser shows NOTHING, because every wav is one level down.
//  It is also why PLUTO 2 had to be pointed at its exact tables subfolder.
//
//  ⚠️ THAT LINE IS NOT IN THE FILE ANY MORE and this cert does not expect it to be. fb606 replaced
//  it with a hand-rolled, depth-capped walk, because JUCE's own recursion cannot carry each hit's
//  relative subfolder and has no depth limit. The bars below ask about BEHAVIOUR, never about that
//  boolean; the extractor is the only thing that knows the shape, and it handles both.
//
//  ⚠️ AND WHY FLIPPING THE BOOLEAN ALONE IS NOT THE FIX. The payload is a FLAT {name,path} list
//  per registered folder. Recurse without carrying structure and 120 names land in ONE
//  undifferentiated blob — the owner's complaint restated, not answered. So this cert asserts
//  BOTH halves: found at depth, AND each file's relative subfolder travels in the payload.
//
//  ⚠️ THE CODE UNDER TEST IS THE SHIPPING CODE. Tests/extract_imports_scan.py cuts the whole
//  '// IMPORTS REGISTRY' section out of Source/PluginProcessor.cpp verbatim and rewrites exactly
//  one token (the class qualifier). This file compiles that slice. A hand-written copy of the
//  scan would drift, and a cert that measures a copy is worse than no cert because it is trusted
//  (see Tests/extract_halfband.py's header for the time that actually happened here).
//
//  THE BARS
//   0  THE SLICE IS THE SHIPPING CODE — hash, the recursion flag AS SLICED, and whether either
//      mutation fired. A detector that can no-op must say so.
//   1  🚨 A MASTER FOLDER FINDS EVERY TABLE AT DEPTH — one registered folder, tables at depth 0,
//      1 and 2, each listed exactly ONCE, each by a path that resolves on disk.
//   2  🚨 THE RELATIVE SUBFOLDER TRAVELS — every item carries the subfolder it came from, either
//      as its own property or as a nesting of named groups. Either shape passes; NO shape fails.
//   3  THE EMPTY FOLDER DOES NOT EXIST — a subfolder with no tables in it is not a category.
//      Max: "delete anything that doesn't have a table inside of it."
//   4  🚨 A 0x7F IN A FOLDER NAME SURVIVES JSON AND BACK — the owner's PLUTO 2 folder really is
//      named with a leading DEL character. Byte-exact through JSON::toString + JSON::parse, and
//      the path still opens.
//   5  RECURSION DID NOT WIDEN THE WILDCARD — a .txt and a .DS_Store beside the wavs stay out.
//   6  THE CAPS REPORT WHEN THEY BIND — a depth/count cap that silently truncates is a second
//      invisible-missing-tables bug. PENDING (not FAIL) on a build that declares no cap, and the
//      line says exactly what was searched for.
//   7  THE COUNT MATCHES THE ITEMS — folder.count is the number of items actually emitted.
//   8  A SYMLINK LOOP TERMINATES — LOOP/Sub/back -> LOOP, scanned in a re-exec'd child on a 15 s
//      budget: recursion + FollowSymlinks::yes is an infinite walk, and it would hang the WebView.
//   9  🚨 THE SECOND SCAN RECURSES TOO — the managed Wavetables folder ("Open Imports Folder")
//      had the OTHER non-recursive findChildFiles. One walk fixed and one left behind is the same
//      bug with a smaller blast radius; a pack dropped in there must show up the same way.
//  10  THE CACHE SAYS WHETHER IT FIRED — a scan cache that answers silently is a detector that
//      can no-op without saying so. Second call inside the TTL must report cached:true with the
//      identical item list, and registering a folder must invalidate it on the very next call.
//
//  MUTATION CONTROLS (three, independent — A needs the fix to have landed, B never does):
//    A  python3 Tests/extract_imports_scan.py <src> <hdr> --mutate, then compile with
//       -DTI_SLICE_HEADER='"<hdr>"'  → the recursion is turned off in the generated header
//       (searchRecursively back to false, or the depth cap clamped to 0, whichever shape the
//       build has) → [1] RED. This is the fb605 bug, restored on purpose.
//    B  TI_RECURSE_MUT=flat   → the cert drops every item below depth 0 before asserting, i.e.
//                               it pretends the scan is still flat → [1] RED on ANY build.
//    C  TI_CAPS_MUT=1         → force bar [6]'s live arm on a build that declares no cap (the way
//                               to see that bar fail before the C++ lands).
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_core/juce_core.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unistd.h>
#include <sys/wait.h>
#include <chrono>
#include <algorithm>
#include <csignal>

// ── the prelude the slice needs: two stubbed roots + the class the members hang off ───────────
// Every field below is spelled exactly as PluginProcessor.h spells it, because the sliced bodies
// are the shipping bodies and they name these directly. A field that drifts out of the header
// stops this cert COMPILING, which is the loud failure — not a silent green.
static juce::File g_dataDir, g_noizefieldDir;
static juce::File terrainDataDirP()       { return g_dataDir; }
static juce::File terrainNoizefieldDirP() { return g_noizefieldDir; }

struct ImportsReg
{
    juce::StringArray importFiles_[3], importFolders_[3];
    juce::var         importsCache_[3];                              // fb606 scan cache
    juce::uint32      importsCacheAt_[3] { 0, 0, 0 };
    bool              importsCacheValid_[3] { false, false, false };
    static constexpr juce::uint32 kImportsCacheTtlMs = 1500;

    // ⚠️ STUBBED, and bar [0] says so. The shipping builtinWtCatItems reads the APVTS for the 46
    // built-in table names — juce_audio_processors, ParameterIDs, the whole plugin. This cert is
    // about the folder WALK. The extractor excises the real one; nothing here can pass BECAUSE of
    // this stub, because no bar below looks at `builtin`.
    juce::Array<juce::var> builtinWtCatItems (int) const { return {}; }

    void         addImportPath            (int kind, const juce::String& path);
    void         removeImportPath         (int kind, const juce::String& path);
    juce::String getImportsJson           (int kind);
    juce::String getManagedWavetablesJson ();
    void         loadImportsRegistry      ();
    void         saveImportsRegistry      (int kind);
};

// ⚠️ A QUOTED #include SEARCHES THIS FILE'S OWN DIRECTORY FIRST — before every -I. So the
// mutated header CANNOT be swapped in with -I<dir>: the un-mutated Tests/ copy always wins, the
// mutant compiles against the healthy slice and "survives", and the control silently reports a
// gate that cannot go red. (Measured: mutation A passed 11/0 that way before this indirection.)
// The runner therefore passes -DTI_SLICE_HEADER='"<abs path to the mutated header>"'.
#ifndef TI_SLICE_HEADER
 #define TI_SLICE_HEADER "ti_imports_scan_extracted.h"
#endif
#include TI_SLICE_HEADER                   // ← the shipping bytes

// ══ reporting ════════════════════════════════════════════════════════════════════════════════
static int g_pass = 0, g_fail = 0, g_pending = 0;
static void bar (bool ok, const char* name, const std::string& detail)
{
    ok ? ++g_pass : ++g_fail;
    std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
}
static void pending (const char* name, const std::string& detail)
{
    ++g_pending;
    std::printf ("  PEND  %s\n        %s\n", name, detail.c_str());
}
static std::string S (const juce::String& s) { return std::string (s.toRawUTF8()); }

// a printable spelling of a name that may hold a control character
static std::string viz (const juce::String& s)
{
    std::string o;
    for (auto c : s.toStdString())
    {
        auto u = (unsigned char) c;
        if (u < 0x20 || u == 0x7F) { char b[8]; std::snprintf (b, 8, "<%02X>", u); o += b; }
        else o += c;
    }
    return o;
}

// ══ the tree ═════════════════════════════════════════════════════════════════════════════════
// A wav small enough to be free and real enough that a scan cannot dismiss it. The scan matches
// on EXTENSION, so the bytes only have to exist — but a zero-byte file is a different kind of
// object to some file APIs, so write a real (tiny) RIFF header.
static void writeWav (const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    static const unsigned char hdr[44] = {
        'R','I','F','F', 36,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0,
        1,0, 1,0, 0x44,0xAC,0,0, 0x88,0x58,1,0, 2,0, 16,0, 'd','a','t','a', 0,0,0,0 };
    f.replaceWithData (hdr, sizeof (hdr));
}

static const juce::String kDelName = juce::String::fromUTF8 ("\x7F") + "PLUTO 2 Wavetable Banks by DYNOX";

struct Tree { juce::File root; juce::StringArray wantRel; };   // wantRel = relative paths of the tables

static Tree buildTree (const juce::File& base)
{
    Tree t; t.root = base.getChildFile ("MASTER");
    t.root.deleteRecursively(); t.root.createDirectory();

    auto add = [&t] (const juce::String& rel) {
        writeWav (t.root.getChildFile (rel)); t.wantRel.add (rel); };

    add ("loose at the top.wav");                          // depth 0 — must not be lost by the fix
    add ("Foundation/Terra Bell.wav");                     // depth 1
    add ("Foundation/Terra Bar.wav");
    add ("Foundation/Terra Choir.wav");
    add ("Digital/Deep/Bitcrush 12.wav");                  // depth 2 — nested two deep
    add ("Digital/Deep/Bitcrush 08.wav");
    add (kDelName + "/PLUTO Saw.wav");                     // a 0x7F leads this folder's name
    add (kDelName + "/PLUTO Bell.aif");

    t.root.getChildFile ("Empty").createDirectory();       // EMPTY — must not become a category
    t.root.getChildFile ("Empty/.keep").replaceWithText ("");

    t.root.getChildFile ("Foundation/readme.txt").replaceWithText ("not a table");   // bar [5]
    t.root.getChildFile ("Foundation/.DS_Store").replaceWithText ("not a table");
    return t;
}

// ══ walking the payload ══════════════════════════════════════════════════════════════════════
struct Item {
    juce::String name, path;
    juce::StringArray ancestorNames;                    // named groups strictly between folder and item
    std::map<juce::String, juce::String> props;         // the item's own string properties
};

static void collect (const juce::var& v, juce::StringArray chain, std::vector<Item>& out, int depth = 0)
{
    if (depth > 24) return;
    if (auto* arr = v.getArray())
    { for (auto& e : *arr) collect (e, chain, out, depth + 1); return; }

    auto* o = v.getDynamicObject();
    if (o == nullptr) return;
    const auto& nvs = o->getProperties();

    const juce::var p = o->getProperty ("path");
    const bool isItem = p.isString() && juce::File (p.toString()).existsAsFile();

    if (isItem)
    {
        Item it; it.name = o->getProperty ("name").toString(); it.path = p.toString();
        it.ancestorNames = chain;
        for (int i = 0; i < nvs.size(); ++i)
            if (nvs.getValueAt (i).isString())
                it.props[nvs.getName (i).toString()] = nvs.getValueAt (i).toString();
        out.push_back (it);
        return;   // an item has no items of its own
    }

    juce::StringArray next = chain;
    const juce::var nm = o->getProperty ("name");
    if (nm.isString() && nm.toString().isNotEmpty()) next.add (nm.toString());
    for (int i = 0; i < nvs.size(); ++i)
        collect (nvs.getValueAt (i), next, out, depth + 1);
}

// every string that appears ANYWHERE in the payload (bar [3] and bar [4] both need this)
static void allStrings (const juce::var& v, juce::StringArray& out, int depth = 0)
{
    if (depth > 24) return;
    if (v.isString()) { out.add (v.toString()); return; }
    if (auto* arr = v.getArray()) { for (auto& e : *arr) allStrings (e, out, depth + 1); return; }
    if (auto* o = v.getDynamicObject())
    { const auto& n = o->getProperties();
      for (int i = 0; i < n.size(); ++i) { out.add (n.getName (i).toString()); allStrings (n.getValueAt (i), out, depth + 1); } }
}

// a truthy value under a key that says "I was cut short"
static bool findCapMarker (const juce::var& v, juce::String& where, int depth = 0)
{
    if (depth > 24) return false;
    if (auto* arr = v.getArray())
    { for (auto& e : *arr) if (findCapMarker (e, where, depth + 1)) return true; return false; }
    auto* o = v.getDynamicObject();
    if (o == nullptr) return false;
    const auto& n = o->getProperties();
    for (int i = 0; i < n.size(); ++i)
    {
        const auto key = n.getName (i).toString().toLowerCase();
        if (key.contains ("cap") || key.contains ("trunc") || key.contains ("overflow")
            || key.contains ("limit") || key.contains ("more") || key.contains ("elided")
            || key.contains ("clipped") || key.contains ("partial"))
        {
            const auto val = n.getValueAt (i);
            const bool truthy = (val.isBool() && (bool) val) || (val.isInt() && (int) val > 0)
                              || (val.isString() && val.toString().isNotEmpty())
                              || (val.isArray() && val.getArray()->size() > 0);
            if (truthy) { where = n.getName (i).toString() + "=" + val.toString(); return true; }
        }
    }
    for (int i = 0; i < n.size(); ++i) if (findCapMarker (n.getValueAt (i), where, depth + 1)) return true;
    return false;
}

// ══ the symlink loop (bar [8]) ═══════════════════════════════════════════════════════════════
// ⚠️ THE CHILD IS A RE-EXEC OF THIS BINARY, NOT A BARE fork(). Measured on this machine: a
// forked-but-not-exec'd child of a juce_core.mm process aborts with SIGABRT the moment it reaches
// getImportsJson — the Obj-C runtime and libmalloc are live in the parent and neither is fork-safe
// without an exec. That is a fact about macOS, not a finding about the scan, and reading it as one
// is how a gate starts lying: the first cut of this bar reported "died on signal 6" against a scan
// whose first line had not run. So `wt_folder_scan_cert --loopchild <dir>` IS the child, via execv.
static bool buildLoopTree (const juce::File& root)
{
    root.deleteRecursively(); root.createDirectory();
    writeWav (root.getChildFile ("Sub/one.wav"));
    // LOOP/Sub/back -> LOOP   (a real directory cycle, the kind a user makes with an alias)
    const auto link = root.getChildFile ("Sub/back");
    return symlink (root.getFullPathName().toRawUTF8(), link.getFullPathName().toRawUTF8()) == 0;
}
static int loopChild (const juce::File& root)
{
    ImportsReg r; r.addImportPath (1, root.getFullPathName());
    const auto js = r.getImportsJson (1);
    return js.isNotEmpty() ? 0 : 1;
}

// ══════════════════════════════════════════════════════════════════════════════════════════════
static std::string g_argv0;

int main (int argc, char** argv)
{
    g_argv0 = argv[0];

    // ── --emit <dir> : write the TWO payloads the WebView really receives, from the REAL C++ ────
    // Tests/wt_folder_menu_gate.js feeds these to the shipping JS instead of a hand-written
    // fixture. That closes the seam no single-language gate can see: a C++ payload whose SHAPE the
    // JS reader does not accept is green on both sides and broken in the plugin.
    if (argc >= 3 && std::string (argv[1]) == "--emit")
    {
        const juce::File dir { juce::String (argv[2]) };   // braces: (T x (y)) is a function declaration
        dir.createDirectory();
        auto sandbox = dir.getChildFile ("tree"); sandbox.deleteRecursively(); sandbox.createDirectory();
        g_dataDir       = sandbox.getChildFile ("data");        g_dataDir.createDirectory();
        g_noizefieldDir = sandbox.getChildFile ("Noizefield");  g_noizefieldDir.createDirectory();

        auto t = buildTree (sandbox);                          // the same MASTER folder the bars use
        // the factory bank, where wtFactoryRoot() looks for it
        auto fac = g_dataDir.getChildFile ("Wavetables").getChildFile ("Factory");
        writeWav (fac.getChildFile ("FOUNDATION/Supersaw.wav"));
        writeWav (fac.getChildFile ("FOUNDATION/Juno Chorus.wav"));
        writeWav (fac.getChildFile ("VOCAL/Choir Wide.wav"));
        writeWav (fac.getChildFile ("SPECTRAL/Deep/Rise 3.wav"));   // a factory pack with a subfolder
        writeWav (fac.getChildFile ("ASH FALL/Ash 1.wav"));         // an unrecognised pack name
        fac.getChildFile ("HOLLOW").createDirectory();              // EMPTY — must not become a category
        // the managed Wavetables folder
        writeWav (g_noizefieldDir.getChildFile ("Wavetables/dropped.wav"));
        writeWav (g_noizefieldDir.getChildFile ("Wavetables/A Pack/inside.wav"));

        ImportsReg r; r.addImportPath (1, t.root.getFullPathName());
        dir.getChildFile ("imports.json").replaceWithText (r.getImportsJson (1));
        dir.getChildFile ("managed.json").replaceWithText (r.getManagedWavetablesJson());
        std::printf ("emitted imports.json + managed.json (registered %s, factory root %s) -> %s\n",
                     t.root.getFileName().toRawUTF8(), fac.getFullPathName().toRawUTF8(),
                     dir.getFullPathName().toRawUTF8());
        return 0;
    }

    // bar [8]'s child — before any reporting
    if (argc >= 3 && std::string (argv[1]) == "--loopchild")
    {
        g_dataDir = juce::File (juce::String (argv[2])).getParentDirectory().getChildFile ("data");
        g_dataDir.createDirectory();
        return loopChild (juce::File (juce::String (argv[2])));
    }

    const juce::String MUT = juce::String (std::getenv ("TI_RECURSE_MUT") ? std::getenv ("TI_RECURSE_MUT") : "");
    const bool MUT_FLAT    = (MUT == "flat");
    const bool MUT_CAPS    = std::getenv ("TI_CAPS_MUT") != nullptr;

    std::printf ("══ wt_folder_scan_cert (fb606) — A MASTER FOLDER MUST SHOW THE TABLES IN ITS SUBFOLDERS ══\n");

    auto base = juce::File::getSpecialLocation (juce::File::tempDirectory)
                  .getChildFile ("ti_fb606_recurse_" + juce::String ((int) getpid()));
    base.deleteRecursively(); base.createDirectory();
    g_dataDir = base.getChildFile ("data"); g_dataDir.createDirectory();

    // an explicit scratch root, for a human who wants to read the tree afterwards. ABSOLUTE ONLY:
    // a relative argv[1] (a stray flag, say) would otherwise scribble a directory into the repo.
    if (argc > 1 && juce::File::isAbsolutePath (juce::String (argv[1]))) {
        base = juce::File (juce::String (argv[1])); base.createDirectory();
        g_dataDir = base.getChildFile ("data"); g_dataDir.createDirectory(); }

    // ── [0] WHAT AM I ACTUALLY TESTING ────────────────────────────────────────────────────────
    {
        const juce::String sliced = TI_IMPORTS_SLICE_RECURSIVE_AS_SLICED;
        std::string d = "slice hash 0x" + S (juce::String::toHexString ((juce::int64) TI_IMPORTS_SLICE_HASH))
            + "  ·  recursion AS SLICED: " + S (sliced)
            + "  ·  extractor mutation: " + (TI_IMPORTS_SLICE_MUTATED ? "FIRED — recursion turned OFF in the generated header" : "not fired")
            + "  ·  TI_RECURSE_MUT=" + (MUT.isEmpty() ? std::string ("(unset)") : S (MUT))
            + (MUT_FLAT ? " FIRING — items below depth 0 are dropped before every assertion" : "")
            + "  ·  TI_CAPS_MUT=" + (MUT_CAPS ? "1 FIRING" : "(unset)");
        bar (! sliced.isEmpty(), "[0] THE SLICE IS THE SHIPPING CODE", d);
    }

    auto tree = buildTree (base);
    ImportsReg reg;
    reg.addImportPath (1, tree.root.getFullPathName());

    const auto t0 = std::chrono::steady_clock::now();
    const juce::String js = reg.getImportsJson (1);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();

    const juce::var payload = juce::JSON::parse (js);

    std::vector<Item> items;
    collect (payload, {}, items);

    // the registered folder object, so "relative to the folder" is a real question
    const juce::String rootPath = tree.root.getFullPathName();

    auto relOf = [&] (const Item& it) {
        return juce::File (it.path).getRelativePathFrom (tree.root); };
    auto subOf = [&] (const Item& it) {
        auto r = relOf (it); auto i = r.lastIndexOfChar ('/');
        return i < 0 ? juce::String() : r.substring (0, i); };

    if (MUT_FLAT)
    {
        std::vector<Item> keep;
        for (auto& it : items) if (subOf (it).isEmpty()) keep.push_back (it);
        items.swap (keep);
    }

    // ── [1] EVERY TABLE, AT EVERY DEPTH, EXACTLY ONCE ─────────────────────────────────────────
    {
        std::map<juce::String, int> seen;
        for (auto& it : items) seen[relOf (it)]++;
        juce::StringArray missing, dupes;
        for (auto& w : tree.wantRel) if (seen.find (w) == seen.end()) missing.add (w);
        for (auto& kv : seen) if (kv.second > 1) dupes.add (kv.first + " x" + juce::String (kv.second));
        juce::StringArray dead;
        for (auto& it : items) if (! juce::File (it.path).existsAsFile()) dead.add (it.path);

        std::string d = "registered ONE folder (" + S (tree.root.getFileName()) + ") holding "
            + std::to_string (tree.wantRel.size()) + " tables at depths 0/1/2 — payload lists "
            + std::to_string (items.size()) + " in " + std::to_string ((int) ms) + " ms";
        if (! missing.isEmpty()) d += "\n        MISSING (this is the owner's bug): " + S (missing.joinIntoString (" · "));
        if (! dupes.isEmpty())   d += "\n        LISTED TWICE: " + S (dupes.joinIntoString (" · "));
        if (! dead.isEmpty())    d += "\n        PATH DOES NOT RESOLVE: " + S (dead.joinIntoString (" · "));
        bar (missing.isEmpty() && dupes.isEmpty() && dead.isEmpty(),
             "[1] A MASTER FOLDER FINDS EVERY TABLE AT DEPTH", d);
    }

    // ── [2] THE RELATIVE SUBFOLDER TRAVELS ────────────────────────────────────────────────────
    {
        juce::StringArray lost; std::set<std::string> shapes;
        int checked = 0;
        for (auto& it : items)
        {
            const auto want = subOf (it);
            if (want.isEmpty()) continue;                 // depth-0 files have no subfolder to carry
            ++checked;
            bool ok = false; juce::String how;
            for (auto& kv : it.props)                      // shape A: a property on the item
                if (kv.second == want || kv.second == (want + "/")
                    || kv.second.replaceCharacter ('\\', '/') == want)
                { ok = true; how = "item." + kv.first; break; }
            if (! ok && it.ancestorNames.size() > 0)       // shape B: nested named groups
            {
                auto joined = it.ancestorNames.joinIntoString ("/");
                if (joined == want || joined.endsWith ("/" + want) || joined == want.fromLastOccurrenceOf ("/", false, false))
                { ok = true; how = "nested groups " + it.ancestorNames.joinIntoString (" > "); }
            }
            if (ok) shapes.insert (S (how)); else lost.add (viz (relOf (it)));
        }
        std::string d;
        if (checked == 0) d = "NO ITEM IS BELOW THE FOLDER ROOT — there is nothing to carry, because "
                              "bar [1] found nothing at depth. Fix [1] first.";
        else
        {
            d = "checked " + std::to_string (checked) + " item(s) below the root; carried as: ";
            for (auto& s : shapes) d += s + "  ";
            if (! lost.isEmpty())
                d += "\n        NO SUBFOLDER ANYWHERE IN THE PAYLOAD FOR: " + S (lost.joinIntoString (" · "))
                   + "\n        (120 names in one undifferentiated blob is the owner's complaint restated. "
                     "Carry the relative subfolder on the item, or nest the items under named groups.)";
            if (! items.empty() && lost.isEmpty() && shapes.empty())
                d += "(none)";
        }
        bar (checked > 0 && lost.isEmpty(), "[2] THE RELATIVE SUBFOLDER TRAVELS", d);
    }

    // ── [3] THE EMPTY FOLDER IS NOT A CATEGORY ────────────────────────────────────────────────
    {
        juce::StringArray strs; allStrings (payload, strs);
        juce::StringArray hits;
        for (auto& s : strs)
            if (s == "Empty" || s.endsWith ("/Empty") || s.endsWith ("/Empty/") || s.endsWith ("MASTER/Empty"))
                hits.add (s);
        bar (hits.isEmpty(), "[3] THE EMPTY FOLDER DOES NOT EXIST",
             hits.isEmpty()
               ? std::string ("MASTER/Empty holds no table, and the word 'Empty' appears nowhere in the payload "
                              "— Max: \"delete anything that doesn't have a table inside of it\"")
               : "an EMPTY subfolder became a category anyway: " + S (hits.joinIntoString (" · ")));
    }

    // ── [4] 0x7F THROUGH JSON AND BACK ────────────────────────────────────────────────────────
    {
        juce::StringArray want;
        for (auto& w : tree.wantRel) if (w.startsWith (kDelName)) want.add (w);

        int found = 0; bool byteExact = true, opens = true; juce::String sample;
        for (auto& it : items)
        {
            const auto rel = relOf (it);
            if (! rel.startsWith (kDelName)) continue;
            ++found;
            if (sample.isEmpty()) sample = it.path;
            if (! juce::File (it.path).existsAsFile()) opens = false;
            // the DEL must still be there, as one raw byte, after toString + parse
            if (! rel.startsWithChar ((juce::juce_wchar) 0x7F)) byteExact = false;
        }
        const bool ok = (found == want.size()) && byteExact && opens;
        std::string d = "folder name " + viz (kDelName.substring (0, 12)) + "…  ·  tables under it: found "
            + std::to_string (found) + " of " + std::to_string (want.size())
            + "  ·  leading 0x7F survived JSON::toString+parse: " + (byteExact ? "yes" : "NO — the byte was dropped or escaped away")
            + "  ·  path still opens: " + (opens ? "yes" : "NO");
        if (found == 0) d += "\n        (nothing under it at all — this is bar [1] again, seen through the owner's real PLUTO 2 folder name)";
        bar (ok, "[4] A 0x7F IN A FOLDER NAME SURVIVES JSON AND BACK", d);
    }

    // ── [5] THE WILDCARD DID NOT WIDEN ────────────────────────────────────────────────────────
    {
        juce::StringArray strays;
        for (auto& it : items)
        {
            const auto ext = juce::File (it.path).getFileExtension().toLowerCase();
            if (! juce::StringArray ({ ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3" }).contains (ext))
                strays.add (juce::File (it.path).getFileName());
        }
        bar (strays.isEmpty(), "[5] RECURSION DID NOT WIDEN THE WILDCARD",
             strays.isEmpty()
               ? std::string ("readme.txt and .DS_Store sit beside the tables and stayed out of the payload")
               : "non-audio was listed as a table: " + S (strays.joinIntoString (" · ")));
    }

    // ── [6] THE CAPS REPORT WHEN THEY BIND ────────────────────────────────────────────────────
    {
        const int depthCap = TI_IMPORTS_SLICE_DEPTH_CAP;
        const int countCap = TI_IMPORTS_SLICE_COUNT_CAP;
        const juce::String capList = TI_IMPORTS_SLICE_CAPS;
        const bool live = MUT_CAPS || depthCap > 0 || countCap > 0;

        if (! live)
        {
            pending ("[6] THE CAPS REPORT WHEN THEY BIND",
                std::string ("the sliced section declares NO cap constant. Searched it for an identifier "
                             "matching /k?\\w*(Max|Cap|Limit)\\w*\\s*=\\s*\\d+/ and found: (none).\n        "
                             "So there is nothing that can silently truncate — and nothing to report. "
                             "THE CONTRACT, when a cap lands: emit a truthy field whose key contains "
                             "cap/trunc/limit/more/overflow/elided/clipped/partial next to the folder it "
                             "cut short. TI_CAPS_MUT=1 forces this bar live."));
        }
        else
        {
            const int dcap = depthCap > 0 ? depthCap : 4;
            const int ccap = countCap > 0 ? countCap : 32;
            auto deep = base.getChildFile ("DEEP"); deep.deleteRecursively(); deep.createDirectory();
            juce::String p;
            for (int i = 0; i < dcap + 3; ++i) p += "L" + juce::String (i) + "/";
            writeWav (deep.getChildFile (p + "too deep.wav"));
            for (int i = 0; i < ccap + 5; ++i)
                writeWav (deep.getChildFile ("Many/f" + juce::String (i).paddedLeft ('0', 4) + ".wav"));

            ImportsReg r2; r2.addImportPath (1, deep.getFullPathName());
            const auto js2 = r2.getImportsJson (1);
            const auto v2  = juce::JSON::parse (js2);
            std::vector<Item> it2; collect (v2, {}, it2);
            const int emitted = (int) it2.size();
            const int offered = (dcap + 3 > 0 ? 1 : 0) + ccap + 5;
            juce::String where;
            const bool marked   = findCapMarker (v2, where);
            const bool truncated = emitted < offered;
            const bool ok = truncated ? marked : true;
            std::string d = "caps declared in the slice: " + S (capList.isEmpty() ? juce::String ("(none — TI_CAPS_MUT forced this bar live)") : capList)
                + "  ·  offered " + std::to_string (offered) + " tables (one at depth " + std::to_string (dcap + 3)
                + ", " + std::to_string (ccap + 5) + " in one folder)  ·  payload emitted " + std::to_string (emitted);
            d += truncated ? ("  ·  TRUNCATED, and it " + std::string (marked ? ("SAYS SO: " + S (where)) : "SAYS NOTHING"))
                           : "  ·  nothing was cut, so there is nothing to report";
            if (truncated && ! marked)
                d += "\n        A cap that truncates in silence is the same invisible-missing-tables bug in a "
                     "second place. Emit a truthy cap/trunc/limit/more field beside the folder it cut.";
            bar (ok, "[6] THE CAPS REPORT WHEN THEY BIND", d);
        }
    }

    // ── [7] folder.count IS THE NUMBER OF ITEMS ───────────────────────────────────────────────
    {
        // find every object carrying BOTH a numeric "count" and a descendant item
        int declared = -1;
        if (auto* o = payload.getDynamicObject())
            if (auto* fa = o->getProperty ("folders").getArray())
                for (auto& f : *fa)
                    if (auto* fo = f.getDynamicObject())
                        if (fo->getProperty ("path").toString() == rootPath)
                            declared = (int) fo->getProperty ("count");
        const int actual = (int) items.size();
        const bool ok = (declared < 0) || (declared == actual);
        bar (ok, "[7] THE COUNT MATCHES THE ITEMS",
             declared < 0
               ? std::string ("the folder object carries no \"count\" — nothing to disagree with")
               : "folder.count = " + std::to_string (declared) + ", items actually emitted = " + std::to_string (actual)
                 + (ok ? "" : "   (the browser prints this number next to the folder name — a recursive scan that "
                              "forgets to update it lies to the owner about how many tables he has)"));
    }

    // ── [8] A SYMLINK LOOP TERMINATES ─────────────────────────────────────────────────────────
    {
        const auto loopRoot = base.getChildFile ("LOOP");
        const bool built = buildLoopTree (loopRoot);
        std::fflush (stdout);
        bool ok = false; std::string how;
        if (! built) { ok = true; how = "could not create the symlink (sandbox?) — inconclusive, not counted against the fix"; }
        else
        {
            const pid_t pid = fork();
            if (pid == 0)
            {
                const std::string ap = S (loopRoot.getFullPathName());
                char* av[4] = { (char*) g_argv0.c_str(), (char*) "--loopchild", (char*) ap.c_str(), nullptr };
                execv (g_argv0.c_str(), av);
                _exit (78);                                     // execv failed — inconclusive
            }
            if (pid < 0) { ok = true; how = "fork() failed — inconclusive"; }
            else
            {
                int st = 0; bool reaped = false;
                const auto tw = std::chrono::steady_clock::now();
                while (std::chrono::duration<double> (std::chrono::steady_clock::now() - tw).count() < 15.0)
                {
                    if (waitpid (pid, &st, WNOHANG) == pid) { reaped = true; break; }
                    usleep (20000);
                }
                if (! reaped)
                {
                    kill (pid, SIGKILL); waitpid (pid, &st, 0);
                    ok = false;
                    how = "THE SCAN NEVER RETURNED — killed at 15 s on a directory cycle. Recursion with "
                          "FollowSymlinks::yes walks a user's alias back into itself forever, and this scan runs "
                          "on the message thread, so the WebView freezes with it.";
                }
                else if (WIFEXITED (st) && WEXITSTATUS (st) == 78) { ok = true; how = "execv of the child failed — inconclusive"; }
                else if (WIFEXITED (st)) { ok = WEXITSTATUS (st) == 0;
                    how = "the scan returned in time (exit " + std::to_string (WEXITSTATUS (st)) + ")"; }
                else { ok = false; how = "the child died on signal " + std::to_string (WTERMSIG (st)); }
            }
        }
        bar (ok, "[8] A SYMLINK LOOP TERMINATES", "LOOP/Sub/back -> LOOP, scanned in a re-exec'd child with a 15 s budget: " + how);
    }

    // ── [9] THE SECOND SCAN RECURSES TOO ──────────────────────────────────────────────────────
    {
        auto nf = base.getChildFile ("Noizefield"); nf.deleteRecursively();
        auto wt = nf.getChildFile ("Wavetables");
        writeWav (wt.getChildFile ("top.wav"));
        writeWav (wt.getChildFile ("A Pack/inside.wav"));
        writeWav (wt.getChildFile ("A Pack/Deeper/further in.wav"));
        g_noizefieldDir = nf;

        ImportsReg r3;
        const auto v3 = juce::JSON::parse (r3.getManagedWavetablesJson());
        std::vector<Item> it3; collect (v3, {}, it3);
        juce::StringArray got;
        for (auto& it : it3) got.add (juce::File (it.path).getRelativePathFrom (wt));
        const bool deep = got.contains ("A Pack/inside.wav") && got.contains ("A Pack/Deeper/further in.wav");
        const bool top  = got.contains ("top.wav");
        std::string d = "the folder \"Open Imports Folder\" reveals, holding 1 loose table + a 2-level pack — "
            "returned " + std::to_string (it3.size()) + ": " + S (got.joinIntoString (" · "));
        if (! deep) d += "\n        THE PACK IS INVISIBLE — this is the second non-recursive scan, still non-recursive.";
        if (! top)  d += "\n        the loose table at the top was lost.";
        bar (deep && top, "[9] THE SECOND SCAN RECURSES TOO", d);
    }

    // ── [10] THE CACHE SAYS WHETHER IT FIRED ──────────────────────────────────────────────────
    {
        ImportsReg r4; r4.addImportPath (1, tree.root.getFullPathName());
        auto cachedFlag = [] (const juce::var& v) -> int {   // -1 = no scan block at all
            if (auto* o = v.getDynamicObject())
                if (auto* sc = o->getProperty ("scan").getDynamicObject())
                    return (bool) sc->getProperty ("cached") ? 1 : 0;
            return -1; };
        auto nItems = [] (const juce::var& v) { std::vector<Item> t; collect (v, {}, t); return (int) t.size(); };

        const auto a = juce::JSON::parse (r4.getImportsJson (1));   // cold
        const auto b = juce::JSON::parse (r4.getImportsJson (1));   // inside the TTL
        auto second = base.getChildFile ("SECOND"); second.deleteRecursively();
        writeWav (second.getChildFile ("Pack/new one.wav"));
        r4.addImportPath (1, second.getFullPathName());             // must invalidate
        const auto c = juce::JSON::parse (r4.getImportsJson (1));

        const int fa = cachedFlag (a), fb2 = cachedFlag (b), fc = cachedFlag (c);
        std::string d;
        bool ok;
        if (fa < 0)
        {
            ok = true;
            d = "the payload carries no scan.cached field — there is no cache to be silent about "
                "(if one is added, it has to say so: this bar arms itself the moment the field exists)";
        }
        else
        {
            const bool sameItems = nItems (a) == nItems (b);
            const bool grew      = nItems (c) > nItems (b);
            ok = (fa == 0) && (fb2 == 1) && sameItems && (fc == 0) && grew;
            d = "cold scan.cached=" + std::to_string (fa) + " (" + std::to_string (nItems (a)) + " items)"
                + "  ·  same call inside the TTL cached=" + std::to_string (fb2) + " (" + std::to_string (nItems (b)) + " items)"
                + "  ·  after registering a second folder cached=" + std::to_string (fc) + " (" + std::to_string (nItems (c)) + " items)";
            if (fb2 != 1) d += "\n        the second call did NOT report itself as cached — either the cache never fires, or it fires without saying so.";
            if (! sameItems) d += "\n        the cached answer is not the answer it cached.";
            if (fc != 0 || ! grew) d += "\n        REGISTERING A FOLDER DID NOT INVALIDATE THE CACHE — an import the owner just made is invisible for the TTL.";
        }
        bar (ok, "[10] THE CACHE SAYS WHETHER IT FIRED", d);
    }

    std::printf ("\n  scratch tree: %s\n", tree.root.getFullPathName().toRawUTF8());
    std::printf ("  %d pass · %d fail · %d pending\n", g_pass, g_fail, g_pending);
    if (g_fail == 0) base.deleteRecursively();
    return g_fail == 0 ? 0 : 1;
}
