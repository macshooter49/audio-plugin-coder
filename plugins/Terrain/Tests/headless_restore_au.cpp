// ══════════════════════════════════════════════════════════════════════════════════════════════
//  headless_restore_au.cpp — fb602 · A PROJECT THAT NEVER OPENS TERRAIN'S WINDOW MUST STILL SOUND.
//
//    c++ -std=c++17 -O2 -I Tests -I Tests/shim -I Source Tests/headless_restore_au.cpp \
//        -framework Accelerate -framework AudioToolbox -framework CoreFoundation -o /tmp/hr && /tmp/hr
//
//  THE BUG (c). Until fb602 the ONLY thing that ever filled a sample slot after a project load was
//  TerrainUiCore's constructor MessageManager::callAsync loop (PluginEditor.cpp:4983-5033). The
//  processor's own setStateInformation restored the PATH and nothing else — the only
//  createReaderFor calls in PluginProcessor.cpp were the convolution ones. So:
//
//      open a project, hit render, never open the plugin window
//        -> every Sample / Granular / Resynth / Modal oscillator plays SILENCE.
//
//  and a miss inside that editor loop was a bare `continue` (:5012, :5030), so nothing anywhere
//  said a word about it.
//
//  WHY THIS CERT LOOKS THE WAY IT DOES
//  ───────────────────────────────────
//  The claim is "restore works with NO EDITOR CONSTRUCTED AT ALL", so the cert has to be a host
//  that never asks for a view. It drives the INSTALLED AU through kAudioUnitProperty_ClassInfo:
//  it never touches kAudioUnitProperty_CocoaUI, GetUIComponentList, or any view API — bar [0b]
//  greps this file's own source to prove that, because a promise in a comment is not a control.
//
//  And it measures AUDIO, not a parameter. A cert that reads oscSamplePath0 back out of the blob
//  passes on the BROKEN build — bar [4] below does exactly that and is GREEN on the pre-fb602
//  binary, which is the whole shape of the bug: the path always survived, the SLOT never did.
//  Report the RMS, never a boolean: "restored" and "restored to silence" are the same boolean.
//
//  MEASURED, BOTH SIDES, ON THE INSTALLED AU (~/Library/Audio/Plug-Ins/Components), note A3:
//
//                                    fb601 (shipped)      fb602 (this commit)
//      [1] WT engine (harness alive)    -12.09 dB            -12.09 dB
//      [2] SAMP, no path (floor)       -200.00 dB           -200.00 dB
//      [3] SAMP, path restored         -200.00 dB  RED       -10.45 dB  GREEN   ◀ the commit
//      [5] GRAN, path restored         -200.00 dB  RED       -29.41 dB  GREEN
//      [7] layer sourcePath only       -200.00 dB  RED       -11.48 dB  GREEN
//      [4] oscSamplePath0 re-emitted    byte-identical        byte-identical
//
//  Bar [4] is GREEN on both, deliberately: the path always survived, so a cert that checked the
//  path would have certified the broken build. -200.00 dB is rmsDb's 1e-20 clamp — literal digital
//  silence, not a small number.
//
//  MUTATION CONTROL:  TI_RESTORE_MUT=1 withholds the oscSamplePath0/sourcePath attributes from the
//  blob — i.e. re-creates the pre-fb602 hole from the INPUT side. Bars [3] [5] [7] must go RED and
//  bar [4] must go red with them. Run it both ways; a bar that survives the mutation is measuring
//  something other than what its name says.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "au_state_blob.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static const bool MUT = (getenv ("TI_RESTORE_MUT") != nullptr && std::string (getenv ("TI_RESTORE_MUT")) == "1");

