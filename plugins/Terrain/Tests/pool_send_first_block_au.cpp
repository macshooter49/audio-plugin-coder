// ══════════════════════════════════════════════════════════════════════════════════════════════
//  pool_send_first_block_au.cpp — fb636 bugA: A POOLED SEND SOUNDS FROM THE FIRST BLOCK.
//
//    c++ -std=c++17 -O2 -I Tests Tests/pool_send_first_block_au.cpp -framework AudioToolbox \
//        -framework CoreFoundation -o /tmp/psfb && TERRAIN_AU_BUNDLE=<Terrain.component> /tmp/psfb [--sweep]
//
//  THE BUG. The render treats a pooled send as ON only once its per-voice filter pair is published
//  (SynthVoice poolOn keys on flt1), and until fb636 bugA the ONLY builder was the processor timer, one
//  tick after the audio thread's route push published the lit send (15 Hz with no editor, fb514). The
//  insert subtraction, meanwhile, removes the routed dry the moment the pill is lit. So after a preset or
//  state load, a prepareToPlay, a UI pill click or a learned CC, the first blocks played with the send
//  MISSING and — in insert mode — the routed oscillator GONE. cpu_pass_bench's loadInto had to pump
//  AFTER the first blocks to hide it (fb636: 10 of Max's 52 presets rendered silent without that).
//
//  THE HOST HERE NEVER PUMPS BETWEEN THE LOAD AND THE NOTE, which is what a DAW that loads a project and
//  plays at once looks like: the timer cannot tick inside AudioUnitRender, so whatever the plugin has not
//  built by the time the note arrives is simply not there.
//
//  THE BLOB IS SYNTHETIC, IN MEMORY ONLY: the virgin state read from a fresh instance plus ONE EAGER pooled
//  device — Utility 1 (and Widen 3, the numbered-ID shape) — ACTIVE at RANK 0 (chain slot 0), POWER on,
//  SRC_A lit, SEND off (insert). Eager on purpose: both are prepared in prepareToPlay, so the pair is the
//  ONLY thing that can be late. Reverb 2-6, granular and tape build their ENGINES on the timer as well and
//  would confound the bar; they are named out of scope in [5]. No preset is ever written.
//
//  BARS
//   [0]  the harness is alive: the virgin patch, osc A, note right after the load — blocks 1-4 > -40 dB
//   [2]  CONTROL: the same blob, warmed exactly like cpu_pass_bench's loadInto — L0. Green on BOTH builds,
//        which is what proves [1] is purely timing, not a level the device itself changes.
//   [1]  THE BUG: fresh instance, load, note, 8 blocks, NO pump. Block 1 and blocks 1-4 within 1.5 dB of
//        L0. The broken build reads far below (the insert subtraction removes osc A, the send is off).
//   [1b] Widen 3 (SYN_WID3_*: the numbered-instance ID table). Widen is TIME-VARYING — its voices roam and
//        drift on silence — so a warmed L0 carrying 12 extra blocks of history is not its level at block 1
//        (measured: -19.30 dB no-pump vs -24.15 warmed on a build where the pair IS there). It is judged the
//        way [3] is instead: the same 12 silent blocks with and without timer ticks, memcmp-equal, and the
//        no-pump run must SOUND (> -40 dB). The broken build reads about -100 dB there.
//   [3c] CONTROL for [3]: the VIRGIN patch, run A (no pumps) vs run B (pumps) — memcmp-equal on BOTH
//        builds, so no other timer-driven lazy work can be what [3] measures
//   [3]  EXACT (TERRAIN_DETERMINISTIC=1): A = load, 12 silent blocks with no pump, chord, 40 blocks;
//        B = the same with pump(0.30) after every 4 silent blocks. memcmp(A, B) == 0: the timer no longer
//        decides anything the patch plays.
//   [4]  state BEFORE AudioUnitInitialize (the pre-prepare setState + prepareToPlay paths): open without
//        initialising, load, initialise, note, 8 blocks with no pump — L0 from block 1
//   [6]  THE OTHER HALF — never AHEAD of a timer-built ENGINE. Tape 1 on insert (osc A): its engine is built by
//        the timer, and until it exists the slot passes the routed oscillator through UNPROCESSED (applyTpe). A
//        pair built at load feeds that slot dry audio for up to a tick — on Max's "Damaged Tapes" that read
//        +10.0 dB over steady state in blocks 1-4 (a build without dropSendsAwaitingEngine). So for reverb 2-6,
//        granular and tape the message side does NOT build the pair while the engine is unbuilt, and the timer
//        lands both in one tick, exactly as before bugA. This bar asserts THAT invariant: with no pump, nothing
//        reaches the engine-less slot — blocks 1-4 stay below -60 dB, the reference's own reading. A level bar
//        ("not louder than warmed") was tried first and could NOT fail: this synthetic tape (Damaged Tapes'
//        settings, SYN_TPE_DELAY 0) is near unity, -16.39 passthrough vs -17.38 dB processed.
//        Mutation: a build that prebuilds without the filter reads -16.39 dB here -> red.
//   [5]  --sweep: REPORT ONLY, read-only. Max's 52 .terrain files, blocks 1-4 of a no-pump load vs a warmed
//        one. Presets whose engines (reverb 2-6, granular, tape) are timer-built may still differ; they are
//        flagged as out of scope.
//
//  MUTATION: TERRAIN_AU_BUNDLE=<a build without bugA> turns [1] [1b] [3] [4] red; [0] [2] [3c] [6] stay green.
//            A build that prebuilds WITHOUT dropSendsAwaitingEngine turns ONLY [6] red.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "au_state_blob.h"
#include <algorithm>
#include <dirent.h>

