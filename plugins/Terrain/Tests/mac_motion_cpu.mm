// ══════════════════════════════════════════════════════════════════════════════════════════════
//  mac_motion_cpu.mm — tp62 · MOTION ON vs OFF, WEIGHED ON REAL EDITORS (the installed AU).
//
//  Max: "I wanna see how much CPU is gonna take on a bunch of instances as well, so I want you to run
//  an actual physical test."  So: N instances of the shipped AU, each with its REAL editor in a real
//  window (rAF needs a visible window — the mac_ui_exp law), audio rendered at real-time pace on a
//  thread per instance with a chord held so every push lane is live, the synth page up. Then the
//  whole cost of the plugin on the machine is read — this process (the message-thread push lanes +
//  the DSP threads) via getrusage, and every WebKit.WebContent process (the pages' JS and paint) via
//  proc_pid_rusage — for `seconds`, first with motion ON, then with the motion-off marker on disk.
//  Memory is phys_footprint of the same set. The user's real preference is restored on every exit.
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/mac_motion_cpu.mm -o /tmp/tpmot \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    /tmp/tpmot <instances> <seconds>      (from the plugin root)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#include <libproc.h>
#include <mach/mach_time.h>
#include <set>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>

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
            NSEvent* e = [NSApp nextEventMatchingMask: NSEventMaskAny untilDate: [NSDate dateWithTimeIntervalSinceNow: 0.02]
                                               inMode: NSDefaultRunLoopMode dequeue: YES];
            if (e != nil) [NSApp sendEvent: e]; }
}
static std::string home() { const char* h = getenv ("HOME"); return h ? h : ""; }
static std::string marker() { return home() + "/Library/Caches/Terrain/motion-off"; }
static bool exists (const std::string& p) { struct stat st {}; return stat (p.c_str(), &st) == 0; }
struct MarkerGuard
{
    bool had; MarkerGuard() : had (exists (marker())) {}
    ~MarkerGuard() { set (had); }
    static void set (bool on)
    { if (on) { FILE* f = fopen (marker().c_str(), "w"); if (f) { fputs ("1", f); fclose (f); } } else unlink (marker().c_str()); }
};
struct AuHost
{
    AudioUnit au = nullptr; std::thread render; std::atomic<bool> stop { false }; std::atomic<int> midi { 0 };
    bool init()
    {
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c) { std::printf ("  !! AU not found\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! instance failed\n"); return false; }
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
            double sampleTime = 0.0; auto next = std::chrono::steady_clock::now();
            const auto period = std::chrono::microseconds ((long) (1.0e6 * BLK / SR));
            static const int chord[4] = { 60, 64, 67, 71 };
            while (! stop.load())
            {
                const int m = midi.exchange (0);
                if (m == 1) for (int k = 0; k < 4; ++k) MusicDeviceMIDIEvent (au, 0x90, (UInt32) chord[k], 100, 0);
                if (m == 2) for (int k = 0; k < 4; ++k) MusicDeviceMIDIEvent (au, 0x80, (UInt32) chord[k], 0, 0);
                abl->mNumberBuffers = 2;
                abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = l.data();
                abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = r.data();
                AudioUnitRenderActionFlags flags = 0; AudioTimeStamp ts {}; ts.mSampleTime = sampleTime; ts.mFlags = kAudioTimeStampSampleTimeValid;
                AudioUnitRender (au, &flags, &ts, 0, (UInt32) BLK, abl);
                sampleTime += BLK; next += period; std::this_thread::sleep_until (next);
            }
        });
    }
    void stopRender() { stop.store (true); if (render.joinable()) render.join(); }
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
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};

// ── the meters ──
// ⚠️ ri_user_time / ri_system_time are MACH TICKS, not nanoseconds (on this chip ~41.7 ns each) — the
//    first cut of this file read the pages at 2 % of a core because of exactly that. Converted here.
static double tickNs() { static double f = 0; if (f == 0) { mach_timebase_info_data_t tb {}; mach_timebase_info (&tb); f = (double) tb.numer / (double) tb.denom; } return f; }
struct Cpu { double selfNs = 0, webNs = 0; double selfMB = 0, webMB = 0; int webPids = 0; };
static double selfCpuNs() { struct rusage r {}; getrusage (RUSAGE_SELF, &r);
    return (r.ru_utime.tv_sec + r.ru_stime.tv_sec) * 1e9 + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) * 1e3; }
