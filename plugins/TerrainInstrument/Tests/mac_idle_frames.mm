// ══════════════════════════════════════════════════════════════════════════════════════════════
//  mac_idle_frames.mm — fb567: DOES THE EDITOR'S ANIMATION LOOP STOP WHEN NOTHING IS PLAYED?
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/mac_idle_frames.mm -o /tmp/mac_idle_frames \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    caffeinate -d -i -u /tmp/mac_idle_frames        (the beacon is switched on by this host)
//
//  Max (2026-09-02): "Stop the animation loop when the MIDI is not inside and there's nothing
//  being played... Put the animation loop back whenever we are playing."
//
//  A raw AU host (the mac_reopen lineage): the real INSTALLED AU, its real editor (windowless —
//  fb521's law), audio RENDERED at real-time pace on a second thread the way a DAW renders, and
//  the plugin's own beacon (terrain-cpu.txt, every ~5 s, opt-in) read for its frames counter.
//  What is counted is the number of coalesced frames the editor SHIPPED to the page — the fb483
//  idle-skip's own count: a byte-identical frame ships nothing but a keep-alive every 30 ticks
//  (~2/s). A loop truly at rest reads ~2 frames/s. Before fb567 the legacy ModulationEngine
//  bank's phases (1 Hz by default, pushed every tick ahead of the quiet gate) changed every
//  frame's bytes and idle read ~60/s: the loop never stopped, and every painter ran forever.
//
//  BARS
//   1  the beacon is alive (terrain-cpu.txt appears and updates) — else nothing below is readable
//   2  IDLE     (editor open, nothing played, 20 s)         ≤ 6 frames/s
//   3  PLAYING  (one held note, 15 s)                       ≥ 20 frames/s   — the loop comes back
//   4  IDLE     (note off, release + tail, then 22 s)       ≤ 6 frames/s    — and rests again
//  Printed, report-only: the beacon's DSP and UI (message thread) load lines per phase — the
//  editor-open cost on the Mac, the number the CPU overpass starts from.
//  Clean-up: terrain-cpu.txt is removed; the run asserts no terrain-ui-exp.js hook remains.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
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
    std::thread render; std::atomic<bool> stop { false }; std::atomic<int> midi { 0 };   // 1 = note on, 2 = note off

    bool init()
    {
        AudioComponentDescription d {};
        d.componentType = kAudioUnitType_MusicDevice;
        d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (c == nullptr) { std::printf ("  !! AU not found\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! instance failed\n"); return false; }
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 maxF = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxF, sizeof maxF);
        if (AudioUnitInitialize (au) != noErr) { std::printf ("  !! initialize failed\n"); return false; }
        return true;
    }

    void startRender()   // real-time paced, the way a DAW's engine keeps calling us with silence
    {
        render = std::thread ([this]
        {
            std::vector<float> l ((size_t) BLK), r ((size_t) BLK);
            std::vector<uint8_t> raw (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            auto* abl = (AudioBufferList*) raw.data();
            double sampleTime = 0.0;
            auto next = std::chrono::steady_clock::now();
            const auto period = std::chrono::microseconds ((long) (1.0e6 * BLK / SR));
            while (! stop.load())
            {
                const int m = midi.exchange (0);
                if (m == 1) MusicDeviceMIDIEvent (au, 0x90, 60, 100, 0);
                if (m == 2) MusicDeviceMIDIEvent (au, 0x80, 60, 0, 0);
                abl->mNumberBuffers = 2;
                abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * sizeof (float); abl->mBuffers[0].mData = l.data();
                abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * sizeof (float); abl->mBuffers[1].mData = r.data();
                AudioUnitRenderActionFlags flags = 0;
                AudioTimeStamp ts {}; ts.mSampleTime = sampleTime; ts.mFlags = kAudioTimeStampSampleTimeValid;
                AudioUnitRender (au, &flags, &ts, 0, (UInt32) BLK, abl);
                sampleTime += BLK;
                next += period;
                std::this_thread::sleep_until (next);
            }
        });
    }
    void stopRender() { stop.store (true); if (render.joinable()) render.join(); }

    NSView* makeView()
    {
        AudioUnitCocoaViewInfo info {};
        UInt32 sz = sizeof (info);
        const OSStatus st = AudioUnitGetProperty (au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &sz);
        if (st != noErr) { std::printf ("  !! CocoaUI property failed: %d\n", (int) st); return nil; }
        NSURL* url = (__bridge_transfer NSURL*) info.mCocoaAUViewBundleLocation;
        NSString* clsName = (__bridge_transfer NSString*) info.mCocoaAUViewClass[0];
        NSBundle* b = [NSBundle bundleWithURL: url];
        [b load];
        Class cls = [b classNamed: clsName];
        if (cls == nil) cls = NSClassFromString (clsName);
        if (cls == nil) { std::printf ("  !! view class '%s' not found\n", clsName.UTF8String); return nil; }
        id<TIAUCocoaUIBase> factory = [[cls alloc] init];
        return [factory uiViewForAudioUnit: au withSize: NSMakeSize (820, 672)];
    }
};

