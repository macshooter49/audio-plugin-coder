// ══════════════════════════════════════════════════════════════════════════════════════════════
//  noise_restore_au.cpp — fb602 · THE NOISE MODULE'S SAMPLE COMES BACK WITHOUT THE WINDOW.
//
//    c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source Tests/noise_restore_au.cpp \
//        -framework Accelerate -framework AudioToolbox -framework CoreFoundation -o /tmp/nr && /tmp/nr
//
//  The third slot in bug (c), and the one with the sharpest edge, because the noise selection was
//  never restored by ANY native code at all: noiseSampleSelJson_ was read into the member by
//  setStateInformation and then sat there until index.html's restoreNoiseSel() ran on GUI OPEN
//  (index.html:37333 — it calls getNoiseSampleSel and re-issues loadNoiseFactory / loadNoiseSample
//  from JavaScript). No window, no JavaScript, no noise sample: the module fell back to the
//  algorithmic type and the patch played a different sound.
//
//  Both descriptor kinds are certified, because they fail differently:
//    {"kind":"factory", cat, file}  → a file under <userAppData>/WavesCrate/TerrainInstrument/Noise
//    {"kind":"user",   name, b64}  → the audio is already inside the patch, base64 in the blob
//
//  MUTATION CONTROL:  TI_NOISE_MUT=1 points the factory descriptor at a file that is not there.
//  Bar [2] must go RED and the restore-miss ledger must name it. A green bar that cannot go red
//  is not a gate.
//
//  THE LEDGER. Set TERRAIN_RESTORE_PROBE=1 in the environment and the processor prints one line
//  per setStateInformation to stderr — `[fb602-restore] filled=N misses=M {json}`. This cert turns
//  it on itself and PARSES it, so the "a miss must not be silent" claim is checked, not asserted.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "au_state_blob.h"
#include <sys/stat.h>
#include <dirent.h>

static const bool MUT = (getenv ("TI_NOISE_MUT") != nullptr && std::string (getenv ("TI_NOISE_MUT")) == "1");

// ── the probe line, captured out of the plugin's own stderr ────────────────────────────────────
//    The AU is loaded into THIS process, so its stderr is this process's stderr.
static std::string probePath()
{ return "/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/fb602/noise_probe.log"; }
static void probeOn()  { setenv ("TERRAIN_RESTORE_PROBE", "1", 1); freopen (probePath().c_str(), "w", stderr); }
static std::string probeTail()
{
    std::fflush (stderr);
    std::ifstream f (probePath()); std::string line, last;
    while (std::getline (f, line)) if (line.find ("[fb602-restore]") != std::string::npos) last = line;
    return last;
}

// ── find one real factory noise file, so the cert never invents audio the plugin cannot read ──
static bool findFactory (std::string& cat, std::string& file)
{
    const std::string root = std::string (getenv ("HOME")) + "/Library/WavesCrate/TerrainInstrument/Noise";
    DIR* d = opendir (root.c_str()); if (! d) return false;
    while (dirent* e = readdir (d))
    {
        if (e->d_name[0] == '.') continue;
        const std::string sub = root + "/" + e->d_name;
        struct stat st {}; if (stat (sub.c_str(), &st) != 0 || ! S_ISDIR (st.st_mode)) continue;
        DIR* d2 = opendir (sub.c_str()); if (! d2) continue;
        while (dirent* f = readdir (d2))
        {
            const std::string n (f->d_name);
            if (n.size() < 5) continue;
            const std::string ext = n.substr (n.size() - 4);
            if (ext == ".wav" || ext == ".ogg" || ext == "flac" || ext == ".aif")
            { cat = e->d_name; file = n; closedir (d2); closedir (d); return true; }
        }
        closedir (d2);
    }
    closedir (d); return false;
}

static const double kFloorDb = -160.0;
static bool sounds (double db) { return db > kFloorDb; }

