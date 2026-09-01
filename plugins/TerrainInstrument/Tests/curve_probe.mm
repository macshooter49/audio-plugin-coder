// ══════════════════════════════════════════════════════════════════════════════════════════════
//  curve_probe.mm — fb559. THE GESTURE AND THE SOUND, IN ONE PROCESS.
//
//    clang++ -std=c++17 -fobjc-arc -O2 Tests/curve_probe.mm -o /tmp/curve_probe \
//            -framework Cocoa -framework AudioToolbox -framework AudioUnit -framework CoreFoundation
//    /tmp/curve_probe <act.js> [note]
//
//  WHY IT EXISTS. fb559 moved three curve editors onto one. Every one of them is reached by a
//  GESTURE (a right-click row, a menu Extend, a drag on a point) and every one of them is only
//  worth anything if the SOUND changes at the end of it. Those are two different harnesses in
//  this repo — the raw AU render probe (warp_filter_probe) and the raw AU editor host
//  (mac_reopen) — and testing them separately is exactly the fb373 hole: "a green harness proves
//  the ENGINE, never that the plugin REACHES it."
//
//  So: ONE AU instance. Measure it. Open its real editor, run the real gesture through the real
//  page. Close. Measure the SAME instance again. The delta is the feature, end to end, and there
//  is no state file, no second process and no place for a mock to hide.
//
//  ⚠️ THE HOOK-DIR LAW (fb521, cost three wrong theories): the plugin's juce tempDirectory is
//     ~/Library/Caches/"Terrain Instrument" in EVERY host — dladdr resolves to the COMPONENT
//     binary, not the host process.
//  ⚠️ THE AUTORELEASE TRAP (fb521): createViewFor returns the view AUTORELEASED. Drain the
//     creation pool immediately or the editor is never deleted and the second open returns nil.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <map>

static const double SR = 48000.0, PI = 3.14159265358979323846;
static const int BLK = 512, WARM = 20, MEAS = 48;

@protocol TIAUCocoaUIBase
- (NSView*) uiViewForAudioUnit: (AudioUnit) au withSize: (NSSize) s;
@end

static double nowMs() { return (double) clock_gettime_nsec_np (CLOCK_MONOTONIC) / 1.0e6; }
static void pumpMs (double ms) { const double t0 = nowMs();
  while (nowMs() - t0 < ms) { @autoreleasepool {
    NSEvent* e = [NSApp nextEventMatchingMask: NSEventMaskAny
                                    untilDate: [NSDate dateWithTimeIntervalSinceNow: 0.02]
                                       inMode: NSDefaultRunLoopMode dequeue: YES];
    if (e != nil) [NSApp sendEvent: e]; } } }

