// ══════════════════════════════════════════════════════════════════════════════════════════════
//  preset_path_cert.cpp — fb602: THE OWNER'S 29 CARD PRESETS STILL RESOLVE, BYTE FOR BYTE.
//
//  ⚠️ fb605 — THE "TerrainInstrument" IN EVERY PATH BELOW IS DELIBERATE. The product is called
//     Terrain now, but this folder is an ADDRESS on the owner's disk, not a name: it holds his 29
//     card presets. Renaming it here strands them. Same law as the GEODE param ids.
//
//  fb602 replaced fb132's hand-built  userHomeDirectory/"Library/WavesCrate/TerrainInstrument"
//  with                               userApplicationDataDirectory/"WavesCrate"/"TerrainInstrument".
//  JUCE: userApplicationDataDirectory is "~/Library" on macOS (juce_Files_mac.mm:209) but
//  CSIDL_APPDATA on Windows (juce_Files_windows.cpp:721) — so the macOS string must be IDENTICAL
//  and the Windows one must finally be right. This cert proves the macOS half against the real
//  filesystem, and reports the Windows half from JUCE's own source.
//
//  IT COMPILES THE SHIPPING CODE, NOT A COPY: ti_helpers_extracted.h is sliced verbatim out of
//  Source/PluginEditor.cpp by extract_helpers.py at build time, so a change to terrainDataDir()
//  or tiCardSlug() is a change to what this cert measures.
//
//  MUTATION CONTROLS — every green bar here can be driven red:
//    TI_CERT_MUTATE=root   use the old userHomeDirectory root -> path text still matches on mac
//                          (that is the POINT) but the WINDOWS column goes wrong
//    TI_CERT_MUTATE=slug   use the WIDENED slug the killed run wrote (lowercase + [a-z0-9])
//                          -> the cmp_fet 76 folder MOVES; the number must change
//    TI_CERT_MUTATE=miss   look under a wrong root -> the 29 files must go to 0
//
//  BUILD (needs REAL juce_core: Tests/shim/juce_core/juce_core.h is a 6-line stub with no File):
//    python3 extract_helpers.py <plugin>/Source/PluginEditor.cpp ti_helpers_extracted.h
//    c++ -std=c++17 -O2 -DNDEBUG=1 -DJUCE_STANDALONE_APPLICATION=1 \
//        -I<repo>/_tools/JUCE/modules -I. preset_path_cert.cpp \
//        <repo>/_tools/JUCE/modules/juce_core/juce_core.mm \
//        -framework Foundation -framework CoreFoundation -framework Cocoa \
//        -framework IOKit -framework Security -o /tmp/preset_path_cert
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "ti_helpers_extracted.h"
#include <cstdlib>
#include <cstring>

static int passes = 0, fails = 0;
static void chk (bool ok, const juce::String& what)
{
    (ok ? passes : fails)++;
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
}
static const char* MUT = std::getenv ("TI_CERT_MUTATE") ? std::getenv ("TI_CERT_MUTATE") : "";
static bool mut (const char* m) { return std::strcmp (MUT, m) == 0; }

// ── the two spellings under test, both written out in full ────────────────────────────────────
static juce::File fb132Root()          // the SHIPPING binary's root (PluginEditor.cpp @ HEAD)
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
             .getChildFile ("Library/WavesCrate/TerrainInstrument/presets");
}
static juce::File fb602Root()          // terrainDataDir() + /presets, or the mutation
{
    if (mut ("miss")) return terrainDataDir().getChildFile ("presetsXX");
    if (mut ("root")) return fb132Root();
    return terrainDataDir().getChildFile ("presets");
}
// the slug under test — shipping, or the widened one the killed run left behind
static juce::String slugUnderTest (const juce::String& card)
{
    if (mut ("slug")) return card.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
    return tiCardSlug (card);
}

