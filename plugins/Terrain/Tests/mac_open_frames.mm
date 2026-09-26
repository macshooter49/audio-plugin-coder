// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tpsz — THE OPEN FILM. Every visible frame of Terrain's window while it opens, in a raw AU host.
//
//  Max: "I have like five Terrains in FL and I'm playing, and I open one — it starts off small for
//  a couple of milliseconds, then gets really big, then finally gets to its exact size." A size
//  log cannot see that (the C++ size was right on every open); only the pixels can. So this host
//  films its own plugin window with ScreenCaptureKit (every COMPLETE frame the window server
//  composites, host-time stamped) and the plugin's opt-in open trace (TERRAIN_OPEN_TRACE=1, the
//  same mach clock) says what the zoom machinery did on each of those frames.
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/mac_open_frames.mm -o /tmp/mac_open_frames \
//      -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreMedia \
//      -framework ScreenCaptureKit -framework ImageIO -framework UniformTypeIdentifiers -framework CoreVideo
//
//    TERRAIN_OPEN_TRACE=1 /tmp/mac_open_frames --component <build>/Terrain.component \
//        --instances 5 --audio --page tp --opens 3 --out <dir>
//
//  Scenarios (tpsz):
//    --park-width 820   while each boot-round core is PARKED, its instance's width changes (the Settings
//                       native) — the next open asks a new size of a core zoomed for the old one.
//    --cold             film the first (cold) opens too.        --audio   every instance plays a chord.
//    --drag syn,mix,mod,tp,settings,browser   per page: 1025 → 533 → 1558 → 1025, a still every ~30 ms
//                       (with TERRAIN_TEST_LIVE_DRAG=1 the plugin treats the steps as a mouse drag).
//    --no-occlusion     keep WebKit rendering when the window is occluded (a LOCKED screen occludes all).
//    --css <file>       inject an experiment stylesheet before the resize.
//  Analysis (scratch scripts, kept with the session): per-frame content scale vs the final frame, and
//  per-still "moved %" of static 8x8 blocks vs the start-size rest still scaled.
//
//  Needs a VISIBLE window (hold the shared screen lock) and Screen Recording permission.
//  Output: <dir>/o<k>_f<n>.png (half-resolution frames), <dir>/frames.tsv (open, frame, ms since the
//  view was added to the window), and the trace lines in ~/Library/Caches/Terrain/terrain-open-trace.txt.
//  Closing mirrors FL (every open is a NEW editor; the trace of Max's FL shows no re-show): hide the
//  window, remove the view, release it, close the window.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <dlfcn.h>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mach/mach_time.h>
#include <objc/message.h>

@protocol TIAUCocoaUIBase
- (NSView*) uiViewForAudioUnit: (AudioUnit) au withSize: (NSSize) s;
@end

static double machMs()
{
    static mach_timebase_info_data_t tb {};
    if (tb.denom == 0) mach_timebase_info (&tb);
    return (double) mach_absolute_time() * tb.numer / tb.denom / 1.0e6;
}

static void pumpMs (double ms)
{
    const double t0 = machMs();
    while (machMs() - t0 < ms)
        @autoreleasepool
        {
            NSEvent* e = [NSApp nextEventMatchingMask: NSEventMaskAny
                                            untilDate: [NSDate dateWithTimeIntervalSinceNow: 0.005]
                                               inMode: NSDefaultRunLoopMode dequeue: YES];
            if (e != nil) [NSApp sendEvent: e];
        }
}

struct Frame { double ms; int w, h; int bpp = 4; std::vector<uint8_t> px; };
static bool gGray = false;   // --drag: 1 byte per pixel (the drag films are long and full-size)