struct AU {
  AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; AudioTimeStamp ts {};
  bool open() {
    AudioComponentDescription d {};
    d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
    AudioComponent c = AudioComponentFindNext (nullptr, &d);
    if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
    AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
    UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
    if (AudioUnitInitialize (au) != noErr) return false;
    for (int i = 0; i < 60; ++i) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false);
    UInt32 sz = 0; Boolean w = false;
    AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
    std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
    AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
    for (auto id : ids) { AudioUnitParameterInfo pi {}; UInt32 s2 = sizeof pi;
      if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s2) != noErr) continue;
      char b[256] = {0};
      if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString)
        CFStringGetCString (pi.cfNameString, b, sizeof b, kCFStringEncodingUTF8);
      else snprintf (b, sizeof b, "%s", pi.name);
      byName[b] = id; }
    return true; }
  void set (const std::string& n, float v) { auto it = byName.find (n);
    if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return; }
    AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0); }
  float get (const std::string& n) { auto it = byName.find (n); if (it == byName.end()) return -1.0f;
    AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v); return v; }
  std::vector<double> harm (int note, int K) {
    std::vector<float> bl (BLK), br (BLK); std::vector<double> ring;
    AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
    MusicDeviceMIDIEvent (au, 0x90, note, 100, 0);
    for (int b = 0; b < WARM + MEAS; ++b) {
      abl->mBuffers[0] = {1, (UInt32)(BLK*4), bl.data()}; abl->mBuffers[1] = {1, (UInt32)(BLK*4), br.data()};
      AudioUnitRenderActionFlags fl = 0; AudioUnitRender (au, &fl, &ts, 0, BLK, abl); ts.mSampleTime += BLK;
      if (b >= WARM) for (int i = 0; i < BLK; ++i) ring.push_back ((double) bl[i]); }
    MusicDeviceMIDIEvent (au, 0x80, note, 0, 0);
    for (int b = 0; b < 8; ++b) { abl->mBuffers[0] = {1,(UInt32)(BLK*4),bl.data()}; abl->mBuffers[1] = {1,(UInt32)(BLK*4),br.data()};
      AudioUnitRenderActionFlags fl = 0; AudioUnitRender (au, &fl, &ts, 0, BLK, abl); ts.mSampleTime += BLK; }
    free (abl);
    const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    const int N = (int) ring.size();
    std::vector<double> H (K + 1, 0.0);
    for (int k = 1; k <= K; ++k) { if (k * f0 >= SR / 2 - 200) break;
      double re = 0, im = 0;
      for (int n = 0; n < N; n += 2) { const double wn = 0.5 - 0.5 * std::cos (2.0 * PI * n / (N - 1));
        const double a = 2.0 * PI * k * f0 * n / SR; re += ring[n] * wn * std::cos (a); im -= ring[n] * wn * std::sin (a); }
      H[k] = std::sqrt (re * re + im * im); }
    return H; }
  NSView* makeView() {
    AudioUnitCocoaViewInfo info {}; UInt32 sz = sizeof (info);
    if (AudioUnitGetProperty (au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &sz) != noErr) return nil;
    NSURL* url = (__bridge_transfer NSURL*) info.mCocoaAUViewBundleLocation;
    NSString* clsName = (__bridge_transfer NSString*) info.mCocoaAUViewClass[0];
    NSBundle* b = [NSBundle bundleWithURL: url]; [b load];
    Class cls = [b classNamed: clsName]; if (cls == nil) cls = NSClassFromString (clsName);
    if (cls == nil) return nil;
    id<TIAUCocoaUIBase> factory = [[cls alloc] init];
    return [factory uiViewForAudioUnit: au withSize: NSMakeSize (820, 672)]; }
};

// centroid in HARMONIC number + how many harmonics carry real energy + total level
static void report (const char* tag, std::vector<double>& H) {
  double n = 0, d = 0, pk = 0, tot = 0;
  for (size_t k = 1; k < H.size(); ++k) { n += k * H[k]; d += H[k]; tot += H[k] * H[k]; if (H[k] > pk) pk = H[k]; }
  int live = 0; for (size_t k = 1; k < H.size(); ++k) if (H[k] > pk * 0.01) ++live;   // within -40 dB of the loudest
  printf ("  %-26s centroid %7.2f h   live harmonics %3d   rms %9.5f\n", tag, d > 0 ? n / d : 0.0, live, std::sqrt (tot));
}
static double dist (std::vector<double>& A, std::vector<double>& B) {   // relative spectrum distance, dB
  double num = 0, den = 0;
  for (size_t k = 1; k < A.size() && k < B.size(); ++k) { const double dd = A[k] - B[k]; num += dd * dd; den += A[k] * A[k]; }
  if (den <= 0) return -999.0;
  return 10.0 * std::log10 (num / den + 1e-30);
}

