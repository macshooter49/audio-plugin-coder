// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb521 — THE MAC KEEP-ALIVE REOPEN HARNESS (raw AU host, the au_blk_cpu lineage).
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/mac_reopen.mm -o /tmp/mac_reopen \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//
//  WHY A RAW AU HOST AND NOT A JUCE ONE — the honest record (three wrong theories died here):
//  the JUCE-hosted variant "failed" because of the HOOK-DIR LAW below, not JUCE-in-JUCE; the
//  raw host then found two REAL traps this file now encodes: (1) createViewFor returns the AU
//  view AUTORELEASED — without an inner pool it pins in main's pool forever and the wrapper's
//  ONLY teardown path (dealloc → shutdown → deleteEditor) never runs; (2) wrapping the AU view
//  in our own NSWindow left a retain with the same effect. Windowless (addToDesktop gives the
//  editor its own desktop presence) + a creation pool = teardown identical to a real host:
//  release → showing false → park (fb516d order law held, trace-verified) → editor deleted.
//  A raw host also keeps this measuring the artifact Logic loads, the au_blk_cpu lineage.
//
//  Readiness signal (fb504/fb516a): terrain-ui-exp.js in the plugin's juce tempDirectory —
//  which, inside this host process, is ~/Library/Caches/<process name>. Its result file
//  appears on the first editor tick after pageReady; attach() re-arms it for every open.
//
//  Close order (fb516d): HIDE the window first (orderOut) — the shell's showing-watcher parks
//  the core there, the real-host moment — then release the view (the AU wrapper deletes the
//  editor when its view goes away), then close the window.
//
//    baseline (cold path):  TERRAIN_UI_CACHE=0 /tmp/mac_reopen --reopen
//    parked   (default 8):                     /tmp/mac_reopen --reopen
//
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#include <cstdio>
#include <cstring>
#include <mach/mach.h>
#include <cstdlib>

@protocol TIAUCocoaUIBase
- (NSView*) uiViewForAudioUnit: (AudioUnit) au withSize: (NSSize) s;
@end

static double nowMs() { return (double) clock_gettime_nsec_np (CLOCK_MONOTONIC) / 1.0e6; }

static double footprintMB()
{
    task_vm_info_data_t vm {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info (mach_task_self(), TASK_VM_INFO, (task_info_t) &vm, &count) != KERN_SUCCESS) return 0.0;
    return (double) vm.phys_footprint / (1024.0 * 1024.0);
}

static void pumpMs (double ms)
{
    const double t0 = nowMs();
    while (nowMs() - t0 < ms)
    {
        @autoreleasepool
        {
            NSEvent* e = [NSApp nextEventMatchingMask: NSEventMaskAny
                                            untilDate: [NSDate dateWithTimeIntervalSinceNow: 0.02]
                                               inMode: NSDefaultRunLoopMode
                                              dequeue: YES];
            if (e != nil) [NSApp sendEvent: e];
        }
    }
}

struct AuHost
{
    AudioUnit au = nullptr;

    bool init()
    {
        AudioComponentDescription d {};
        d.componentType = kAudioUnitType_MusicDevice;
        d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (c == nullptr) { std::printf ("  !! AU not found\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! instance failed\n"); return false; }
        UInt32 maxF = 512;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF);
        if (AudioUnitInitialize (au) != noErr) { std::printf ("  !! initialize failed\n"); return false; }
        return true;
    }

    NSView* makeView()
    {
        AudioUnitCocoaViewInfo info {};
        UInt32 sz = sizeof (info);
        const OSStatus st = AudioUnitGetProperty (au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &sz);
        if (st != noErr)
        { std::printf ("  !! CocoaUI property failed: %d\n", (int) st); std::fflush (stdout); return nil; }
        NSURL* url = (__bridge_transfer NSURL*) info.mCocoaAUViewBundleLocation;
        NSString* clsName = (__bridge_transfer NSString*) info.mCocoaAUViewClass[0];
        NSBundle* b = [NSBundle bundleWithURL: url];
        [b load];
        Class cls = [b classNamed: clsName];
        if (cls == nil) cls = NSClassFromString (clsName);
        if (cls == nil) { std::printf ("  !! view class '%s' not found\n", clsName.UTF8String); return nil; }
        id<TIAUCocoaUIBase> factory = [[cls alloc] init];
        NSView* v = [factory uiViewForAudioUnit: au withSize: NSMakeSize (820, 672)];
        std::printf ("    makeView: class=%s factory=%p view=%p\n", clsName.UTF8String, (__bridge void*) factory, (__bridge void*) v);
        std::fflush (stdout);
        return v;
    }
};

