// ══════════════════════════════════════════════════════════════════════════════════════════════
//  mac_dice_memory.mm — tp63 · DOES THE MEMORY STACK UP ON EVERY RANDOMIZE? (the installed AU, real editor)
//
//  Max: "every time I randomize my memory goes up ... why should my memory just keep scaling up every
//  time I want a random patch? ... it just won't go down."
//  One instance, its real editor in a real window, chord held. The fb504 experiment hook runs a script
//  in the page that opens the Patcher and rolls the dice every `gapMs`, N times, publishing the roll
//  number in document.title. Each roll the host process's phys_footprint (the DSP side: tables,
//  samples, engines) and the page's WebContent footprint are read and printed, so the answer is a
//  table, not a feeling.
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/mac_dice_memory.mm -o /tmp/tpdice \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    /tmp/tpdice <rolls> <gapMs>      (from the plugin root)
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#include <libproc.h>
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
static double selfMB() { struct rusage_info_v4 ri {}; if (proc_pid_rusage (getpid(), RUSAGE_INFO_V4, (rusage_info_t*) &ri) != 0) return 0; return ri.ri_phys_footprint / 1e6; }
static std::set<pid_t> webPids()
{
    std::set<pid_t> out; int n = proc_listpids (PROC_ALL_PIDS, 0, nullptr, 0); std::vector<pid_t> pids ((size_t) n + 64);
    n = proc_listpids (PROC_ALL_PIDS, 0, pids.data(), (int) (pids.size() * sizeof (pid_t))) / (int) sizeof (pid_t);
    for (int i = 0; i < n; ++i) { if (pids[(size_t) i] <= 0) continue; char path[PROC_PIDPATHINFO_MAXSIZE] = {};
        if (proc_pidpath (pids[(size_t) i], path, sizeof path) > 0 && std::strstr (path, "WebKit.WebContent")) out.insert (pids[(size_t) i]); }
    return out;
}
static double webMB (const std::set<pid_t>& only)
{ double m = 0; for (pid_t p : only) { struct rusage_info_v4 ri {}; if (proc_pid_rusage (p, RUSAGE_INFO_V4, (rusage_info_t*) &ri) == 0) m += ri.ri_phys_footprint / 1e6; } return m; }

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        setvbuf (stdout, nullptr, _IONBF, 0);
        [NSApplication sharedApplication]; [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular]; [NSApp finishLaunching]; [NSApp activateIgnoringOtherApps: YES];
        const int rolls = argc > 1 ? atoi (argv[1]) : 12; const int gapMs = argc > 2 ? atoi (argv[2]) : 4000;
        NSString* dir = [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Caches/Terrain"];
        [[NSFileManager defaultManager] createDirectoryAtPath: dir withIntermediateDirectories: YES attributes: nil error: nil];
        NSString* expPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp.js"];
        NSString* resPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp-result.txt"];
        // the script: open the Patcher, roll the dice every gap, say which roll in the title
        NSString* script = [NSString stringWithFormat:
            @"(function(){ try{ window.setActivePanel('tp'); }catch(e){} var n=0, N=%d; document.title='dice:0';"
             "setTimeout(function step(){ n++; try{ if(window.__tpDice) window.__tpDice(); }catch(e){} document.title='dice:'+n; if(n<N) setTimeout(step,%d); else setTimeout(function(){ document.title='dice:done'; },%d); }, 2500); return 'armed'; })()", rolls, gapMs, gapMs];
        const std::set<pid_t> before = webPids();
        AuHost h; if (! h.init()) return 1; h.startRender();
        [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
        [script writeToFile: expPath atomically: YES encoding: NSUTF8StringEncoding error: nil];
        NSView* v = nil; @autoreleasepool { v = h.makeView(); } if (v == nil) { std::printf ("  !! no view\n"); return 1; }
        NSWindow* w = [[NSWindow alloc] initWithContentRect: NSMakeRect (40, 40, v.frame.size.width, v.frame.size.height) styleMask: NSWindowStyleMaskTitled backing: NSBackingStoreBuffered defer: NO];
        w.releasedWhenClosed = NO; [w.contentView addSubview: v]; [w makeKeyAndOrderFront: nil];
        { const double t0 = nowMs(); while (! [[NSFileManager defaultManager] fileExistsAtPath: resPath]) { pumpMs (50); if (nowMs() - t0 > 30000) break; } }
        h.midi.store (1); pumpMs (1500);
        // tp63 — the chord is STRUCK per roll and RELEASED between rolls: an engine is only given back
        //  while no voice is sounding (a tail may still be using it), and a musician stops playing.
        h.midi.store (2); pumpMs (200);
        std::set<pid_t> mine; for (pid_t p : webPids()) if (! before.count (p)) mine.insert (p);
        auto findWK = [] (NSView* root) -> NSView* { std::vector<NSView*> st { root };
            while (! st.empty()) { NSView* x = st.back(); st.pop_back(); if ([x isKindOfClass: NSClassFromString (@"WKWebView")]) return x; for (NSView* c in x.subviews) st.push_back (c); } return nil; };
        std::printf ("\n== tp63 — MEMORY PER RANDOMIZE · %d rolls, %d ms apart, real editor, chord held ==\n\n", rolls, gapMs);
        std::printf ("  roll   host MB   page MB   (host delta since roll 0)\n");
        double host0 = -1; NSString* last = nil; const double t0 = nowMs(); int printed = -1;
        while (nowMs() - t0 < (rolls + 4) * (double) gapMs + 8000)
        {
            pumpMs (100);
            NSView* wk = findWK (v); NSString* t = wk ? [wk valueForKey: @"title"] : nil;
            if (! t || [t isEqualToString: last] || ! [t hasPrefix: @"dice:"]) continue;
            last = t;
            if ([t isEqualToString: @"dice:done"]) break;
            const int n = [[t substringFromIndex: 5] intValue];
            h.midi.store (1); pumpMs (1500); h.midi.store (2);        // play the new patch, then let go
            pumpMs ((double) gapMs * 0.6);   // let the roll's loads and builds land — and the idle release fire — before weighing
            const double hm = selfMB(), wm = webMB (mine); if (host0 < 0) host0 = hm;
            if (n != printed) { std::printf ("  %4d   %7.0f   %7.0f   (%+.0f)\n", n, hm, wm, hm - host0); printed = n; }
        }
        h.midi.store (2); pumpMs (300);
        [w orderOut: nil]; pumpMs (300); [v removeFromSuperview]; v = nil; pumpMs (400); [w close]; w = nil; pumpMs (300);
        [[NSFileManager defaultManager] removeItemAtPath: expPath error: nil]; [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
        h.stopRender(); h.close();
        return 0;
    }
}
