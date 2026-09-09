// ══ fb619 — PRESET BANKS: the file layer, driven on a temp root with real juce_core ════════════
//   (compiled by Tests/fb619_gates.sh with the preset_path_cert line: real juce_core, no processor)
//   BK_MUT=nocap      the control: expects a scan past the cap NOT to report — must go RED
//   BK_MUT=escape     the control: expects a write OUTSIDE the user root to succeed — must go RED
//
// Everything a bank can do to a file, on a throwaway root: write, scan, rewrite meta byte-cleanly,
// move, favourite, export a .terrainpack, import it into a fresh root, rename a bank, delete.
#include "PresetBank.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
static int pass = 0, fail = 0; static std::vector<std::string> failedBars;
static void chk (bool ok, const char* label, const juce::String& detail)
{ if (ok) ++pass; else { ++fail; failedBars.push_back (label); }
  std::printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", label, detail.toRawUTF8()); }

// a small but real chunk: the same bytes copyXmlToBinary would write for this tree
static juce::MemoryBlock mkChunk (const juce::String& name, const juce::String& bank, const juce::String& type, int level)
{
    juce::XmlElement root ("Parameters");
    root.setAttribute ("version", 3);
    auto* pe = root.createNewChildElement ("preset");
    pe->setAttribute ("name", name); pe->setAttribute ("bank", bank); pe->setAttribute ("author", "Gate");
    pe->setAttribute ("type", type); pe->setAttribute ("styles", "Dark,Wide"); pe->setAttribute ("note", "");
    pe->setAttribute ("fv", 1); pe->setAttribute ("carries", "{\"wt\":1,\"smp\":0,\"ir\":0,\"flow\":0,\"lfo\":0,\"nodes\":0}");
    auto* p = root.createNewChildElement ("PARAM"); p->setAttribute ("id", "SYN_OSC_A_LEVEL"); p->setAttribute ("value", level);
    juce::MemoryBlock mb; tw::bank::xmlToChunk (root, mb); return mb;
}
static juce::String manifestOf (const juce::MemoryBlock& chunk)
{ auto x = tw::bank::chunkToXml (chunk); return tw::bank::manifestFromChild (*x->getChildByName ("preset")); }
static juce::String body (const juce::MemoryBlock& chunk)   // the chunk with the <preset> child cut out
{ auto x = tw::bank::chunkToXml (chunk); x->removeChildElement (x->getChildByName ("preset"), true); juce::MemoryBlock mb; tw::bank::xmlToChunk (*x, mb); return juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()); }