@interface TIFrameSink : NSObject <SCStreamOutput>
{
@public
    std::mutex mu;
    std::vector<Frame> frames;
}
@end
@implementation TIFrameSink
- (void) stream: (SCStream*) stream didOutputSampleBuffer: (CMSampleBufferRef) sb ofType: (SCStreamOutputType) type
{
    if (type != SCStreamOutputTypeScreen) return;
    CFArrayRef atts = CMSampleBufferGetSampleAttachmentsArray (sb, false);
    if (atts == nullptr || CFArrayGetCount (atts) == 0) return;
    NSDictionary* d = (__bridge NSDictionary*) CFArrayGetValueAtIndex (atts, 0);
    NSNumber* st = d[SCStreamFrameInfoStatus];
    if (st == nil || st.integerValue != SCFrameStatusComplete) return;
    CVPixelBufferRef pb = CMSampleBufferGetImageBuffer (sb);
    if (pb == nullptr) return;
    const double ms = CMTimeGetSeconds (CMSampleBufferGetPresentationTimeStamp (sb)) * 1000.0;
    CVPixelBufferLockBaseAddress (pb, kCVPixelBufferLock_ReadOnly);
    Frame f; f.ms = ms; f.w = (int) CVPixelBufferGetWidth (pb); f.h = (int) CVPixelBufferGetHeight (pb);
    const size_t bpr = CVPixelBufferGetBytesPerRow (pb);
    const uint8_t* base = (const uint8_t*) CVPixelBufferGetBaseAddress (pb);
    if (gGray)
    {
        f.bpp = 1; f.px.resize ((size_t) f.w * f.h);
        for (int y = 0; y < f.h; ++y)
        {
            const uint8_t* r = base + (size_t) y * bpr;
            for (int x = 0; x < f.w; ++x)
                f.px[(size_t) y * f.w + x] = (uint8_t) ((r[x * 4] * 29 + r[x * 4 + 1] * 150 + r[x * 4 + 2] * 77) >> 8);
        }
    }
    else
    {
        f.px.resize ((size_t) f.w * f.h * 4);
        for (int y = 0; y < f.h; ++y) std::memcpy (&f.px[(size_t) y * f.w * 4], base + (size_t) y * bpr, (size_t) f.w * 4);
    }
    CVPixelBufferUnlockBaseAddress (pb, kCVPixelBufferLock_ReadOnly);
    std::lock_guard<std::mutex> l (mu);
    frames.push_back (std::move (f));
}
@end