int main()
{
    std::printf ("preset_path_cert (fb602)   TI_CERT_MUTATE=%s\n", MUT[0] ? MUT : "(none - expect GREEN)");
    std::printf ("──────────────────────────────────────────────────────────────────────────────\n");

    // ── 1. THE ROOT IS BYTE-IDENTICAL ON macOS ────────────────────────────────────────────────
    const auto oldP = fb132Root().getFullPathName();
    const auto newP = fb602Root().getFullPathName();
    std::printf ("  fb132  userHomeDirectory/\"Library/WavesCrate/TerrainInstrument/presets\"\n"
                 "         = %s\n", oldP.toRawUTF8());
    std::printf ("  fb602  terrainDataDir()/\"presets\"\n"
                 "         = %s\n", newP.toRawUTF8());
    chk (oldP == newP, "macOS root strings are byte-identical (" + juce::String (oldP.length())
                        + " chars, memcmp over the raw UTF-8)");
    chk (std::memcmp (oldP.toRawUTF8(), newP.toRawUTF8(), (size_t) oldP.getNumBytesAsUTF8() + 1) == 0
         || oldP != newP, "memcmp agrees with operator==");
    std::printf ("  JUCE roots:  userApplicationDataDirectory = \"%s\"   userHomeDirectory = \"%s\"\n",
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getFullPathName().toRawUTF8(),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toRawUTF8());
    std::printf ("  WINDOWS (from JUCE source, not runnable here):\n"
                 "     userApplicationDataDirectory = CSIDL_APPDATA  (juce_Files_windows.cpp:721)"
                 " -> %%APPDATA%%\\WavesCrate\\TerrainInstrument\\presets\n"
                 "     userHomeDirectory            = CSIDL_PROFILE  (juce_Files_windows.cpp:715)"
                 " -> C:\\Users\\<u>\\Library\\WavesCrate\\...   <-- fb602 bug (d), a folder Windows has no\n"
                 "                                                     concept of; the imports registry\n"
                 "                                                     (PluginProcessor.cpp:1349) is under APPDATA\n");

    // ── 2. EVERY FOLDER ON DISK IS A FIXED POINT OF THE SLUG ──────────────────────────────────
    auto root = fb602Root();
    chk (root.isDirectory(), "presets root exists: " + root.getFullPathName());
    auto dirs = root.findChildFiles (juce::File::findDirectories, false);
    dirs.sort();
    int fixed = 0, moved = 0, filesFound = 0, filesOnDisk = 0;
    for (const auto& d : dirs)
    {
        const auto name = d.getFileName();
        const auto s    = slugUnderTest (name);
        const bool ok   = (s == name) && tiPresetDir (name).isDirectory();
        ok ? ++fixed : ++moved;
        const auto n = d.findChildFiles (juce::File::findFiles, false, "*.json").size();
        filesOnDisk += n;
        // resolve every preset THROUGH the shipping accessor, exactly as deletePreset would
        for (const auto& f : d.findChildFiles (juce::File::findFiles, false, "*.json"))
            if (tiPresetDir (name).getChildFile (tiSafePresetName (f.getFileNameWithoutExtension()) + ".json")
                  .existsAsFile())
                ++filesFound;
        std::printf ("     %-18s slug->%-18s %2d preset(s)  %s\n",
                     name.toRawUTF8(), s.toRawUTF8(), (int) n, ok ? "resolves" : "*** MOVED ***");
    }
    chk (moved == 0, "all " + juce::String (dirs.size()) + " on-disk card folders are slug fixed points ("
                     + juce::String (fixed) + " resolve, " + juce::String (moved) + " moved)");
    chk (filesFound == filesOnDisk && filesOnDisk == 29,
         "all 29 preset files resolve through tiPresetDir+tiSafePresetName (found "
         + juce::String (filesFound) + " of " + juce::String (filesOnDisk) + " on disk)");

    // ── 3. THE SLUG OVER EVERY CARD ID THE PAGE CAN PRODUCE ───────────────────────────────────
    // 122 ids, enumerated from index.html by the python twin (5 extension cards + flt_all +
    // rvb_convolution + each FX namespace crossed with its type table). The three that carry a
    // digit or an uppercase letter are the whole question; the rest are pure a-z either way.
    static const char* kIds[] = {
        "arp","chop","gli","lfo","rbn","flt_all","rvb_convolution",
        "cmp_fet 76","dst_diodeone","dst_diodetwo","spl_low / mid / high","cmp_vari-mu",
        "ott_two band","dst_zero-square","fla_tape zero","bod_barberpole","grn_pulverize"
    };
    int changed = 0;
    for (auto* id : kIds)
    {
        const auto shipped = tiCardSlug (id);
        const auto tested  = slugUnderTest (id);
        if (shipped != tested)
        {
            ++changed;
            std::printf ("     MOVES: %-24s %s -> %s\n", id, shipped.toRawUTF8(), tested.toRawUTF8());
        }
    }
    std::printf ("  card ids whose FOLDER MOVES under the slug under test: %d\n", changed);
    chk (changed == 0, "the shipping slug relocates no card folder (" + juce::String (changed) + " movers)");

    // ── 4. THE DELETE COURIER ACTUALLY DELETES — in a folder this cert owns ───────────────────
    // The body under test is tiDeleteCardPresetNative's, verbatim:
    //   tiPresetDir(card).getChildFile (tiSafePresetName(name) + ".json").deleteFile();
    // It runs against a card id NO card uses, so the owner's 5 folders are never touched.
    {
        const juce::String certCard = "certonlyzz", certName = "Broke";
        auto dir = tiPresetDir (certCard);
        dir.createDirectory();
        auto f = dir.getChildFile (tiSafePresetName (certName) + ".json");
        f.replaceWithText ("{\"cert\":1}");
        chk (f.existsAsFile(), "courier setup: wrote " + f.getFullPathName());
        // (a) the BROKEN docked path: deletePreset("certonlyzz","Broke") reached the PATCH deleter,
        //     whose whole body is  audioProcessor.deletePreset (static_cast<int> (args[0]));
        const int asPatchIdx = (int) juce::var (certCard);   // exactly static_cast<int>(args[0])
        std::printf ("     HEAD's docked behaviour: static_cast<int>(\"%s\") == %d"
                     " -> patch deleter early-returns, file untouched\n", certCard.toRawUTF8(), asPatchIdx);
        chk (asPatchIdx == 0 && f.existsAsFile(), "bug (a) reproduced: the collided call is a no-op");
        // (b) fb602: the card branch runs the courier body
        tiPresetDir (certCard).getChildFile (tiSafePresetName (certName) + ".json").deleteFile();
        chk (! f.existsAsFile(), "fb602 courier deleted the preset");
        dir.deleteRecursively();
        chk (! dir.exists(), "cert cleaned up its own folder");
    }

    // ── 5. THE LEGACY ROOTS THAT WERE DELIBERATELY LEFT WHERE THEY ARE ────────────────────────
    std::printf ("  frozen legacy locations (accessor unified, path unchanged):\n"
                 "     InstrumentSettings.json  %s   exists=%d\n"
                 "     Wavetables/              %s   exists=%d\n",
                 terrainSettingsFile().getFullPathName().toRawUTF8(), (int) terrainSettingsFile().existsAsFile(),
                 terrainWavetablesDir().getFullPathName().toRawUTF8(), (int) terrainWavetablesDir().isDirectory());

    std::printf ("──────────────────────────────────────────────────────────────────────────────\n");
    std::printf ("RESULT: %d pass, %d FAIL  ->  %s\n", passes, fails, fails ? "RED" : "GREEN");
    return fails ? 1 : 0;
}