int main()
{
    const char* mutC = std::getenv ("BK_MUT"); const juce::String mut = mutC ? mutC : "";
    std::printf ("══ fb619 PRESET BANKS ══   mutation: %s\n", mut.isEmpty() ? "(none)" : mut.toRawUTF8());
    const auto root = juce::File::createTempFile ("banks"); root.deleteFile(); root.createDirectory();
    const auto user = root.getChildFile ("Banks"), factory = root.getChildFile ("FactoryBanks");
    user.createDirectory(); factory.createDirectory();
    juce::String err;

    // [1] write three presets into two banks; the folder and the file are what the contract says
    juce::File d; bool ok = tw::bank::ensureBank (user, "Max Vol 1", "Max", d, err);
    auto c1 = mkChunk ("Cold Open", "Max Vol 1", "Pad", 1), c2 = mkChunk ("Slatt", "Max Vol 1", "Bass", 2), c3 = mkChunk ("Kiln", "Glasswork", "Keys", 3);
    ok = ok && tw::bank::writePreset (tw::bank::presetPath (user, "Max Vol 1", "Cold Open"), user, manifestOf (c1), c1, err)
            && tw::bank::writePreset (tw::bank::presetPath (user, "Max Vol 1", "Slatt"), user, manifestOf (c2), c2, err);
    juce::File d2; ok = ok && tw::bank::ensureBank (user, "Glasswork", "Elin Sato", d2, err)
            && tw::bank::writePreset (tw::bank::presetPath (user, "Glasswork", "Kiln"), user, manifestOf (c3), c3, err);
    chk (ok && user.getChildFile ("Max Vol 1").getChildFile ("Cold Open.terrain").existsAsFile() && d.getChildFile ("bank.json").existsAsFile(),
         "[1] A BANK IS A FOLDER WITH bank.json; A PRESET IS <Bank>/<Name>.terrain", err.isEmpty() ? "Banks/Max Vol 1/Cold Open.terrain · Banks/Glasswork/Kiln.terrain" : err);

    // [2] the scan reads headers only and returns the catalogue
    tw::bank::ScanStats st; tw::bank::Caps caps;
    auto cat = tw::bank::scan (factory, user, caps, st);
    auto* banks = cat.getProperty ("banks", juce::var()).getArray();
    int nb = banks ? banks->size() : 0, np = 0; juce::String first;
    if (banks) for (auto& b : *banks) if (auto* ps = b.getProperty ("presets", juce::var()).getArray()) { np += ps->size(); if (first.isEmpty() && ps->size()) first = (*ps)[0].getProperty ("bank", "").toString() + " - " + (*ps)[0].getProperty ("name", "").toString(); }
    chk (nb == 2 && np == 3 && st.unreadable == 0 && cat.getProperty ("caps", juce::var()).getProperty ("hitPresets", true).equals (false),
         "[2] THE CATALOGUE LISTS 2 BANKS AND 3 PRESETS FROM THEIR HEADERS", juce::String (nb) + " banks · " + juce::String (np) + " presets · first " + first);

    // [3] a meta rewrite touches only the <preset> child — the rest of the chunk is byte-identical
    const auto cold = tw::bank::presetPath (user, "Max Vol 1", "Cold Open");
    juce::MemoryBlock before; cold.loadFileAsData (before);
    { juce::String m; juce::MemoryBlock ch; tw::bank::unwrap (before, m, ch, err); before = ch; }
    auto* patch = new juce::DynamicObject(); patch->setProperty ("note", "mod wheel opens the filter"); patch->setProperty ("type", "Keys");
    ok = tw::bank::rewriteMeta (cold, user, juce::var (patch), err);
    juce::MemoryBlock after; cold.loadFileAsData (after);
    juce::String man2; juce::MemoryBlock ch2; tw::bank::unwrap (after, man2, ch2, err);
    const auto mv = juce::JSON::parse (man2);
    chk (ok && body (before) == body (ch2) && mv.getProperty ("note", "").toString() == "mod wheel opens the filter" && mv.getProperty ("type", "").toString() == "Keys" && (int) mv.getProperty ("fv", 0) == 1,
         "[3] A META REWRITE LEAVES EVERY OTHER BYTE OF THE CHUNK ALONE", "manifest note/type updated · chunk body identical · fv kept");

    // [4] move a preset between banks: the file moves and its bank field follows
    juce::File moved; ok = tw::bank::movePreset (cold, user, "Glasswork", moved, err);
    juce::String man3; tw::bank::readHeader (moved, man3, err);
    chk (ok && moved.getParentDirectory().getFileName() == "Glasswork" && juce::JSON::parse (man3).getProperty ("bank", "").toString() == "Glasswork" && ! cold.existsAsFile(),
         "[4] MOVING A PRESET MOVES THE FILE AND REWRITES ITS BANK", moved.getRelativePathFrom (user));

    // [5] favourites are a sidecar keyed Bank/Name
    tw::bank::setFavourite (user, "Glasswork", "Kiln", true); tw::bank::setFavourite (user, "Max Vol 1", "Slatt", true); tw::bank::setFavourite (user, "Glasswork", "Kiln", false);
    auto fv = tw::bank::readJson (tw::bank::favouritesFile (user)); auto* keys = fv.getProperty ("keys", juce::var()).getArray();
    chk (keys && keys->size() == 1 && (*keys)[0].toString() == "Max Vol 1/Slatt", "[5] FAVOURITES LIVE IN A SIDECAR, TOGGLE ON AND OFF", keys ? juce::JSON::toString (fv, true) : "no keys");

    // [6] export Glasswork as a .terrainpack, import it into a FRESH root: same presets, same bytes
    const auto pack = root.getChildFile ("Glasswork.terrainpack");
    auto* bi = new juce::DynamicObject(); bi->setProperty ("name", "Glasswork"); bi->setProperty ("author", "Elin Sato"); bi->setProperty ("version", "1");
    ok = tw::bank::exportPack (user.getChildFile ("Glasswork"), juce::var (bi), pack, err);
    const auto user2 = root.getChildFile ("Banks2"); user2.createDirectory();
    juce::File imported; ok = ok && tw::bank::importPack (pack, user2, imported, err);
    juce::MemoryBlock a, b; user.getChildFile ("Glasswork").getChildFile ("Kiln.terrain").loadFileAsData (a); imported.getChildFile ("Kiln.terrain").loadFileAsData (b);
    const int nIn = imported.isDirectory() ? imported.findChildFiles (juce::File::findFiles, false, "*.terrain").size() : 0;
    chk (ok && nIn == 2 && a == b && imported.getChildFile ("bank.json").existsAsFile(),
         "[6] EXPORT → IMPORT INTO A FRESH ROOT: 2 PRESETS, BYTE-IDENTICAL, bank.json REBUILT", err.isEmpty() ? juce::String (nIn) + " presets · Kiln identical · " + juce::String ((int) pack.getSize()) + " bytes packed" : err);
    juce::File again; const bool refused = ! tw::bank::importPack (pack, user2, again, err);
    chk (refused && err.contains ("already installed"), "[6b] IMPORTING THE SAME PACK TWICE IS REFUSED, NOT MERGED", err);

    // [7] a rename re-files every preset under the new bank name
    ok = tw::bank::renameBank (user, "Max Vol 1", "Max Vol One", err);
    juce::String man4; tw::bank::readHeader (user.getChildFile ("Max Vol One").getChildFile ("Slatt.terrain"), man4, err);
    chk (ok && juce::JSON::parse (man4).getProperty ("bank", "").toString() == "Max Vol One" && ! user.getChildFile ("Max Vol 1").exists(),
         "[7] RENAMING A BANK RENAMES THE FOLDER AND EVERY PRESET'S BANK FIELD", "Banks/Max Vol One/Slatt → bank=" + juce::JSON::parse (man4).getProperty ("bank", "").toString());

    // [8] names: the dash survives, filesystem hazards do not
    chk (tw::bank::safeName ("Terra - Glacier") == "Terra - Glacier" && tw::bank::safeName ("a/b#c..d") == "a_b_c__d" && tw::bank::safeName ("  x  ") == "x",
         "[8] safeName KEEPS 'Bank - Name' AND MAPS EVERYTHING ELSE TO _", "'Terra - Glacier' · 'a/b#c..d' → '" + tw::bank::safeName ("a/b#c..d") + "'");

    // [9] no write escapes the user root
    const auto outside = root.getChildFile ("escape.terrain");
    const bool wrote = tw::bank::writePreset (outside, user, manifestOf (c1), c1, err);
    const bool wantEscape = (mut == "escape");
    chk (wrote == wantEscape && (wantEscape || ! outside.existsAsFile()), "[9] A WRITE OUTSIDE THE USER ROOT IS REFUSED", wrote ? "WROTE " + outside.getFullPathName() : "refused: " + err);
    const auto zipSlip = root.getChildFile ("slip.terrainpack");
    { juce::ZipFile::Builder zb; const auto t = juce::File::createTempFile ("terrain"); { juce::MemoryOutputStream o; tw::bank::wrap (o, manifestOf (c1), c1); t.replaceWithData (o.getData(), o.getDataSize()); }
      zb.addFile (t, 6, "presets/../../evil.terrain"); zb.addFile (t, 6, "presets/ok.terrain"); juce::FileOutputStream o (zipSlip); zb.writeToStream (o, nullptr); o.flush(); t.deleteFile(); }
    const auto user3 = root.getChildFile ("Banks3"); user3.createDirectory(); juce::File slipDir; tw::bank::importPack (zipSlip, user3, slipDir, err);
    const bool evilLanded = root.getChildFile ("evil.terrain").existsAsFile() || user3.getChildFile ("evil.terrain").existsAsFile() || root.getParentDirectory().getChildFile ("evil.terrain").existsAsFile();
    chk (! evilLanded && slipDir.isDirectory() && slipDir.getChildFile ("ok.terrain").existsAsFile(), "[9b] A ZIP ENTRY WITH .. IS DROPPED, THE REST IMPORTS", evilLanded ? "EVIL FILE LANDED" : "evil dropped · ok.terrain imported");

    // [10] caps report — a scan past the limit says so
    { tw::bank::Caps small; small.maxPresets = 2; tw::bank::ScanStats s2; auto c = tw::bank::scan (factory, user, small, s2);
      const bool reported = c.getProperty ("caps", juce::var()).getProperty ("hitPresets", false);
      const bool want = (mut != "nocap");
      chk (reported == want, "[10] A SCAN PAST ITS CAP REPORTS IT — NEVER A SILENT TRUNCATION", juce::String ("maxPresets 2 over 3 presets · hitPresets=") + (reported ? "true" : "false")); }

    // [11] delete a preset, then a bank; nothing outside them moves
    ok = tw::bank::removePreset (user.getChildFile ("Max Vol One").getChildFile ("Slatt.terrain"), user, err) && tw::bank::removeBank (user, "Glasswork", err);
    chk (ok && ! user.getChildFile ("Glasswork").exists() && user.getChildFile ("Max Vol One").isDirectory(), "[11] DELETE A PRESET, DELETE A BANK", err.isEmpty() ? "Glasswork gone · Max Vol One stays" : err);

    root.deleteRecursively();
    std::printf ("\n  %d passed, %d FAILED\n", pass, fail);
    if (! failedBars.empty()) { std::printf ("  FAILED BARS:\n"); for (auto& b : failedBars) std::printf ("    - %s\n", b.c_str()); }
    std::printf ("  %d pass, %d fail\n", pass, fail);
    return fail ? 1 : 0;
}