int main (int argc, char** argv)
{
    // fb521 -- when launched via LaunchServices/launchctl the console is gone: mirror stdout
    if (const char* lp = std::getenv ("TERRAIN_BENCH_LOG"))
        freopen (lp, "w", stdout);
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];   // fb521 -- WebKit gates on app/GUI state
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps: YES];

        bool two = false, probe = false;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp (argv[i], "--reopen2") == 0) two = true;
            if (std::strcmp (argv[i], "--probe") == 0) probe = true;
        }

        // ⚠️ THE HOOK-DIR LAW (cost three wrong theories): the PLUGIN's juce tempDirectory is
        // ~/Library/Caches/<executable name> where juce_getExecutableFile resolves via dladdr —
        // for code in a plugin DYLIB that is the COMPONENT BINARY "Terrain Instrument", NOT the
        // host process. The standalone matched only by coincidence of name. So the hook lives in
        // caches/"Terrain Instrument" in EVERY host.
        NSString* caches = NSSearchPathForDirectoriesInDomains (NSCachesDirectory, NSUserDomainMask, YES)[0];
        NSString* dir = [caches stringByAppendingPathComponent: @"Terrain Instrument"];
        [[NSFileManager defaultManager] createDirectoryAtPath: dir withIntermediateDirectories: YES attributes: nil error: nil];
        NSString* expPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp.js"];
        NSString* resPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp-result.txt"];
        std::printf ("\n  Terrain — Mac keep-alive reopen harness (raw AU host, fb521)\n  hookDir: %s\n\n", dir.UTF8String);

        if (probe)
        {
            // Retain bisect: which step pins the AU view? Watch stderr for the shell-dtor trace.
            AuHost h; if (! h.init()) return 1;
            std::printf ("── probe A: create → release, NO window\n"); std::fflush (stdout);
            { NSView* v = nil; @autoreleasepool { v = h.makeView(); } pumpMs (400); v = nil; pumpMs (600); }
            std::printf ("── probe A done (did shell dtor fire above?)\n"); std::fflush (stdout);
            std::printf ("── probe B: create → window → orderOut → remove → close → release\n"); std::fflush (stdout);
            {
                NSView* v = nil; @autoreleasepool { v = h.makeView(); }
                NSWindow* w = [[NSWindow alloc] initWithContentRect: NSMakeRect (80, 80, v.frame.size.width, v.frame.size.height)
                                                          styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
                w.releasedWhenClosed = NO;
                [w.contentView addSubview: v];
                [w makeKeyAndOrderFront: nil];
                pumpMs (800);
                [w orderOut: nil]; pumpMs (150);
                [v removeFromSuperview]; v = nil; pumpMs (300);
                [w close]; w = nil; pumpMs (600);
            }
            std::printf ("── probe B done (did shell dtor fire above?)\n"); std::fflush (stdout);
            return 0;
        }

        const int N = two ? 2 : 1;
        AuHost host[2];
        for (int i = 0; i < N; ++i)
            if (! host[i].init()) return 1;

        NSWindow* win[2] = { nil, nil };
        NSView*   view[2] = { nil, nil };

        auto openEd = [&] (int i, const char* tag, double* msOut) -> bool
        {
            [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
            NSString* js = [NSString stringWithFormat: @"'%s'", tag];
            [js writeToFile: expPath atomically: YES encoding: NSUTF8StringEncoding error: nil];
            const double t0 = nowMs();
            // ⚠️ THE AUTORELEASE TRAP (cost the whole first hour): createViewFor returns the AU
            // view AUTORELEASED, and without an inner pool that reference parks in main's pool
            // until process exit — the view never deallocs, the wrapper's dealloc->shutdown->
            // deleteEditor teardown (the ONLY close path real hosts have) never runs, and the
            // second uiViewForAudioUnit returns nil. Drain the creation pool immediately; ARC's
            // strong ref in view[i] is then the only one we hold.
            // fb521 WINDOWLESS HOST LAW (probe-bisected): wrapping the AU view in our own
            // NSWindow left a retain that blocked the wrapper's ONLY teardown path (dealloc →
            // shutdown → deleteEditor); bare create/release tears down perfectly, and JUCE's
            // addToDesktop inside createViewFor already gives the editor a desktop presence
            // (isShowing()=1, page boots, park-on-hide fires on release in the right order).
            @autoreleasepool { view[i] = host[i].makeView(); }
            if (view[i] == nil) return false;
            while (! [[NSFileManager defaultManager] fileExistsAtPath: resPath])
            {
                pumpMs (50);
                if (nowMs() - t0 > 30000.0)
                { std::printf ("  !! %s: page never became ready (30 s)\n", tag); return false; }
            }
            *msOut = nowMs() - t0;
            return true;
        };
        auto closeEd = [&] (int i)
        {
            // fb521 -- releasing the view is the close: dealloc → showing goes false (park
            // fires here, order-law intact per the probe trace) → deleteEditor.
            view[i] = nil;
            pumpMs (400);
        };

        if (! two)
        {
            double open1 = 0.0, reopen = 0.0;
            if (! openEd (0, "r1", &open1)) return 1;
            std::printf ("  OPEN1   %8.0f ms   (fp %6.0f MB)\n", open1, footprintMB());
            pumpMs (800);
            closeEd (0);
            std::printf ("  HIDDEN-PHASE-START  (fp %6.0f MB after close)\n", footprintMB());
            pumpMs (8000);
            std::printf ("  HIDDEN-PHASE-END    (fp %6.0f MB)\n", footprintMB());
            if (! openEd (0, "r2", &reopen)) { std::printf ("  !! r2 openEd FAILED (see above)\n"); return 1; }
            std::printf ("  REOPEN  %8.0f ms   (fp %6.0f MB)\n", reopen, footprintMB());
            pumpMs (400);
            closeEd (0);
            const char* cache = std::getenv ("TERRAIN_UI_CACHE");
            std::printf ("  CACHE   %s\n", cache != nullptr ? cache : "(default)");
            std::printf ("  verdict: %s\n\n", reopen < 600.0 ? "REOPEN IS INSTANT-CLASS (< 600 ms)" : "reopen is a cold boot");
            return reopen < 600.0 ? 0 : 2;
        }

        double a1=0, b1=0, a2=0, b2=0;
        if (! openEd (0, "A1", &a1)) return 1;
        pumpMs (800); closeEd (0);
        if (! openEd (1, "B1", &b1)) return 1;
        pumpMs (800); closeEd (1);
        pumpMs (1500);
        if (! openEd (0, "A2", &a2)) return 1;   // the OLDER park — the fb519 acceptance case
        pumpMs (400); closeEd (0);
        if (! openEd (1, "B2", &b2)) return 1;
        pumpMs (400); closeEd (1);
        const char* cache = std::getenv ("TERRAIN_UI_CACHE");
        std::printf ("  OPEN  A1 %6.0f ms   B1 %6.0f ms   (cold)\n", a1, b1);
        std::printf ("  REOPEN A2 %6.0f ms   B2 %6.0f ms   (cache=%s)   fp %6.0f MB\n",
                     a2, b2, cache != nullptr ? cache : "default", footprintMB());
        const bool bothInstant = a2 < 600.0 && b2 < 600.0;
        std::printf ("  verdict: %s\n\n", bothInstant ? "BOTH INSTANT — every instance keeps its park" : "inspect");
        return bothInstant ? 0 : 2;
    }
}
