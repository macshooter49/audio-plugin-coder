// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp105 — THE ORGANICS VIEW IN THE REAL WEBVIEW (Agent V). A raw AU host in the mac_ui_exp.mm lineage, with three
//  differences that the Organics round needed:
//    1. it loads the AU from a BUNDLE PATH (the worktree's build) and registers it in-process under its own subtype
//       ('TerV'), so nothing is installed into ~/Library and the installed Terrain is never touched;
//    2. it drives the page with WKWebView evaluateJavaScript directly (no title channel): the driver script (argv[2])
//       defines window.__orgReal.next(result) → a command, and the host answers each command:
//         {cmd:'wait', ms}                       · sleep, then call next(null)
//         {cmd:'notes', notes:[…], vel}          · hold exactly these notes (note-offs for the rest)
//         {cmd:'run', notes:[…], hz}             · a fast run: step through `notes` at hz notes/s, one sounding at a time
//         {cmd:'snap', path}                     · WKWebView takeSnapshot → PNG at `path`, result = base64 + size
//         {cmd:'cpu', secs}                      · CPU over `secs` s of wall time: this process (audio + message
//                                                  thread), the page's WebContent process, the WebKit GPU process,
//                                                  and the plugin's own probe line (TERRAIN_CPU_PROBE → terrain-cpu.txt)
//         {cmd:'log', text}                      · print
//         {cmd:'done', result}                   · print the result, write it to argv[3], exit
//    3. the plugin's built-in CPU probe is armed by setting TERRAIN_CPU_PROBE in this process (no marker file).
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/mac_org_ui.mm -o /tmp/orgui -framework Cocoa -framework AudioToolbox \
//            -framework AudioUnit -framework CoreFoundation -framework WebKit
//    /tmp/orgui <Terrain.component> <driver.js> <result.json> [seconds cap]
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <AudioToolbox/AudioToolbox.h>
#include <libproc.h>
#include <objc/message.h>
#include <mach/mach_time.h>
#include <sys/resource.h>
#include <cstdio>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <set>
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
            NSEvent* e = [NSApp nextEventMatchingMask: NSEventMaskAny untilDate: [NSDate dateWithTimeIntervalSinceNow: 0.01]
                                               inMode: NSDefaultRunLoopMode dequeue: YES];
            if (e != nil) [NSApp sendEvent: e]; }
}
static double ticksToMs (uint64_t t) { static mach_timebase_info_data_t tb {}; if (tb.denom == 0) mach_timebase_info (&tb); return (double) t * tb.numer / tb.denom / 1.0e6; }
static double procCpuMs (int pid) { rusage_info_v2 ri {}; if (pid <= 0 || proc_pid_rusage (pid, RUSAGE_INFO_V2, (rusage_info_t*) &ri) != 0) return -1; return ticksToMs (ri.ri_user_time + ri.ri_system_time); }
static double selfCpuMs() { rusage u {}; getrusage (RUSAGE_SELF, &u); return u.ru_utime.tv_sec * 1000.0 + u.ru_utime.tv_usec / 1000.0 + u.ru_stime.tv_sec * 1000.0 + u.ru_stime.tv_usec / 1000.0; }
static std::set<int> pidsNamed (const char* frag)
{
    std::set<int> out; std::vector<int> p (4096); int n = proc_listallpids (p.data(), (int) (p.size() * sizeof (int)));
    for (int i = 0; i < n; ++i) { char nm[256] = {}; if (proc_name (p[i], nm, sizeof nm) > 0 && strstr (nm, frag)) out.insert (p[i]);
        else { char path[PROC_PIDPATHINFO_MAXSIZE] = {}; if (proc_pidpath (p[i], path, sizeof path) > 0 && strstr (path, frag)) out.insert (p[i]); } }
    return out;
}

