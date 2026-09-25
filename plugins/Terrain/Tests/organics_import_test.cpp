// organics_import_test.cpp — tp108: the user SoundFont import, measured (built + run by Tests/organics_import_test.sh).
//
//   organics_import_test <fixtureDir>     (the dir Tests/organics_import_fixtures.py wrote; a unicode name on purpose)
//
// Bars (one PASS/FAIL line each; exit 0 only when all pass):
//   [1] SFZ import: ok, id user.<slug>, number ≥ 2048, the folder holds map.json + samples/*.flac + source/ (the .sfz and its
//       includes) + preview.flac, no .importing-* left, user-index.json lists it under "User", the library index() merges it.
//   [2] the engine: loadFromFolder works; note 60 (Sustain, the looped layers) plays 261.63 Hz ± 3 ¢ through OrganicEngine,
//       note 67 (repitched) 392 Hz ± 3 ¢, the release region sounds after note-off, the Staccato keyswitch artic plays.
//   [3] reload by id: idToIndex(id) ≥ 2048 and indexToId(back) == id; a fresh rescan + request(id) delivers the same
//       instrument (region/sample counts) — what a saved session does (the processor saves the id string).
//   [4] SF2: the generated two-zone file imports, zone A (root 55) at key 55 → 196 Hz, zone B (root 64, looped) → 329.63 Hz.
//   [5] SF2 presets: listSf2Presets names all 3; preset 2 alone imports; "Import all" makes 3 instruments with distinct ids;
//       the stereo pair became one stereo sample.
//   [6] SF3: the Ogg Vorbis samples decode (pitch right) — or, in a build without Ogg, "SF3 not supported".
//   [7] the job API: startImport → progress events (0 < pct < 100) then ONE done event with ok + id, on the message thread.
//   [8] Remove: deleteUserInstrument → folder + index entry gone, the number stays reserved in user-ids.json, a factory id refused.
//   [9] errors, each with its message and NOTHING half-written (no new folder, no .importing-*, user-index.json unchanged):
//       missing file · wrong extension · garbage .sf2 · truncated .sf2 · SFZ without regions · SFZ whose samples are all
//       missing · an #include loop · a size-guard SF2 (needConfirm, then with allowLarge → the truncation error) · an SF2
//       preset out of range · an undecodable sample.
#include "OrganicEngine.h"
#include "OrganicsLibrary.h"
#include "OrganicsImport.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace tw;
static int npass = 0, nfail = 0;
static void chk (bool ok, const std::string& what, const juce::String& d = {})
{
    std::printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (d.isNotEmpty()) std::printf ("        %s\n", d.toRawUTF8());
    ok ? ++npass : ++nfail;
    std::fflush (stdout);
}
static constexpr double kSR = 48000.0;
static constexpr double kPi = 3.14159265358979323846;

static void setEnv (const char* k, const char* v)
{
   #if defined (_WIN32)
    _putenv_s (k, v);
   #else
    setenv (k, v, 1);
   #endif
}
static void pump (double ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) ms); }

static std::shared_ptr<const OrganicInstrument> requestSync (const juce::String& id)
{
    std::shared_ptr<const OrganicInstrument> got; bool done = false;
    OrganicsLibrary::get().request (id, [&] (std::shared_ptr<const OrganicInstrument> i) { got = i; done = true; });
    for (int k = 0; k < 200 && ! done; ++k) pump (25);
    return got;
}