// ── the .terrain reader, VERBATIM from Tests/cpu_pass_bench.cpp (read-only: an ifstream, nothing else) ──
static std::string slurpBin (const std::string& p) { std::ifstream f (p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static uint32_t u32 (const std::string& s, size_t at) { return (uint32_t) (uint8_t) s[at] | ((uint32_t) (uint8_t) s[at+1] << 8) | ((uint32_t) (uint8_t) s[at+2] << 16) | ((uint32_t) (uint8_t) s[at+3] << 24); }
static std::string xmlOfTerrain (const std::string& path)
{
    const std::string f = slurpBin (path);
    if (f.size() < 12 || f.compare (0, 4, "TRN1") != 0) return {};
    const uint32_t ml = u32 (f, 4); if (8 + ml + 4 > f.size()) return {};
    const uint32_t cl = u32 (f, 8 + ml); const size_t c0 = 12 + ml; if (c0 + cl > f.size() || cl < 8) return {};
    const std::string chunk = f.substr (c0, cl);
    if (chunk.compare (0, 4, "VC2!") != 0) return {};
    std::string xml = chunk.substr (8); while (! xml.empty() && xml.back() == '\0') xml.pop_back();
    return xml;
}

// ── AU::open without AudioUnitInitialize (bar [4]): the same component, format and slice size ─────────
static bool openNoInit (AU& a)
{
    AudioComponentDescription d {};
    d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
    if (const char* bundle = std::getenv ("TERRAIN_AU_BUNDLE")) { if (! sideLoad (bundle, d)) return false; }
    AudioComponent c = AudioComponentFindNext (nullptr, &d);
    if (! c || AudioComponentInstanceNew (c, &a.au) != noErr) { std::printf ("  !! instantiate failed\n"); return false; }
    AudioStreamBasicDescription f {};
    f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    AudioUnitSetProperty (a.au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
    UInt32 mx = BLK;
    AudioUnitSetProperty (a.au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
    return true;
}

static const int CHORD[4] = { 48, 55, 60, 64 };
static double blocksDb (const std::vector<float>& v, int b0, int b1)   // blocks [b0, b1), 0 = the first after note-on
{
    const size_t s = (size_t) b0 * BLK, e = std::min (v.size(), (size_t) b1 * BLK);
    if (s >= e) return -999.0;
    return rmsDb (std::vector<float> (v.begin() + (long) s, v.begin() + (long) e));
}
static std::string fmt (const char* f, double a, double b = 0, double c = 0, double d = 0)
{ char s[512]; std::snprintf (s, sizeof s, f, a, b, c, d); return s; }

// fresh instance, load, note, render — NO pump between the load and the note
static std::vector<float> runNoPump (const std::string& xml, int nblk)
{
    AU a; if (! a.open()) return {};
    if (! a.writeXml (xml)) { a.close(); return {}; }
    a.midi (0x90, 60, 100); auto v = a.render (nblk);
    a.close(); return v;
}
// fresh instance, load, warmed exactly like cpu_pass_bench's loadInto, then the same note
static std::vector<float> runWarm (const std::string& xml, int nblk, const int* notes = nullptr, int nn = 0)
{
    AU a; if (! a.open()) return {};
    if (! a.writeXml (xml)) { a.close(); return {}; }
    a.pump (1.0);
    for (int k = 0; k < 3; ++k) { a.render (4); a.pump (0.25); }
    if (notes) for (int i = 0; i < nn; ++i) a.midi (0x90, (UInt32) notes[i], 100); else a.midi (0x90, 60, 100);
    auto v = a.render (nblk);
    a.close(); return v;
}
// bar [3]: 12 silent blocks (optionally a pump after every 4), a chord, 40 blocks
static std::vector<float> runSeq (const std::string& xml, bool pumps)
{
    AU a; if (! a.open()) return {};
    if (! a.writeXml (xml)) { a.close(); return {}; }
    for (int b = 0; b < 12; b += 4) { a.render (4); if (pumps) a.pump (0.30); }
    for (int n : CHORD) a.midi (0x90, (UInt32) n, 100);
    auto v = a.render (40);
    a.close(); return v;
}
static std::string sameOrWhere (const std::vector<float>& A, const std::vector<float>& B, bool& same)
{
    same = ! A.empty() && A.size() == B.size() && std::memcmp (A.data(), B.data(), A.size() * sizeof (float)) == 0;
    if (same) return "memcmp-equal over " + std::to_string (A.size()) + " samples";
    if (A.empty() || A.size() != B.size()) return "render failed or lengths differ (" + std::to_string (A.size()) + " / " + std::to_string (B.size()) + ")";
    size_t first = 0; while (first < A.size() && std::memcmp (&A[first], &B[first], 4) == 0) ++first;
    double mx = 0; for (size_t i = 0; i < A.size(); ++i) mx = std::max (mx, (double) std::fabs (A[i] - B[i]));
    return fmt ("DIFFER — first at sample %.0f (block %.0f), max |diff| %.3g", (double) first, (double) (first / BLK), mx)
         + fmt (" · chord blocks 1-4: A %.2f dB vs B %.2f dB", blocksDb (A, 0, 4), blocksDb (B, 0, 4));
}

// every device ACTIVE in a blob, with its RANK (preflight: RANK 0 must be the lowest)
static std::vector<std::pair<std::string, double>> activeDevices (const std::string& xml)
{
    std::vector<std::pair<std::string, double>> out;
    const std::string key = "_ACTIVE\" value=\"";
    for (size_t at = xml.find (key); at != std::string::npos; at = xml.find (key, at + 1))
    {
        const size_t idq = xml.rfind ("<PARAM id=\"", at); if (idq == std::string::npos) continue;
        const std::string pfx = xml.substr (idq + 11, at + 1 - (idq + 11));   // "SYN_XXX_"
        if (std::atof (xml.c_str() + at + key.size()) > 0.5) out.push_back ({ pfx, getParam (xml, pfx + "RANK") });
    }
    return out;
}
static std::string makeBlob (const std::string& virgin, const std::string& pfx, std::string& why)
{
    std::string x = virgin;
    for (const char* s : { "ACTIVE", "RANK", "POWER", "SRC_A", "SEND" })
        if (! hasParam (x, pfx + s)) { why = "the virgin blob has no PARAM " + pfx + s; return ""; }
    setParam (x, pfx + "ACTIVE", 1.0); setParam (x, pfx + "RANK", 0.0); setParam (x, pfx + "POWER", 1.0);
    setParam (x, pfx + "SRC_A", 1.0);  setParam (x, pfx + "SEND", 0.0);
    return x;
}

// ── [5] the read-only sweep of Max's bank ───────────────────────────────────────────────────────────
static bool timerEngineClass (const std::string& xml)   // an ACTIVE pooled reverb 2-6, granular or tape: engine is timer-built
{
    for (const auto& d : activeDevices (xml))
    {
        const std::string& p = d.first;
        if (p.rfind ("SYN_GRN", 0) == 0 || p.rfind ("SYN_TPE", 0) == 0) return true;
        if (p.size() == 9 && p.rfind ("SYN_RVB", 0) == 0 && p[7] >= '2' && p[7] <= '6') return true;
    }
    return false;
}
static void sweep()
{
    const char* home = std::getenv ("HOME");
    const std::string dir = std::string (home ? home : "") + "/Library/WavesCrate/TerrainInstrument/Banks/User";
    std::vector<std::string> files;
    if (DIR* dp = opendir (dir.c_str()))
    {
        while (dirent* e = readdir (dp)) { std::string n = e->d_name; if (n.size() > 8 && n.compare (n.size() - 8, 8, ".terrain") == 0) files.push_back (dir + "/" + n); }
        closedir (dp);
    }
    std::sort (files.begin(), files.end());
    std::printf ("\n  [5] REPORT ONLY — %zu presets, read-only; blocks 1-4 of the chord, no-pump load vs warmed load\n", files.size());
    int moved = 0, movedInScope = 0;
    for (const auto& f : files)
    {
        const std::string xml = xmlOfTerrain (f);
        std::string name = f.substr (f.find_last_of ('/') + 1); name = name.substr (0, name.size() - 8);
        if (xml.empty()) { std::printf ("      %-28s UNREADABLE\n", name.c_str()); continue; }
        std::vector<float> np;
        { AU a; if (! a.open()) return; if (a.writeXml (xml)) { for (int n : CHORD) a.midi (0x90, (UInt32) n, 100); np = a.render (4); } a.close(); }
        const auto w = runWarm (xml, 4, CHORD, 4);
        const double dn = blocksDb (np, 0, 4), dw = blocksDb (w, 0, 4), dd = dn - dw;
        const bool eng = timerEngineClass (xml), mv = std::fabs (dd) > 1.5;
        if (mv) { ++moved; if (! eng) ++movedInScope; }
        std::printf ("      %-28s no-pump %8.2f dB  warmed %8.2f dB  delta %+7.2f dB%s%s\n", name.c_str(), dn, dw, dd,
                     mv ? "  <- MOVED" : "", eng ? "  (reverb2-6/granular/tape engine: out of scope)" : "");
    }
    std::printf ("      %d of %zu differ by more than 1.5 dB; %d of them have no timer-built engine\n", moved, files.size(), movedInScope);
}

int main (int argc, char** argv)
{
    setenv ("TERRAIN_DETERMINISTIC", "1", 0);   // stable seeds, read once when the plugin loads (TerrainDeterminism.h)
    const bool doSweep = argc > 1 && std::string (argv[1]) == "--sweep";
    const char* bundle = std::getenv ("TERRAIN_AU_BUNDLE");
    std::printf ("\n  pool_send_first_block_au — fb636 bugA · %s\n", bundle ? bundle : "(the installed AU)");

    std::string virgin, why;
    { AU a; if (! a.open()) return 2; virgin = a.readXml (why); a.close(); }
    if (virgin.empty()) { std::printf ("  !! no virgin blob: %s\n", why.c_str()); return 2; }
    const std::string blobU = makeBlob (virgin, "SYN_UTL_", why);  if (blobU.empty()) { std::printf ("  !! %s\n", why.c_str()); return 2; }
    const std::string blobW = makeBlob (virgin, "SYN_WID3_", why); if (blobW.empty()) { std::printf ("  !! %s\n", why.c_str()); return 2; }
    std::string blobT = makeBlob (virgin, "SYN_TPE_", why);  if (blobT.empty()) { std::printf ("  !! %s\n", why.c_str()); return 2; }
    {   // [6]'s tape must PROCESS, or a dry passthrough and the processed signal read the same (the defaults do:
        // -16.39 vs -16.56 dB). These are Max's "Damaged Tapes" tape settings, copied ONCE as constants, so the gate
        // never reads his files: fully wet, tempo-synced (1/8 at the 120 bpm default) — the first blocks of the
        // processed signal are the echo still on its way, while a passthrough is the dry oscillator at full level.
        static const std::pair<const char*, double> kTape[] = {
            { "MIX", 1.0 }, { "SYNC", 1.0 }, { "SYNCDIV", 10.0 }, { "TIME", 0.45 }, { "REPEATS", 0.30 },
            { "DRIVE", 0.08 }, { "SAT", 0.4535 }, { "AGE", 0.7778 }, { "FLUTTER", 0.5317 }, { "WOW", 0.1475 },
            { "HISS", 0.0428 }, { "BUMP", 0.30 }, { "WIDTH", 0.60 } };
        for (const auto& [id, v] : kTape)
            if (! setParam (blobT, std::string ("SYN_TPE_") + id, v)) { std::printf ("  !! the virgin blob has no PARAM SYN_TPE_%s\n", id); return 2; }
    }
    {
        std::string act; double lowest = 1e9;
        for (const auto& d : activeDevices (virgin)) { act += " " + d.first + "RANK=" + fmt ("%.3f", d.second); lowest = std::min (lowest, d.second); }
        std::printf ("  PREFLIGHT  virgin blob %zu bytes · ACTIVE in the virgin patch:%s\n", virgin.size(), act.empty() ? " (none)" : act.c_str());
        for (const char* p : { "SYN_UTL_", "SYN_WID3_" })
        {
            const std::string& b = std::string (p) == "SYN_UTL_" ? blobU : blobW;
            std::printf ("             %sACTIVE=%g RANK=%g POWER=%g SRC_A=%g SEND=%g (SRC_B..NOISE %g %g %g %g %g)\n", p,
                         getParam (b, std::string (p) + "ACTIVE"), getParam (b, std::string (p) + "RANK"), getParam (b, std::string (p) + "POWER"),
                         getParam (b, std::string (p) + "SRC_A"), getParam (b, std::string (p) + "SEND"),
                         getParam (b, std::string (p) + "SRC_B"), getParam (b, std::string (p) + "SRC_C"), getParam (b, std::string (p) + "SRC_D"),
                         getParam (b, std::string (p) + "SRC_SUB"), getParam (b, std::string (p) + "SRC_NOISE"));
        }
        if (! (lowest > 0.0)) { std::printf ("  !! a virgin device already sits at RANK <= 0; slot 0 is not guaranteed\n"); return 2; }
    }

    // [0] harness alive
    const auto v0 = runNoPump (virgin, 8);
    const double V0 = blocksDb (v0, 0, 4);
    chk (V0 > -40.0, "[0] harness alive: virgin patch, osc A, note right after the load", fmt ("blocks 1-4 %.2f dB (bar > -40 dB)", V0));

    // [2] control: the same blob, warmed — L0
    const auto wU = runWarm (blobU, 8), wW = runWarm (blobW, 8);
    const double L0u1 = blocksDb (wU, 0, 1), L0u = blocksDb (wU, 0, 4), L0w1 = blocksDb (wW, 0, 1), L0w = blocksDb (wW, 0, 4);
    chk (L0u > -40.0 && L0w > -40.0, "[2] CONTROL: the same blobs warmed like loadInto sound (this is L0; green on every build)",
         fmt ("Utility 1: block 1 %.2f dB, blocks 1-4 %.2f dB · Widen 3: block 1 %.2f dB, blocks 1-4 %.2f dB", L0u1, L0u, L0w1, L0w)
         + fmt (" · virgin %.2f dB", V0));

    // [1] / [1b] the bug: no pump between the load and the note
    const auto nU = runNoPump (blobU, 8);
    const double Nu1 = blocksDb (nU, 0, 1), Nu = blocksDb (nU, 0, 4);
    chk (std::fabs (Nu1 - L0u1) <= 1.5 && std::fabs (Nu - L0u) <= 1.5, "[1] Utility 1 (insert, osc A): sounds from block 1 with NO pump after the load",
         fmt ("block 1 %.2f dB vs L0 %.2f · blocks 1-4 %.2f dB vs L0 %.2f (bar: within 1.5 dB)", Nu1, L0u1, Nu, L0u));
    {   // [1b] — see the header: Widen is time-varying, so it is judged A/B over the SAME block history
        bool sameW = false;
        const auto aW = runSeq (blobW, false), bW = runSeq (blobW, true);
        const std::string dW = sameOrWhere (aW, bW, sameW);
        const double Aw = blocksDb (aW, 0, 4);
        chk (sameW && Aw > -40.0, "[1b] Widen 3 (SYN_WID3_*, insert, osc A): sounds with NO pump, bit-identical to the pumped run",
             fmt ("no-pump chord blocks 1-4 %.2f dB (bar > -40 dB) · ", Aw) + dW);
    }

    // [3c] control, then [3]
    bool sameV = false, sameU = false;
    const std::string dV = sameOrWhere (runSeq (virgin, false), runSeq (virgin, true), sameV);
    chk (sameV, "[3c] CONTROL: the virgin patch plays the same with or without timer ticks before the chord", dV);
    const std::string dU = sameOrWhere (runSeq (blobU, false), runSeq (blobU, true), sameU);
    chk (sameU, "[3] EXACT: the Utility blob plays bit-identically with or without timer ticks before the chord", dU);

    // [4] the state arrives before AudioUnitInitialize
    std::vector<float> pre;
    {
        AU a;
        if (openNoInit (a))
        {
            const bool w = a.writeXml (blobU);
            if (w && AudioUnitInitialize (a.au) == noErr) { a.midi (0x90, 60, 100); pre = a.render (8); }
            else std::printf ("  !! [4] writeXml %s / initialise failed\n", w ? "ok" : "FAILED");
            a.close();
        }
    }
    const double P1 = blocksDb (pre, 0, 1), P = blocksDb (pre, 0, 4);
    chk (std::fabs (P1 - L0u1) <= 1.5 && std::fabs (P - L0u) <= 1.5, "[4] state set BEFORE AudioUnitInitialize: sounds from block 1 with no pump",
         fmt ("block 1 %.2f dB vs L0 %.2f · blocks 1-4 %.2f dB vs L0 %.2f (bar: within 1.5 dB)", P1, L0u1, P, L0u));

    // [6] a timer-built engine: never a dry burst ahead of it
    {
        const auto nT = runNoPump (blobT, 8), wT = runWarm (blobT, 8);
        const double Nt = blocksDb (nT, 0, 4), Wt = blocksDb (wT, 0, 4);
        chk (Wt > -40.0 && Nt < -60.0, "[6] Tape 1 (insert, osc A; engine timer-built): its slot is NOT fed before its engine exists",
             fmt ("no-pump blocks 1-4 %.2f dB (bar < -60 dB: nothing reaches the engine-less slot) · warmed %.2f dB (bar > -40: the tape itself works)", Nt, Wt));
    }

    if (doSweep) sweep();
    return summary();
}
