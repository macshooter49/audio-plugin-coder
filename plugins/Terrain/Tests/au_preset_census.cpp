// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp36 — THE PRESET CENSUS.  Max: "do a deep dive on the presets that are on my user. Load them
//  up and play stuff, run simulations, figure out what the fuck is driving the CPU so high."
//  And, mid-session: "it's the RELEASE bro."
//
//  Loads every .terrain in the user bank into the REAL installed AU (the same chunk the preset
//  browser loads, through kAudioUnitProperty_ClassInfo), then renders the scenarios that match
//  how he plays and times every block on the wall clock: idle · one note · a 4-note chord · an
//  8-note chord · the RELEASE TAIL of the 4-note chord at 0.25/0.5/1/2/4 s after note-off (and
//  the time until the plugin is idle again) · a re-triggered chord (the arp/chop shape: a new
//  4-note chord every 250 ms with the old ones still releasing — the voice count climbs to the
//  polyphony). CPU% is of ONE core at 512 frames / 48 kHz (a 10.667 ms budget), the number a
//  DAW's meter shows before it divides by cores.
//
//  ablate <preset-substring>: the heaviest thing, one change at a time, on one preset —
//  unison→1 · FX power off · engines→wavetable · filters off · release→short — each delta IS
//  the cost of that thing in this preset.
//
//    clang++ -O2 -std=c++17 Tests/au_preset_census.cpp -o /tmp/aucensus \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    /tmp/aucensus                 -> the table, every user preset, sorted by the 8-note chord
//    /tmp/aucensus tail <sub>      -> the release tail of one preset, block by block
//    /tmp/aucensus ablate <sub>    -> the ablation table for one preset
//    /tmp/aucensus names <sub>     -> parameter names containing <sub>
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <functional>
static const double SR = 48000.0; static int BLK = 512;
static double budgetUs() { return (double) BLK / SR * 1.0e6; }
static double nowUs()
{ static mach_timebase_info_data_t tb; if (tb.denom == 0) mach_timebase_info (&tb);
  return (double) mach_absolute_time() * (double) tb.numer / (double) tb.denom / 1000.0; }
static double median (std::vector<double> v) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }
static double pct (std::vector<double> v, double p) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[std::min (v.size() - 1, (size_t) (v.size() * p))]; }

struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    double stamp = 0.0;
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
    float get (const std::string& name) { auto it = byName.find (name); if (it == byName.end()) return NAN; AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v); return v; }
    bool setRaw (const std::string& name, float v) { auto it = byName.find (name); if (it == byName.end()) return false; return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    bool setNorm (const std::string& name, float norm) { auto it = byName.find (name); if (it == byName.end()) return false; const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr; }
    // the preset's chunk goes in the way the AU host restores state: ClassInfo["jucePluginState"]
    bool loadChunk (const std::vector<char>& chunk)
    {
        CFPropertyListRef dict = nullptr; UInt32 sz = sizeof dict;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &dict, &sz) != noErr || dict == nullptr) return false;
        CFMutableDictionaryRef m = CFDictionaryCreateMutableCopy (nullptr, 0, (CFDictionaryRef) dict);
        CFDataRef data = CFDataCreate (nullptr, (const UInt8*) chunk.data(), (CFIndex) chunk.size());
        CFDictionarySetValue (m, CFSTR ("jucePluginState"), data);
        CFPropertyListRef pl = m; const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, sizeof pl);
        CFRelease (data); CFRelease (m); CFRelease (dict);
        return st == noErr;
    }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void capture (int blocks, std::vector<float>& mono)   // tp39f — raw left-channel samples (the pitch probe)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        for (int b = 0; b < blocks; ++b)
        {
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            mono.insert (mono.end(), bl.begin(), bl.end());
        }
        free (abl);
    }
    void render (int blocks, std::vector<double>* times, float* peakOut = nullptr)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        float peak = 0;
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
            for (int i = 0; i < BLK; ++i) peak = std::max (peak, std::fabs (bl[(size_t) i]));
        }
        if (peakOut) *peakOut = peak;
        free (abl);
    }
    void allOff() { for (int n = 0; n < 128; ++n) note (n, 0); }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};

struct Preset { std::string name, file, manifest; std::vector<char> chunk; long bytes = 0; };
static bool readPreset (const std::string& path, Preset& p)
{
    FILE* f = fopen (path.c_str(), "rb"); if (! f) return false;
    std::vector<char> b; char buf[65536]; size_t n; while ((n = fread (buf, 1, sizeof buf, f)) > 0) b.insert (b.end(), buf, buf + n); fclose (f);
    if (b.size() < 12 || std::memcmp (b.data(), "TRN1", 4) != 0) return false;
    int ml = 0; std::memcpy (&ml, b.data() + 4, 4); if (ml < 0 || (size_t) ml > b.size()) return false;
    p.manifest.assign (b.data() + 8, (size_t) ml);
    int cl = 0; std::memcpy (&cl, b.data() + 8 + ml, 4); if (cl <= 8 || (size_t) (12 + ml + cl) > b.size()) return false;
    p.chunk.assign (b.data() + 12 + ml, b.data() + 12 + ml + cl);
    p.bytes = (long) b.size(); p.file = path;
    auto k = p.manifest.find ("\"name\": \""); if (k == std::string::npos) k = p.manifest.find ("\"name\":\"");
    if (k != std::string::npos) { k = p.manifest.find ('"', k + 7); auto e = p.manifest.find ('"', k + 1); p.name = p.manifest.substr (k + 1, e - k - 1); }
    else p.name = path.substr (path.rfind ('/') + 1);
    return true;
}
static std::string field (const std::string& m, const char* key)
{ auto k = m.find (std::string ("\"") + key + "\""); if (k == std::string::npos) return ""; k = m.find (':', k); auto v = m.find_first_not_of (" ", k + 1);
  if (m[v] == '"') { auto e = m.find ('"', v + 1); return m.substr (v + 1, e - v - 1); } auto e = m.find_first_of (",}", v); return m.substr (v, e - v); }

