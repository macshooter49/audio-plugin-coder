// ══════════════════════════════════════════════════════════════════════════════════════════════
//  editor_cpu.mm — fb636: WHAT DOES THE OPEN EDITOR COST, PAGE BY PAGE, AT REST AND WHILE PLAYING?
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/editor_cpu.mm -o /tmp/editor_cpu \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    /tmp/editor_cpu [seconds-per-phase] [frontPageIndex] [synthPageIndex]
//
//  The installed AU, its real editor in a VISIBLE window (a hidden WKWebView suspends requestAnimationFrame,
//  so a windowless host is blind to every animation loop — fb577), audio rendered at real-time pace on a
//  second thread the way a DAW renders. Reported per phase, as % of ONE core:
//     host    — this process: the render thread + the message thread (the editor's timer, the frame build)
//     web     — the WebKit WebContent process(es) that appeared with the editor (the page's JS + layout + paint)
//     gpu     — the WebKit GPU process(es) that appeared with it (canvas rasterisation, compositing)
//  Phases: the FRONT page (the hero) at rest and playing; the SYNTH page at rest and playing; the editor
//  CLOSED (fb521 parks the page instead of destroying it — does a parked page still spend?).
//  A chord every second, keys up at 0.6 s, while "playing". The page is chosen through the saved state's
//  uiPage (fb514: the editor boots straight onto it), so each page gets a fresh editor.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#include <dlfcn.h>
#include <string>
#include <libproc.h>
#include <sys/resource.h>
#include <mach/mach_time.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <thread>
#include <atomic>
#include <chrono>

@protocol TIAUCocoaUIBase
- (NSView*) uiViewForAudioUnit: (AudioUnit) au withSize: (NSSize) s;
@end

static double nowMs() { return (double) clock_gettime_nsec_np (CLOCK_MONOTONIC) / 1.0e6; }
static const double SR = 48000.0; static const int BLK = 512;
static void pumpMs (double ms)
{
    const double t0 = nowMs();
    while (nowMs() - t0 < ms)
        @autoreleasepool {
            NSEvent* e = [NSApp nextEventMatchingMask: NSEventMaskAny untilDate: [NSDate dateWithTimeIntervalSinceNow: 0.02] inMode: NSDefaultRunLoopMode dequeue: YES];
            if (e != nil) [NSApp sendEvent: e];
        }
}

// ── process CPU time, in ns, for any pid of this user ────────────────────────────────────────
static double g_tickNs = 1.0;
static double cpuNs (pid_t pid)
{
    rusage_info_v2 ri {};
    if (proc_pid_rusage (pid, RUSAGE_INFO_V2, (rusage_info_t*) &ri) != 0) return -1;
    return (double) (ri.ri_user_time + ri.ri_system_time) * g_tickNs;
}
static void calibrate()
{   // proc_pid_rusage reports mach ticks on Apple Silicon and ns on Intel; getrusage is always µs. Burn, compare.
    const double a0 = cpuNs (getpid()); rusage r0; getrusage (RUSAGE_SELF, &r0);
    volatile double x = 0; const double t = nowMs(); while (nowMs() - t < 150) x += std::sqrt (x + 1.0);
    const double a1 = cpuNs (getpid()); rusage r1; getrusage (RUSAGE_SELF, &r1);
    auto us = [] (const rusage& r) { return (double) (r.ru_utime.tv_sec + r.ru_stime.tv_sec) * 1e6 + (double) (r.ru_utime.tv_usec + r.ru_stime.tv_usec); };
    const double ratio = (us (r1) - us (r0)) * 1000.0 / std::max (1.0, a1 - a0);
    mach_timebase_info_data_t tb; mach_timebase_info (&tb);
    g_tickNs = (std::fabs (ratio - 1.0) < 0.2) ? 1.0 : (double) tb.numer / tb.denom;
    std::printf ("  (rusage unit: %s)\n", g_tickNs == 1.0 ? "ns" : "mach ticks");
}
static std::set<pid_t> webkitPids (const char* kind)
{
    std::set<pid_t> out; std::vector<pid_t> p (8192);
    const int n = proc_listallpids (p.data(), (int) (p.size() * sizeof (pid_t)));
    for (int i = 0; i < n; ++i)
    {
        char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
        if (proc_pidpath (p[(size_t) i], path, sizeof path) <= 0) continue;
        if (std::strstr (path, "com.apple.WebKit") && std::strstr (path, kind)) out.insert (p[(size_t) i]);
    }
    return out;
}

