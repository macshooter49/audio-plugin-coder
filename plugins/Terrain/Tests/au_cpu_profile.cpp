// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp32 — WHERE THE CPU GOES.  Max: "what's making the CPU so high even on only 2 osc? Sometimes
//  when I randomize I get really simple presets but it takes HELLA CPU when I'm playing 7th
//  chords. I even turn the ENV and UNISON's down and it still applies."
//
//  Renders the REAL installed AU and times every block on the wall clock, so the number is the one
//  the DAW's meter shows: a 512-frame block at 48 kHz has a 10.667 ms budget, so
//  CPU% = microseconds-per-block / 10666.7 * 100. Each scenario changes exactly ONE thing against
//  the same baseline, so the delta between two lines IS the cost of that thing.
//
//  ⚠️ No editor is attached, so vizLive is false and every UI feed is skipped. This measures the
//     AUDIO THREAD only — which is what a DAW's meter reports, and the part a patch controls.
//
//    clang++ -O2 -std=c++17 Tests/au_cpu_profile.cpp -o /tmp/aucpu \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    /tmp/aucpu            -> the scenario table
//    /tmp/aucpu list <sub> -> parameters whose name contains <sub>
//    TERRAIN_ORGANICS_DIR=<lib> /tmp/aucpu organic [id]   -> tp104: the Organics engine vs Wavetable (design §8 CPU
//          test): 4-note chord at unison 1 and 8-note chord at unison 7, same patch otherwise. The instrument (default
//          test.sine, the fixture) is handed over the way a host restores state: <ORGANICS><OSC slot="0" id=…/> in
//          jucePluginState, then the engine choice set to 7 (ORGANIC), then a pump so the library's load lands.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <functional>
static const double SR = 48000.0; static int BLK = 512;   // tp32 — settable: the FIXED per-block cost is what a small buffer multiplies
static double budgetUs() { return (double) BLK / SR * 1.0e6; }
static double nowUs()
{ static mach_timebase_info_data_t tb; if (tb.denom == 0) mach_timebase_info (&tb);
  return (double) mach_absolute_time() * (double) tb.numer / (double) tb.denom / 1000.0; }

struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    double stamp = 0.0;
    float peak = 0.0f;   // tp104 — the loudest sample rendered (a silent scenario is not a price)
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) return false;
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char nm[256] = {}; if (pi.cfNameString) CFStringGetCString (pi.cfNameString, nm, sizeof nm, kCFStringEncodingUTF8); else std::strncpy (nm, pi.name, 255);
            byName[nm] = id; info[id] = pi;
        }
        return true;
    }
    bool has (const char* n) const { return byName.count (n) != 0; }
    bool set (const char* name, float norm, bool loud = true)
    {
        auto it = byName.find (name);
        if (it == byName.end()) { if (loud) printf ("    !! no parameter named '%s'\n", name); return false; }
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    bool setIdx (const char* name, int idx)
    { auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
      return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, (float) idx, 0) == noErr; }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    // renders `blocks`, returning the per-block microseconds of each
    void render (int blocks, std::vector<double>* times)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        for (int b = 0; b < blocks; ++b)
        {
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            const double t0 = nowUs();
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            const double dt = nowUs() - t0;
            stamp += BLK;
            if (times) times->push_back (dt);
            for (int i = 0; i < BLK; ++i) peak = std::max (peak, std::fabs (bl[(size_t) i]));   // tp104 — proof the scenario SOUNDS
        }
        free (abl);
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
    // tp104 — splice <ORGANICS><OSC slot="0" id="…" rev="1"/></ORGANICS> into the plugin's own state (JUCE's binary XML:
    //   u32 magic 0x21324356, u32 byte count, UTF-8 XML + NUL) and hand it back through ClassInfo, like a host restore.
    bool injectOrganic (const std::string& id)
    {
        CFPropertyListRef dict = nullptr; UInt32 sz = sizeof dict;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &dict, &sz) != noErr || dict == nullptr) return false;
        CFDataRef d0 = (CFDataRef) CFDictionaryGetValue ((CFDictionaryRef) dict, CFSTR ("jucePluginState"));
        if (d0 == nullptr) { CFRelease (dict); return false; }
        const UInt8* b = CFDataGetBytePtr (d0); const CFIndex n = CFDataGetLength (d0);
        if (n < 9) { CFRelease (dict); return false; }
        std::string xml ((const char*) b + 8, (size_t) (n - 8)); while (! xml.empty() && xml.back() == 0) xml.pop_back();
        const size_t close = xml.rfind ("</");
        if (close == std::string::npos) { CFRelease (dict); return false; }
        if (getenv ("ORG_DEBUG")) printf ("    [inject] %zu bytes of state XML, tail: %s\n", xml.size(), xml.substr (xml.size() > 80 ? xml.size() - 80 : 0).c_str());
        xml.insert (close, "<ORGANICS><OSC slot=\"0\" id=\"" + id + "\" rev=\"1\"/></ORGANICS>");
        std::vector<UInt8> out (8 + xml.size() + 1, 0);
        const uint32_t magic = 0x21324356u, len = (uint32_t) (xml.size() + 1);
        std::memcpy (out.data(), &magic, 4); std::memcpy (out.data() + 4, &len, 4); std::memcpy (out.data() + 8, xml.data(), xml.size());
        CFMutableDictionaryRef m = CFDictionaryCreateMutableCopy (nullptr, 0, (CFDictionaryRef) dict);
        CFDataRef data = CFDataCreate (nullptr, out.data(), (CFIndex) out.size());
        CFDictionarySetValue (m, CFSTR ("jucePluginState"), data);
        CFPropertyListRef pl = m; const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, sizeof pl);
        CFRelease (data); CFRelease (m); CFRelease (dict);
        return st == noErr;
    }
};
// median is the honest statistic here: the first blocks after a note-on carry one-off setup,
// and the OS will occasionally steal the thread. The mean would report both as DSP cost.
static double med (std::vector<double> v)
{ if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }

struct Row { std::string name; double us; double cpu; };
static std::vector<Row> rows;
static double baseUs = -1;