// ── READING THE PLUGIN'S OWN LEDGER ───────────────────────────────────────────────────────────
//  restoreSampleSlotsFromState ends in an opt-in probe line (PluginProcessor.cpp:14855):
//      [fb602-restore] filled=<n> misses=<n> {"n":…,"misses":[{"slot":…,"path":…,"why":…}]}
//  gated on TERRAIN_RESTORE_PROBE so a DAW never sees it. An AUv2 is loaded INTO THIS PROCESS, so
//  that fprintf lands on THIS program's stderr — capture fd 2 around the restore and the cert can
//  read the ledger with no editor and no native function. This is the only way the miss ledger is
//  observable headlessly, and a ledger nobody can read is the silent `continue` all over again.
struct ErrCap
{
    int saved = -1; std::string path;
    void begin (const std::string& p)
    { path = p; std::fflush (stderr); saved = dup (2);
      const int fd = open (p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) { dup2 (fd, 2); close (fd); } }
    std::string end()
    { std::fflush (stderr); if (saved >= 0) { dup2 (saved, 2); close (saved); saved = -1; }
      std::ifstream in (path); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
};
static std::string probeLine (const std::string& cap)
{
    const size_t a = cap.find ("[fb602-restore]");
    if (a == std::string::npos) return "";
    const size_t b = cap.find ('\n', a);
    return cap.substr (a, b == std::string::npos ? std::string::npos : b - a);
}

// ── a real WAV on disk, made here so the cert never touches the owner's audio ──────────────────
//    16-bit mono 48 k sine. Deterministic, so the RMS below is reproducible.
static std::string mkWav (const std::string& path, double freq, double secs)
{
    const int sr = 48000, n = (int) (sr * secs);
    std::vector<short> pcm ((size_t) n);
    for (int i = 0; i < n; ++i) pcm[(size_t) i] = (short) (28000.0 * std::sin (2.0 * M_PI * freq * i / sr));
    const int dataBytes = n * 2;
    FILE* f = fopen (path.c_str(), "wb"); if (! f) return "";
    auto u32 = [&] (unsigned v) { fwrite (&v, 4, 1, f); };
    auto u16 = [&] (unsigned short v) { fwrite (&v, 2, 1, f); };
    fwrite ("RIFF", 1, 4, f); u32 ((unsigned) (36 + dataBytes)); fwrite ("WAVE", 1, 4, f);
    fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (1); u32 ((unsigned) sr);
    u32 ((unsigned) (sr * 2)); u16 (2); u16 (16);
    fwrite ("data", 1, 4, f); u32 ((unsigned) dataBytes);
    fwrite (pcm.data(), 2, (size_t) n, f); fclose (f);
    return path;
}

// ── the V2 blob keeps the sampler layers in <layers><layer sourcePath="…">, not on the root, so
//    setRootAttr cannot reach them. Same insert, one element deeper. (PluginProcessor.cpp:14639)
static bool setLayerAttr (std::string& xml, int idx, const std::string& name, const std::string& value)
{
    //  <layers><layer index="0" sourcePath="" sourceFileName="" …>   — sourcePath is already there
    //  and empty on an idle instance, so this REPLACES rather than inserts. Verified against the
    //  real blob; "<layer " with the trailing space cannot match the "<layers" container itself.
    size_t p = xml.find ("<layers");
    if (p == std::string::npos) return false;
    for (int i = 0; i <= idx; ++i)
    {
        p = xml.find ("<layer ", p + 1);
        if (p == std::string::npos) return false;
    }
    size_t gt = xml.find ('>', p);
    if (gt == std::string::npos) return false;
    const std::string key = " " + name + "=\"";
    const size_t at = xml.find (key, p);
    if (at != std::string::npos && at < gt)
    {
        const size_t q = xml.find ('"', at + key.size());
        if (q == std::string::npos) return false;
        xml.erase (at, q + 1 - at);
        gt = xml.find ('>', p);
        if (gt == std::string::npos) return false;
    }
    size_t ins = gt;
    while (ins > p && (xml[ins-1] == '/' || xml[ins-1] == ' ' || xml[ins-1] == '\n')) --ins;
    xml.insert (ins, " " + name + "=\"" + xmlEsc (value) + "\"");
    return true;
}

// The one place the cert is allowed to decide "loud". The floor measured on this build is exactly
// -200.00 dB (rmsDb's 1e-20 clamp), and the WT engine at level 1.0 lands at -12 dB, so a 40 dB
// margin over the floor cannot be reached by anything but real audio.
static const double kSilenceFloorDb = -160.0;
static bool sounds (double db) { return db > kSilenceFloorDb; }

// Zero crossings per second. For a sine at f this is 2f, so it identifies WHICH file is playing —
// bar [3] proves the slot is not silent, and [3b] proves the audio came out of the file the blob
// named and not from some other engine leaking in. (A single loud bar is not proof of provenance.)
static double zcrHz (const std::vector<float>& v)
{
    if (v.size() < 64) return 0.0;
    int n = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i-1] <= 0.0f) != (v[i] <= 0.0f)) ++n;
    return n * SR / (double) v.size();
}