static const int CHORD4[4] = { 48, 52, 55, 59 }, CHORD8[8] = { 36, 43, 48, 52, 55, 59, 62, 67 };
struct Row { std::string name; double idle, n1, n4, n8, t025, t05, t1, t2, t4, retrig; double toIdle; std::string fx, eng; long kb; };

// one preset, fully measured — the AU is fresh for every preset so nothing leaks between them
static bool census (const Preset& p, Row& r, bool verbose)
{
    Au a; if (! a.open()) { printf ("  !! AU open failed\n"); return false; }
    if (! a.loadChunk (p.chunk)) { printf ("  !! %s: ClassInfo set failed\n", p.name.c_str()); a.close(); return false; }
    a.pump (0.8); a.render (20, nullptr); a.pump (0.4);   // the load's async work (table bakes, sample decodes) — let it land
    r.name = p.name; r.kb = p.bytes / 1024; r.fx = field (p.manifest, "fx");
    std::vector<double> t;
    auto meas = [&] (int settle, int blocks) { a.render (settle, nullptr); t.clear(); a.render (blocks, &t); return median (t); };
    r.idle = meas (10, 40);
    a.note (CHORD4[0], 100); r.n1 = meas (12, 50); a.allOff(); a.render (200, nullptr);
    for (int n : CHORD4) a.note (n, 100); r.n4 = meas (12, 50);
    // the release tail: note-off, then the cost at 0.25 / 0.5 / 1 / 2 / 4 s, and the time to idle
    a.allOff();
    const double bs = (double) BLK / SR; auto at = [&] (double sec) { return (int) std::lround (sec / bs); };
    std::vector<double> tail; float pk = 0; a.render (at (4.2), &tail);
    auto tailAt = [&] (double sec) { const int i = at (sec); std::vector<double> w (tail.begin() + std::max (0, i - 3), tail.begin() + std::min ((int) tail.size(), i + 4)); return median (w); };
    r.t025 = tailAt (0.25); r.t05 = tailAt (0.5); r.t1 = tailAt (1.0); r.t2 = tailAt (2.0); r.t4 = tailAt (4.0);
    r.toIdle = -1; { const double floor = r.idle * 1.15 + 15.0; int run = 0; for (size_t i = 0; i < tail.size(); ++i) { if (tail[i] <= floor) { if (++run >= 8) { r.toIdle = (double) (i - 7) * bs; break; } } else run = 0; } }
    a.render (at (2.0), nullptr);
    for (int n : CHORD8) a.note (n, 100); r.n8 = meas (12, 50); a.allOff(); a.render (at (6.0), nullptr);
    // the arp/chop shape: a new 4-note chord every 250 ms (the old ones still releasing) for 3 s, measured over the last second
    { std::vector<double> rt; for (int k = 0; k < 12; ++k) { a.allOff(); for (int n : CHORD4) a.note (n + (k % 3) * 2, 100); std::vector<double> tt; a.render (at (0.25), &tt); if (k >= 8) rt.insert (rt.end(), tt.begin(), tt.end()); } r.retrig = median (rt); a.allOff(); }
    (void) pk; (void) verbose;
    a.close(); return true;
}

static std::vector<Preset> loadAll (const char* dir, const char* sub)
{
    std::vector<Preset> out; DIR* d = opendir (dir); if (! d) return out;
    while (auto* e = readdir (d)) { std::string n = e->d_name; if (n.size() < 9 || n.substr (n.size() - 8) != ".terrain") continue;
        if (sub && *sub && strcasestr (n.c_str(), sub) == nullptr) continue;
        Preset p; if (readPreset (std::string (dir) + "/" + n, p)) out.push_back (std::move (p)); }
    closedir (d); std::sort (out.begin(), out.end(), [] (const Preset& x, const Preset& y) { return x.name < y.name; }); return out;
}
static std::string userBank() { if (const char* b = getenv ("TP_BANK")) return b;   /* tp36 — any folder of .terrain files (the dice rolls the probe saved) */
    return std::string (getenv ("HOME")) + "/Library/WavesCrate/TerrainInstrument/Banks/User"; }
static int juce_jlimit0 (int v) { return v < 0 ? 0 : (v > 6 ? 6 : v); }
static double cpu (double us) { return us / budgetUs() * 100.0; }

