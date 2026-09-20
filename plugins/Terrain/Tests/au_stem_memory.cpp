// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_stem_memory.cpp — tp61 · CAPTURE OFF GIVES THE STEM RINGS BACK, WEIGHED (the installed AU).
//
//  Max: "whenever I have capture off in the settings, the stem capture should just be greyed out
//  ... make sure nothing ghostly is running in the background taking up memory, especially memory.
//  We want people to load up multiple instances ... if I duplicate it then it's two times the
//  memory."  And then, on being told this could only be pinned structurally: "Why can't you run it?"
//
//  He was right to ask. The rings arm when a chop layer HAS A SAMPLE, and no AU parameter loads
//  one — but setStateInformation does: a V2 blob's <layers> tree names each layer's sourcePath and
//  restoreSamples() decodes it. That is precisely a hidden instance restoring a DAW project, which
//  is the case that matters for "load up multiple instances". So this opens the shipped AU twice
//  with the SAME four-sample state — once with the capture-off preference on disk, once without —
//  and weighs the process (mach task_info, phys_footprint) before and after the load.
//
//  🔑 THE PREFERENCE IS A FILE (~/Library/Caches/Terrain/capture-off). This test writes and deletes
//  it, and PUTS IT BACK the way it found it, whatever happens (RAII guard) — it is the user's real
//  setting.
//
//  ⚠️ WHAT IS NOT REACHED, PLAINLY: the live toggle. setCaptureEnabled is a native function the
//  editor calls, so ON→OFF on a running instance (releaseStemBuffers) is not driven here; that path
//  is pinned by Tests/stem_memory_gate.py. What IS weighed is the promise Max actually made to his
//  users: an instance opened with capture off never allocates the rings at all.
//
//  clang++ -O2 -std=c++17 Tests/au_stem_memory.cpp -o /tmp/austem -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/austem
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const double SR = 48000.0; static const int BLK = 512;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& detail = "")
{ if (ok) { ++npass; printf ("  PASS  %s\n        %s\n", what, detail.c_str()); } else { ++nfail; printf ("  FAIL  %s\n        %s\n", what, detail.c_str()); } }

static double footprintMB()
{
    task_vm_info_data_t vm {}; mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info (mach_task_self(), TASK_VM_INFO, (task_info_t) &vm, &n) != KERN_SUCCESS) return -1.0;
    return (double) vm.phys_footprint / 1e6;
}
static std::string home() { const char* h = getenv ("HOME"); return h ? h : ""; }
static std::string markerPath() { return home() + "/Library/Caches/Terrain/capture-off"; }
static bool exists (const std::string& p) { struct stat st {}; return stat (p.c_str(), &st) == 0; }

// the user's real preference, restored on every exit path
struct MarkerGuard
{
    bool had; MarkerGuard() : had (exists (markerPath())) {}
    ~MarkerGuard() { set (had); }
    static void set (bool on)
    {
        if (on) { const std::string d = home() + "/Library/Caches/Terrain"; mkdir (d.c_str(), 0755);
                  FILE* f = fopen (markerPath().c_str(), "w"); if (f) { fputs ("1", f); fclose (f); } }
        else unlink (markerPath().c_str());
    }
};

// four real factory one-shots, so a layer decodes exactly what a user's patch would name
static std::vector<std::string> fourFlacs()
{
    std::vector<std::string> out; const std::string root = "Resources/Samples";
    DIR* d = opendir (root.c_str()); if (! d) return out;
    while (dirent* e = readdir (d))
    {
        if (e->d_name[0] == '.') continue;
        const std::string sub = root + "/" + e->d_name; DIR* s = opendir (sub.c_str()); if (! s) continue;
        while (dirent* f = readdir (s))
        { const std::string n = f->d_name; if (n.size() > 5 && n.substr (n.size() - 5) == ".flac") { out.push_back (sub + "/" + n); break; } }
        closedir (s); if (out.size() >= 4) break;
    }
    closedir (d);
    for (auto& p : out) { char buf[4096]; if (realpath (p.c_str(), buf)) p = buf; }
    return out;
}