// ── the beacon ───────────────────────────────────────────────────────────────────────────────
struct Sample { double t; unsigned frames; std::string dsp, ui; };
struct Beacon
{
    std::vector<NSString*> paths; std::string lastText; std::vector<Sample> samples; NSString* found = nil;
    Beacon()
    {
        NSString* caches = NSSearchPathForDirectoriesInDomains (NSCachesDirectory, NSUserDomainMask, YES)[0];
        // the plugin's juce tempDirectory: ~/Library/Caches/<binary name> — the COMPONENT's name in every host
        // (the hook-dir law, mac_reopen.mm); the host's own name is kept as a fallback.
        paths.push_back ([caches stringByAppendingPathComponent: @"Terrain Instrument/terrain-cpu.txt"]);
        paths.push_back ([caches stringByAppendingPathComponent: @"mac_idle_frames/terrain-cpu.txt"]);
    }
    void poll (double t0)
    {
        for (NSString* p : paths)
        {
            NSString* s = [NSString stringWithContentsOfFile: p encoding: NSUTF8StringEncoding error: nil];
            if (s == nil) continue;
            std::string txt = s.UTF8String;
            if (txt == lastText) return;
            lastText = txt; found = p;
            unsigned fr = 0; const char* q = std::strstr (txt.c_str(), "frames ");
            if (q == nullptr || std::sscanf (q, "frames %u", &fr) != 1) return;
            Sample smp; smp.t = (nowMs() - t0) / 1000.0; smp.frames = fr;
            const size_t nl = txt.find ('\n'); smp.dsp = txt.substr (0, nl == std::string::npos ? txt.size() : nl);
            if (nl != std::string::npos) { const size_t nl2 = txt.find ('\n', nl + 1); smp.ui = txt.substr (nl + 1, nl2 == std::string::npos ? std::string::npos : nl2 - nl - 1); }
            samples.push_back (smp);
            return;
        }
    }
    // frames/s between the first sample at or after `a` and the last at or before `b` (-1 = not enough samples)
    double rate (double a, double b, std::string* uiLine) const
    {
        const Sample* f = nullptr; const Sample* l = nullptr;
        for (const auto& s : samples) { if (s.t >= a && s.t <= b) { if (f == nullptr) f = &s; l = &s; } }
        if (f == nullptr || l == nullptr || l->t - f->t < 4.0) return -1.0;
        if (uiLine != nullptr) *uiLine = l->ui;
        return (double) (l->frames - f->frames) / (l->t - f->t);
    }
    void clean() { for (NSString* p : paths) [[NSFileManager defaultManager] removeItemAtPath: p error: nil]; }
};

static int pass = 0, fail = 0;
static void chk (bool ok, const char* label, const std::string& detail)
{
    if (ok) ++pass; else ++fail;
    std::printf ("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", label, detail.empty() ? "" : "   ", detail.c_str());
    std::fflush (stdout);
}

