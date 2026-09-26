// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp110 — THE PATCHER'S GESTURES (pan / zoom), MEASURED IN THE REAL WEBVIEW.
//
//  The tp34 host (Tests/mac_patcher_fps.mm) grown three ways:
//   • it can load a BUILT component bundle in-process (TERRAIN_AU_BUNDLE=<.component>): the
//     factory is registered with AudioComponentRegister, so nothing is installed and Max's
//     installed plugin is never touched;
//   • the script is an argument and gets `window.__tpzArgs = <TPZ_ARGS json>` prepended
//     (which preset to load, how big a stress patch to build);
//   • it samples the CPU of its OWN WebKit processes (the WebContent process does the page's JS,
//     style, layout and paint; the GPU process rasterises and composites) plus itself, per phase.
//     The page announces its phase through document.title ('tpzP:<phase>'); the final results
//     come back in 'tp34:' chunks exactly like tp34.
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/mac_patcher_glide.mm -o /tmp/tpglide \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    TERRAIN_AU_BUNDLE=<build>/Terrain.component TPZ_ARGS='{"preset":"/path/x.terrain"}' \
//        /tmp/tpglide Tests/_tpz_measure.js 90        (from the plugin root)
//
//  Needs a VISIBLE window and an awake display (rAF starves otherwise) — hold the screen lock.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#include <libproc.h>
#include <objc/message.h>
#include <mach/mach_time.h>
#include <sys/resource.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <set>
#include <map>

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
            NSEvent* e = [NSApp nextEventMatchingMask: NSEventMaskAny untilDate: [NSDate dateWithTimeIntervalSinceNow: 0.01]
                                               inMode: NSDefaultRunLoopMode dequeue: YES];
            if (e != nil) [NSApp sendEvent: e]; }
}
// ── the WebKit helper processes, by name ─────────────────────────────────────────────────────
static std::set<int> pidsNamed (const char* name)
{
    std::set<int> out; std::vector<int> buf (4096);
    const int n = proc_listallpids (buf.data(), (int) (buf.size() * sizeof (int)));
    for (int i = 0; i < n; ++i)
    {
        char nm[256] = {}; if (proc_name (buf[(size_t) i], nm, sizeof nm) <= 0) continue;
        if (std::strstr (nm, name) != nullptr) out.insert (buf[(size_t) i]);
    }
    return out;
}
static double cpuMsOf (int pid)
{
    rusage_info_v2 ri {}; if (proc_pid_rusage (pid, RUSAGE_INFO_V2, (rusage_info_t*) &ri) != 0) return -1.0;
    mach_timebase_info_data_t tb; mach_timebase_info (&tb);
    // ri_*_time is in mach absolute-time units on Apple Silicon
    return (double) (ri.ri_user_time + ri.ri_system_time) * tb.numer / tb.denom / 1.0e6;
}
struct AuHost
{
    AudioUnit au = nullptr; std::thread render; std::atomic<bool> stop { false }; std::atomic<int> midi { 0 };
    bool init()
    {
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        if (std::getenv ("TPZ_FX")) { d.componentType = kAudioUnitType_Effect; d.componentSubType = 'Trrn'; }   // tpfx: open Terrain FX (the effect) instead
        AudioComponent c = nullptr;
        if (const char* bp = std::getenv ("TERRAIN_AU_BUNDLE"))
        {
            // load the BUILT bundle and register its factory in this process only
            NSBundle* b = [NSBundle bundleWithPath: [NSString stringWithUTF8String: bp]];
            if (b == nil || ! [b load]) { std::printf ("  !! cannot load %s\n", bp); return false; }
            CFBundleRef cb = CFBundleCreate (nullptr, (__bridge CFURLRef) b.bundleURL);
            auto factory = (AudioComponentFactoryFunction) CFBundleGetFunctionPointerForName (cb, std::getenv ("TPZ_FX") ? CFSTR ("Terrain_FXAUFactory") : CFSTR ("TerrainAUFactory"));
            if (factory == nullptr) { std::printf ("  !! no TerrainAUFactory in %s\n", bp); return false; }
            c = AudioComponentRegister (&d, CFSTR ("Waves Crate: Terrain (local build)"), 0x10000, factory);
            std::printf ("  using the BUILT bundle %s\n", bp);
        }
        else c = AudioComponentFindNext (nullptr, &d);
        if (! c) { std::printf ("  !! AU not found\n"); return false; }
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
                if (m == 1) for (int n : chord) MusicDeviceMIDIEvent (au, 0x90, (UInt32) n, 100, 0);
                if (m == 2) for (int n : chord) MusicDeviceMIDIEvent (au, 0x80, (UInt32) n, 0, 0);
                // tp112 — TPZ_PRESS=<0..127>: hold channel pressure on channel 1 while the chord sounds (the ring probe)
                static const int press = std::getenv ("TPZ_PRESS") ? atoi (std::getenv ("TPZ_PRESS")) : -1;
                if (press >= 0 && m == 1) MusicDeviceMIDIEvent (au, 0xD0, (UInt32) press, 0, 0);
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
};
int main (int argc, char** argv)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching]; [NSApp activateIgnoringOtherApps: YES];
        const char* scriptArg = argc > 1 ? argv[1] : "Tests/_tpz_measure.js";
        const double secs = argc > 2 ? atof (argv[2]) : 120.0;
        NSString* dir = [NSHomeDirectory() stringByAppendingPathComponent: std::getenv ("TPZ_FX") ? @"Library/Caches/Terrain FX" : @"Library/Caches/Terrain"];   /* tpfx: JUCE tempDirectory = the plugin binary name */
        [[NSFileManager defaultManager] createDirectoryAtPath: dir withIntermediateDirectories: YES attributes: nil error: nil];
        NSString* expPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp.js"];
        NSString* resPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp-result.txt"];
        NSString* script = [NSString stringWithContentsOfFile: [NSString stringWithUTF8String: scriptArg] encoding: NSUTF8StringEncoding error: nil];
        if (script == nil) { std::printf ("  !! script %s not found\n", scriptArg); return 2; }
        const char* args = std::getenv ("TPZ_ARGS");
        script = [NSString stringWithFormat: @"window.__tpzArgs=%s;\n%@", args ? args : "{}", script];
        AuHost h; if (! h.init()) return 1;
        h.startRender();
        std::printf ("\n== tp110 - THE PATCHER'S GESTURES, IN THE REAL WEBVIEW ==  args %s\n", args ? args : "{}");
        const auto wcBefore = pidsNamed ("WebContent"), gpuBefore = pidsNamed ("WebKit.GPU");
        [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
        [script writeToFile: expPath atomically: YES encoding: NSUTF8StringEncoding error: nil];
        NSView* v = nil; @autoreleasepool { v = h.makeView(); }
        if (v == nil) { std::printf ("  !! no view\n"); [[NSFileManager defaultManager] removeItemAtPath: expPath error: nil]; return 1; }
        NSWindow* w = [[NSWindow alloc] initWithContentRect: NSMakeRect (60, 60, v.frame.size.width, v.frame.size.height)
                                                  styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
        // TPZ_NO_OCCLUSION=1: WebKit ignores window occlusion (a locked screen) so rAF keeps running — the page
        // work is real but WindowServer composition is not; such numbers are marked and never the headline
        if (std::getenv ("TPZ_NO_OCCLUSION") != nullptr)
        {
            std::vector<NSView*> st { v };
            while (! st.empty()) { NSView* x = st.back(); st.pop_back();
                if ([x isKindOfClass: NSClassFromString (@"WKWebView")] && [x respondsToSelector: NSSelectorFromString (@"_setWindowOcclusionDetectionEnabled:")])
                { ((void (*)(id, SEL, BOOL)) objc_msgSend) (x, NSSelectorFromString (@"_setWindowOcclusionDetectionEnabled:"), NO); std::printf ("  occlusion detection OFF\n"); }
                for (NSView* c in x.subviews) st.push_back (c); }
        }
        w.releasedWhenClosed = NO; [w.contentView addSubview: v]; [w makeKeyAndOrderFront: nil];
        { const double t0 = nowMs(); while (! [[NSFileManager defaultManager] fileExistsAtPath: resPath]) { pumpMs (50); if (nowMs() - t0 > 30000) break; } }
        [[NSFileManager defaultManager] removeItemAtPath: expPath error: nil];   // ran once; never leave the hook armed
        std::printf ("  editor open, script started: %s", [[NSString stringWithContentsOfFile: resPath encoding: NSUTF8StringEncoding error: nil] UTF8String]);
        // OUR WebKit processes = the ones that appeared with the view
        std::vector<int> wc, gpu;
        for (int p : pidsNamed ("WebContent")) if (! wcBefore.count (p)) wc.push_back (p);
        for (int p : pidsNamed ("WebKit.GPU")) if (! gpuBefore.count (p)) gpu.push_back (p);
        std::printf ("  WebContent pids:"); for (int p : wc) std::printf (" %d", p);
        std::printf ("   GPU pids:"); for (int p : gpu) std::printf (" %d", p); std::printf ("\n");
        auto sumCpu = [] (const std::vector<int>& ps) { double s = 0; for (int p : ps) { const double c = cpuMsOf (p); if (c > 0) s += c; } return s; };
        h.midi.store (1);
        auto findWK = [] (NSView* root) -> NSView* {
            std::vector<NSView*> st { root };
            while (! st.empty()) { NSView* x = st.back(); st.pop_back();
                if ([x isKindOfClass: NSClassFromString (@"WKWebView")]) return x;
                for (NSView* c in x.subviews) st.push_back (c); }
            return nil; };
        NSString* out = nil; const double t0 = nowMs();
        NSMutableDictionary* parts = [NSMutableDictionary new]; long curGen = -1; long need = 0; bool struck = false;
        std::string phase = "boot"; double phT = nowMs(), phWc = sumCpu (wc), phGpu = sumCpu (gpu), phSelf = cpuMsOf (getpid());
        std::string cpuReport;
        // dev: WebKit's compositing borders + repaint counters (TPZ_BORDERS=1), and window shots
        // 900 ms into every phase (TPZ_SHOTS=<dir>) — screencapture -l of THIS window only
        if (std::getenv ("TPZ_BORDERS") != nullptr)
            if (NSView* wk0 = findWK (v)) { id prefs = [[wk0 valueForKey: @"configuration"] valueForKey: @"preferences"];
                ((void (*)(id, SEL, BOOL)) objc_msgSend) (prefs, NSSelectorFromString (@"_setCompositingBordersVisible:"), YES);
                ((void (*)(id, SEL, BOOL)) objc_msgSend) (prefs, NSSelectorFromString (@"_setCompositingRepaintCountersVisible:"), YES); }
        const char* shots = std::getenv ("TPZ_SHOTS"); int shotN = 0; double shotAt = -1; std::string shotPhase;
        while (nowMs() - t0 < secs * 1000.0)
        {
            pumpMs (15);
            if (shots && shotAt > 0 && nowMs() >= shotAt)
            {
                shotAt = -1; char cmd[1024];
                std::snprintf (cmd, sizeof cmd, "screencapture -x -o -l%ld '%s/%02d_%s.png'", (long) w.windowNumber, shots, shotN++, shotPhase.c_str());
                std::system (cmd);
                // TPZ_SHOTS_EVERY=<ms>: keep shooting through the zoom phases (evidence runs only — a
                // shot blocks this thread, which is WebKit's UI process, so timings of such a run are void)
                if (const char* ev = std::getenv ("TPZ_SHOTS_EVERY"))
                    if (shotPhase.rfind ("zoom", 0) == 0 && shotN < 80) shotAt = nowMs() + atof (ev);
            }
            NSView* wk = findWK (v); NSString* t = wk ? [wk valueForKey: @"title"] : nil;
            if (! t) continue;
            if ([t hasPrefix: @"tpzP:"])
            {
                std::string np = [[t substringFromIndex: 5] UTF8String];
                if (np != phase)
                {
                    const double tn = nowMs(), cw = sumCpu (wc), cg = sumCpu (gpu), cs = cpuMsOf (getpid()), dt = tn - phT;
                    char line[400]; std::snprintf (line, sizeof line, "  cpu %-22s %6.0f ms  WebContent %5.1f %%  GPU %5.1f %%  host %5.1f %%%s\n",
                                                    phase.c_str(), dt, 100.0 * (cw - phWc) / dt, 100.0 * (cg - phGpu) / dt, 100.0 * (cs - phSelf) / dt,
                                                    (w.occlusionState & NSWindowOcclusionStateVisible) ? "" : "  !! WINDOW OCCLUDED (display off / locked?) - numbers void");
                    cpuReport += line; std::printf ("%s", line); std::fflush (stdout);
                    phase = np; phT = tn; phWc = cw; phGpu = cg; phSelf = cs;
                    if (shots) { shotAt = tn + (std::getenv ("TPZ_SHOTS_EVERY") && np.rfind ("zoom", 0) == 0 ? 150 : 900); shotPhase = np; }
                    if (np == "cpu-silent") { h.midi.store (2); std::printf ("  chord released (silent rest)\n"); }
                    if (! struck && np == "built") { struck = true; h.midi.store (2); pumpMs (120); h.midi.store (1); std::printf ("  patch built - chord re-struck\n"); }
                }
                continue;
            }
            if (! [t hasPrefix: @"tp34:"]) continue;
            NSArray* hd = [[t substringFromIndex: 5] componentsSeparatedByString: @":"];
            if (hd.count < 3) continue;
            long g = [hd[0] integerValue]; NSArray* in = [hd[1] componentsSeparatedByString: @"/"]; if (in.count != 2) continue;
            long idx = [in[0] integerValue], n = [in[1] integerValue];
            NSString* chunk = [t substringFromIndex: 5 + [hd[0] length] + 1 + [hd[1] length] + 1];
            if (g != curGen) { curGen = g; need = n; [parts removeAllObjects]; }
            parts[@(idx)] = chunk;
            if ((long) parts.count == need)
            {
                NSMutableString* joined = [NSMutableString new];
                for (long k = 0; k < need; ++k) { NSString* c = parts[@(k)]; if (! c) { joined = nil; break; } [joined appendString: c]; }
                if (joined) { out = joined; if ([out containsString: @"\"phase\":\"done\""] || [out containsString: @"\"phase\":\"failed\""]) break; }
            }
        }
        h.midi.store (2); pumpMs (300);
        std::printf ("\nRESULT %s\n", out ? out.UTF8String : "(no result in the title)");
        std::printf ("\nCPU\n%s", cpuReport.c_str());
        [w orderOut: nil]; pumpMs (300); [v removeFromSuperview]; v = nil; pumpMs (400); [w close]; w = nil; pumpMs (300);
        [[NSFileManager defaultManager] removeItemAtPath: expPath error: nil];
        [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
        h.stopRender();
        return 0;
    }
}