int main (int argc, char** argv) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy: NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching]; [NSApp activateIgnoringOtherApps: YES];

    const char* actPath = argc > 1 ? argv[1] : nullptr;
    const int note = argc > 2 ? atoi (argv[2]) : 60;

    AU a; if (! a.open()) { printf ("  !! AU not found\n"); return 1; }
    // a REAL table, not preset 0: a sine has one harmonic and cannot fail a shaping test
    // (the fb553 law — "A SINE CARRIER CANNOT FAIL A MIP TEST").
    if (const char* wt = getenv ("CP_WT")) a.set ("Synth OSC A WT Preset", (float) atof (wt));
    else a.set ("Synth OSC A WT Preset", 4.0f);
    a.set ("Synth OSC B Level", 0.0f);
    for (int i = 0; i < 40; ++i) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false);

    std::vector<std::string> watch;
    if (const char* w = getenv ("CP_WATCH")) { std::string t (w), cur;
      for (char ch : t) { if (ch == ',') { if (cur.size()) watch.push_back (cur); cur.clear(); } else cur += ch; }
      if (cur.size()) watch.push_back (cur); }
    auto dumpWatch = [&] (const char* tag) { for (auto& n : watch)
      printf ("  %-8s %-34s = %g\n", tag, n.c_str(), (double) a.get (n)); };

    // CP_SET="Param Name=value,Param Name=value" — the BEFORE state, so the delta isolates the
    // gesture instead of also carrying whatever setup the gesture happened to do first.
    if (const char* sv = getenv ("CP_SET")) { std::string t (sv), cur;
      auto one = [&] (const std::string& kv) { auto e = kv.find ('=');
        if (e == std::string::npos) return; a.set (kv.substr (0, e), (float) atof (kv.substr (e + 1).c_str())); };
      for (char ch : t) { if (ch == ',') { one (cur); cur.clear(); } else cur += ch; }
      one (cur);
      for (int i = 0; i < 40; ++i) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); }

    auto Hbefore = a.harm (note, 120);
    report ("BEFORE the gesture", Hbefore);
    dumpWatch ("before");

    NSString* caches = NSSearchPathForDirectoriesInDomains (NSCachesDirectory, NSUserDomainMask, YES)[0];
    NSString* dir = [caches stringByAppendingPathComponent: @"Terrain Instrument"];
    [[NSFileManager defaultManager] createDirectoryAtPath: dir withIntermediateDirectories: YES attributes: nil error: nil];
    NSString* expPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp.js"];
    NSString* resPath = [dir stringByAppendingPathComponent: @"terrain-ui-exp-result.txt"];
    // ONE editor open = ONE hook (uiExpDone_ latches per open), so a gesture that needs to run
    // ASYNC reports on the NEXT open. Whether window state survives a close is a property of the
    // fb521 park, and the report line says plainly when it did not.
    auto runHook = [&] (const char* path, double settleMs) -> std::string {
      [[NSFileManager defaultManager] removeItemAtPath: resPath error: nil];
      NSString* js = [NSString stringWithContentsOfFile: [NSString stringWithUTF8String: path]
                                               encoding: NSUTF8StringEncoding error: nil];
      if (js == nil) { printf ("  !! cannot read %s\n", path); return ""; }
      [js writeToFile: expPath atomically: YES encoding: NSUTF8StringEncoding error: nil];
      NSView* v = nil; @autoreleasepool { v = a.makeView(); }
      if (v == nil) { printf ("  !! editor view failed\n"); return ""; }
      const double t0 = nowMs();
      while (! [[NSFileManager defaultManager] fileExistsAtPath: resPath]) {
        pumpMs (50);
        if (nowMs() - t0 > 40000.0) { printf ("  !! the page never answered (40 s)\n"); v = nil; return ""; } }
      pumpMs (settleMs);
      NSString* out = [NSString stringWithContentsOfFile: resPath encoding: NSUTF8StringEncoding error: nil];
      [[NSFileManager defaultManager] removeItemAtPath: expPath error: nil];   // never leave the gun loaded
      v = nil; pumpMs (600);
      return out ? std::string (out.UTF8String) : std::string(); };

    if (actPath) printf ("  gesture: %s", runHook (actPath, 2500.0).c_str());
    if (const char* rp = getenv ("CP_REPORT")) printf ("  report : %s", runHook (rp, 300.0).c_str());

    for (int i = 0; i < 40; ++i) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false);
    auto Hafter = a.harm (note, 120);
    report ("AFTER  the gesture", Hafter);
    dumpWatch ("after ");
    printf ("  spectrum moved by %.1f dB relative\n", dist (Hbefore, Hafter));
    return 0;
  }
}
