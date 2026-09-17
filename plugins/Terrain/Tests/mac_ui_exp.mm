// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp35 — a GENERIC twin of mac_patcher_fps.mm: any experiment script, in the real WebView.
//    /tmp/tpexp <script.js> <title-tag> <seconds>   (the script publishes '<tag>:<gen>:<i>/<n>:<chunk>' in document.title)
//
//  Max: "when I zoom out it's laggy, when I zoom in it starts to get smooth ... when I generate a
//  random preset it lags." Headless Chrome cannot see this: the painters are driven by the C++
//  push lane (window.__tiFrame), which does not exist without an editor. So this is a raw AU host
//  (the mac_idle_frames lineage): the REAL installed AU, its REAL editor, audio rendered at
//  real-time pace on a second thread with a chord held so the lane stays live, and
//  SCRIPT_ARG run INSIDE the page by the fb504 experiment hook. The page is kept
//  alive across a close (fb521), so the numbers are read back by a second open.
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/mac_patcher_fps.mm -o /tmp/tpfps \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    /tmp/tpfps            (from the plugin root, so it finds SCRIPT_ARG)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#include <cstdio>
#include <unistd.h>
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
                if (m == 1) for (int n : chord) MusicDeviceMIDIEvent (au, 0x90, (UInt32) n, 100, 0);
                if (m == 2) for (int n : chord) MusicDeviceMIDIEvent (au, 0x80, (UInt32) n, 0, 0);
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
        const char* scriptPath = argc > 1 ? argv[1] : "Tests/_tp35_measure.js"; NSString* tag = [NSString stringWithFormat: @"%s:", argc > 2 ? argv[2] : "tp35"]; const double secs = argc > 3 ? atof (argv[3]) : 60.0;
        NSString* dir = [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Caches/Terrain"];
        [[NSFileManager defaultManager] createDirectoryAtPath: dir withIntermediateDirectories: YES attributes: nil error: nil];
        NSString* expPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp.js"];
        NSString* resPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp-result.txt"];
        NSString* script = [NSString stringWithContentsOfFile: [NSString stringWithUTF8String: scriptPath] encoding: NSUTF8StringEncoding error: nil];
        if (script == nil) { std::printf ("  !! run from the plugin root: script not found\n"); return 2; }
        AuHost h; if (! h.init()) return 1;
        h.startRender();
        std::printf ("\n== ui experiment %s in the real WebView ==\n\n", scriptPath);
        // open 1: the measurement script goes in, the chord starts, the page works for `secs`
        [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
        [script writeToFile: expPath atomically: YES encoding: NSUTF8StringEncoding error: nil];
        NSView* v = nil; @autoreleasepool { v = h.makeView(); }
        if (v == nil) { std::printf ("  !! no view\n"); return 1; }
        // rAF only fires for a view in a VISIBLE window — windowless (the mac_reopen law for
        // teardown) starves it and every measurement stalls. So: a real window, and fb521's close
        // order on the way out (orderOut → remove the subview → release → close).
        NSWindow* w = [[NSWindow alloc] initWithContentRect: NSMakeRect (40, 40, MAX (1500.0, v.frame.size.width), MAX (1000.0, v.frame.size.height))
                                                  styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
        w.releasedWhenClosed = NO; [w.contentView addSubview: v]; [w makeKeyAndOrderFront: nil];
        { const double t0 = nowMs(); while (! [[NSFileManager defaultManager] fileExistsAtPath: resPath]) { pumpMs (50); if (nowMs() - t0 > 30000) break; } }
        std::printf ("  editor open, script started: %s", [[NSString stringWithContentsOfFile: resPath encoding: NSUTF8StringEncoding error: nil] UTF8String]);
        h.midi.store (1);
        std::printf ("  chord held, measuring (up to %.0f s) ...\n", secs); std::fflush (stdout);
        // the WKWebView's title IS the result channel (see _tp34_measure.js publish())
        auto findWK = [] (NSView* root) -> NSView* {
            std::vector<NSView*> st { root };
            while (! st.empty()) { NSView* x = st.back(); st.pop_back();
                if ([x isKindOfClass: NSClassFromString (@"WKWebView")]) return x;
                for (NSView* c in x.subviews) st.push_back (c); }
            return nil; };
        NSString* out = nil; const double t0 = nowMs();
        // reassemble 'tp34:<gen>:<i>/<n>:<chunk>' — WebKit clamps a title to 1000 chars
        NSMutableDictionary* parts = [NSMutableDictionary new]; long curGen = -1; long need = 0; bool struck = false;
        // correlate the plugin's stall log with the script's phase: any growth of terrain-stall.txt
        // is printed with the phase the page was in and the elapsed time
        NSString* stallPath = [dir stringByAppendingPathComponent: @"terrain-stall.txt"];
        unsigned long long stallSz = [[[NSFileManager defaultManager] attributesOfItemAtPath: stallPath error: nil] fileSize];
        NSString* lastPhase = @"?"; NSString* lastRaw = nil; double lastChange = nowMs();
        static std::atomic<double> wdLast { 0 }; wdLast.store (nowMs()); static std::atomic<bool> wdStop { false }; static bool sampled = false;
        /* tp35 — the pop-out's first frames, photographed: when terrain-card-trace.txt (TERRAIN_CARD_TRACE) grows with a
           "popOut " line, the screen is captured at +60, +200 and +700 ms (/tmp/tpexp_popout_0/1/2.png) */
        std::thread shot ([] { NSString* tp = [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Caches/Terrain/terrain-card-trace.txt"]; unsigned long long last = 0; bool done = false;
            while (! wdStop.load() && ! done) { std::this_thread::sleep_for (std::chrono::milliseconds (5));
                unsigned long long sz = [[[NSFileManager defaultManager] attributesOfItemAtPath: tp error: nil] fileSize];
                if (sz > last) { NSString* all = [NSString stringWithContentsOfFile: tp encoding: NSUTF8StringEncoding error: nil]; last = sz;
                    if ([all rangeOfString: @"popOut "].location != NSNotFound) { std::this_thread::sleep_for (std::chrono::milliseconds (60)); system ("screencapture -x -R 0,0,1400,1000 /tmp/tpexp_popout_0.png"); std::this_thread::sleep_for (std::chrono::milliseconds (120)); system ("screencapture -x -R 0,0,1400,1000 /tmp/tpexp_popout_1.png"); std::this_thread::sleep_for (std::chrono::milliseconds (500)); system ("screencapture -x -R 0,0,1400,1000 /tmp/tpexp_popout_2.png"); done = true; } } } });
        std::thread wd ([] { while (! wdStop.load()) { std::this_thread::sleep_for (std::chrono::milliseconds (500));
            if (nowMs() - wdLast.load() > 3000.0 && ! sampled) { std::printf ("  !! watchdog: no title change for 5 s - sampling the process (main thread stack -> /tmp/tpexp_sample.txt)\n"); std::fflush (stdout);
                char cmd[512]; std::snprintf (cmd, sizeof (cmd), "sample %d 2 -mayDie -file /tmp/tpexp_sample.txt > /dev/null 2>&1", (int) getpid()); sampled = true; system (cmd);
                /* the JS runs in the WebContent process: sample the busiest one (ours is the one burning CPU) */
                system ("P=$(ps -Ao pid,%cpu,comm | grep -i 'WebKit.WebContent' | sort -k2 -nr | head -1 | awk '{print $1}'); [ -n \"$P\" ] && sample $P 2 -mayDie -file /tmp/tpexp_webcontent.txt > /dev/null 2>&1; ps -Ao pid,%cpu,comm | grep -i 'WebKit.WebContent' | sort -k2 -nr | head -3 > /tmp/tpexp_webcontent_ps.txt"); wdLast.store (nowMs() + 1e9); } } });
        while (nowMs() - t0 < secs * 1000.0)
        {
            pumpMs (40);   // poll well under the 250 ms chunk rotation, or chunks are missed
            NSView* wk = findWK (v); NSString* t = wk ? [wk valueForKey: @"title"] : nil;
            if (t && ! [t isEqualToString: lastRaw]) { lastRaw = t; lastChange = nowMs(); wdLast.store (lastChange); }
            if (nowMs() - lastChange > 25000.0) { std::printf ("  !! the title has not changed for 25 s - the page is stuck (or the script ended without 'done')\n"); break; }
            if (! t || ! [t hasPrefix: tag]) continue;
            NSArray* hd = [[t substringFromIndex: tag.length] componentsSeparatedByString: @":"];
            if (hd.count < 3) continue;
            long g = [hd[0] integerValue]; NSArray* in = [hd[1] componentsSeparatedByString: @"/"]; if (in.count != 2) continue;
            long idx = [in[0] integerValue], n = [in[1] integerValue];
            NSString* chunk = [t substringFromIndex: tag.length + [hd[0] length] + 1 + [hd[1] length] + 1];
            { NSRange pr = [chunk rangeOfString: @"\"phase\":\""]; if (pr.location != NSNotFound) { NSString* rest = [chunk substringFromIndex: pr.location + pr.length];
                NSRange q = [rest rangeOfString: @"\""]; if (q.location != NSNotFound) lastPhase = [rest substringToIndex: q.location]; } }
            { unsigned long long sz = [[[NSFileManager defaultManager] attributesOfItemAtPath: stallPath error: nil] fileSize];
              if (sz > stallSz) { NSString* all = [NSString stringWithContentsOfFile: stallPath encoding: NSUTF8StringEncoding error: nil];
                  NSString* tailS = all.length > 110 ? [all substringFromIndex: all.length - 110] : all;
                  std::printf ("  [%5.1f s, phase %s] STALL LOG GREW: ...%s", (nowMs() - t0) / 1000.0, lastPhase.UTF8String,
                               [[tailS stringByReplacingOccurrencesOfString: @"\n" withString: @" | "] UTF8String]); std::printf ("\n"); std::fflush (stdout); stallSz = sz; } }
            if (g != curGen) { curGen = g; need = n; [parts removeAllObjects]; }
            // the patch is built once the script reaches 'measure': strike the chord THEN, so the
            // enabled oscillators actually sound and the push lane has something to ship
            if (! struck) { struck = true; h.midi.store (2); pumpMs (120); h.midi.store (1); std::printf ("  patch built - chord re-struck\n"); std::fflush (stdout); }
            parts[@(idx)] = chunk;
            if ((long) parts.count == need)
            {
                NSMutableString* joined = [NSMutableString new];
                for (long k = 0; k < need; ++k) { NSString* c = parts[@(k)]; if (! c) { joined = nil; break; } [joined appendString: c]; }
                if (joined) { out = joined; if ([out containsString: @"\"phase\":\"done\""] || [out containsString: @"\"phase\":\"failed\""]) break; }
            }
        }
        wdStop.store (true); wd.join(); shot.join();
        h.midi.store (2); pumpMs (300);
        std::printf ("\n%s\n", out ? out.UTF8String : "(no result in the title)");
        { NSView* wk = findWK (v); std::printf ("  geometry: window %s  editor view %s  wk %s  occluded=%d hidden=%d\n", NSStringFromRect (w.frame).UTF8String, NSStringFromRect (v.frame).UTF8String, wk ? NSStringFromRect (wk.frame).UTF8String : "-", (int) ! (w.occlusionState & NSWindowOcclusionStateVisible), (int) (wk ? wk.hiddenOrHasHiddenAncestor : -1)); }
        std::printf ("  last raw title: %s\n", lastRaw ? [[lastRaw substringToIndex: MIN (1000, lastRaw.length)] UTF8String] : "(none)");
        [w orderOut: nil]; pumpMs (300); [v removeFromSuperview]; v = nil; pumpMs (400); [w close]; w = nil; pumpMs (300);
        [[NSFileManager defaultManager] removeItemAtPath: expPath error: nil];   // never leave the hook armed
        [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
        h.stopRender();
        return 0;
    }
}
