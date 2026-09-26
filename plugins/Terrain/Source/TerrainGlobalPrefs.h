#pragma once
// ══ tp103 — THE SETTINGS PAGE'S ENGINE HALF, READ WHERE THE PAGE CANNOT REACH ═════════════════════════════════
//  The Settings page keeps its whole state object (S) inside InstrumentSettings.json under "settings"
//  (index.html saveSettings → the saveSettings native). That is enough while an editor is open, but several
//  settings have to hold in a session that NEVER opens one — a DAW project reloaded and bounced, a template
//  instance nobody looked at: Sleep when silent, Cut tails on stop, the quality policy, Scan on start, the
//  window size a NEW window opens at. So the processor and the editor read the same file here, at
//  construction, and the page's own natives then keep the live values current.
//
//  And the LIBRARY LOCATIONS the page can now change (Settings → Presets & Library → Where things live) live
//  in their own small file beside it, LibraryPaths.json — a path must be readable by banksUserRoot() on any
//  thread without parsing the page's whole state, so it is cached here behind one mutex.
//
//  ⚠️ settingsFile() is the SAME path PluginEditor.cpp::terrainSettingsFile() names (userAppData/"Waves Crate"/
//  Terrain). It is repeated, not shared, for the same reason terrainDataDirP() repeats terrainDataDir(): that
//  body is sliced verbatim by a test. The two must move together.
#include <juce_core/juce_core.h>
#include <mutex>

namespace tw { namespace prefs {

inline juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            #if TERRAIN_FX
             .getChildFile ("Waves Crate").getChildFile ("Terrain FX")   // tpfx — the effect's own Settings
            #else
             .getChildFile ("Waves Crate").getChildFile ("Terrain")
            #endif
             .getChildFile ("InstrumentSettings.json");
}

/** The page's S object as last saved (an empty var when there is none). */
inline juce::var readPageSettings()
{
    const auto f = settingsFile();
    if (! f.existsAsFile()) return {};
    const auto root = juce::JSON::parse (f.loadFileAsString());
    return root.isObject() ? root["settings"] : juce::var();
}

// Playing quality: Eco / Standard / High → 0 / 1 / 2.  Bounce quality: Same / High / Best → 0 / 1 / 2.
inline int rtQualityIndex  (const juce::String& s) { return s == "Eco" ? 0 : (s == "High" ? 2 : 1); }
inline int offQualityIndex (const juce::String& s) { return s == "Same" ? 0 : (s == "Best" ? 2 : 1); }

struct EnginePrefs
{
    bool sleep = true;              // Settings → Performance → Sleep when silent   (page default On)
    bool cutTails = false;          // … Cut tails when the DAW stops                (page default Off)
    int  rtQ = 1;                   // … Playing quality                             (Standard)
    int  offQ = 1;                  // … Bounce quality                              (High)
    bool presetsMayQuality = true;  // … Presets can change quality                  (On)
    bool scanOnStart = true;        // Presets & Library → Scan on start             (On)
    int  sizePct = 0;               // Interface → Window size (0 = never chosen)
};

inline EnginePrefs readEnginePrefs()
{
    EnginePrefs p;
    const auto s = readPageSettings();
    if (! s.isObject()) return p;
    auto str = [&s] (const char* k) { return s.hasProperty (k) ? s[k].toString() : juce::String(); };
    if (str ("sleep").isNotEmpty())    p.sleep    = str ("sleep") == "On";
    if (str ("tails").isNotEmpty())    p.cutTails = str ("tails") == "On";
    if (str ("rtQ").isNotEmpty())      p.rtQ      = rtQualityIndex (str ("rtQ"));
    if (str ("offQ").isNotEmpty())     p.offQ     = offQualityIndex (str ("offQ"));
    if (str ("presetQ").isNotEmpty())  p.presetsMayQuality = str ("presetQ") == "On";
    if (str ("scan").isNotEmpty())     p.scanOnStart = str ("scan") != "Off";
    if (s.hasProperty ("sizeSet") && (bool) s["sizeSet"] && s.hasProperty ("size"))
        p.sizePct = juce::jlimit (0, 190, (int) s["size"]);
    return p;
}

// ── LIBRARY LOCATIONS ────────────────────────────────────────────────────────────────────────────────────────
enum class Lib { presets = 0, samples = 1, factory = 2 };
inline const char* libKey (Lib l) { return l == Lib::presets ? "presets" : (l == Lib::samples ? "samples" : "factory"); }

inline juce::File libraryPathsFile() { return settingsFile().getSiblingFile ("LibraryPaths.json"); }

struct LibCache { std::mutex m; bool loaded = false; juce::String path[3]; };
inline LibCache& libCache() { static LibCache c; return c; }

/** The folder the user chose for `l`, or an empty path when they never chose one. Any thread. */
inline juce::File libraryOverride (Lib l)
{
    auto& c = libCache();
    const std::lock_guard<std::mutex> g (c.m);
    if (! c.loaded)
    {
        c.loaded = true;
        const auto f = libraryPathsFile();
        if (f.existsAsFile())
        {
            const auto v = juce::JSON::parse (f.loadFileAsString());
            for (int i = 0; i < 3; ++i)
                if (v.isObject() && v.hasProperty (libKey ((Lib) i))) c.path[i] = v[libKey ((Lib) i)].toString();
        }
    }
    const auto& p = c.path[(int) l];
    return (p.isNotEmpty() && juce::File::isAbsolutePath (p)) ? juce::File (p) : juce::File();
}

/** Remember `dir` for `l` (an empty File = back to the default). Message thread. Returns false if the file
    could not be written — the choice then only holds until the plugin is unloaded. */
inline bool setLibraryOverride (Lib l, const juce::File& dir)
{
    (void) libraryOverride (l);   // load what is there first, so the other two entries survive the write
    auto& c = libCache();
    const std::lock_guard<std::mutex> g (c.m);
    c.path[(int) l] = dir == juce::File() ? juce::String() : dir.getFullPathName();
    auto* o = new juce::DynamicObject();
    for (int i = 0; i < 3; ++i) if (c.path[i].isNotEmpty()) o->setProperty (libKey ((Lib) i), c.path[i]);
    const auto f = libraryPathsFile();
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (juce::var (o), false));
}

}} // namespace tw::prefs