static void writePng (const Frame& f, NSString* path)
{
    CGColorSpaceRef cs = f.bpp == 1 ? CGColorSpaceCreateDeviceGray() : CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = f.bpp == 1
        ? CGBitmapContextCreate ((void*) f.px.data(), f.w, f.h, 8, (size_t) f.w, cs, kCGImageAlphaNone)
        : CGBitmapContextCreate ((void*) f.px.data(), f.w, f.h, 8, (size_t) f.w * 4, cs,
                                 kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = CGBitmapContextCreateImage (ctx);
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL ((__bridge CFURLRef) [NSURL fileURLWithPath: path],
                                                                 (__bridge CFStringRef) UTTypePNG.identifier, 1, nullptr);
    CGImageDestinationAddImage (dst, img, nullptr);
    CGImageDestinationFinalize (dst);
    CFRelease (dst); CGImageRelease (img); CGContextRelease (ctx); CGColorSpaceRelease (cs);
}

struct Inst
{
    AudioUnit au = nullptr;
    std::thread th;
    std::atomic<bool> run { false };
};

static AudioComponent gComp = nullptr;

static bool initInst (Inst& in)
{
    if (AudioComponentInstanceNew (gComp, &in.au) != noErr) return false;
    UInt32 maxF = 512;
    AudioUnitSetProperty (in.au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF);
    return AudioUnitInitialize (in.au) == noErr;
}

static void audioLoop (Inst* in)
{
    // A real-time-paced render of 512 frames at 48 k with a held chord re-struck every 2 s: the
    // playing-session load of Max's report, not a benchmark.
    const UInt32 n = 512;
    std::vector<float> l (n), r (n);
    struct { AudioBufferList abl; AudioBuffer extra; } bl {};
    AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
    double next = machMs();
    int blk = 0;
    const int notes[4] = { 48, 55, 60, 64 };
    while (in->run.load())
    {
        if (blk % 188 == 0)
            for (int k = 0; k < 4; ++k) { MusicDeviceMIDIEvent (in->au, 0x80, notes[k], 0, 0); MusicDeviceMIDIEvent (in->au, 0x90, notes[k], 96, 0); }
        bl.abl.mNumberBuffers = 2;
        bl.abl.mBuffers[0] = { 1, n * 4, l.data() };
        bl.extra           = { 1, n * 4, r.data() };
        AudioUnitRenderActionFlags fl = 0;
        AudioUnitRender (in->au, &fl, &ts, 0, n, &bl.abl);
        ts.mSampleTime += n; ++blk;
        next += 1000.0 * n / 48000.0;
        const double wait = next - machMs();
        if (wait > 0) std::this_thread::sleep_for (std::chrono::microseconds ((int) (wait * 1000)));
        else next = machMs();
    }
}

// --no-occlusion: WebKit hides a page whose window is occluded — and a LOCKED screen occludes every
// window, so an unattended film shows a page that never reveals (no rAF). WKWebView's own switch for
// that (_setWindowOcclusionDetectionEnabled:, the one Safari's tests use) keeps it rendering. Test-only.
static bool gNoOcclusion = false;
static void noOcclusion (NSView* v)
{
    if (! gNoOcclusion || v == nil) return;
    SEL sel = NSSelectorFromString (@"_setWindowOcclusionDetectionEnabled:");
    if ([v respondsToSelector: sel]) ((void (*) (id, SEL, BOOL)) objc_msgSend) (v, sel, NO);
    for (NSView* s in v.subviews) noOcclusion (s);
}

static NSView* makeView (AudioUnit au)
{
    AudioUnitCocoaViewInfo info {};
    UInt32 sz = sizeof (info);
    if (AudioUnitGetProperty (au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &sz) != noErr) return nil;
    NSURL* url = (__bridge_transfer NSURL*) info.mCocoaAUViewBundleLocation;
    NSString* clsName = (__bridge_transfer NSString*) info.mCocoaAUViewClass[0];
    NSBundle* b = [NSBundle bundleWithURL: url];
    [b load];
    Class cls = [b classNamed: clsName];
    if (cls == nil) cls = NSClassFromString (clsName);
    if (cls == nil) return nil;
    id<TIAUCocoaUIBase> factory = [[cls alloc] init];
    return [factory uiViewForAudioUnit: au withSize: NSMakeSize (820, 672)];
}

int main (int argc, char** argv)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps: YES];

        const char* compPath = nullptr; const char* outDir = "/tmp/tpsz"; const char* page = nullptr;
        int K = 1, opens = 2, parkW = 0; bool audio = false; const char* dragPages = nullptr; const char* injectCss = nullptr; double captureMs = 2500.0; bool warm = true;
        for (int i = 1; i < argc; ++i)
        {
            if (! std::strcmp (argv[i], "--component") && i + 1 < argc) compPath = argv[++i];
            else if (! std::strcmp (argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
            else if (! std::strcmp (argv[i], "--page") && i + 1 < argc) page = argv[++i];
            else if (! std::strcmp (argv[i], "--instances") && i + 1 < argc) K = atoi (argv[++i]);
            else if (! std::strcmp (argv[i], "--opens") && i + 1 < argc) opens = atoi (argv[++i]);
            else if (! std::strcmp (argv[i], "--ms") && i + 1 < argc) captureMs = atof (argv[++i]);
            else if (! std::strcmp (argv[i], "--audio")) audio = true;
            else if (! std::strcmp (argv[i], "--no-occlusion")) gNoOcclusion = true;
            else if (! std::strcmp (argv[i], "--css") && i + 1 < argc)   // a stylesheet FILE injected before the resize
                injectCss = strdup ([NSString stringWithContentsOfFile: @(argv[++i]) encoding: NSUTF8StringEncoding error: nil].UTF8String ?: "");
            // --drag "syn,mix,mod,tp,settings,browser": per page, one open, then (past the 4 s size heal) the
            // HOST resizes the window 1025 → 533 → 1558 → 1025 in 60 Hz steps, filmed 1:1 in grey.
            else if (! std::strcmp (argv[i], "--drag") && i + 1 < argc) { dragPages = argv[++i]; gGray = true; }
            // --park-width W: WHILE PARKED (3 s after the boot open closes) the kept-alive page calls the Settings
            // page's own setBootWidth(W) native — the instance's remembered width changes under a parked core, the
            // way a Settings → Window size change reaches every other instance. The next open asks a new size of a
            // core that is still zoomed for the old one. Nothing global is written (setBootWidth stores only the
            // instance's editorWidth when no window is attached).
            else if (! std::strcmp (argv[i], "--park-width") && i + 1 < argc) parkW = atoi (argv[++i]);
            else if (! std::strcmp (argv[i], "--cold")) warm = false;   // film the very first (cold) open too
        }
        [[NSFileManager defaultManager] createDirectoryAtPath: @(outDir) withIntermediateDirectories: YES attributes: nil error: nil];

        AudioComponentDescription desc {}; desc.componentType = kAudioUnitType_MusicDevice;
        desc.componentSubType = 'Tern'; desc.componentManufacturer = 'Wvcr';
        if (compPath != nullptr)
        {
            // Load THIS build (not the installed plugin): register its factory in-process.
            NSString* bin = [NSString stringWithFormat: @"%s/Contents/MacOS/Terrain", compPath];
            void* h = dlopen (bin.UTF8String, RTLD_NOW | RTLD_LOCAL);
            if (h == nullptr) { std::printf ("!! dlopen: %s\n", dlerror()); return 1; }
            auto fac = (AudioComponentFactoryFunction) dlsym (h, "TerrainAUFactory");
            if (fac == nullptr) { std::printf ("!! no TerrainAUFactory\n"); return 1; }
            gComp = AudioComponentRegister (&desc, CFSTR ("Wvcr: Terrain (film)"), 0x10000, fac);
        }
        else gComp = AudioComponentFindNext (nullptr, &desc);
        if (gComp == nullptr) { std::printf ("!! component not found\n"); return 1; }

        NSString* caches = NSSearchPathForDirectoriesInDomains (NSCachesDirectory, NSUserDomainMask, YES)[0];
        NSString* dir = [caches stringByAppendingPathComponent: @"Terrain"];
        NSString* expPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp.js"];
        NSString* resPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp-result.txt"];

        std::vector<Inst> inst (K);
        std::vector<NSSize> lastSize (K, NSMakeSize (820, 672));
        for (auto& in : inst) if (! initInst (in)) { std::printf ("!! instance init failed\n"); return 1; }
        if (audio) for (auto& in : inst) { in.run = true; in.th = std::thread (audioLoop, &in); }

        NSMutableArray* box = [NSMutableArray array];   // the async answer lands here (a __block var cannot enter a C++ lambda)
        auto refreshContent = [box] () -> SCShareableContent* {
            [box removeAllObjects];
            [SCShareableContent getShareableContentExcludingDesktopWindows: YES onScreenWindowsOnly: YES
                                                        completionHandler: ^(SCShareableContent* c, NSError* e) {
                                                            dispatch_async (dispatch_get_main_queue(), ^{ if (c) [box addObject: c]; else [box addObject: NSNull.null]; });
                                                            if (e) NSLog (@"%@", e); }];
            const double t0 = machMs();
            while (box.count == 0 && machMs() - t0 < 3000) pumpMs (10);
            return box.count && [box[0] isKindOfClass: SCShareableContent.class] ? box[0] : nil;
        };

        FILE* tsv = std::fopen ([NSString stringWithFormat: @"%s/frames.tsv", outDir].UTF8String, "w");
        std::fprintf (tsv, "open\tinst\tframe\tms\n");

        // One open of instance i. film = capture the window from before the view is added until captureMs after.
        FILE* stepsTsv = nullptr;
        auto openOnce = [&] (int i, int tag, bool film, const char* hookJs, bool drag = false) -> bool
        {
            [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
            [@(hookJs) writeToFile: expPath atomically: YES encoding: NSUTF8StringEncoding error: nil];

            NSWindow* w = [[NSWindow alloc] initWithContentRect: NSMakeRect (60, 60, lastSize[i].width, lastSize[i].height)
                                                      styleMask: NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                        backing: NSBackingStoreBuffered defer: NO];
            w.releasedWhenClosed = NO;
            w.backgroundColor = [NSColor colorWithRed: 0.5 green: 0.5 blue: 0.5 alpha: 1];   // a grey no Terrain frame can be
            [w setFrameTopLeftPoint: NSMakePoint (60, NSMaxY (NSScreen.mainScreen.visibleFrame) - 20)];
            [w makeKeyAndOrderFront: nil];
            pumpMs (250);

            TIFrameSink* sink = nil; SCStream* stream = nil;
            NSView* v = nil;
            double tAdd = 0, tMade = 0, tIn = 0;
            auto addView = [&] () -> bool
            {
                tAdd = machMs();
                @autoreleasepool { v = makeView (inst[i].au); }
                if (v == nil) { std::printf ("!! open %d: no view\n", tag); return false; }
                tMade = machMs();
                // FL sizes its window to the view it was handed, then shows it.
                if (std::abs (v.frame.size.width - lastSize[i].width) > 0.5)
                    std::printf ("  (window pre-sized %.0f, view asks %.0f)\n", lastSize[i].width, v.frame.size.width);
                lastSize[i] = v.frame.size;
                [w setContentSize: v.frame.size];
                [w setFrameTopLeftPoint: NSMakePoint (60, NSMaxY (NSScreen.mainScreen.visibleFrame) - 20)];
                noOcclusion (v);
                [w.contentView addSubview: v];
                tIn = machMs();
                std::printf ("  open %-3d inst %d  view %4.0fx%-4.0f  create %5.1f ms  (film %s)\n", tag, i,
                             v.frame.size.width, v.frame.size.height, tMade - tAdd, film ? "on" : "off");
                std::fflush (stdout);
                return true;
            };
            if (drag)
            {
                if (! addView()) return false;
                const double t0 = machMs();
                while (! [[NSFileManager defaultManager] fileExistsAtPath: resPath] && machMs() - t0 < 30000) pumpMs (20);
                // (no wait: setBootWidth marks the size as chosen, so the size heal never fights it)
            }
            if (film)
            {
                SCShareableContent* content = refreshContent();
                SCWindow* scw = nil;
                for (SCWindow* x in content.windows) if (x.windowID == (CGWindowID) w.windowNumber) scw = x;
                if (scw == nil) { std::printf ("!! window not shareable\n"); return false; }
                SCContentFilter* filt = [[SCContentFilter alloc] initWithDesktopIndependentWindow: scw];
                SCStreamConfiguration* cfg = [SCStreamConfiguration new];
                // Half resolution (one pixel per point). The window is pre-sized to this instance's last
                // open, which is what the plugin will ask for again.
                cfg.width = (size_t) filt.contentRect.size.width; cfg.height = (size_t) filt.contentRect.size.height;
                cfg.scalesToFit = YES;
                if (drag) { cfg.width = 1600; cfg.height = 1400; cfg.scalesToFit = NO; }   // 1:1, the window grows inside it
                cfg.pixelFormat = kCVPixelFormatType_32BGRA;
                cfg.minimumFrameInterval = CMTimeMake (1, 120);
                cfg.showsCursor = NO;
                cfg.queueDepth = 8;
                sink = [TIFrameSink new];
                stream = [[SCStream alloc] initWithFilter: filt configuration: cfg delegate: nil];
                NSError* err = nil;
                [stream addStreamOutput: sink type: SCStreamOutputTypeScreen
                     sampleHandlerQueue: dispatch_queue_create ("film", DISPATCH_QUEUE_SERIAL) error: &err];
                __block bool started = false;
                [stream startCaptureWithCompletionHandler: ^(NSError* e) { started = true; if (e) NSLog (@"start %@", e); }];
                const double t0 = machMs();
                while (! started && machMs() - t0 < 3000) pumpMs (5);
                pumpMs (200);
            }

            if (drag)
            {
                // The host drags: window + view frame together, one step per 60 Hz frame, with holds at the
                // ends so every page is also filmed AT REST at 65 %, 190 % and back at its start size.
                tIn = machMs();
                const double w0 = v.frame.size.width;
                // A still per step, straight from the window server. (The stream stalls after ~1 s whenever the
                // screen is locked; this does not.) CGWindowListCreateImage is gone from the macOS 15 SDK
                // headers but not from the system, hence dlsym.
                using WLImg = CGImageRef (*) (CGRect, uint32_t, uint32_t, uint32_t);
                static WLImg wlImg = (WLImg) dlsym (RTLD_DEFAULT, "CGWindowListCreateImage");
                int snapIdx = 0;
                auto snap = [&] (NSWindow* win, const char* fmt, int isRest)
                {
                    if (wlImg == nullptr) return;
                    CGImageRef img = wlImg (CGRectNull, 1 << 3 /* IncludingWindow */, (uint32_t) win.windowNumber,
                                            (1 << 0) | (1 << 4) /* BoundsIgnoreFraming | NominalResolution */);
                    if (img == nullptr) return;
                    NSString* path = [NSString stringWithFormat: @(fmt), outDir, tag, snapIdx];
                    CGImageDestinationRef dst = CGImageDestinationCreateWithURL ((__bridge CFURLRef) [NSURL fileURLWithPath: path],
                                                                                 (__bridge CFStringRef) UTTypePNG.identifier, 1, nullptr);
                    CGImageDestinationAddImage (dst, img, nullptr); CGImageDestinationFinalize (dst); CFRelease (dst);
                    std::fprintf (stepsTsv, "%d\t%.1f\t%.0f\tsnap%d %d\n", tag, machMs() - tIn, v.frame.size.width, isRest, snapIdx);
                    CGImageRelease (img);
                    ++snapIdx;
                };
                snap (w, "%s/o%d_s%03d.png", 1);
                // THE RESIZE is driven from inside the page (the drag hook below): the Settings page's own
                // setBootWidth native, stepped at 60 Hz — shell setSize → core resized(), the path every
                // size change takes. (An AU host cannot resize a JUCE editor by setting the view frame, the
                // wrapper snaps its view back; and a synthesized corner drag is dropped by JUCE because no
                // button is REALLY down.) The window follows the view, pinned at its top-left, as a host's
                // does, and a still is taken every ~30 ms with the width the plugin had at that moment.
                tIn = machMs();
                const NSPoint topLeft = NSMakePoint (60, NSMaxY (NSScreen.mainScreen.visibleFrame) - 20);
                snap (w, "%s/o%d_s%03d.png", 1);
                while (machMs() - tIn < 8200)
                {
                    pumpMs (12);
                    if (std::abs (w.contentView.frame.size.width - v.frame.size.width) > 0.5
                        || std::abs (w.contentView.frame.size.height - v.frame.size.height) > 0.5)
                    {
                        [w setContentSize: v.frame.size];
                        [w setFrameTopLeftPoint: topLeft];
                    }
                    snap (w, "%s/o%d_s%03d.png", 0);
                }
                std::fflush (stepsTsv);
            }
            else
            {
                if (! addView()) return false;
                const double t0 = machMs();
                while (! [[NSFileManager defaultManager] fileExistsAtPath: resPath] && machMs() - t0 < 30000) pumpMs (20);
                const double left = captureMs - (machMs() - tIn);
                if (left > 0) pumpMs (left);
            }

            if (film)
            {
                __block bool stopped = false;
                [stream stopCaptureWithCompletionHandler: ^(NSError*) { stopped = true; }];
                const double t1 = machMs();
                while (! stopped && machMs() - t1 < 3000) pumpMs (5);
                std::lock_guard<std::mutex> l (sink->mu);
                int n = 0;
                for (auto& f : sink->frames)
                {
                    std::fprintf (tsv, "%d\t%d\t%d\t%.1f\n", tag, i, n, f.ms - tIn);
                    writePng (f, [NSString stringWithFormat: @"%s/o%d_f%03d.png", outDir, tag, n]);
                    ++n;
                }
                std::fflush (tsv);
                std::printf ("           %d frames; view in window at mach %.1f ms (created at %.1f)\n", n, tIn, tMade);
                std::fflush (stdout);
            }
            else pumpMs (1500);

            // the close, FL-style: hide → remove → release → close
            [w orderOut: nil]; pumpMs (120);
            [v removeFromSuperview];
            // A window we own keeps a reference to the view (first responder / key-view loop), so the AU
            // wrapper's dealloc-driven teardown never runs and the NEXT open gets no view (the fb521 trap).
            // The wrapper's view class answers applicationWillTerminate: with the same shutdown() its
            // dealloc uses — editorBeingDeleted → the shell dtor → park — so call that, then release.
            if ([v respondsToSelector: @selector (applicationWillTerminate:)])
                [v performSelector: @selector (applicationWillTerminate:) withObject: nil];
            v = nil; pumpMs (250);
            [w close]; w = nil; pumpMs (400);
            return true;
        };

        std::string nav = "(function(){";
        if (page != nullptr) nav += std::string ("try{setActivePanel('") + page + "');}catch(e){}";
        if (parkW > 0)
            nav += "setTimeout(function(){try{window.Juce.getNativeFunction('setBootWidth')(" + std::to_string (parkW) + ");}catch(e){}},6000);";
        nav += "return 'nav';})()";

        int tag = 0;
        if (dragPages != nullptr)
        {
            stepsTsv = std::fopen ([NSString stringWithFormat: @"%s/steps.tsv", outDir].UTF8String, "w");
            std::fprintf (stepsTsv, "open\tms\twidth\tphase\n");
            NSArray* pages = [@(dragPages) componentsSeparatedByString: @","];
            for (NSString* pg in pages)
            {
                NSString* js = [pg isEqualToString: @"settings"] ? @"(function(){document.getElementById('syn-btn').click();setTimeout(function(){document.getElementById('settings-btn').click();},300);return 'nav';})()"
                             : [pg isEqualToString: @"browser"]  ? @"(function(){document.getElementById('syn-btn').click();setTimeout(function(){window.__openPresetBrowser&&window.__openPresetBrowser();},300);return 'nav';})()"
                             : [pg isEqualToString: @"tp"]       ? @"(function(){setActivePanel('tp');return 'nav';})()"
                             : [NSString stringWithFormat: @"(function(){document.getElementById('%@-btn').click();return 'nav';})()", pg];
                // + the resize schedule, 1.5 s after the page settled: hold, → 65 %, hold, → 190 %, hold, → start, hold
                js = [js stringByAppendingString: @";(function(){var f=window.Juce&&window.Juce.getNativeFunction('setBootWidth');if(!f)return;"
                       "var w0=Math.round(window.innerWidth*(window.__uiScale||1)),seq=[],t=1500;"
                       "function ramp(a,b,ms){var n=Math.max(1,Math.round(ms/16.7));for(var k=1;k<=n;k++)seq.push([t+=16.7,Math.round(a+(b-a)*k/n)]);t+=700;}"
                       "ramp(w0,533,900);ramp(533,1558,1500);ramp(1558,w0,900);"
                       "seq.forEach(function(s){setTimeout(function(){try{f(s[1]);}catch(e){}},s[0]);});})()"];
                if (injectCss != nullptr)   // --css: an experiment stylesheet added to the page before the resize
                    js = [js stringByAppendingFormat: @";(function(){var s=document.createElement('style');s.textContent=%@;document.head.appendChild(s);})()", [[NSString alloc] initWithData: [NSJSONSerialization dataWithJSONObject: @[@(injectCss)] options: 0 error: nil] encoding: NSUTF8StringEncoding]];
                std::printf ("  page %s\n", pg.UTF8String);
                if (! openOnce (0, tag++, false, js.UTF8String, true)) return 1;   // stills per step (snap), not the stream
            }
            std::fclose (stepsTsv);
            opens = 0; K = 0;
        }
        // Boot every instance once (cold) on the chosen page, so each keeps a PARKED core — Max's session state.
        for (int i = 0; i < K; ++i)
            if (! openOnce (i, tag++, ! warm, nav.c_str())) return 1;
        pumpMs (1000);
        for (int k = 0; k < opens; ++k)
            for (int i = 0; i < K; ++i)
                if (! openOnce (i, tag++, true, "'r'")) return 1;

        std::fclose (tsv);
        for (auto& in : inst) { in.run = false; if (in.th.joinable()) in.th.join(); }
        [[NSFileManager defaultManager] removeItemAtPath: expPath error: nil];
        for (auto& in : inst) { AudioUnitUninitialize (in.au); AudioComponentInstanceDispose (in.au); }
        std::printf ("  done → %s\n", outDir);
    }
    return 0;
}