int main (int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "names") { Au a; if (! a.open()) return 1; int n = 0; for (auto& kv : a.byName) if (argc < 3 || strcasestr (kv.first.c_str(), argv[2])) { const auto& pi = a.info.at (kv.second); printf ("  %s  [%g..%g def %g]\n", kv.first.c_str(), pi.minValue, pi.maxValue, pi.defaultValue); ++n; } printf ("  (%d of %zu)\n", n, a.byName.size()); a.close(); return 0; }
    if (mode == "price")   // THE PRICE LIST on the init patch: 8 notes held; every filter type, engine, unison count; every FX kind's idle cost
    {
        Au a0; if (! a0.open()) return 1; auto info = [&] (const char* n) { auto it = a0.byName.find (n); if (it == a0.byName.end()) return std::make_pair (0.f, 0.f); auto& pi = a0.info.at (it->second); return std::make_pair (pi.minValue, pi.maxValue); };
        auto ft = info ("Synth Filter 1 Type"), en = info ("Synth OSC A Engine"), un = info ("Synth OSC A Unison"); a0.close();
        printf ("  ranges: filter type %g..%g  engine %g..%g  unison %g..%g\n", ft.first, ft.second, en.first, en.second, un.first, un.second);
        auto measure = [&] (std::function<void (Au&)> setup, int notes) { Au a; if (! a.open()) return -1.0; a.pump (0.3); a.render (10, nullptr); setup (a); a.render (20, nullptr);
            for (int i = 0; i < notes; ++i) a.note (CHORD8[i], 100); a.render (12, nullptr); std::vector<double> t; a.render (40, &t); a.allOff(); a.close(); return median (t); };
        const double base8 = measure ([] (Au&) {}, 8), base0 = measure ([] (Au&) {}, 0);
        printf ("  init patch: idle %.0f us (%.1f%%)   8 notes %.0f us (%.1f%%)\n", base0, cpu (base0), base8, cpu (base8));
        printf ("\n  FILTER 1 TYPE (8 notes, drive 0, res 0), delta vs init:\n");
        for (int ty = 0; ty <= (int) ft.second; ++ty) { const double v = measure ([&] (Au& a) { a.setRaw ("Synth Filter 1 Type", (float) ty); }, 8); printf ("   type %3d  %6.0f us  %+6.0f\n", ty, v, v - base8); fflush (stdout); }
        printf ("\n  FILTER 1 TYPE with DRIVE 0.5 (the 2x gate), a few families:\n");
        for (int ty : { 0, 5, 27, 60, 80, 96, 104, 107, 110, 117 }) { if (ty > (int) ft.second) continue; const double v = measure ([&] (Au& a) { a.setRaw ("Synth Filter 1 Type", (float) ty); a.setNorm ("Synth Filter 1 Drive", 0.5f); }, 8); printf ("   type %3d drv .5  %6.0f us  %+6.0f\n", ty, v, v - base8); fflush (stdout); }
        printf ("\n  ENGINE on osc A (8 notes):\n");
        for (int e = 0; e <= (int) en.second; ++e) { const double v = measure ([&] (Au& a) { a.setRaw ("Synth OSC A Engine", (float) e); }, 8); printf ("   engine %d  %6.0f us  %+6.0f\n", e, v, v - base8); fflush (stdout); }
        printf ("\n  UNISON on osc A (8 notes):\n");
        for (float u : { 0.f, 0.0667f, 0.2f, 0.4f, 0.6f, 1.f }) { const double v = measure ([&] (Au& a) { a.setNorm ("Synth OSC A Unison", u); }, 8); printf ("   unison norm %.3f  %6.0f us  %+6.0f\n", u, v, v - base8); fflush (stdout); }
        printf ("\n  FX KIND powered, idle (no notes), delta vs init idle:\n");
        { Au a; a.open(); std::vector<std::string> kinds; for (auto& kv : a.byName) { const auto& n = kv.first; if (n.size() > 6 && n.compare (n.size() - 6, 6, " Power") == 0 && n.rfind ("Synth", 0) != 0 && n.find_first_of ("0123456789") == std::string::npos) kinds.push_back (n); } a.close();
          for (auto& k : kinds) { const double v = measure ([&] (Au& a) { a.setNorm (k, 1.0f); }, 0); const double v8 = measure ([&] (Au& a) { a.setNorm (k, 1.0f); }, 8); printf ("   %-22s idle %6.0f us  %+6.0f    8 notes %+6.0f\n", k.c_str(), v, v - base0, v8 - base8); fflush (stdout); } }
        return 0;
    }
    if (mode == "scan")   // every preset's makeup, one line each: engines, unison, filters, release, voices, fx
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : "");
        printf ("name|engA|engB|engC|engD|uniA|uniB|uniC|uniD|voices|f1type|f1mix|f1drv|f1res|f2type|f2mix|f2drv|f2res|rel|relcurve|sus|fx\n");
        for (auto& p : ps)
        {
            Au a; if (! a.open() || ! a.loadChunk (p.chunk)) continue; a.pump (0.5); a.render (10, nullptr); a.pump (0.2);
            auto g = [&] (const std::string& n) { return a.get (n); };
            printf ("%s|%.0f|%.0f|%.0f|%.0f|%.2f|%.2f|%.2f|%.2f|%.3f|%.0f|%.2f|%.2f|%.2f|%.0f|%.2f|%.2f|%.2f|%.3f|%.2f|%.2f|%s\n", p.name.c_str(),
                g ("Synth OSC A Engine"), g ("Synth OSC B Engine"), g ("Synth OSC C Engine"), g ("Synth OSC D Engine"),
                g ("Synth OSC A Unison"), g ("Synth OSC B Unison"), g ("Synth OSC C Unison"), g ("Synth OSC D Unison"), g ("Synth Voices"),
                g ("Synth Filter 1 Type"), g ("Synth Filter 1 Mix"), g ("Synth Filter 1 Drive"), g ("Synth Filter 1 Resonance"),
                g ("Synth Filter 2 Type"), g ("Synth Filter 2 Mix"), g ("Synth Filter 2 Drive"), g ("Synth Filter 2 Resonance"),
                g ("Synth Amp Release"), g ("Synth Amp Release Curve"), g ("Synth Amp Sustain"), field (p.manifest, "fx").c_str());
            fflush (stdout); a.close();
        }
        return 0;
    }
    if (mode == "render")   // render <preset> <out.f32>: a fixed scenario (4-note chord 1 s, release 3 s, 8 notes 1 s, release 2 s), left channel as float32
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty() || argc < 4) { printf ("usage: render <preset> <out.f32>\n"); return 1; }
        Au a; if (! a.open() || ! a.loadChunk (ps[0].chunk)) return 1; a.pump (0.8); a.render (20, nullptr); a.pump (0.4);
        FILE* f = fopen (argv[3], "wb"); if (! f) return 1;
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        auto blocks = [&] (int n) { for (int b = 0; b < n; ++b) { abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = a.stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (a.au, &fl, &ts, 0, BLK, abl); a.stamp += BLK; fwrite (bl.data(), 4, (size_t) BLK, f); } };
        const int sec = (int) (SR / BLK);
        const bool toggle = argc > 4 && std::string (argv[4]) == "toggle";   // every oscillator OFF at 1 s, back ON at 2.5 s (the filter skip's wake path)
        float en[8]; for (char o = 'A'; o <= 'H'; ++o) en[o - 'A'] = a.get (std::string ("Osc ") + o + " Enable");
        for (int n : CHORD4) a.note (n, 100); blocks (sec);
        if (toggle) { for (char o = 'A'; o <= 'H'; ++o) a.setRaw (std::string ("Osc ") + o + " Enable", 0.0f); blocks ((int) (1.5 * sec)); for (char o = 'A'; o <= 'H'; ++o) a.setRaw (std::string ("Osc ") + o + " Enable", en[o - 'A']); blocks ((int) (1.5 * sec)); }
        else blocks (sec);
        a.allOff(); blocks (2 * sec);
        for (int n : CHORD8) a.note (n, 100); blocks (sec); a.allOff(); blocks (2 * sec);
        fclose (f); free (abl); a.close(); printf ("wrote %s (%d s)\n", argv[3], 7); return 0;
    }
    if (mode == "pitch")   // tp39f — pitch <preset>: every ENABLED oscillator alone, ONE note at a time (48 / 55 / 60) — does the fundamental follow the key? (Max: "some one shots stay locked to one note")
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty()) { printf ("no preset matches\n"); return 1; }
        static const int NOTES[3] = { 48, 55, 60 }; static const double EXP[2] = { 7.0 / 12.0, 5.0 / 12.0 };
        static const char* EN[] = { "WT", "Sample", "Granular", "Resynth", "FM", "Additive", "Modal" };
        auto f0of = [] (const std::vector<float>& x, double& quality) -> double
        {
            const int N = (int) x.size(); quality = 0; if (N < 8192) return 0;
            double mean = 0; for (float v : x) mean += v; mean /= N;
            std::vector<double> y ((size_t) N); double e0 = 0; for (int i = 0; i < N; ++i) { y[(size_t) i] = x[(size_t) i] - mean; e0 += y[(size_t) i] * y[(size_t) i]; }
            if (e0 / N < 1e-10) return 0;   // silent
            const int minLag = (int) (SR / 1200.0), maxLag = (int) (SR / 30.0), W = N - maxLag;   // 30 Hz .. 1.2 kHz — two octaves either side of the notes played
            std::vector<double> r ((size_t) maxLag + 1, 0.0);
            for (int l = minLag; l <= maxLag; ++l) { double sxy = 0, sxx = 0, syy = 0; for (int i = 0; i < W; ++i) { const double a = y[(size_t) i], b = y[(size_t) (i + l)]; sxy += a * b; sxx += a * a; syy += b * b; } r[(size_t) l] = sxy / (std::sqrt (sxx * syy) + 1e-12); }
            // LOCAL maxima only (a boundary lag is never a period), then the LOWEST lag whose peak is within 15 % of the strongest — the fundamental, not a sub-multiple
            double bestR = -1; int bestL = 0; for (int l = minLag + 1; l < maxLag; ++l) if (r[(size_t) l] > r[(size_t) l - 1] && r[(size_t) l] >= r[(size_t) l + 1] && r[(size_t) l] > bestR) { bestR = r[(size_t) l]; bestL = l; }
            if (bestL > 0) for (int l = minLag + 1; l < bestL; ++l) if (r[(size_t) l] > r[(size_t) l - 1] && r[(size_t) l] >= r[(size_t) l + 1] && r[(size_t) l] >= 0.85 * bestR) { bestR = r[(size_t) l]; bestL = l; break; }
            quality = bestR; return bestL > 0 ? SR / bestL : 0;
        };
        int locked = 0, tracks = 0, unsure = 0;
        for (auto& p : ps)
        {
            Au a0; if (! a0.open() || ! a0.loadChunk (p.chunk)) continue; a0.pump (0.8); a0.render (20, nullptr); a0.pump (0.4);
            float en[8], eng[8]; for (char o = 'A'; o <= 'H'; ++o) { en[o - 'A'] = a0.get (std::string ("Osc ") + o + " Enable"); eng[o - 'A'] = a0.get (std::string ("Synth OSC ") + o + " Engine"); }
            a0.close();
            printf ("== %s ==\n", p.name.c_str());
            for (char o = 'A'; o <= 'H'; ++o)
            {
                if (en[o - 'A'] < 0.5f) continue;
                double f0[3] = { 0, 0, 0 }, q[3] = { 0, 0, 0 };
                for (int ni = 0; ni < 3; ++ni)
                {
                    Au a; if (! a.open() || ! a.loadChunk (p.chunk)) continue; a.pump (0.8); a.render (20, nullptr); a.pump (0.6);
                    for (char qo = 'A'; qo <= 'H'; ++qo) if (qo != o) a.setRaw (std::string ("Osc ") + qo + " Enable", 0.0f);
                    for (auto& kv : a.byName) { const std::string& nm = kv.first;   // the ENGINE alone: no effect, no flow card, no filter, no unison, no latch
                        if (nm.size() > 6 && nm.compare (nm.size() - 6, 6, " Power") == 0) a.setRaw (nm, 0.0f);
                        if (nm.rfind ("Flow", 0) == 0 && nm.find (" Mode") != std::string::npos) a.setRaw (nm, 0.0f);
                        if (nm == "Synth Filter 1 Mix" || nm == "Synth Filter 2 Mix" || nm == "Filter 2 Mix") a.setRaw (nm, 0.0f);
                        if (nm.rfind ("Synth OSC ", 0) == 0 && nm.size() == 18 && nm.compare (12, 6, "Unison") == 0) a.setRaw (nm, 0.0f);
                        if (nm.find ("Latch") != std::string::npos) a.setRaw (nm, 0.0f); }
                    a.render (20, nullptr); a.note (NOTES[ni], 100); a.render (30, nullptr);
                    std::vector<float> mono; a.capture (56, mono); a.allOff(); a.render (6, nullptr); a.close();
                    f0[ni] = f0of (mono, q[ni]);
                }
                const int ei = (int) std::lround (eng[o - 'A']); const char* en_ = (ei >= 0 && ei < 7) ? EN[ei] : "?";
                std::string extra; if (ei == 6) { Au ax; if (ax.open() && ax.loadChunk (p.chunk)) { ax.pump (0.5); const std::string b = std::string ("Synth OSC ") + o + " Modal "; static const char* FAM[] = { "Grand", "Pluck", "Bow", "Flute", "Reed", "Brass", "Bars", "Bells", "Skin" }; const int fam = (int) std::lround (ax.get (b + "Family")); extra = std::string ("  [") + (fam >= 0 && fam < 9 ? FAM[fam] : "?") + " form " + std::to_string ((int) std::lround (ax.get (b + "Form"))) + " src " + std::to_string ((int) std::lround (ax.get (b + "Source"))) + " stretch " + std::to_string (ax.get (b + "Stretch")).substr (0, 4) + "]"; ax.close(); } }
                const bool silent = f0[0] <= 0 || f0[1] <= 0 || f0[2] <= 0, noisy = q[0] < 0.3 || q[1] < 0.3 || q[2] < 0.3;
                double d1 = silent ? 0 : std::log2 (f0[1] / f0[0]), d2 = silent ? 0 : std::log2 (f0[2] / f0[1]);
                const char* verdict = silent ? "silent" : noisy ? "noisy/unpitched" : (std::fabs (d1) < 0.12 && std::fabs (d2) < 0.12) ? "LOCKED <-- one pitch for every key"
                                    : (std::fabs (d1 - EXP[0]) < 0.12 && std::fabs (d2 - EXP[1]) < 0.12) ? "tracks" : "odd (octave/other)";
                if (! silent && ! noisy) { if (verdict[0] == 'L') ++locked; else if (verdict[0] == 't') ++tracks; else ++unsure; }
                printf ("  osc %c  %-9s f0 @48 %7.1f  @55 %7.1f  @60 %7.1f   (q %.2f %.2f %.2f)  d %+.2f %+.2f oct   %s%s\n", o, en_, f0[0], f0[1], f0[2], q[0], q[1], q[2], d1, d2, verdict, extra.c_str());
            }
        }
        printf ("summary: tracks %d  LOCKED %d  odd %d\n", tracks, locked, unsure); return 0;
    }
    if (mode == "modalscan")   // tp39f — modalscan <preset> <osc>: that oscillator alone, every family x STRETCH 0..0.5 overriding the patch, one note — the peak (a silent cell = a dead setting)
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty() || argc < 4) { printf ("usage: modalscan <preset> <osc>\n"); return 1; }
        const char O = argv[3][0]; const std::string P = std::string ("Synth OSC ") + O + " ";
        static const char* FAM[] = { "Grand", "Pluck", "Bow", "Flute", "Reed", "Brass", "Bars", "Bells", "Skin" };
        static const float ST[] = { 0.0f, 0.02f, 0.05f, 0.1f, 0.15f, 0.3f, 0.5f };
        printf ("%-7s", "family"); for (float st : ST) printf ("  st%.2f", st); printf ("\n");
        for (int fam = 0; fam < 9; ++fam)
        {
            printf ("%-7s", FAM[fam]);
            for (float st : ST)
            {
                Au a; if (! a.open() || ! a.loadChunk (ps[0].chunk)) { printf ("   open?"); continue; } a.pump (0.8); a.render (20, nullptr); a.pump (0.6);
                for (char q = 'A'; q <= 'H'; ++q) a.setRaw (std::string ("Osc ") + q + " Enable", q == O ? 1.0f : 0.0f);
                a.setRaw (P + "Engine", 6.0f); a.setRaw (P + "Modal Family", (float) fam); a.setRaw (P + "Modal Stretch", st);
                a.render (20, nullptr); a.note (60, 100); std::vector<double> t; float pk = 0; a.render (60, &t, &pk); a.allOff(); a.close();
                printf ("  %6.1f", 20.0 * std::log10 (std::max (1e-12f, pk)));
            }
            printf ("\n");
        }
        return 0;
    }
    if (mode == "override")   // tp39f — override <preset> <osc> "Display Name=value" ... : that oscillator alone with those parameters forced, one note 60 — the peak
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty() || argc < 4) { printf ("usage: override <preset> <osc> \"Name=value\"...\n"); return 1; }
        const char O = argv[3][0];
        Au a; if (! a.open() || ! a.loadChunk (ps[0].chunk)) return 1; a.pump (0.8); a.render (20, nullptr); a.pump (0.6);
        for (char q = 'A'; q <= 'H'; ++q) a.setRaw (std::string ("Osc ") + q + " Enable", q == O ? 1.0f : 0.0f);
        for (int i = 4; i < argc; ++i) { std::string kv = argv[i]; auto eq = kv.find ('='); if (eq == std::string::npos) continue; const std::string nm = kv.substr (0, eq); const float v = (float) atof (kv.c_str() + eq + 1); printf ("  set %-40s = %6.3f  %s\n", nm.c_str(), v, a.setRaw (nm, v) ? "" : "<-- NO SUCH PARAM"); }
        a.render (20, nullptr); a.note (60, 100); std::vector<double> t; float pk = 0; a.render (60, &t, &pk); a.allOff(); a.close();
        printf ("  osc %c alone, note 60: peak %.1f dBFS\n", O, 20.0 * std::log10 (std::max (1e-12f, pk))); return 0;
    }
    if (mode == "bisect")   // tp39f — bisect <preset> <osc>: which of the oscillator's own non-default parameters silences it (each reset to default alone; then all)
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty() || argc < 4) { printf ("usage: bisect <preset> <osc>\n"); return 1; }
        const char O = argv[3][0]; std::string k1 = std::string ("OSC ") + O + " ", k2 = std::string ("Osc ") + O + " ";
        if (argc > 4) { k1 = argv[4]; k2 = argv[4]; }   // an optional name filter instead of the oscillator's own parameters (e.g. "Chop")
        auto peakWith = [&] (const std::vector<std::pair<std::string, float>>& sets) -> double
        {
            Au a; if (! a.open() || ! a.loadChunk (ps[0].chunk)) return -999; a.pump (0.8); a.render (20, nullptr); a.pump (0.6);
            for (char q = 'A'; q <= 'H'; ++q) a.setRaw (std::string ("Osc ") + q + " Enable", q == O ? 1.0f : 0.0f);
            for (auto& kv : sets) a.setRaw (kv.first, kv.second);
            a.render (20, nullptr); a.note (60, 100); std::vector<double> t; float pk = 0; a.render (60, &t, &pk); a.allOff(); a.close();
            return 20.0 * std::log10 (std::max (1e-12f, pk));
        };
        // the oscillator's non-default parameters
        std::vector<std::pair<std::string, float>> nd;
        { Au a; if (! a.open() || ! a.loadChunk (ps[0].chunk)) return 1; a.pump (0.8);
          for (auto& kv : a.byName) { const std::string& nm = kv.first; if (nm.find (k1) == std::string::npos && nm.find (k2) == std::string::npos) continue; if (nm.find ("Enable") != std::string::npos) continue;
              const float v = a.get (nm), d = a.info[kv.second].defaultValue; if (std::fabs (v - d) > 1e-4f) nd.push_back ({ nm, d }); } a.close(); }
        printf ("osc %c: %zu non-default parameters; as rolled %.1f dBFS; all reset %.1f dBFS\n", O, nd.size(), peakWith ({}), peakWith (nd));
        for (auto& kv : nd) { const double pk = peakWith ({ kv }); if (pk > -60) printf ("  RESET %-40s -> %6.1f dBFS  <-- this one silences it\n", kv.first.c_str(), pk); }
        printf ("bisect done\n"); return 0;
    }
    if (mode == "solo")   // solo <preset>: every ENABLED oscillator alone (the others off) — its engine, and whether it makes sound (8 notes, peak dBFS)
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty()) { printf ("no preset matches\n"); return 1; }
        for (auto& p : ps)
        {
            Au a0; if (! a0.open() || ! a0.loadChunk (p.chunk)) continue; a0.pump (0.8); a0.render (20, nullptr); a0.pump (0.4);
            float en[8], eng[8]; for (char o = 'A'; o <= 'H'; ++o) { en[o - 'A'] = a0.get (std::string ("Osc ") + o + " Enable"); eng[o - 'A'] = a0.get (std::string ("Synth OSC ") + o + " Engine"); } a0.close();
            printf ("== %s ==\n", p.name.c_str());
            for (char o = 'A'; o <= 'H'; ++o)
            {
                if (en[o - 'A'] < 0.5f) continue;
                Au a; if (! a.open() || ! a.loadChunk (p.chunk)) continue; a.pump (0.8); a.render (20, nullptr); a.pump (0.6);
                for (char q = 'A'; q <= 'H'; ++q) if (q != o) a.setRaw (std::string ("Osc ") + q + " Enable", 0.0f);
                a.render (20, nullptr); for (int n : CHORD8) a.note (n, 100); a.render (12, nullptr); std::vector<double> t; float pk = 0; a.render (40, &t, &pk); a.allOff();
                static const char* EN[] = { "WT", "Sample", "Granular", "Resynth", "FM", "Additive", "Modal" };
                const int ei = (int) std::lround (eng[o - 'A']);   // the AU hands back the choice index itself (0..6)
                printf ("  osc %c  %-9s  peak %7.1f dBFS  %s\n", o, EN[juce_jlimit0 (ei)], pk > 1e-9f ? 20.0 * std::log10 (pk) : -240.0, (pk < 1e-4f && ei >= 1 && ei <= 3) ? "<-- EMPTY sample engine" : "");
                a.close();
            }
        }
        return 0;
    }
    if (mode == "params")   // params <preset> <substr>: every parameter containing <substr> whose value differs from its default
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty()) { printf ("no preset matches\n"); return 1; }
        Au a; if (! a.open() || ! a.loadChunk (ps[0].chunk)) return 1; a.pump (0.8); a.render (10, nullptr);
        printf ("== %s: non-default parameters containing '%s' ==\n", ps[0].name.c_str(), argc > 3 ? argv[3] : "");
        for (auto& kv : a.byName) { if (argc > 3 && ! strcasestr (kv.first.c_str(), argv[3])) continue; const auto& pi = a.info.at (kv.second); const float v = a.get (kv.first);
            if (std::fabs (v - pi.defaultValue) > 1e-4f) printf ("  %-34s %8.3f  (def %g)\n", kv.first.c_str(), v, pi.defaultValue); }
        a.close(); return 0;
    }
    if (mode == "oscgate")   // tp36b — Max: "I had four loaded but two active — something is bleeding through." Does OFF really skip the work, and the sound?
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty()) { printf ("no preset matches\n"); return 1; }
        const Preset& p = ps[0]; printf ("\n== oscgate: %s ==\n", p.name.c_str());
        auto fresh = [&] () { Au* a = new Au(); if (! a->open() || ! a->loadChunk (p.chunk)) { delete a; return (Au*) nullptr; } a->pump (0.8); a->render (20, nullptr); a->pump (0.4); return a; };
        auto chord = [&] (Au& a, const char* label) { for (int n : CHORD8) a.note (n, 100); a.render (12, nullptr); std::vector<double> t; float pk = 0; a.render (50, &t, &pk);
            printf ("  %-52s %6.0f us %5.1f%%   peak %7.2f dBFS\n", label, median (t), cpu (median (t)), pk > 1e-9f ? 20.0 * std::log10 (pk) : -240.0); a.allOff(); a.render (240, nullptr); };
        { Au* a = fresh(); if (! a) return 1;
          printf ("  enables as saved:"); for (char o = 'A'; o <= 'H'; ++o) printf ("  %c=%.0f", o, a->get (std::string ("Osc ") + o + " Enable")); printf ("\n");
          chord (*a, "as saved"); a->close(); delete a; }
        { Au* a = fresh(); if (! a) return 1; for (char o = 'A'; o <= 'H'; ++o) a->setRaw (std::string ("Osc ") + o + " Enable", 0.0f); a->render (20, nullptr);
          chord (*a, "EVERY oscillator OFF (should be ~idle, silent)"); a->close(); delete a; }
        for (char o = 'A'; o <= 'H'; ++o)
        { Au* a = fresh(); if (! a) return 1; const float was = a->get (std::string ("Osc ") + o + " Enable"); if (was < 0.5f) { a->close(); delete a; continue; }
          a->setRaw (std::string ("Osc ") + o + " Enable", 0.0f); a->render (20, nullptr); chord (*a, (std::string ("only osc ") + o + " turned OFF").c_str()); a->close(); delete a; }
        for (char o = 'A'; o <= 'H'; ++o)
        { Au* a = fresh(); if (! a) return 1; const float was = a->get (std::string ("Osc ") + o + " Enable"); if (was > 0.5f) { a->close(); delete a; continue; }
          a->setRaw (std::string ("Osc ") + o + " Enable", 1.0f); a->render (20, nullptr); chord (*a, (std::string ("only osc ") + o + " turned ON").c_str()); a->close(); delete a; }
        return 0;
    }
    if (mode == "hold")   // hold an 8-note chord and render at wall pace for <secs> — for `sample <pid>` from outside
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty()) { printf ("no preset matches\n"); return 1; }
        const double secs = argc > 3 ? atof (argv[3]) : 8.0; const int notes = argc > 4 ? atoi (argv[4]) : 8;
        Au a; if (! a.open() || ! a.loadChunk (ps[0].chunk)) return 1; a.pump (0.8); a.render (20, nullptr); a.pump (0.4);
        for (int i = 0; i < notes; ++i) a.note (CHORD8[i], 100);
        printf ("holding %d notes of %s for %.0f s (pid %d)\n", notes, ps[0].name.c_str(), secs, (int) getpid()); fflush (stdout);
        if (argc > 5 && std::string (argv[5]) == "shortrel") { a.setNorm ("Synth Amp Release", 0.02f); a.render (10, nullptr); printf ("  (amp release -> short)\n"); }
        if (argc > 5 && std::string (argv[5]) == "alloff") { for (char o = 'A'; o <= 'H'; ++o) a.setRaw (std::string ("Osc ") + o + " Enable", 0.0f); a.render (20, nullptr); printf ("  (every oscillator OFF)\n"); }
        const double t0 = nowUs(); std::vector<double> t, sec;
        int lastS = -1;
        while (nowUs() - t0 < secs * 1e6) { a.render (1, &sec); t.push_back (sec.back());   /* the cost second by second: does it CLIMB while the same notes are held? */
            const int sNow = (int) ((double) t.size() * BLK / SR); if (sNow != lastS) { lastS = sNow; if (sec.size() > 4) printf ("   +%2d s  %6.0f us  %5.1f%%\n", sNow, median (sec), cpu (median (sec))); sec.clear(); } }
        printf ("  median %.0f us/block = %.1f%%\n", median (t), cpu (median (t))); a.allOff(); a.close(); return 0;
    }
    if (mode == "tail" || mode == "ablate")
    {
        auto ps = loadAll (userBank().c_str(), argc > 2 ? argv[2] : ""); if (ps.empty()) { printf ("no preset matches\n"); return 1; }
        const Preset& p = ps[0]; printf ("\n== %s: %s  (%ld KB)  fx: %s ==\n", mode.c_str(), p.name.c_str(), p.bytes / 1024, field (p.manifest, "fx").c_str());
        if (mode == "tail")
        {
            Au a; if (! a.open() || ! a.loadChunk (p.chunk)) return 1; a.pump (0.8); a.render (20, nullptr); a.pump (0.4);
            for (const char* k : { "Synth Amp Release", "Synth Amp Release Curve", "Synth Amp Sustain", "Synth Voices", "Synth Filter 1 Type", "Synth Filter 2 Type", "Synth Filter 1 Mix", "Synth Filter 2 Mix" }) printf ("  %-26s = %.3f\n", k, a.get (k));
            for (char o = 'A'; o <= 'H'; ++o) { std::string b = std::string ("Synth OSC ") + o; printf ("  osc %c: on=%.0f engine=%.0f unison=%.0f level=%.2f\n", o, a.get (b + " On"), a.get (b + " Engine"), a.get (b + " Unison"), a.get (b + " Level")); }
            for (int n : CHORD4) a.note (n, 100); a.render (30, nullptr);
            a.allOff(); std::vector<double> tail; a.render ((int) (6.0 * SR / BLK), &tail);
            printf ("  after note-off (4-note chord):\n");
            for (double s = 0.0; s <= 6.0; s += 0.25) { int i = (int) (s * SR / BLK); if (i >= (int) tail.size()) break; std::vector<double> w (tail.begin() + i, tail.begin() + std::min ((int) tail.size(), i + 6)); printf ("   +%.2f s  %6.0f us  %5.1f%%\n", s, median (w), cpu (median (w))); }
            a.close(); return 0;
        }
        // ablate: baseline = the 8-note chord, then one change at a time from a fresh load
        auto run = [&] (const char* label, std::function<void (Au&)> change) {
            Au a; if (! a.open() || ! a.loadChunk (p.chunk)) return; a.pump (0.8); a.render (20, nullptr); a.pump (0.4);
            change (a); a.render (20, nullptr);
            for (int n : CHORD8) a.note (n, 100); a.render (12, nullptr); std::vector<double> t; a.render (50, &t);
            a.allOff(); std::vector<double> tail; a.render ((int) (1.0 * SR / BLK), &tail); std::vector<double> w (tail.begin() + (int) (0.5 * SR / BLK), tail.end());
            a.render ((int) (4.0 * SR / BLK), nullptr);
            std::vector<double> rt; for (int k = 0; k < 12; ++k) { a.allOff(); for (int n : CHORD4) a.note (n + (k % 3) * 2, 100); std::vector<double> tt; a.render ((int) (0.25 * SR / BLK), &tt); if (k >= 8) rt.insert (rt.end(), tt.begin(), tt.end()); } a.allOff();
            printf ("  %-44s  8 notes %6.0f us %5.1f%%   tail@0.5-1s %6.0f us %5.1f%%   retrig %6.0f us %5.1f%%\n", label, median (t), cpu (median (t)), median (w), cpu (median (w)), median (rt), cpu (median (rt)));
            a.close(); };
        run ("baseline", [] (Au&) {});
        auto unison1 = [] (Au& a) { for (char o = 'A'; o <= 'H'; ++o) a.setNorm (std::string ("Synth OSC ") + o + " Unison", 0.0f); };
        run ("unison -> 1 on every osc", unison1);
        run ("every FX power off", [] (Au& a) { for (auto& kv : a.byName) { const auto& n = kv.first; if (n.size() > 6 && n.compare (n.size() - 6, 6, " Power") == 0 && n.rfind ("Synth", 0) != 0) a.setNorm (n, 0.0f); } });
        run ("every engine -> Wavetable", [] (Au& a) { for (char o = 'A'; o <= 'H'; ++o) a.setRaw (std::string ("Synth OSC ") + o + " Engine", 0.0f); });
        run ("both filters -> mix 0", [] (Au& a) { a.setNorm ("Synth Filter 1 Mix", 0.0f); a.setNorm ("Synth Filter 2 Mix", 0.0f); });
        run ("amp release -> short (norm 0.02)", [] (Au& a) { a.setNorm ("Synth Amp Release", 0.02f); });
        run ("voices -> 8", [] (Au& a) { a.setRaw ("Synth Voices", 8.0f); });
        run ("voices -> 16", [] (Au& a) { a.setRaw ("Synth Voices", 16.0f); });
        run ("release short + unison 1", [&] (Au& a) { a.setNorm ("Synth Amp Release", 0.02f); unison1 (a); });
        return 0;
    }
    auto ps = loadAll (userBank().c_str(), argc > 1 ? argv[1] : "");
    printf ("\n== THE PRESET CENSUS: %zu presets from %s ==\n   %d frames @ %.0f Hz, budget %.0f us; CPU%% of one core\n\n", ps.size(), userBank().c_str(), BLK, SR, budgetUs());
    std::vector<Row> rows;
    for (size_t i = 0; i < ps.size(); ++i)
    {
        Row r {}; fprintf (stderr, "\r  [%zu/%zu] %-40s", i + 1, ps.size(), ps[i].name.c_str());
        if (census (ps[i], r, false)) rows.push_back (r);
    }
    fprintf (stderr, "\n");
    std::sort (rows.begin(), rows.end(), [] (const Row& x, const Row& y) { return x.n8 > y.n8; });
    printf ("  %-26s %6s %6s %6s %6s | %6s %6s %6s %6s %6s %7s | %6s  %s\n", "preset", "idle", "1 note", "4 note", "8 note", "+.25s", "+.5s", "+1s", "+2s", "+4s", "toIdle", "retrig", "fx");
    for (auto& r : rows)
        printf ("  %-26s %5.1f%% %5.1f%% %5.1f%% %5.1f%% | %5.1f%% %5.1f%% %5.1f%% %5.1f%% %5.1f%% %6.2fs | %5.1f%%  %s\n", r.name.substr (0, 26).c_str(), cpu (r.idle), cpu (r.n1), cpu (r.n4), cpu (r.n8),
                cpu (r.t025), cpu (r.t05), cpu (r.t1), cpu (r.t2), cpu (r.t4), r.toIdle, cpu (r.retrig), r.fx.substr (0, 40).c_str());
    // the summary numbers Max asked for
    if (! rows.empty())
    {
        std::vector<double> n8, idle, tail1, retrig, toIdle; for (auto& r : rows) { n8.push_back (cpu (r.n8)); idle.push_back (cpu (r.idle)); tail1.push_back (cpu (r.t1)); retrig.push_back (cpu (r.retrig)); if (r.toIdle >= 0) toIdle.push_back (r.toIdle); }
        printf ("\n  %zu presets: 8-note chord median %.1f%%  p90 %.1f%%  max %.1f%%  ·  idle median %.1f%%  max %.1f%%  ·  tail@1s median %.1f%%  max %.1f%%  ·  retrig median %.1f%%  max %.1f%%  ·  time-to-idle median %.2fs  p90 %.2fs\n",
                rows.size(), median (n8), pct (n8, 0.9), *std::max_element (n8.begin(), n8.end()), median (idle), *std::max_element (idle.begin(), idle.end()),
                median (tail1), *std::max_element (tail1.begin(), tail1.end()), median (retrig), *std::max_element (retrig.begin(), retrig.end()), median (toIdle), pct (toIdle, 0.9));
    }
    return 0;
}