static double selfMB() { struct rusage_info_v4 ri {}; if (proc_pid_rusage (getpid(), RUSAGE_INFO_V4, (rusage_info_t*) &ri) != 0) return 0; return ri.ri_phys_footprint / 1e6; }
static std::set<pid_t> webPids()
{
    std::set<pid_t> out;
    int n = proc_listpids (PROC_ALL_PIDS, 0, nullptr, 0); std::vector<pid_t> pids ((size_t) n + 64);
    n = proc_listpids (PROC_ALL_PIDS, 0, pids.data(), (int) (pids.size() * sizeof (pid_t))) / (int) sizeof (pid_t);
    for (int i = 0; i < n; ++i)
    {
        if (pids[(size_t) i] <= 0) continue;
        char path[PROC_PIDPATHINFO_MAXSIZE] = {}; if (proc_pidpath (pids[(size_t) i], path, sizeof path) <= 0) continue;
        if (std::strstr (path, "WebKit.WebContent") != nullptr) out.insert (pids[(size_t) i]);
    }
    return out;
}
// ⚠️ A CLOSED PAGE IS KEPT ALIVE (fb521), so the ON scenario's WebContent processes are still there when
//    the OFF scenario runs. Only the processes that APPEARED for this scenario are weighed — the pid set
//    is snapshotted before the instances are made and diffed. `only` is that set.
static Cpu webCpu (const std::set<pid_t>& only)
{
    Cpu c; c.selfNs = selfCpuNs(); c.selfMB = selfMB();
    for (pid_t pid : only)
    {
        struct rusage_info_v4 ri {}; if (proc_pid_rusage (pid, RUSAGE_INFO_V4, (rusage_info_t*) &ri) != 0) continue;
        c.webNs += (double) (ri.ri_user_time + ri.ri_system_time) * tickNs(); c.webMB += ri.ri_phys_footprint / 1e6; ++c.webPids;
    }
    return c;
}
struct Run { double selfPct, webPct, selfMB, webMB; int webPids; };
static Run measure (double secs, const std::set<pid_t>& only)
{
    const Cpu a = webCpu (only); const double t0 = nowMs();
    pumpMs (secs * 1000.0);
    const Cpu b = webCpu (only); const double dt = (nowMs() - t0) * 1e6;   // ns
    Run r; r.selfPct = 100.0 * (b.selfNs - a.selfNs) / dt; r.webPct = 100.0 * (b.webNs - a.webNs) / dt;
    r.selfMB = b.selfMB; r.webMB = b.webMB; r.webPids = b.webPids; return r;
}

static Run scenario (bool motionOff, int N, double secs)
{
    MarkerGuard::set (motionOff);
    const std::set<pid_t> before = webPids();
    std::vector<AuHost*> hosts; std::vector<NSWindow*> wins; std::vector<NSView*> views;
    for (int i = 0; i < N; ++i)
    {
        auto* h = new AuHost(); if (! h->init()) { std::printf ("  !! instance %d failed\n", i); break; }
        h->startRender(); hosts.push_back (h);
        NSView* v = nil; @autoreleasepool { v = h->makeView(); }
        if (v == nil) { std::printf ("  !! no view for instance %d\n", i); continue; }
        const int col = i % 3, row = i / 3;
        NSWindow* w = [[NSWindow alloc] initWithContentRect: NSMakeRect (20 + col * 840, 40 + row * 700, v.frame.size.width, v.frame.size.height)
                                                  styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
        w.releasedWhenClosed = NO; [w.contentView addSubview: v]; [w makeKeyAndOrderFront: nil];
        wins.push_back (w); views.push_back (v);
    }
    pumpMs (3500);   // pages boot, restore pushes drain
    for (auto* h : hosts) h->midi.store (1);
    pumpMs (2500);   // wind-ups, blooms, spectra all live
    std::set<pid_t> mine; for (pid_t p : webPids()) if (! before.count (p)) mine.insert (p);
    Run r = measure (secs, mine);
    std::printf ("  motion %-3s  %zu WebContent process(es) appeared for %d editor(s)\n", motionOff ? "OFF" : "ON", mine.size(), N); std::fflush (stdout);
    for (auto* h : hosts) h->midi.store (2);
    pumpMs (300);
    for (size_t i = 0; i < wins.size(); ++i) { [wins[i] orderOut: nil]; }
    pumpMs (300);
    for (size_t i = 0; i < views.size(); ++i) { [views[i] removeFromSuperview]; views[i] = nil; }
    pumpMs (400);
    for (size_t i = 0; i < wins.size(); ++i) { [wins[i] close]; wins[i] = nil; }
    pumpMs (300);
    for (auto* h : hosts) { h->stopRender(); h->close(); delete h; }
    pumpMs (800);
    return r;
}

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        MarkerGuard guard; setvbuf (stdout, nullptr, _IONBF, 0);
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching]; [NSApp activateIgnoringOtherApps: YES];
        const int N = argc > 1 ? atoi (argv[1]) : 1; const double secs = argc > 2 ? atof (argv[2]) : 10.0;
        const std::string d = home() + "/Library/Caches/Terrain"; mkdir (d.c_str(), 0755);
        std::printf ("\n== tp62 — MOTION ON vs OFF · %d instance(s), real editors, chord held, %.0f s each ==\n\n", N, secs);
        std::printf ("  WebContent processes already on the machine: %zu (not weighed)\n\n", webPids().size());
        const Run on  = scenario (false, N, secs);
        const Run off = scenario (true,  N, secs);
        std::printf ("                    host process (push lanes + DSP)   WebContent (pages)        TOTAL        memory (host + pages)\n");
        std::printf ("  motion ON         %6.1f %% of a core                 %6.1f %%                 %6.1f %%     %6.0f + %6.0f MB\n", on.selfPct,  on.webPct,  on.selfPct  + on.webPct,  on.selfMB,  on.webMB);
        std::printf ("  motion OFF        %6.1f %% of a core                 %6.1f %%                 %6.1f %%     %6.0f + %6.0f MB\n", off.selfPct, off.webPct, off.selfPct + off.webPct, off.selfMB, off.webMB);
        const double tOn = on.selfPct + on.webPct, tOff = off.selfPct + off.webPct;
        std::printf ("  saved             %6.1f %%                           %6.1f %%                 %6.1f %%  (%.0f %% of the UI's cost)\n\n",
                     on.selfPct - off.selfPct, on.webPct - off.webPct, tOn - tOff, tOn > 0 ? 100.0 * (tOn - tOff) / tOn : 0.0);
        std::printf ("  ⚠️ the host column includes the DSP of %d instance(s), which the setting does not touch (Tests/au_motion_null.cpp);\n"
                     "     the difference between the rows is therefore entirely the UI.\n\n", N);
        return 0;
    }
}