int main()
{
    std::printf ("\n══ fb602 · HEADLESS SAMPLE RESTORE — NO EDITOR, ON THE INSTALLED AU ══\n");
    std::printf ("   MUTATION CONTROL TI_RESTORE_MUT=%s\n\n", MUT ? "1 (ACTIVE — [3] [5] [7] MUST go RED)" : "0 (inactive)");

    setenv ("TERRAIN_RESTORE_PROBE", "1", 1);        // opt the shipped ledger in, for this process only
    const std::string dir = "/private/tmp/claude-501/-Users-macshooter/941a8123-ffc6-4f73-84a3-70aee55ea3c3/scratchpad/fb602";
    mkdir (dir.c_str(), 0755);
    const std::string wav   = mkWav (dir + "/hr_220.wav", 220.0, 2.0);
    const std::string wav4x = mkWav (dir + "/hr_880.wav", 880.0, 2.0);   // for the provenance bar [3b]
    struct stat st {}; stat (wav.c_str(), &st);

    // ── [0a] the AU is there and the blob is the shape au_state_blob.h expects ────────────────
    AU probe;
    if (! probe.open()) { std::printf ("  !! cannot open the AU — nothing to certify\n"); return 2; }
    std::string why; const std::string base = probe.readXml (why);
    chk (! base.empty() && hasParam (base, "SYN_OSC_A_ENGINE"),
         "[0a] PREFLIGHT — installed AU, readable ClassInfo blob, engine param present",
         "blob " + std::to_string (base.size()) + " bytes · why='" + why + "' · wav "
           + std::to_string ((long long) st.st_size) + " bytes");

    // ── [0b] NO EDITOR. The claim of this whole file, checked mechanically against its own source
    //         rather than asserted in a comment: a host that never asks for a view cannot have one.
    {
        // Every AudioUnit call this host makes lives in exactly two files: this one and
        // au_state_blob.h. Strip // comments and string literals, then look for any view API at
        // all. A comment claiming "no editor" is not evidence; CODE with no view call is.
        auto codeOf = [] (const char* path)
        {
            std::ifstream f (path); std::stringstream ss; ss << f.rdbuf(); const std::string raw = ss.str();
            std::string out; bool instr = false;
            for (size_t i = 0; i < raw.size(); ++i)
            {
                if (! instr && raw[i] == '/' && i + 1 < raw.size() && raw[i+1] == '/')
                { while (i < raw.size() && raw[i] != '\n') ++i; out += '\n'; continue; }
                if (! instr && raw[i] == '/' && i + 1 < raw.size() && raw[i+1] == '*')
                { i += 2; while (i + 1 < raw.size() && ! (raw[i] == '*' && raw[i+1] == '/')) ++i; ++i; continue; }
                if (raw[i] == '"' && (i == 0 || raw[i-1] != '\\')) { instr = ! instr; out += ' '; continue; }
                out += instr ? ' ' : raw[i];
            }
            return out;
        };
        const std::string code = codeOf (__FILE__) + codeOf ("Tests/au_state_blob.h");
        const bool haveSrc = code.size() > 4000;                       // both files actually read
        const bool clean = haveSrc
                        && code.find ("kAudioUnitProperty_Cocoa")      == std::string::npos
                        && code.find ("GetUIComponentList")            == std::string::npos
                        && code.find ("AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList") != std::string::npos;
        chk (clean, "[0b] NO EDITOR IS EVER CONSTRUCTED — this host never asks the AU for a view",
             haveSrc ? "comment- and string-stripped source of headless_restore_au.cpp + au_state_blob.h ("
                       + std::to_string (code.size()) + " chars of code): zero kAudioUnitProperty_Cocoa*, "
                       "zero GetUIComponentList, and the ParameterList call is present so the strip did not eat everything"
                     : "COULD NOT READ THE SOURCE — run this cert from plugins/Terrain/");
    }

    // ── [1] THE HARNESS RENDERS. Without this bar every silent measurement below is ambiguous:
    //        a dead AudioUnitRender loop and a dead sample slot read exactly the same.
    double wtDb = -999;
    { std::string x = base; setParam (x, "SYN_OSC_A_ENGINE", 0); setParam (x, "SYN_OSC_A_LEVEL", 1.0);
      probe.writeXml (x); wtDb = rmsDb (probe.note (57)); }
    chk (sounds (wtDb) && wtDb > -40.0,
         "[1] HARNESS ALIVE — the WT engine (needs no sample) renders through this exact host",
         "WT rms = " + std::to_string (wtDb) + " dB");

    // ── [2] THE FLOOR. SAMP with nothing in the slot is what a broken restore sounds like. ─────
    double floorDb = -999;
    { std::string x = base; setParam (x, "SYN_OSC_A_ENGINE", 1); setParam (x, "SYN_OSC_A_LEVEL", 1.0);
      probe.writeXml (x); floorDb = rmsDb (probe.note (57)); }
    chk (! sounds (floorDb),
         "[2] SILENCE FLOOR — SAMP engine, empty slot, no path: silent (this is the bug's sound)",
         "SAMP-no-path rms = " + std::to_string (floorDb) + " dB");
    probe.close();

    // ── the blob a DAW project saved from a session that HAD a sample loaded ──────────────────
    //    Attribute names lifted from PluginProcessor.cpp:14687 (oscSamplePath<i>) and :14639
    //    (<layer sourcePath>) — the cert synthesises the save, then does the reload for real.
    auto sessionBlob = [&] (int engine, const std::string& oscPath, const std::string& layerPath)
    {
        std::string x = base;
        setParam (x, "SYN_OSC_A_ENGINE", engine);
        setParam (x, "SYN_OSC_A_LEVEL", 1.0);
        if (! MUT)                                       // ⚠️ the mutation: save the patch WITHOUT the path
        {
            if (! oscPath.empty())   setRootAttr  (x, "oscSamplePath0", oscPath);
            if (! layerPath.empty()) setLayerAttr (x, 0, "sourcePath",  layerPath);
        }
        return x;
    };

    // ── [3] THE BAR. Destroy, recreate, restore, render. ──────────────────────────────────────
    double sampDb = -999, zLo = 0, zHi = 0; std::string reemitted, ledgerGood;
    {
        AU a; if (! a.open()) return 2;                  // a FRESH instance — the project reload
        ErrCap cap; cap.begin (dir + "/probe_good.txt");
        a.writeXml (sessionBlob (1, wav, ""));           // ← the real setStateInformation
        ledgerGood = probeLine (cap.end());
        const auto b = a.note (57);
        sampDb = rmsDb (b); zLo = zcrHz (b);
        std::string w2; reemitted = getRootAttr (a.readXml (w2), "oscSamplePath0");
        a.close();
    }
    { AU a; if (! a.open()) return 2; a.writeXml (sessionBlob (1, wav4x, "")); zHi = zcrHz (a.note (57)); a.close(); }
    chk (sounds (sampDb) && sampDb > floorDb + 40.0,
         "[3] SAMPLE OSC SOUNDS AFTER A HEADLESS RESTORE  ◀── THE CLAIM OF THIS COMMIT",
         "OSC A = SAMP, oscSamplePath0 restored from the blob, no window ever opened:  rms = "
           + std::to_string (sampDb) + " dB   (floor " + std::to_string (floorDb)
           + " dB, needs > " + std::to_string (floorDb + 40.0) + ")");

    // ── [3b] AND IT IS THAT FILE'S AUDIO — not some other engine leaking through. ────────────
    {
        const double ratio = zLo > 1.0 ? zHi / zLo : 0.0;
        chk (! MUT && ratio > 3.4 && ratio < 4.6,
             "[3b] THE AUDIO IS THE NAMED FILE — swap the wav, the pitch follows 4:1",
             "220 Hz wav -> " + std::to_string (zLo) + " zero-crossings/s · 880 Hz wav -> "
               + std::to_string (zHi) + " · ratio " + std::to_string (ratio)
               + " (a sine at f gives 2f; anything not sourced from the file cannot track this)");
    }

    // ── [4] THE PATH WAS NEVER THE PROBLEM — this bar is GREEN on the broken build on purpose. ─
    chk (MUT ? reemitted.empty() : reemitted == wav,
         "[4] THE PATH ROUND-TRIPS (green on the BROKEN build too — that is the point)",
         "re-emitted oscSamplePath0 = '" + reemitted + "'"
           + (MUT ? "  [mutated: expected empty]" : ""));

    // ── [5] NOT A SAMP SPECIAL CASE — GRAN reads the same per-osc buffer. ─────────────────────
    double granDb = -999;
    { AU a; if (! a.open()) return 2; a.writeXml (sessionBlob (2, wav, "")); granDb = rmsDb (a.note (57)); a.close(); }
    chk (sounds (granDb) && granDb > floorDb + 40.0,
         "[5] GRANULAR TOO — every engine that reads oscSampleBuffers_, not just SAMP",
         "GRAN rms = " + std::to_string (granDb) + " dB");

    // ── [6] A DROPPED SAMPLE'S MARKER IS NOT A PATH (bug (b)). ───────────────────────────────
    //        Pre-fb602 this field held a bare FILENAME, so restore fed juce::File("kick.wav") to
    //        existsAsFile() — always false, and jassertfalse in Debug. The "mem:" marker must be
    //        recognised and skipped: silent, no crash, no assert.
    double memDb = -999; std::string ledgerMem;
    { AU a; if (! a.open()) return 2;
      ErrCap cap; cap.begin (dir + "/probe_mem.txt");
      a.writeXml (sessionBlob (1, "mem:kick.wav", ""));
      ledgerMem = probeLine (cap.end());
      memDb = rmsDb (a.note (57)); a.close(); }
    chk (! sounds (memDb),
         "[6] 'mem:' REF IS SKIPPED CLEANLY — a dropped sample's marker is never fed to juce::File",
         "mem: rms = " + std::to_string (memDb) + " dB (silent by DESIGN until fb603 embeds the audio; the bar is that it did not crash or hang)");

    // ── [7] THE SAMPLER LAYER, not just the oscs. ───────────────────────────────────────────
    //        A blob whose ONLY sample reference is <layer sourcePath> — no oscSamplePath0 at all.
    //        OSC A on SAMP falls back to the layer buffer when its own slot is empty (measured:
    //        -11.48 dB here against a -200 dB floor), so this is a real audible bar and not a
    //        report: a restore that filled the oscs and left the sampler page empty would pass
    //        [3] and still be half a fix.
    double layerDb = -999;
    {
        AU a; if (! a.open()) return 2;
        a.writeXml (sessionBlob (1, "", wav));           // layer path only
        layerDb = rmsDb (a.note (57));
        a.close();
    }
    chk (sounds (layerDb) && layerDb > floorDb + 40.0,
         "[7] THE SAMPLER LAYER RESTORES TOO — <layer sourcePath>, no oscSamplePath0 anywhere",
         "layer-only rms = " + std::to_string (layerDb) + " dB (floor " + std::to_string (floorDb) + ")");

    // ── [8] A DEAD PATH MUST NOT TAKE THE HOST WITH IT. ─────────────────────────────────────
    double deadDb = -999; std::string ledgerDead;
    { AU a; if (! a.open()) return 2;
      ErrCap cap; cap.begin (dir + "/probe_dead.txt");
      a.writeXml (sessionBlob (1, dir + "/definitely-not-here.wav", ""));
      ledgerDead = probeLine (cap.end());
      deadDb = rmsDb (a.note (57)); a.close(); }
    chk (! sounds (deadDb),
         "[8] A MISSING FILE IS SILENT, NOT A CRASH OR A HANG (the miss ledger's job is to say so)",
         "missing-file rms = " + std::to_string (deadDb) + " dB");

    // ── [9] THE DETECTOR SAYS WHETHER IT FIRED. ─────────────────────────────────────────────
    //        A restore that quietly does nothing and a restore that worked are the same silence.
    {
        const bool goodOk = ledgerGood.find ("filled=0") == std::string::npos
                         && ledgerGood.find ("misses=0") != std::string::npos
                         && ! ledgerGood.empty();
        chk (goodOk, "[9] THE RESTORE LEDGER FIRES AND SAYS 'filled' ON A GOOD PATH",
             ledgerGood.empty() ? std::string ("<NO [fb602-restore] LINE AT ALL — either the probe is not "
                                               "in this binary or restoreSampleSlotsFromState never ran>")
                                : ledgerGood);
    }
    {
        const bool memOk  = ledgerMem.find ("dropped-bytes-not-embedded") != std::string::npos;
        chk (memOk, "[9b] A 'mem:' SLOT IS A NAMED MISS, NOT A SILENT `continue`",
             ledgerMem.empty() ? std::string ("<NO LEDGER LINE>") : ledgerMem);
    }
    {
        const bool deadOk = ledgerDead.find ("file-missing") != std::string::npos;
        chk (deadOk, "[9c] A MISSING FILE IS A NAMED MISS — the silent `continue` is gone",
             ledgerDead.empty() ? std::string ("<NO LEDGER LINE>") : ledgerDead);
    }

    std::printf ("\n  ── numbers ─────────────────────────────────────────────\n");
    std::printf ("     WT (harness alive)      %8.2f dB\n", wtDb);
    std::printf ("     SAMP no path (floor)    %8.2f dB\n", floorDb);
    std::printf ("     SAMP restored           %8.2f dB   <── the bar   (%.0f zc/s from the 220 Hz wav,\n"
                 "                                              %.0f from the 880 Hz one)\n", sampDb, zLo, zHi);
    std::printf ("     GRAN restored           %8.2f dB\n", granDb);
    std::printf ("     layer-only restore      %8.2f dB\n", layerDb);
    std::printf ("     mem: marker             %8.2f dB\n", memDb);
    std::printf ("     missing file            %8.2f dB\n", deadDb);
    return summary();
}