struct AuHost
{
    AudioUnit au = nullptr;
    std::thread render; std::atomic<bool> stop { false }; std::atomic<bool> playing { false };
    bool init()
    {
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        // fb636 — TERRAIN_AU_BUNDLE=/path/Terrain.component measures THAT build (registered in-process as 'TrnX', the
        // au_state_blob.h side-load), so a baseline and a candidate are A/B'd in one session without installing either.
        if (const char* bundle = std::getenv ("TERRAIN_AU_BUNDLE"))
        {
            d.componentSubType = 'TrnX';
            void* h = dlopen ((std::string (bundle) + "/Contents/MacOS/Terrain").c_str(), RTLD_NOW | RTLD_LOCAL);
            auto fn = h ? (AudioComponentFactoryFunction) dlsym (h, "TerrainAUFactory") : nullptr;
            if (! fn || ! AudioComponentRegister (&d, CFSTR ("Waves Crate: Terrain (side-loaded)"), 0x10000, fn))
            { std::printf ("  !! side-load failed: %s\n", bundle); return false; }
            std::printf ("  (side-loaded %s)\n", bundle);
        }
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (c == nullptr || AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! AU not found\n"); return false; }
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 maxF = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF);
        return AudioUnitInitialize (au) == noErr;
    }
    void startRender()
    {
        render = std::thread ([this]
        {
            std::vector<float> l ((size_t) BLK), r ((size_t) BLK);
            std::vector<uint8_t> raw (sizeof (AudioBufferList) + sizeof (AudioBuffer)); auto* abl = (AudioBufferList*) raw.data();
            double st = 0.0; long blk = 0; bool down = false; const int chord[4] = { 48, 55, 60, 64 };
            auto next = std::chrono::steady_clock::now(); const auto period = std::chrono::microseconds ((long) (1.0e6 * BLK / SR));
            while (! stop.load())
            {
                const long ph = blk % 94;
                if (playing.load() && ph == 0) { for (int k : chord) MusicDeviceMIDIEvent (au, 0x90, (UInt32) k, 100, 0); down = true; }
                if (down && (ph == 56 || ! playing.load())) { for (int k : chord) MusicDeviceMIDIEvent (au, 0x80, (UInt32) k, 0, 0); down = false; }
                abl->mNumberBuffers = 2;
                abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), l.data() }; abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), r.data() };
                AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = st; ts.mFlags = kAudioTimeStampSampleTimeValid;
                AudioUnitRender (au, &fl, &ts, 0, (UInt32) BLK, abl);
                st += BLK; ++blk; next += period; std::this_thread::sleep_until (next);
            }
        });
    }
    void stopRender() { stop.store (true); if (render.joinable()) render.join(); }
    bool setPage (int page)
    {   // the saved state's root attribute uiPage — setStateInformation stores it, the editor boots onto it (fb514/fb537)
        CFPropertyListRef pl = nullptr; UInt32 s = sizeof pl;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &s) != noErr || pl == nullptr) return false;
        CFMutableDictionaryRef m = CFDictionaryCreateMutableCopy (nullptr, 0, (CFDictionaryRef) pl); CFRelease (pl);
        CFDataRef dat = (CFDataRef) CFDictionaryGetValue (m, CFSTR ("jucePluginState"));
        if (dat == nullptr) { CFRelease (m); return false; }
        std::string xml ((const char*) CFDataGetBytePtr (dat) + 8, (size_t) CFDataGetLength (dat) - 8);
        while (! xml.empty() && xml.back() == '\0') xml.pop_back();
        const size_t rs = xml.find ("<Parameters"), re = xml.find ('>', rs);
        std::string tag = xml.substr (rs, re - rs);
        const size_t up = tag.find (" uiPage=\"");
        const std::string attr = " uiPage=\"" + std::to_string (page) + "\"";
        if (up != std::string::npos) { const size_t e = tag.find ('"', up + 9); tag.replace (up, e + 1 - up, attr); } else tag += attr;
        xml = xml.substr (0, rs) + tag + xml.substr (re);
        std::vector<UInt8> blob; const UInt32 magic = 0x21324356u, len = (UInt32) (xml.size() + 1);
        blob.insert (blob.end(), (const UInt8*) &magic, (const UInt8*) &magic + 4); blob.insert (blob.end(), (const UInt8*) &len, (const UInt8*) &len + 4);
        blob.insert (blob.end(), xml.begin(), xml.end()); blob.push_back (0);
        CFDataRef nd = CFDataCreate (nullptr, blob.data(), (CFIndex) blob.size()); CFDictionarySetValue (m, CFSTR ("jucePluginState"), nd);
        CFPropertyListRef npl = m; const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &npl, sizeof npl);
        CFRelease (nd); CFRelease (m); return st == noErr;
    }
    NSView* makeView()
    {
        AudioUnitCocoaViewInfo info {}; UInt32 sz = sizeof (info);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &sz) != noErr) return nil;
        NSURL* url = (__bridge_transfer NSURL*) info.mCocoaAUViewBundleLocation;
        NSString* clsName = (__bridge_transfer NSString*) info.mCocoaAUViewClass[0];
        NSBundle* b = [NSBundle bundleWithURL: url]; [b load];
        Class cls = [b classNamed: clsName]; if (cls == nil) cls = NSClassFromString (clsName);
        if (cls == nil) return nil;
        id<TIAUCocoaUIBase> factory = [[cls alloc] init];
        return [factory uiViewForAudioUnit: au withSize: NSMakeSize (820, 672)];
    }
};