int main()
{
    mkdir ("/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/fb602", 0755);
    std::printf ("\n══ fb602 · NOISE SAMPLE RESTORE — NO EDITOR, NO JAVASCRIPT ══\n");
    std::printf ("   MUTATION CONTROL TI_NOISE_MUT=%s\n\n", MUT ? "1 (ACTIVE — [2] MUST go RED)" : "0 (inactive)");

    std::string cat, file;
    const bool haveFactory = findFactory (cat, file);

    probeOn();
    AU a; if (! a.open()) { std::printf ("  !! cannot open the AU\n"); return 2; }
    std::string why; const std::string base = a.readXml (why);
    chk (! base.empty() && haveFactory,
         "[0] PREFLIGHT — AU open, blob readable, one real factory noise file on disk",
         "blob " + std::to_string (base.size()) + " B · factory = " + cat + "/" + file);

    // Silence everything but the noise module: OSC A..D level 0, noise level 1.
    auto quietPatch = [&] ()
    {
        std::string x = base;
        for (const char* id : { "SYN_OSC_A_LEVEL", "SYN_OSC_B_LEVEL", "SYN_OSC_C_LEVEL", "SYN_OSC_D_LEVEL" })
            setParam (x, id, 0.0);
        setParam (x, "SYN_NOISE_LEVEL", 1.0);
        // SYN_NOISE_ON is the module's power switch and it defaults OFF. Without it bar [1] reads
        // -200 dB and every measurement below is a comparison of two silences — which is exactly
        // how this cert first came up red, and why bar [1] exists at all.
        setParam (x, "SYN_NOISE_ON",    1.0);
        return x;
    };

    // ── [1] THE FLOOR — noise level up, NO sample selected: the algorithmic type. ─────────────
    //        This bar is what "the patch played a different sound" means, in dB.
    double algoDb = -999;
    { AU b; b.open(); b.writeXml (quietPatch()); algoDb = rmsDb (b.note (57)); b.close(); }
    chk (sounds (algoDb),
         "[1] REFERENCE — with no sample selected the module renders its ALGORITHMIC noise",
         "algorithmic rms = " + std::to_string (algoDb) + " dB (this is what a lost sample sounds like)");

    // ── [2] THE BAR — a factory selection restores its AUDIO with no window ever opened. ──────
    double factDb = -999; std::string factProbe;
    {
        std::string x = quietPatch();
        const std::string f = MUT ? "definitely-not-a-file.wav" : file;
        setRootAttr (x, "noiseSampleSel",
                     "{\"kind\":\"factory\",\"cat\":\"" + cat + "\",\"file\":\"" + f + "\",\"name\":\"cert\"}");
        AU b; b.open(); b.writeXml (x); factProbe = probeTail(); factDb = rmsDb (b.note (57)); b.close();
    }
    {
        // The buffer is a 30 s peak-normalised loop at 0.9, so it is LOUDER than the algorithmic
        // type at the same knob. Equality would be the failure: that is the fallback.
        const bool moved = sounds (factDb) && std::fabs (factDb - algoDb) > 1.0;
        chk (moved, "[2] FACTORY NOISE RESTORES ITS SAMPLE  ◀── no window, no JavaScript",
             "factory rms = " + std::to_string (factDb) + " dB vs algorithmic "
               + std::to_string (algoDb) + " dB (must differ by > 1 dB — same sound = the fallback)"
               + "\n        probe: " + (factProbe.empty() ? "<no probe line>" : factProbe));
    }

    // ── [3] THE LEDGER — a miss is not silent. Under the mutation the probe must NAME the file. ─
    chk (MUT ? (factProbe.find ("factory-file-missing") != std::string::npos)
             : (factProbe.find ("filled=1") != std::string::npos
                && factProbe.find ("misses=0") != std::string::npos),
         MUT ? "[3] THE MISS LEDGER NAMES THE LOST FILE (mutation path)"
             : "[3] THE PROBE REPORTS THE FILL (filled=1, misses=0)",
         "probe: " + (factProbe.empty() ? std::string ("<NOTHING PRINTED — the detector no-opped silently>") : factProbe));

    // ── [4] AN EMPTY SELECTION IS NOT A MISS — algorithmic noise is a legitimate patch. ───────
    {
        AU b; b.open(); b.writeXml (quietPatch());
        const std::string p = probeTail(); b.close();
        chk (p.find ("misses=0") != std::string::npos,
             "[4] NO SELECTION IS NOT A MISS — an algorithmic-noise patch reports nothing lost",
             "probe: " + (p.empty() ? std::string ("<no probe line>") : p));
    }

    a.close();
    std::printf ("\n  ── numbers ─────────────────────────────────────────────\n");
    std::printf ("     algorithmic (no sample)  %8.2f dB\n", algoDb);
    std::printf ("     factory sample restored  %8.2f dB   <── the bar\n", factDb);
    std::printf ("\n  %d passed, %d FAILED\n\n", pass, fail);
    return fail ? 1 : 0;
}