int main (int argc, char** argv)
{
    setenv ("TERRAIN_CPU_PROBE", "1", 1);   // the beacon's opt-in, read when the plugin's beacon thread starts
    const bool quick = (argc > 1 && std::strcmp (argv[1], "--quick") == 0);
    const double IDLE1 = quick ? 12.0 : 20.0, PLAY = quick ? 12.0 : 15.0, IDLE2 = quick ? 14.0 : 22.0;
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps: YES];

        std::printf ("\n══ fb567 — DOES THE EDITOR'S LOOP REST WHEN NOTHING IS PLAYED? (installed AU, real editor, real-time render) ══\n\n");
        AuHost h; if (! h.init()) return 1;
        Beacon bc; bc.clean();
        h.startRender();
        pumpMs (300);
        NSView* view = nil;
        @autoreleasepool { view = h.makeView(); }   // windowless: the creation pool drains, ARC's ref is the only one (fb521)
        chk (view != nil, "0  the AU opened and its editor view was created", "");
        if (view == nil) { h.stopRender(); return 1; }

        const double t0 = nowMs();
        auto phase = [&] (const char* name, double secs)
        {
            const double a = (nowMs() - t0) / 1000.0;
            const double end = nowMs() + secs * 1000.0;
            while (nowMs() < end) { pumpMs (250); bc.poll (t0); }
            const double b = (nowMs() - t0) / 1000.0;
            std::string ui; const double r = bc.rate (a, b, &ui);
            std::printf ("     %-10s %5.1f s   frames/s %s   %s\n", name, b - a, r < 0 ? "  n/a" : std::to_string ((int) (r + 0.5)).c_str(), ui.c_str());
            std::fflush (stdout);
            return r;
        };

        // boot + the restore pushes settle; the beacon's first write lands inside here
        std::printf ("     booting the page (12 s: boot, the restore pushes, the page-flip burst)...\n"); std::fflush (stdout);
        { const double end = nowMs() + 12000.0; while (nowMs() < end) { pumpMs (250); bc.poll (t0); } }
        chk (! bc.samples.empty(), "1  the beacon is alive (terrain-cpu.txt appears while the editor is open)",
             bc.found != nil ? std::string (bc.found.UTF8String) : std::string ("no file in either caches dir after 12 s"));
        if (bc.samples.empty()) { view = nil; pumpMs (400); h.stopRender(); bc.clean(); return 1; }

        const double idle1 = phase ("IDLE", IDLE1);
        chk (idle1 >= 0 && idle1 <= 6.0, "2  IDLE: with nothing played the editor ships <= 6 frames/s (keep-alives only)",
             idle1 < 0 ? "not enough beacon samples" : std::to_string ((int) (idle1 + 0.5)) + " frames/s");

        h.midi.store (1);
        const double play = phase ("PLAYING", PLAY);
        chk (play >= 20.0, "3  PLAYING: a held note brings the loop back (>= 20 frames/s)",
             play < 0 ? "not enough beacon samples" : std::to_string ((int) (play + 0.5)) + " frames/s");

        h.midi.store (2);
        std::printf ("     note off: release + tail (4 s)...\n"); std::fflush (stdout);
        { const double end = nowMs() + 4000.0; while (nowMs() < end) { pumpMs (250); bc.poll (t0); } }
        const double idle2 = phase ("IDLE", IDLE2);
        chk (idle2 >= 0 && idle2 <= 6.0, "4  IDLE AGAIN: after the note the loop rests again (<= 6 frames/s)",
             idle2 < 0 ? "not enough beacon samples" : std::to_string ((int) (idle2 + 0.5)) + " frames/s");

        // the beacon's own lines, for the record (the Mac editor-open cost)
        if (! bc.samples.empty()) { std::printf ("\n     last beacon:\n       %s\n       %s\n", bc.samples.back().dsp.c_str(), bc.samples.back().ui.c_str()); }

        view = nil; pumpMs (600);   // releasing the view is the close (fb521)
        h.stopRender();
        AudioUnitUninitialize (h.au); AudioComponentInstanceDispose (h.au);
        bc.clean();
        NSString* caches = NSSearchPathForDirectoriesInDomains (NSCachesDirectory, NSUserDomainMask, YES)[0];
        NSString* hook = [caches stringByAppendingPathComponent: @"Terrain Instrument/terrain-ui-exp.js"];
        const bool hookLeft = [[NSFileManager defaultManager] fileExistsAtPath: hook];
        chk (! hookLeft, "5  no terrain-ui-exp.js hook file remains in the plugin's caches dir", "");
        std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", pass, fail);
        return fail ? 1 : 0;
    }
}