static void run (const char* label, const std::vector<int>& notes, const std::function<void(Au&)>& setup)
{
    Au a; if (! a.open()) { printf ("no AU\n"); exit (2); }
    a.set ("Osc A Enable", 1.0f, false);
    setup (a);
    a.pump (0.40);
    a.render (6, nullptr);                       // settle: buffers sized, smoothers converged
    for (int n : notes) a.note (n, 100);
    a.render (10, nullptr);                      // let the attack pass
    a.peak = 0.0f;
    std::vector<double> t; a.render (60, &t);
    const float pk = a.peak;
    a.close();
    const double us = med (t);
    rows.push_back ({ label, us, us / budgetUs() * 100.0 });
    printf ("  %-46s %8.0f us   %6.2f %% of one core%s   [peak %.1f dBFS]\n", label, us, us / budgetUs() * 100.0,
            baseUs > 0 ? (std::string ("   (+") + std::to_string ((long) (us - baseUs)) + " us)").c_str() : "",
            pk > 1e-9f ? 20.0 * std::log10 ((double) pk) : -240.0);
}
int main (int argc, char** argv)
{
    // ── prof <which> — ONE scenario, so the plugin's own per-section timer (TERRAIN_PROFILE) can
    //    be aggregated without the scenarios' lines interleaving. Run as:
    //      TERRAIN_PROFILE=1 TERRAIN_PROFILE_US=0 /tmp/aucpu prof ref 2>prof.txt
    if (argc > 2 && ! std::strcmp (argv[1], "prof"))
    {
        const std::string w = argv[2];
        const std::vector<int> sev { 60, 64, 67, 71 };
        const std::vector<int> quiet {};
        if      (w == "idle")   run ("idle",   quiet, [] (Au&) {});
        else if (w == "ref")    run ("ref",    sev, [] (Au&) {});
        else if (w == "unison") run ("unison", sev, [] (Au& a) { a.set ("Synth OSC A Unison", 6.0f / 15.0f); });
        else if (w == "geode")  run ("geode",  sev, [] (Au& a) { a.setIdx ("Synth OSC A Engine", 3); });
        else if (w == "harm")   run ("harm",   sev, [] (Au& a) { a.setIdx ("Synth OSC A Engine", 5); });
        else if (w == "fx")     run ("fx",     sev, [] (Au& a) {
            static const char* const K[6] = { "Reverb", "Delay", "Distortion", "Chorus", "Flanger", "Phaser" };
            for (const char* k : K) { a.set ((std::string (k) + " In Chain").c_str(), 1.0f, false);
                                      a.set ((std::string (k) + " Power").c_str(),    1.0f, false); }
            a.set ("SYN_RVB_SRC_A", 1.0f, false); a.set ("SYN_DLY_SRC_A", 1.0f, false); a.set ("SYN_DST_SRC_A", 1.0f, false);
            a.set ("SYN_CHO_SRC_A", 1.0f, false); a.set ("SYN_FLA_SRC_A", 1.0f, false); a.set ("SYN_PHA_SRC_A", 1.0f, false); });
        else if (w == "heavy")  run ("heavy",  sev, [] (Au& a) {
            a.set ("Osc B Enable", 1.0f, false);
            a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC B Unison", 6.0f / 15.0f);
            a.set ("Synth OSC A Filter 2 Send", 1.0f); a.set ("Synth OSC B Filter 2 Send", 1.0f); });
        else { printf ("unknown: %s\n", w.c_str()); return 2; }
        return 0;
    }
    if (argc > 2 && ! std::strcmp (argv[1], "list"))
    { Au a; if (! a.open()) { printf ("no AU\n"); return 2; }
      for (auto& kv : a.byName) if (kv.first.find (argv[2]) != std::string::npos) printf ("  %s\n", kv.first.c_str());
      printf ("  (%zu parameters)\n", a.byName.size()); return 0; }

    // ── blocks <scenario> — the SAME patch at every buffer size a DAW offers. A cost that is
    //    per-BLOCK rather than per-sample is invisible at 512 and brutal at 64, which is exactly
    //    the shape of "a simple preset that still eats CPU".
    if (argc > 2 && ! std::strcmp (argv[1], "blocks"))
    {
        const std::string w = argv[2];
        const std::vector<int> sev { 60, 64, 67, 71 };
        const std::vector<int> quiet {};
        printf ("\n== tp32 - THE SAME PATCH AT EVERY BUFFER SIZE (%s) ==\n\n", w.c_str());
        for (int bs : { 64, 128, 256, 512, 1024 })
        {
            BLK = bs; rows.clear(); baseUs = -1;
            char lab[64]; snprintf (lab, sizeof lab, "%d frames  (%.2f ms budget)", bs, budgetUs() / 1000.0);
            if (w == "idle") run (lab, quiet, [] (Au&) {});
            else             run (lab, sev,   [] (Au&) {});
        }
        printf ("\n");
        return 0;
    }

    // ── extremes <n> — how bad it can actually get, so the numbers above have a ceiling to sit under.
    if (argc > 1 && ! std::strcmp (argv[1], "extremes"))
    {
        const std::vector<int> sev  { 60, 64, 67, 71 };
        const std::vector<int> big  { 48, 52, 55, 59, 60, 64, 67, 71 };
        printf ("\n== tp32 - HOW BAD IT GETS ==   %.0f us budget per block\n\n", budgetUs());
        run ("4 notes, 1 osc, nothing on (the floor)", sev, [] (Au&) {});
        baseUs = rows.back().us;
        run ("4 notes, unison STACK on top of unison 7", sev, [] (Au& a) {
            a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC A Unison Stack", 1.0f); });
        run ("8 notes, 1 osc, unison 7",                 big, [] (Au& a) { a.set ("Synth OSC A Unison", 6.0f / 15.0f); });
        run ("8 notes, 2 osc, unison 7 each",            big, [] (Au& a) {
            a.set ("Osc B Enable", 1.0f, false); a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC B Unison", 6.0f / 15.0f); });
        run ("8 notes, 4 osc, unison 7 each",            big, [] (Au& a) {
            a.set ("Osc B Enable", 1.0f, false); a.set ("Osc C Enable", 1.0f, false); a.set ("Osc D Enable", 1.0f, false);
            for (const char* o : { "A", "B", "C", "D" }) a.set ((std::string ("Synth OSC ") + o + " Unison").c_str(), 6.0f/15.0f, false); });
        run ("8 notes, 2 osc unison 7, BOTH on Geode",   big, [] (Au& a) {
            a.set ("Osc B Enable", 1.0f, false); a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC B Unison", 6.0f / 15.0f);
            a.setIdx ("Synth OSC A Engine", 3); a.setIdx ("Synth OSC B Engine", 3); });
        run ("8 notes, 2 osc unison 7, both Harmonic",   big, [] (Au& a) {
            a.set ("Osc B Enable", 1.0f, false); a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC B Unison", 6.0f / 15.0f);
            a.setIdx ("Synth OSC A Engine", 5); a.setIdx ("Synth OSC B Engine", 5); });
        run ("...and E-H on as well (the second bank)",  big, [] (Au& a) {
            a.set ("Osc B Enable", 1.0f, false); a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC B Unison", 6.0f / 15.0f);
            for (const char* o : { "E", "F", "G", "H" }) a.set ((std::string ("Osc ") + o + " Enable").c_str(), 1.0f, false); });
        printf ("\n");
        return 0;
    }

    // ── knobs — the per-oscillator SHAPING controls. These are the ones that look free on the
    //    panel: a number on a dial, no extra module, no obvious "feature". Max's own screenshot has
    //    Spectral at 100 and Fold at 9 on oscillator A.
    if (argc > 1 && ! std::strcmp (argv[1], "knobs"))
    {
        const std::vector<int> sev { 60, 64, 67, 71 };
        printf ("\n== tp32 - WHAT A SINGLE OSCILLATOR KNOB COSTS ==   4-note chord, one oscillator\n\n");
        run ("reference: every shaping knob at zero", sev, [] (Au& a) {
            a.set ("OSC A Spectral Amount", 0.0f, false); a.set ("OSC A Fold Amount", 0.0f, false);
            a.set ("Synth OSC A Warp Amount", 0.0f, false); a.set ("OSC A Feedback", 0.0f, false); });
        baseUs = rows.back().us;
        run ("+ Spectral Amount 100",    sev, [] (Au& a) { a.set ("OSC A Spectral Amount", 1.0f); });
        run ("+ Fold Amount 100",        sev, [] (Au& a) { a.set ("OSC A Fold Amount", 1.0f); });
        run ("+ Warp Amount 100",        sev, [] (Au& a) { a.set ("Synth OSC A Warp Amount", 1.0f); });
        run ("+ Warp 2 Amount 100",      sev, [] (Au& a) { a.set ("Synth OSC A Warp 2 Amount", 1.0f); });
        run ("+ Feedback 100",           sev, [] (Au& a) { a.set ("OSC A Feedback", 1.0f); });
        run ("+ Blend 1 depth 100 (FM)", sev, [] (Au& a) { a.set ("Synth OSC A Blend 1 Depth", 1.0f); });
        run ("+ ALL of them at once",    sev, [] (Au& a) {
            a.set ("OSC A Spectral Amount", 1.0f, false); a.set ("OSC A Fold Amount", 1.0f, false);
            a.set ("Synth OSC A Warp Amount", 1.0f, false); a.set ("Synth OSC A Warp 2 Amount", 1.0f, false);
            a.set ("OSC A Feedback", 1.0f, false); a.set ("Synth OSC A Blend 1 Depth", 1.0f, false); });
        run ("...all of them, WITH unison 7", sev, [] (Au& a) {
            a.set ("OSC A Spectral Amount", 1.0f, false); a.set ("OSC A Fold Amount", 1.0f, false);
            a.set ("Synth OSC A Warp Amount", 1.0f, false); a.set ("Synth OSC A Warp 2 Amount", 1.0f, false);
            a.set ("OSC A Feedback", 1.0f, false); a.set ("Synth OSC A Blend 1 Depth", 1.0f, false);
            a.set ("Synth OSC A Unison", 6.0f/15.0f, false); });
        printf ("\n");
        return 0;
    }

    // ── unison — Max: "I even turn the ENV and UNISON'S down but it still applies." If the cost
    //    does not fall when the count falls, that is a bug and not a misunderstanding. Sweep it.
    if (argc > 1 && ! std::strcmp (argv[1], "unison"))
    {
        const std::vector<int> sev { 60, 64, 67, 71 };
        printf ("\n== tp32 - DOES TURNING UNISON DOWN ACTUALLY COST LESS? ==   4-note chord, one oscillator\n\n");
        { Au a; if (a.open()) { auto it = a.byName.find ("Synth OSC A Unison");
            if (it != a.byName.end()) { const auto& pi = a.info.at (it->second);
              printf ("  (the parameter reports min %.2f  max %.2f)\n\n", pi.minValue, pi.maxValue); } a.close(); } }
        for (int v : { 1, 2, 3, 4, 5, 6, 7, 8, 12, 16 })
        { char lab[64]; snprintf (lab, sizeof lab, "unison = %d voice%s", v, v == 1 ? "" : "s");
          // ⚠️ NORMALISED, not an index. The AU reports this parameter as 0..1 over 1..16 voices,
          //    so setIdx(7) clamps to 1.0 = SIXTEEN voices. Getting this wrong makes every row on
          //    the sweep identical and the answer "turning it down does nothing" - which is exactly
          //    the false conclusion this test exists to avoid.
          run (lab, sev, [v] (Au& a) { a.set ("Synth OSC A Unison", (float) (v - 1) / 15.0f); }); }
        printf ("\n  -- and the RELEASE tail: a voice keeps costing until its amp envelope ends --\n");
        baseUs = -1;
        run ("release short, chord held",  sev, [] (Au& a) { a.set ("Synth Amp Release", 0.0f); });
        run ("release long,  chord held",  sev, [] (Au& a) { a.set ("Synth Amp Release", 1.0f); });
        printf ("\n");
        return 0;
    }

    // ── notes — the per-VOICE curve, plus the run-to-run noise floor, so no claim is made about
    //    an effect smaller than the measurement can see.
    if (argc > 1 && ! std::strcmp (argv[1], "notes"))
    {
        printf ("\n== tp32 - WHAT ONE NOTE COSTS ==   one oscillator, wavetable, no unison\n\n");
        printf ("  the same scenario five times, to show what the measurement's own spread is:\n");
        const std::vector<int> sev { 60, 64, 67, 71 };
        for (int i = 0; i < 5; ++i) run ("    4-note chord, repeat", sev, [] (Au&) {});
        double lo = 1e9, hi = 0; for (auto& r : rows) { lo = std::min (lo, r.us); hi = std::max (hi, r.us); }
        printf ("  -> spread %.0f us (%.2f %% of a core). Nothing smaller than that is a result.\n\n", hi - lo, (hi - lo) / budgetUs() * 100.0);
        rows.clear(); baseUs = -1;
        printf ("  how it scales with polyphony:\n");
        static const int kChord[] = { 36, 40, 43, 47, 48, 52, 55, 59, 60, 64, 67, 71, 72, 76, 79, 83 };
        for (int n : { 0, 1, 2, 4, 6, 8, 12, 16 })
        { std::vector<int> v (kChord, kChord + n); char lab[64];
          snprintf (lab, sizeof lab, "%2d note%s sounding", n, n == 1 ? " " : "s");
          run (lab, v, [] (Au&) {}); }
        printf ("\n");
        return 0;
    }

    // ── rack — the FX rack is a PER-BLOCK cost: it runs whether one note sounds or none, so it
    //    lands on a "simple" patch exactly as hard as on a busy one. The chain dice fills it.
    if (argc > 1 && ! std::strcmp (argv[1], "rack"))
    {
        const std::vector<int> sev { 60, 64, 67, 71 };
        static const char* const K[12] = { "Reverb", "Delay", "Distortion", "Chorus", "Flanger", "Phaser",
                                           "Equalizer", "Widen", "Compress", "Multiband", "Bode", "Utility" };
        printf ("\n== tp32 - WHAT THE FX RACK COSTS ==   4-note chord, one oscillator\n\n");
        run ("no effects at all", sev, [] (Au&) {});
        baseUs = rows.back().us;
        for (int n : { 1, 2, 4, 6, 9, 12 })
        { char lab[64]; snprintf (lab, sizeof lab, "%2d effect%s in the chain, all powered", n, n == 1 ? " " : "s");
          run (lab, sev, [n] (Au& a) { for (int i = 0; i < n; ++i) {
              a.set ((std::string (K[i]) + " In Chain").c_str(), 1.0f, false);
              a.set ((std::string (K[i]) + " Power").c_str(),    1.0f, false); } }); }
        printf ("\n  -- the same, but each one ROUTED to oscillator A (a send filter pair per voice) --\n");
        baseUs = rows.front().us;
        static const char* const S[6] = { "SYN_RVB_SRC_A", "SYN_DLY_SRC_A", "SYN_DST_SRC_A",
                                          "SYN_CHO_SRC_A", "SYN_FLA_SRC_A", "SYN_PHA_SRC_A" };
        for (int n : { 1, 3, 6 })
        { char lab[64]; snprintf (lab, sizeof lab, "%2d effect%s, routed to oscillator A", n, n == 1 ? " " : "s");
          run (lab, sev, [n] (Au& a) { for (int i = 0; i < n; ++i) {
              a.set ((std::string (K[i]) + " In Chain").c_str(), 1.0f, false);
              a.set ((std::string (K[i]) + " Power").c_str(),    1.0f, false);
              a.set (S[i], 1.0f, false); } }); }
        printf ("\n");
        return 0;
    }

    // ── beacon <secs> <scenario> — hold a chord and render for long enough (WALL time) that the
    //    plugin's OWN cpu probe ticks. That probe is the deepest attribution available: it splits
    //    the DSP into gather / voices / fx+master AND reports the message thread separately.
    //    It writes /tmp/terrain-cpu.txt; switch it on by creating /tmp/terrain-cpu-on.txt.
    if (argc > 1 && ! std::strcmp (argv[1], "beacon"))
    {
        setenv ("TERRAIN_CPU_PROBE", "1", 1);
        const double secs = argc > 2 ? atof (argv[2]) : 12.0;
        const std::string w = argc > 3 ? argv[3] : "chord";
        Au a; if (! a.open()) { printf ("no AU\n"); return 2; }
        a.set ("Osc A Enable", 1.0f, false);
        if (w == "unison") a.set ("Synth OSC A Unison", 6.0f / 15.0f);
        if (w == "heavy")  { a.set ("Osc B Enable", 1.0f, false);
                             a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC B Unison", 6.0f / 15.0f);
                             a.setIdx ("Synth OSC A Engine", 5); a.setIdx ("Synth OSC B Engine", 5); }
        a.pump (0.4); a.render (4, nullptr);
        for (int n : { 48, 52, 55, 59, 60, 64, 67, 71 }) a.note (n, 100);
        printf ("  rendering %s for %.0f s of wall time so the plugin's own probe ticks...\n", w.c_str(), secs);
        const double t0 = CFAbsoluteTimeGetCurrent();
        long blocks = 0;
        while (CFAbsoluteTimeGetCurrent() - t0 < secs) { a.render (8, nullptr); ++blocks; }
        a.close();
        printf ("  done (%ld renders)\n\n", blocks);
        return 0;
    }

    // ── tp104 — organic [id]: the Organics engine against Wavetable, the design §8 CPU bar ──
    if (argc > 1 && ! std::strcmp (argv[1], "organic"))
    {
        const std::string id = argc > 2 ? argv[2] : "test.sine";
        const std::vector<int> c4 { 60, 64, 67, 71 }, c8 { 48, 52, 55, 59, 60, 64, 67, 71 };
        printf ("\n== tp104 - THE ORGANICS ENGINE vs WAVETABLE ==   instrument '%s' (TERRAIN_ORGANICS_DIR=%s)\n\n", id.c_str(),
                getenv ("TERRAIN_ORGANICS_DIR") ? getenv ("TERRAIN_ORGANICS_DIR") : "(the installed library)");
        auto org = [id] (Au& a) { a.setIdx ("Synth OSC A Engine", 7); a.pump (0.2); const bool ok = a.injectOrganic (id); if (getenv ("ORG_DEBUG")) printf ("    [inject] %s\n", ok ? "ok" : "FAILED"); a.pump (1.5);
            if (getenv ("ORG_DEBUG")) { for (const char* n : { "Synth OSC A Organic Instrument", "Synth OSC A Engine", "Osc A Enable" }) { auto it = a.byName.find (n); AudioUnitParameterValue v = -1; if (it != a.byName.end()) AudioUnitGetParameter (a.au, it->second, kAudioUnitScope_Global, 0, &v); printf ("    [param] %s = %g\n", n, v); } } };
        baseUs = -1;
        run ("idle, no notes",                                 {},  [] (Au&) {});
        baseUs = rows.back().us;
        run ("WT       4-note chord, unison 1",                c4,  [] (Au&) {});
        const double wt4 = rows.back().us;
        run ("ORGANIC  4-note chord, unison 1",                c4,  org);
        const double or4 = rows.back().us;
        run ("WT       8-note chord, unison 7",                c8,  [] (Au& a) { a.set ("Synth OSC A Unison", 6.0f / 15.0f); });
        const double wt8 = rows.back().us;
        run ("ORGANIC  8-note chord, unison 7 (Ensemble)",     c8,  [org] (Au& a) { a.set ("Synth OSC A Unison", 6.0f / 15.0f); org (a); });
        const double or8 = rows.back().us;
        printf ("\n  4-note: Organic %.0f us vs WT %.0f us  -> %s\n", or4, wt4, or4 <= wt4 * 1.02 ? "PASS (not slower than WT, within 2 pct timer noise)" : "FAIL (slower than WT)");
        printf ("  8-note unison 7: Organic %.0f us vs WT %.0f us  -> %s\n\n", or8, wt8, or8 <= wt8 * 1.02 ? "PASS" : "FAIL");
        return (or4 <= wt4 * 1.02 && or8 <= wt8 * 1.02) ? 0 : 1;
    }

    printf ("\n== tp32 - WHERE THE CPU GOES ==   512 frames @ 48 kHz = a %.0f us budget per block\n\n", budgetUs());
    const std::vector<int> none {};
    const std::vector<int> one  { 60 };
    const std::vector<int> sev  { 60, 64, 67, 71 };                  // a C major 7th
    const std::vector<int> sev2 { 48, 52, 55, 59, 60, 64, 67, 71 };  // the same chord, two octaves

    printf ("  -- how the cost scales with what you PLAY (one oscillator, default everything) --\n");
    run ("idle, no notes",                     none, [] (Au&) {});
    const double idleUs = rows.back().us; baseUs = idleUs;
    run ("1 note",                             one,  [] (Au&) {});
    run ("a 4-note 7th chord",                 sev,  [] (Au&) {});
    run ("the same chord in two octaves (8)",  sev2, [] (Au&) {});

    printf ("\n  -- the same 4-note chord, changing ONE thing --\n");
    run ("4-note chord (the reference line)",  sev,  [] (Au&) {});
    baseUs = rows.back().us;
    run ("+ a second oscillator",              sev,  [] (Au& a) { a.set ("Osc B Enable", 1.0f, false); });
    run ("+ UNISON 7 on oscillator A",         sev,  [] (Au& a) { a.set ("Synth OSC A Unison", 6.0f / 15.0f); });
    run ("+ FILTER 2 as well as filter 1",     sev,  [] (Au& a) { a.set ("Synth OSC A Filter 2 Send", 1.0f); });
    run ("+ filter DRIVE at full (2x gate)",   sev,  [] (Au& a) { a.set ("Synth Filter 1 Drive", 1.0f); });
    run ("+ a Reverb routed to oscillator A",  sev,  [] (Au& a) {
        a.set ("Reverb In Chain", 1.0f, false); a.set ("Reverb Power", 1.0f, false); a.set ("SYN_RVB_SRC_A", 1.0f, false); });
    run ("+ a Glitch flow card",               sev,  [] (Au& a) { a.setIdx ("Flow Chain 1", 3); });

    // ── THE FILTER TYPE. 118 of them and the dice picks freely; they are not the same price. ──
    printf ("\n  -- the same 4-note chord, sweeping the FILTER TYPE (the dice picks any of 118) --\n");
    struct FT { const char* nm; int idx; };
    static const FT kTypes[] = {
        { "Off (no filter)",            27 },
        { "Ladder LP 24 (the default)",  0 },
        { "Acid 303",                    4 },
        { "Comb",                       10 },
        { "Shimmer",                    12 },
        { "Karplus",                    13 },
        { "Formant A",                  14 },
        { "Reverb filter",              18 },
        { "Phaser 4",                   19 },
        { "Phaser 8",                   20 },
        { "Bode shifter",               22 },
        { "Grain mask",                 25 },
        { "Convolution reverb filter",  26 },
        { "Diffusor",                   75 },
        { "Phaser 32",                 101 },
        { "Phaser 48",                 103 },
        { "Comb band",                 109 },
        { "Formant soprano",           115 },
    };
    baseUs = -1;
    for (const auto& ft : kTypes)
    { const int ix = ft.idx; char lab[96]; snprintf (lab, sizeof lab, "filter = %s", ft.nm);
      run (lab, sev, [ix] (Au& a) { a.setIdx ("Synth Filter 1 Type", ix); }); }

    // ── THE ENGINE. Same chord, same filter, different oscillator engine. ──
    printf ("\n  -- the same 4-note chord, sweeping the OSCILLATOR ENGINE --\n");
    static const char* const kEng[] = { "Wavetable", "Sample", "Granular", "Geode/Spectral", "FM", "Harmonic", "Modal",
                                        "Organic (no instrument)" };   // tp104 — the choice is 12 wide now (8..11 reserved)
    baseUs = -1;
    for (int e = 0; e < 8; ++e)
    { char lab[96]; snprintf (lab, sizeof lab, "engine = %s", kEng[e]);
      run (lab, sev, [e] (Au& a) { a.setIdx ("Synth OSC A Engine", e); }); }
    run ("engine = Harmonic, 512 partials", sev, [] (Au& a) {
        a.setIdx ("Synth OSC A Engine", 5); a.set ("Synth OSC A Harm Count", 1.0f); });

    printf ("\n  -- and the combination a dice roll actually produces --\n");
    baseUs = idleUs;
    run ("2 osc, unison 7 each, both filters, drive", sev, [] (Au& a) {
        a.set ("Osc B Enable", 1.0f, false);
        a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC B Unison", 6.0f / 15.0f);
        a.set ("Synth OSC A Filter 2 Send", 1.0f); a.set ("Synth OSC B Filter 2 Send", 1.0f);
        a.set ("Synth Filter 1 Drive", 0.7f); a.set ("Synth Filter 2 Drive", 0.7f); });
    run ("...and a Phaser 48 in filter 1",    sev, [] (Au& a) {
        a.set ("Osc B Enable", 1.0f, false);
        a.set ("Synth OSC A Unison", 6.0f / 15.0f); a.set ("Synth OSC B Unison", 6.0f / 15.0f);
        a.set ("Synth OSC A Filter 2 Send", 1.0f); a.set ("Synth OSC B Filter 2 Send", 1.0f);
        a.set ("Synth Filter 1 Drive", 0.7f); a.set ("Synth Filter 2 Drive", 0.7f);
        a.setIdx ("Synth Filter 1 Type", 103); });
    printf ("\n");
    return 0;
}