int main (int argc, char** argv)
{
    const double SECS = argc > 1 ? std::atof (argv[1]) : 15.0;
    const int FRONT = argc > 2 ? std::atoi (argv[2]) : 0, SYNTH = argc > 3 ? std::atoi (argv[3]) : 1;
    @autoreleasepool
    {
        [NSApplication sharedApplication]; [NSApp setActivationPolicy: NSApplicationActivationPolicyAccessory];   // no Dock icon, no focus steal [NSApp finishLaunching];
        // no activateIgnoringOtherApps: the window shows without taking focus from whatever the user is doing
        std::printf ("\n══ fb636 — THE OPEN EDITOR'S COST, PAGE BY PAGE (installed AU, visible window, real-time render) ══\n");
        calibrate();
        AuHost h; if (! h.init()) return 1;
        h.startRender(); pumpMs (500);
        const std::set<pid_t> web0 = webkitPids ("WebContent"), gpu0 = webkitPids ("GPU");
        NSWindow* win = [[NSWindow alloc] initWithContentRect: NSMakeRect (120, 120, 820, 672) styleMask: NSWindowStyleMaskTitled
                                                      backing: NSBackingStoreBuffered defer: NO];
        [win setReleasedWhenClosed: NO];
        std::set<pid_t> web, gpu;
        auto measure = [&] (const char* label, double secs)
        {
            auto sum = [&] (const std::set<pid_t>& s) { double t = 0; for (pid_t p : s) { const double v = cpuNs (p); if (v > 0) t += v; } return t; };
            const double h0 = cpuNs (getpid()), w0 = sum (web), g0 = sum (gpu), t0 = nowMs();
            pumpMs (secs * 1000.0);
            const double dt = (nowMs() - t0) * 1e6;
            std::printf ("  %-34s host %6.1f%%   web %6.1f%%   gpu %6.1f%%   (web pids %zu, gpu pids %zu)\n", label,
                         100 * (cpuNs (getpid()) - h0) / dt, 100 * (sum (web) - w0) / dt, 100 * (sum (gpu) - g0) / dt, web.size(), gpu.size());
            std::fflush (stdout);
        };
        auto open = [&] (int page) -> NSView*
        {
            h.setPage (page); pumpMs (400);
            NSView* v = nil; @autoreleasepool { v = h.makeView(); }
            if (v == nil) { std::printf ("  !! no editor view\n"); return nil; }
            [win setContentView: v]; [win orderFrontRegardless];   // visible (rAF runs), never key
            pumpMs (10000);   // boot, the restore pushes, the flip burst
            for (pid_t p : webkitPids ("WebContent")) if (! web0.count (p)) web.insert (p);
            for (pid_t p : webkitPids ("GPU"))        if (! gpu0.count (p)) gpu.insert (p);
            return v;
        };
        measure ("no editor, silence", SECS);
        NSView* v = open (FRONT);
        measure ("FRONT page (hero), at rest", SECS);
        h.playing = true;  measure ("FRONT page (hero), playing", SECS);
        h.playing = false; pumpMs (4000); measure ("FRONT page (hero), rest again", SECS);
        [win setContentView: [[NSView alloc] init]]; v = nil; pumpMs (1500);
        v = open (SYNTH);
        measure ("SYNTH page, at rest", SECS);
        h.playing = true;  measure ("SYNTH page, playing", SECS);
        h.playing = false; pumpMs (4000); measure ("SYNTH page, rest again", SECS);
        [win orderOut: nil]; [win setContentView: [[NSView alloc] init]]; v = nil; pumpMs (2000);
        measure ("editor CLOSED (parked page), rest", SECS);
        h.playing = true;  measure ("editor CLOSED (parked page), playing", SECS);
        h.playing = false; pumpMs (1000);
        h.stopRender(); AudioUnitUninitialize (h.au); AudioComponentInstanceDispose (h.au);
        std::printf ("\n");
    }
    return 0;
}