static bool busy = false; static NSString* cmdStr = nil;   // the evaluateJavaScript completion's mailbox (file scope: a block and a lambda both reach it)
struct AuHost
{
    AudioUnit au = nullptr; std::thread render; std::atomic<bool> stop { false };
    std::atomic<int> want[16]; std::atomic<int> nWant { 0 }, runHz { 0 }, vel { 100 }; std::atomic<int> gen { 0 };
    AuHost() { for (auto& w : want) w.store (-1); }
    bool init (const char* bundlePath)
    {
        CFURLRef url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) bundlePath, (CFIndex) strlen (bundlePath), true);
        CFBundleRef b = CFBundleCreate (nullptr, url); CFRelease (url);
        if (! b || ! CFBundleLoadExecutable (b)) { std::printf ("  !! cannot load %s\n", bundlePath); return false; }
        auto fac = (AudioComponentFactoryFunction) CFBundleGetFunctionPointerForName (b, CFSTR ("TerrainAUFactory"));
        if (! fac) { std::printf ("  !! no TerrainAUFactory\n"); return false; }
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'TerV'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentRegister (&d, CFSTR ("Waves Crate: Terrain (worktree)"), 0x10000, fac);
        if (! c) { std::printf ("  !! register failed\n"); return false; }
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
            std::set<int> on; long blk = 0; int runIdx = 0; double runAcc = 0;
            while (! stop.load())
            {
                std::set<int> tgt; const int n = nWant.load(), hz = runHz.load();
                if (hz > 0 && n > 0) { runAcc += (double) BLK / SR * hz; if (runAcc >= 1.0) { runAcc -= 1.0; runIdx = (runIdx + 1) % n; } tgt.insert (want[runIdx % n].load()); }
                else for (int k = 0; k < n; ++k) tgt.insert (want[k].load());
                for (int x : on) if (! tgt.count (x)) MusicDeviceMIDIEvent (au, 0x80, (UInt32) x, 0, 0);
                for (int x : tgt) if (x >= 0 && ! on.count (x)) MusicDeviceMIDIEvent (au, 0x90, (UInt32) x, (UInt32) vel.load(), 0);
                on = tgt; on.erase (-1);
                abl->mNumberBuffers = 2;
                abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = l.data();
                abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = r.data();
                AudioUnitRenderActionFlags flags = 0; AudioTimeStamp ts {}; ts.mSampleTime = sampleTime; ts.mFlags = kAudioTimeStampSampleTimeValid;
                AudioUnitRender (au, &flags, &ts, 0, (UInt32) BLK, abl);
                sampleTime += BLK; next += period; ++blk; std::this_thread::sleep_until (next);
            }
            for (int x : on) MusicDeviceMIDIEvent (au, 0x80, (UInt32) x, 0, 0);
        });
    }
    void setNotes (NSArray* a, int hz) { int n = 0; for (NSNumber* x in a) { if (n < 16) want[n++].store (x.intValue); } nWant.store (n); runHz.store (hz); }
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
        if (argc < 4) { std::printf ("usage: orgui <Terrain.component> <driver.js> <result.json> [cap s]\n"); return 2; }
        setenv ("TERRAIN_CPU_PROBE", "1", 1);
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching]; [NSApp activateIgnoringOtherApps: YES];
        NSString* driver = [NSString stringWithContentsOfFile: [NSString stringWithUTF8String: argv[2]] encoding: NSUTF8StringEncoding error: nil];
        if (driver == nil) { std::printf ("  !! no driver script\n"); return 2; }
        const double cap = argc > 4 ? atof (argv[4]) : 900.0;
        const std::set<int> gpuBefore = pidsNamed ("WebKit.GPU"), wcBefore = pidsNamed ("WebKit.WebContent");
        AuHost h; if (! h.init (argv[1])) return 1;
        h.startRender();
        NSView* v = nil; @autoreleasepool { v = h.makeView(); }
        if (v == nil) { std::printf ("  !! no view\n"); return 1; }
        NSWindow* w = [[NSWindow alloc] initWithContentRect: NSMakeRect (40, 40, MAX (1500.0, v.frame.size.width), MAX (1000.0, v.frame.size.height))
                                                  styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
        w.releasedWhenClosed = NO;
        /* ORGUI_RETINA=1: open on a 2x screen (the Mac's built-in Retina panel), ORGUI_RETINA=0 / unset: the main screen */
        if (getenv ("ORGUI_RETINA")) for (NSScreen* sc in [NSScreen screens]) if (sc.backingScaleFactor >= 2.0) { NSRect f = sc.visibleFrame; [w setFrameOrigin: NSMakePoint (f.origin.x + 20, f.origin.y + 20)]; break; }
        [w.contentView addSubview: v]; [w makeKeyAndOrderFront: nil];
        std::printf ("  window backing scale %.1f\n", w.backingScaleFactor);
        auto findWK = [] (NSView* root) -> WKWebView* { std::vector<NSView*> st { root };
            while (! st.empty()) { NSView* x = st.back(); st.pop_back(); if ([x isKindOfClass: [WKWebView class]]) return (WKWebView*) x; for (NSView* c in x.subviews) st.push_back (c); }
            return nil; };
        WKWebView* wk = nil; { const double t0 = nowMs(); while (! (wk = findWK (v)) && nowMs() - t0 < 20000) pumpMs (50); }
        if (! wk) { std::printf ("  !! no WKWebView\n"); return 1; }
        std::printf ("  editor %s  wk %s\n", NSStringFromRect (v.frame).UTF8String, NSStringFromRect (wk.frame).UTF8String);
        int wcPid = 0; { SEL sel = NSSelectorFromString (@"_webProcessIdentifier"); if ([wk respondsToSelector: sel]) wcPid = ((int (*)(id, SEL)) objc_msgSend) (wk, sel); }
        if (wcPid <= 0) { for (int p : pidsNamed ("WebKit.WebContent")) if (! wcBefore.count (p)) wcPid = p; }
        std::set<int> gpuNow = pidsNamed ("WebKit.GPU"); int gpuPid = 0; for (int p : gpuNow) if (! gpuBefore.count (p)) gpuPid = p;
        std::printf ("  WebContent pid %d  GPU pid %d (new)\n", wcPid, gpuPid); std::fflush (stdout);

        id lastResult = [NSNull null];
        auto evalJs = [wk] (NSString* js) { busy = true; cmdStr = nil;
            [wk evaluateJavaScript: js completionHandler: ^(id res, NSError* err) { cmdStr = [res isKindOfClass: [NSString class]] ? (NSString*) res : (err ? [NSString stringWithFormat: @"{\"cmd\":\"err\",\"e\":%@}", [[NSString alloc] initWithData: [NSJSONSerialization dataWithJSONObject: @[ err.localizedDescription ?: @"?" ] options: 0 error: nil] encoding: NSUTF8StringEncoding]] : @"{\"cmd\":\"none\"}"); busy = false; }]; };
        auto waitEval = []() { const double t0 = nowMs(); while (busy && nowMs() - t0 < 30000) pumpMs (5); };
        // the page must be up (the line-kit loaded) before the driver goes in; the driver goes in again if the page reloads
        { const double t0 = nowMs(); for (;;) { evalJs (@"String(!!(window.__orgArt && document.readyState === 'complete'))"); waitEval(); if ([cmdStr isEqualToString: @"true"]) break; if (nowMs() - t0 > 30000) { std::printf ("  !! page never came up\n"); return 1; } pumpMs (200); } }
        pumpMs (2500);
        evalJs (driver); waitEval();
        std::printf ("  driver in\n"); std::fflush (stdout);
        const double tStart = nowMs(); NSString* beaconPath = [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Caches/Terrain/terrain-cpu.txt"];
        NSString* final = nil;
        while (nowMs() - tStart < cap * 1000.0)
        {
            NSData* rj = [NSJSONSerialization dataWithJSONObject: @[ lastResult ] options: 0 error: nil];
            NSString* arg = [[NSString alloc] initWithData: rj encoding: NSUTF8StringEncoding];
            evalJs ([NSString stringWithFormat: @"(function(){ if (!window.__orgReal) return '{\"cmd\":\"reinject\"}'; var a = %@; return JSON.stringify(window.__orgReal.next(a[0])); })()", arg]); waitEval();
            lastResult = [NSNull null];
            NSDictionary* c = cmdStr ? [NSJSONSerialization JSONObjectWithData: [cmdStr dataUsingEncoding: NSUTF8StringEncoding] options: 0 error: nil] : nil;
            NSString* k = c[@"cmd"] ?: @"none";
            if ([k isEqualToString: @"reinject"]) { static int nRe = 0; if (++nRe > 5) { std::printf ("  !! the driver will not install (a syntax error in it?) - giving up\n"); break; } std::printf ("  (page reloaded - driver re-injected)\n"); pumpMs (2500); evalJs (driver); waitEval(); continue; }
            if ([k isEqualToString: @"wait"] || [k isEqualToString: @"none"]) { pumpMs (c[@"ms"] ? [c[@"ms"] doubleValue] : 60); continue; }
            if ([k isEqualToString: @"err"]) { std::printf ("  !! js error %s\n", cmdStr.UTF8String); pumpMs (200); continue; }
            if ([k isEqualToString: @"log"]) { std::printf ("  | %s\n", [c[@"text"] description].UTF8String); std::fflush (stdout); continue; }
            if ([k isEqualToString: @"notes"] || [k isEqualToString: @"run"]) { if (c[@"vel"]) h.vel.store ([c[@"vel"] intValue]); h.setNotes (c[@"notes"] ?: @[], [k isEqualToString: @"run"] ? [c[@"hz"] intValue] : 0); pumpMs (30); continue; }
            if ([k isEqualToString: @"snap"])
            {
                __block NSImage* img = nil; __block bool got = false;
                WKSnapshotConfiguration* cfg = [WKSnapshotConfiguration new]; const double sc = c[@"scale"] ? [c[@"scale"] doubleValue] : 1.0;
                if (sc != 1.0) cfg.snapshotWidth = @(wk.bounds.size.width * sc);
                [wk takeSnapshotWithConfiguration: cfg completionHandler: ^(NSImage* i, NSError*) { img = i; got = true; }];
                const double t0 = nowMs(); while (! got && nowMs() - t0 < 10000) pumpMs (5);
                if (! img) { lastResult = @{ @"err": @"no snapshot" }; continue; }
                CGImageRef cg = [img CGImageForProposedRect: nil context: nil hints: nil];
                NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage: cg];
                NSData* png = [rep representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
                if (c[@"path"]) [png writeToFile: c[@"path"] atomically: YES];
                lastResult = @{ @"b64": [png base64EncodedStringWithOptions: 0], @"w": @(CGImageGetWidth (cg)), @"h": @(CGImageGetHeight (cg)) };
                continue;
            }
            if ([k isEqualToString: @"cpu"])
            {
                const double secs = [c[@"secs"] doubleValue] > 0 ? [c[@"secs"] doubleValue] : 8.0;
                const double s0 = selfCpuMs(), w0 = procCpuMs (wcPid), g0 = procCpuMs (gpuPid), t0 = nowMs();
                pumpMs (secs * 1000.0);
                const double wall = nowMs() - t0, s1 = selfCpuMs(), w1 = procCpuMs (wcPid), g1 = procCpuMs (gpuPid);
                NSString* bc = [NSString stringWithContentsOfFile: beaconPath encoding: NSUTF8StringEncoding error: nil] ?: @"";
                lastResult = @{ @"wall": @(wall), @"host": @((s1 - s0) / wall * 100.0), @"webcontent": @(w0 < 0 ? -1 : (w1 - w0) / wall * 100.0),
                                @"gpu": @(g0 < 0 ? -1 : (g1 - g0) / wall * 100.0), @"beacon": bc, @"occluded": @((int) ! (w.occlusionState & NSWindowOcclusionStateVisible)) };
                continue;
            }
            if ([k isEqualToString: @"done"]) { NSData* d = [NSJSONSerialization dataWithJSONObject: c[@"result"] ?: @{} options: NSJSONWritingPrettyPrinted error: nil];
                final = [[NSString alloc] initWithData: d encoding: NSUTF8StringEncoding]; [final writeToFile: [NSString stringWithUTF8String: argv[3]] atomically: YES encoding: NSUTF8StringEncoding error: nil]; break; }
            pumpMs (50);
        }
        h.setNotes (@[], 0); pumpMs (400);
        std::printf ("%s\n", final ? "  result written" : "  !! no result (cap reached)");
        [w orderOut: nil]; pumpMs (300); [v removeFromSuperview]; v = nil; pumpMs (400); [w close]; w = nil; pumpMs (300);
        h.stopRender();
        return final ? 0 : 1;
    }
}