struct Au
{
    AudioUnit au = nullptr; double stamp = 0.0;
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        return AudioUnitInitialize (au) == noErr;
    }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void render (int blocks)
    {
        std::vector<float> L (BLK), R (BLK);
        for (int b = 0; b < blocks; ++b)
        {
            AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = L.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = R.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.003, false);
        }
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }

    // The blob under "jucePluginState" is copyXmlToBinary()'s: [magic 0x21324356][len][XML][NUL].
    // Inside its <layers> tree, the i-th <layer …sourcePath=""…/> gets the i-th path.
    bool loadStateWithLayers (const std::vector<std::string>& paths, std::string& why)
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) { why = "no ClassInfo"; return false; }
        CFDictionaryRef dict = (CFDictionaryRef) pl; CFStringRef key = CFSTR ("jucePluginState");
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue (dict, key);
        if (dd == nullptr) { CFRelease (pl); why = "no jucePluginState"; return false; }
        const UInt8* p = CFDataGetBytePtr (dd);
        uint32_t magic = 0, len = 0; memcpy (&magic, p, 4); memcpy (&len, p + 4, 4);
        if (magic != 0x21324356u || (CFIndex) (len + 8) > CFDataGetLength (dd)) { CFRelease (pl); why = "bad magic"; return false; }
        std::string xml ((const char*) p + 8, len);
        const size_t l0 = xml.find ("<layers"), l1 = xml.find ("</layers>", l0 == std::string::npos ? 0 : l0);
        if (l0 == std::string::npos || l1 == std::string::npos) { CFRelease (pl); why = "no <layers> tree in the blob"; return false; }
        std::string span = xml.substr (l0, l1 - l0); size_t at = 0; int set = 0;
        for (const auto& path : paths)
        {
            at = span.find ("sourcePath=\"\"", at); if (at == std::string::npos) break;
            std::string esc; for (char ch : path) esc += (ch == '"' ? "&quot;" : ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : std::string (1, ch));
            span = span.substr (0, at) + "sourcePath=\"" + esc + "\"" + span.substr (at + 13); at += 13 + esc.size(); ++set;
        }
        if (set != (int) paths.size()) { CFRelease (pl); why = "only " + std::to_string (set) + " empty sourcePath slots in <layers>"; return false; }
        xml = xml.substr (0, l0) + span + xml.substr (l1);
        std::vector<UInt8> blob (8 + xml.size() + 1, 0);
        const uint32_t m = 0x21324356u, l = (uint32_t) xml.size() + 1;
        memcpy (blob.data(), &m, 4); memcpy (blob.data() + 4, &l, 4); memcpy (blob.data() + 8, xml.data(), xml.size());
        CFMutableDictionaryRef nd = CFDictionaryCreateMutableCopy (nullptr, 0, dict);
        CFDataRef ndata = CFDataCreate (kCFAllocatorDefault, blob.data(), (CFIndex) blob.size());
        CFDictionarySetValue (nd, key, ndata);
        CFPropertyListRef npl = (CFPropertyListRef) nd;
        const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &npl, sizeof (npl));
        CFRelease (ndata); CFRelease (nd); CFRelease (pl);
        if (st != noErr) { why = "SetProperty ClassInfo failed " + std::to_string (st); return false; }
        return true;
    }
    // reads the paths back so a silently-dropped restore cannot pass as "no rings"
    int layersNamed()
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return -1;
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue ((CFDictionaryRef) pl, CFSTR ("jucePluginState")); if (! dd) { CFRelease (pl); return -1; }
        const UInt8* p = CFDataGetBytePtr (dd); uint32_t len = 0; memcpy (&len, p + 4, 4);
        std::string xml ((const char*) p + 8, len); CFRelease (pl);
        int n = 0; size_t at = 0; while ((at = xml.find ("sourcePath=\"/", at)) != std::string::npos) { ++n; at += 13; }
        return n;
    }
};

struct Weigh { double base = 0, loaded = 0; int named = 0; bool ok = false; std::string why; };
static Weigh run (bool captureOff, const std::vector<std::string>& paths)
{
    Weigh w; MarkerGuard::set (captureOff);
    Au a; if (! a.open()) { w.why = "cannot open the AU"; return w; }
    a.pump (0.6); a.render (20);
    w.base = footprintMB();
    if (! a.loadStateWithLayers (paths, w.why)) { a.close(); return w; }
    // the timer arms a ring the first tick it sees a loaded layer (fb517); give it a generous
    // window, and render so prepareToPlay's rate is live
    a.pump (2.5); a.render (40); a.pump (0.5);
    w.loaded = footprintMB(); w.named = a.layersNamed(); w.ok = true;
    a.close();
    return w;
}

int main()
{
    MarkerGuard guard;   // restores the user's real setting on every exit path
    printf ("\ntp61 — THE STEM RINGS, WEIGHED ON THE INSTALLED AU (phys_footprint, %g Hz)\n\n", SR);
    const auto paths = fourFlacs();
    if (paths.size() < 4) { printf ("  !! need four factory .flac files under Resources/Samples (found %zu) — rebuild the library, see scripts/sample-library/\n", paths.size()); return 2; }
    for (const auto& p : paths) printf ("  layer: %s\n", p.c_str());
    const double ring = 600.0 * SR * 2 * 4 / 1e6, five = 5 * ring;
    printf ("  one ring = 600 s x %g x 2ch x float32 = %.1f MB · five rings = %.1f MB\n\n", SR, ring, five);

    const Weigh off = run (true,  paths);
    const Weigh on  = run (false, paths);
    if (! off.ok || ! on.ok) { printf ("  !! %s / %s\n", off.why.c_str(), on.why.c_str()); return 2; }
    const double dOff = off.loaded - off.base, dOn = on.loaded - on.base;

    chk (off.named == 4 && on.named == 4,
         "[0] BOTH INSTANCES RESTORED A STATE NAMING FOUR SAMPLES (the hidden-instance-in-a-DAW-project case)",
         "paths read back: off=" + std::to_string (off.named) + " on=" + std::to_string (on.named));
    chk (dOn >= 0.85 * five,
         "[1] CAPTURE ON: loading four layers arms the five rings — the memory the setting is about is REAL",
         "footprint " + std::to_string ((int) on.base) + " -> " + std::to_string ((int) on.loaded) + " MB, +"
         + std::to_string ((int) dOn) + " MB (five rings = " + std::to_string ((int) five) + ")");
    chk (dOff < 0.25 * ring,
         "🚨 [2] CAPTURE OFF: the same four layers arm NO ring — not one of the five",
         "footprint " + std::to_string ((int) off.base) + " -> " + std::to_string ((int) off.loaded) + " MB, +"
         + std::to_string ((int) dOff) + " MB (a single ring would be +" + std::to_string ((int) ring) + ")");
    chk (dOn - dOff >= 0.85 * five,
         "[3] THE DIFFERENCE IS THE RINGS: on minus off is at least 85% of five rings",
         "on-off = " + std::to_string ((int) (dOn - dOff)) + " MB of " + std::to_string ((int) five));
    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