/** Render a note through the runtime engine: `hold` s held, then `tail` s after note-off. */
static std::vector<float> play (std::shared_ptr<const OrganicInstrument> inst, int note, float vel, double hold, double tail, int artic = 0)
{
    OrganicEngine e; e.prepare (kSR, 512); e.setInstrument (inst);
    OrganicParams p; p.human = 0.0f; p.artic = artic;
    const float det[1] = { 0.0f };
    std::vector<float> L ((size_t) ((hold + tail) * kSR), 0.0f), R (L.size(), 0.0f);
    e.noteOn (note, vel, 1, det, 1u);
    const size_t offAt = (size_t) (hold * kSR);
    for (size_t pos = 0; pos < L.size(); pos += 512)
    {
        if (pos <= offAt && offAt < pos + 512) e.noteOff (false);
        const int n = (int) std::min<size_t> (512, L.size() - pos);
        e.render (p, 0.0f, L.data() + pos, R.data() + pos, n);
    }
    e.setInstrument (nullptr);
    org::drainDeferredReleases();
    return L;
}
static double rms (const std::vector<float>& x, size_t a, size_t b)
{ double s = 0; size_t n = 0; for (size_t i = a; i < b && i < x.size(); ++i) { s += (double) x[i] * x[i]; ++n; } return n ? std::sqrt (s / (double) n) : 0.0; }
static double dbOf (double r) { return 20.0 * std::log10 (std::max (1e-12, r)); }
/** The strongest frequency within ±60 ¢ of nominal (Hann Goertzel scan) → cents error. */
static double centsOff (const std::vector<float>& x, size_t a, size_t n, double nominal)
{
    if (a + n > x.size()) n = x.size() > a ? x.size() - a : 0;
    if (n < 4096) return 1e9;
    double best = 0, bestF = nominal;
    for (double c = -60.0; c <= 60.0; c += 0.25)
    {
        const double f = nominal * std::pow (2.0, c / 1200.0), w = 2.0 * kPi * f / kSR;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (n - 1));
            re += x[a + i] * h * std::cos (w * (double) i); im -= x[a + i] * h * std::sin (w * (double) i);
        }
        if (re * re + im * im > best) { best = re * re + im * im; bestF = f; }
    }
    return 1200.0 * std::log2 (bestF / nominal);
}
static double mtof (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

static juce::var readJson (const juce::File& f) { return juce::JSON::parse (f.loadFileAsString()); }
static int countTemps (const juce::File& user)
{
    int n = 0;
    for (const auto& e : juce::RangedDirectoryIterator (user, false, ".*", juce::File::findFilesAndDirectories))
        if (e.getFile().getFileName().startsWith (".importing-") || e.getFile().getFileName().startsWith (".deleting-")) ++n;
    return n;
}
static juce::StringArray userDirs (const juce::File& user)
{
    juce::StringArray a;
    for (const auto& e : juce::RangedDirectoryIterator (user, false, "*", juce::File::findDirectories)) a.add (e.getFile().getFileName());
    a.sort (true);
    return a;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: organics_import_test <fixtureDir>\n"); return 2; }
    const juce::File fx (juce::String::fromUTF8 (argv[1]));
    const juce::File root = fx.getChildFile ("library");                 // the temp library root (factory index + ids copied in)
    const juce::File in = fx.getChildFile (juce::String::fromUTF8 ("Sons d\xc3\xa9j\xc3\xa0 \xe2\x9c\x93"));   // "Sons déjà ✓"
    setEnv ("TERRAIN_ORGANICS_DIR", root.getFullPathName().toRawUTF8());
    OrganicsLibrary::get().rescan();
    const auto user = root.getChildFile ("User");
    std::printf ("organics_import_test — library %s\n", root.getFullPathName().toRawUTF8());

    auto importSync = [&] (const juce::File& f, int preset = orgimport::kFirstPreset, bool allowLarge = false)
    {
        orgimport::Request r; r.source = f; r.preset = preset; r.allowLarge = allowLarge; r.root = root;
        auto res = orgimport::importSoundFont (r);
        OrganicsLibrary::get().rescan();
        return res;
    };

    // ═══ [1] SFZ ═══
    std::printf ("\n[1] SFZ import (unicode path, keyswitch, layers, loop, RR, release)\n");
    const auto sfz = in.getChildFile ("sfz-src").getChildFile ("fixture.sfz");
    const auto r1 = importSync (sfz);
    const juce::String id1 = r1.ids.isEmpty() ? juce::String() : r1.ids[0];
    chk (r1.ok && id1 == "user.fixture", "1a the SFZ imports as user.fixture", r1.ok ? id1 + (r1.warning.isNotEmpty() ? " (" + r1.warning + ")" : juce::String()) : r1.error);
    const auto d1 = user.getChildFile (id1);
    const auto m1 = readJson (d1.getChildFile ("map.json"));
    int nSmp = 0; if (auto* a = m1["samples"].getArray()) nSmp = a->size();
    bool smpOk = nSmp > 0;
    for (int i = 0; i < nSmp; ++i) smpOk = smpOk && d1.getChildFile ("samples").getChildFile (m1["samples"][i].toString()).existsAsFile();
    chk (m1["id"].toString() == id1 && m1["category"].toString() == "User" && smpOk && d1.getChildFile ("source/fixture.sfz").existsAsFile()
         && d1.getChildFile ("source/inc/defs.sfzh").existsAsFile() && d1.getChildFile ("preview.flac").existsAsFile(),
         "1b the folder: map.json (id, category User) + samples/*.flac + source/ (the .sfz + includes) + preview.flac",
         juce::String (nSmp) + " samples · artics " + juce::JSON::toString (m1["artics"], true));
    chk (m1["artics"].size() == 2 && m1["artics"][0].toString() == "Sustain" && m1["artics"][1].toString() == "Staccato",
         "1c the keyswitches became the articulations (sw_label names, note order)", juce::JSON::toString (m1["artics"], true));
    const int num1 = OrganicsLibrary::get().idToIndex (id1);
    chk (num1 >= 2048 && readJson (user.getChildFile ("user-ids.json"))[juce::Identifier (id1)].operator int() == num1,
         "1d user-ids.json numbers it from 2048", juce::String (num1));
    bool inIdx = false; juce::String cat;
    if (auto* a = OrganicsLibrary::get().index().getArray()) for (auto& e : *a) if (e["id"].toString() == id1) { inIdx = true; cat = e["category"].toString(); }
    bool factoryStill = false;
    if (auto* a = OrganicsLibrary::get().index().getArray()) for (auto& e : *a) if (e["id"].toString() == "test.sine") factoryStill = true;
    chk (inIdx && cat == "User" && factoryStill && countTemps (user) == 0, "1e index() merges it under \"User\" beside the factory entries; no temp folder left");
    chk (! root.getChildFile ("index.json").loadFileAsString().contains ("user.") && ! root.getChildFile ("ids.json").loadFileAsString().contains ("user."),
         "1f the factory index.json / ids.json were not written");

    // ═══ [2] ENGINE ═══
    std::printf ("\n[2] Through OrganicEngine\n");
    juce::String err;
    auto inst1 = OrganicInstrument::loadFromFolder (d1, &err);
    chk (inst1 != nullptr, "2a the runtime loads the imported folder", err);
    if (inst1 != nullptr)
    {
        auto y = play (inst1, 60, 0.8f, 1.2, 0.6);
        const double c60 = centsOff (y, (size_t) (0.3 * kSR), 16384, mtof (60));
        chk (std::fabs (c60) <= 3.0 && rms (y, (size_t) (0.3 * kSR), (size_t) (0.6 * kSR)) > 1e-3, "2b note 60 (Sustain) plays 261.63 Hz", juce::String (c60, 2) + " cents");
        auto y2 = play (inst1, 67, 0.8f, 1.2, 0.2);
        const double c67 = centsOff (y2, (size_t) (0.3 * kSR), 16384, mtof (67));
        chk (std::fabs (c67) <= 3.0, "2c note 67 (repitched) plays 392 Hz", juce::String (c67, 2) + " cents");
        // the loop: still sounding at 1.1 s (the source files are 0.8 s long)
        const double late = rms (y, (size_t) (1.0 * kSR), (size_t) (1.15 * kSR)), early = rms (y, (size_t) (0.3 * kSR), (size_t) (0.45 * kSR));
        chk (late > 0.3 * early, "2d the authored loop sustains past the end of the recording", juce::String (dbOf (late) - dbOf (early), 1) + " dB at 1.0 s vs 0.3 s");
        auto ys = play (inst1, 60, 0.8f, 0.4, 0.8, 1);
        chk (rms (ys, (size_t) (0.1 * kSR), (size_t) (0.3 * kSR)) > 1e-3 && std::fabs (centsOff (ys, (size_t) (0.05 * kSR), 8192, mtof (60))) <= 12.0,
             "2e the Staccato keyswitch articulation plays (tune ±10 ¢ per RR take)");
        chk (inst1->hasRelease && rms (y, (size_t) (1.25 * kSR), (size_t) (1.45 * kSR)) > 1e-4, "2f the release region sounds after note-off",
             juce::String (dbOf (rms (y, (size_t) (1.25 * kSR), (size_t) (1.45 * kSR))), 1) + " dBFS");
    }

    // ═══ [3] RELOAD BY ID ═══
    std::printf ("\n[3] Reload by id (what a saved session does)\n");
    {
        const auto back = OrganicsLibrary::get().indexToId (num1);
        OrganicsLibrary::get().rescan();
        auto got = requestSync (id1);
        chk (back == id1 && got != nullptr && inst1 != nullptr && got->regions.size() == inst1->regions.size() && got->samples.size() == inst1->samples.size(),
             "3a indexToId(idToIndex(id)) == id and request(id) after a rescan delivers the instrument",
             back + " · " + juce::String (got ? (int) got->regions.size() : -1) + " regions");
        got = nullptr;
    }

    // ═══ [4] SF2 ═══
    std::printf ("\n[4] SF2 (generated: two zones, one looped, attenuation + fine tune)\n");
    const auto r4 = importSync (in.getChildFile ("t.sf2"));
    const juce::String id4 = r4.ids.isEmpty() ? juce::String() : r4.ids[0];
    chk (r4.ok && id4 == "user.t", "4a the SF2 imports", r4.ok ? id4 : r4.error);
    if (auto i4 = OrganicInstrument::loadFromFolder (user.getChildFile (id4), &err))
    {
        auto a = play (i4, 55, 0.8f, 0.5, 0.1), b = play (i4, 64, 0.8f, 1.2, 0.1);
        const double ca = centsOff (a, (size_t) (0.05 * kSR), 8192, mtof (55)), cb = centsOff (b, (size_t) (0.3 * kSR), 16384, mtof (64));
        chk (std::fabs (ca) <= 3.0 && std::fabs (cb) <= 3.0, "4b zone A plays 196 Hz, zone B 329.63 Hz", juce::String (ca, 2) + " / " + juce::String (cb, 2) + " cents");
        chk (rms (b, (size_t) (1.0 * kSR), (size_t) (1.15 * kSR)) > 1e-3, "4c zone B's loop holds past its 0.8 s recording");
    }
    else chk (false, "4b the SF2 instrument loads", err);

    // ═══ [5] PRESETS ═══
    std::printf ("\n[5] SF2 with 3 presets: the list, one preset, Import all\n");
    {
        const auto multi = in.getChildFile ("multi.sf2");
        const auto names = orgimport::listSf2Presets (multi, &err);
        chk (names.size() == 3 && names[0] == "Soft Piano" && names[2] == "Choir Pad", "5a listSf2Presets", names.joinIntoString (" | ") + err);
        const auto one = importSync (multi, 1);
        chk (one.ok && one.ids.size() == 1 && one.names[0] == "Bright Strings", "5b preset 2 alone imports (named after the preset)",
             one.ok ? one.ids.joinIntoString (",") : one.error);
        const auto all = importSync (multi, orgimport::kAllPresets);
        chk (all.ok && all.ids.size() == 3 && all.ids[1] == "user.bright-strings-2", "5c Import all: 3 instruments, ids unique (-2 on a clash)",
             all.ok ? all.ids.joinIntoString (",") : all.error);
        auto mStr = readJson (user.getChildFile (all.ids.size() > 1 ? all.ids[1] : "x").getChildFile ("map.json"));
        auto iStr = all.ids.size() > 1 ? OrganicInstrument::loadFromFolder (user.getChildFile (all.ids[1])) : nullptr;
        chk (iStr != nullptr && iStr->samples.size() == 1 && iStr->samples[0].channels == 2, "5d the linked stereo pair became one stereo sample",
             iStr ? juce::String ((int) iStr->samples.size()) + " samples, " + juce::String (iStr->samples.empty() ? 0 : iStr->samples[0].channels) + " ch" : juce::String ("no instrument"));
        juce::String fams;
        for (auto& id : all.ids) if (auto* a = OrganicsLibrary::get().index().getArray()) for (auto& e : *a) if (e["id"].toString() == id) fams << e["family"].toString() << " ";
        chk (fams.trim() == "grand violin choir", "5e family guessed from the name / GM program", fams);
    }

    // ═══ [6] SF3 ═══
    std::printf ("\n[6] SF3 (Ogg Vorbis samples)\n");
    {
        const auto sf3 = in.getChildFile ("t.sf3");
        if (! sf3.existsAsFile()) chk (true, "6 (no SF3 fixture: this Python has no Ogg writer) — skipped");
        else
        {
            const auto r6 = importSync (sf3);
           #if JUCE_USE_OGGVORBIS
            auto i6 = r6.ok ? OrganicInstrument::loadFromFolder (user.getChildFile (r6.ids[0])) : nullptr;
            double c = 1e9;
            if (i6) { auto y = play (i6, 64, 0.8f, 0.6, 0.1); c = centsOff (y, (size_t) (0.1 * kSR), 16384, mtof (64)); }
            chk (r6.ok && i6 != nullptr && std::fabs (c) <= 5.0, "6 the SF3 imports and plays 329.63 Hz", r6.ok ? juce::String (c, 2) + " cents" : r6.error);
           #else
            chk (! r6.ok && r6.error.contains ("SF3 not supported"), "6 SF3 not supported (no Ogg decoder in this build)", r6.error);
           #endif
        }
    }

    // ═══ [7] JOBS ═══
    std::printf ("\n[7] The background job API\n");
    {
        orgimport::Request rq; rq.source = sfz; rq.root = root;
        int progress = 0, doneEv = 0; bool onMsg = true; orgimport::JobEvent last;
        const int job = orgimport::startImport (rq, [&] (const orgimport::JobEvent& ev)
        {
            onMsg = onMsg && juce::MessageManager::getInstance()->isThisTheMessageThread();
            if (ev.done) { ++doneEv; last = ev; } else if (ev.pct > 0.0f && ev.pct < 100.0f) ++progress;
        });
        for (int k = 0; k < 400 && doneEv == 0; ++k) pump (25);
        pump (200);
        bool inIndex = false;
        if (last.result.ok) if (auto* a = OrganicsLibrary::get().index().getArray()) for (auto& e : *a) if (e["id"].toString() == last.result.ids[0]) inIndex = true;
        chk (job > 0 && progress >= 1 && doneEv == 1 && last.job == job && last.result.ok && last.result.ids[0] == "user.fixture-2" && onMsg && inIndex,
             "7 startImport: progress, then one done event (ok, id user.fixture-2) on the message thread, already in index()",
             "progress events " + juce::String (progress) + " · done " + juce::String (doneEv) + " · " + (last.result.ok ? last.result.ids.joinIntoString (",") : last.result.error));
    }

    // ═══ [8] REMOVE ═══
    std::printf ("\n[8] Remove\n");
    {
        const juce::String victim = "user.fixture-2";
        const int num = OrganicsLibrary::get().idToIndex (victim);
        const bool ok = orgimport::deleteUserInstrument (root, victim, &err);
        OrganicsLibrary::get().rescan();
        bool still = false; if (auto* a = OrganicsLibrary::get().index().getArray()) for (auto& e : *a) if (e["id"].toString() == victim) still = true;
        chk (ok && ! user.getChildFile (victim).exists() && ! still && (int) readJson (user.getChildFile ("user-ids.json"))[juce::Identifier (victim)] == num && countTemps (user) == 0,
             "8a Remove: the folder and the index entry go, the number stays reserved", err);
        juce::String e2;
        chk (! orgimport::deleteUserInstrument (root, "test.sine", &e2) && root.getChildFile ("test.sine").isDirectory() && e2.contains ("Not a user instrument"),
             "8b a factory id is refused", e2);
    }

    // ═══ [9] ERRORS ═══
    std::printf ("\n[9] Errors: the message, and nothing half-written\n");
    {
        struct Case { const char* file; const char* expect; int preset; bool allowLarge; bool wantConfirm; };
        const Case cases[] = {
            { "nope.sfz",          "File not found",                orgimport::kFirstPreset, false, false },
            { "bad.wav",           "is not an .sfz, .sf2 or .sf3",  orgimport::kFirstPreset, false, false },
            { "garbage.sf2",       "is not a SoundFont",            orgimport::kFirstPreset, false, false },
            { "truncated.sf2",     "truncated",                     orgimport::kFirstPreset, false, false },
            { "noregions.sfz",     "No <region>",                   orgimport::kFirstPreset, false, false },
            { "missing.sfz",       "samples are missing",           orgimport::kFirstPreset, false, false },
            { "loop.sfz",          "#include nesting is too deep",  orgimport::kFirstPreset, false, false },
            { "huge.sf2",          "Import anyway?",                orgimport::kFirstPreset, false, true  },
            { "huge.sf2",          "truncated",                     orgimport::kFirstPreset, true,  false },
            { "t.sf2",             "is not in",                     7,                       false, false },
            { "undecodable.sfz",   "Cannot decode",                 orgimport::kFirstPreset, false, false },
        };
        for (auto& c : cases)
        {
            const auto dirsBefore = userDirs (user);
            const auto idxBefore = user.getChildFile ("user-index.json").loadFileAsString();
            const auto res = importSync (in.getChildFile (c.file), c.preset, c.allowLarge);
            const bool clean = userDirs (user) == dirsBefore && countTemps (user) == 0 && user.getChildFile ("user-index.json").loadFileAsString() == idxBefore;
            chk (! res.ok && res.error.contains (c.expect) && res.needConfirm == c.wantConfirm && clean,
                 std::string ("9 ") + c.file + (c.allowLarge ? " (allowLarge)" : "") + ": \"" + c.expect + "\"" + (c.wantConfirm ? " + needConfirm" : ""),
                 res.error + (res.needConfirm ? " [needConfirm, " + juce::String (res.mb, 0) + " MB]" : juce::String()) + (clean ? "" : "  << LEFT FILES BEHIND"));
        }
    }

    std::printf ("\n%d passed, %d failed\n", npass, nfail);
    orgimport::cancelAll();
    return nfail == 0 ? 0 : 1;
}
