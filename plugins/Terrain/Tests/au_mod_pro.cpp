// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_mod_pro.cpp — tp96 · THE MOD PAGE'S PRO COLUMNS, IN THE INSTALLED AU.
//
//  The matrix page added four fields to a route (pl polarity · ai aux invert · ac aux curve · o output)
//  and found that the per-sample CUTOFF path never applied the aux at all. This installs routes the way a
//  loaded patch does (the synModJson attribute) and measures what each column does to a held chord through
//  Filter 1 (Ladder LP 24) at 250 Hz, with LFO 1 on the cutoff:
//    A  baseline swing · B  Output 25 % shrinks it · C  Pol Uni lifts the MEAN brightness (the cutoff only
//    goes up) · D  aux = Velocity: a hard note swings far more than a soft one (was: identical — the bug) ·
//    E  + INV: the soft note swings more · F  no route: no swing.
//  Brightness per 10.7 ms frame = rms(first difference) / rms; swing = p90/p10 of it in dB.
//
//  clang++ -O2 -std=c++17 Tests/au_mod_pro.cpp -o /tmp/aumodpro -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/aumodpro
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static const double SR = 48000.0; static const int BLK = 512; static const double BPM = 120.0;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& d = "")
{ printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what); if (! d.empty()) printf ("        %s\n", d.c_str()); if (ok) ++npass; else ++nfail; fflush (stdout); }

static double gPpq0 = 0.0, gStamp0 = 0.0; static bool gPlaying = false;
static OSStatus beatAndTempo (void*, Float64* b, Float64* t) { if (t) *t = BPM; if (b) *b = gPpq0; return noErr; }
static OSStatus transportState (void*, Boolean* p, Boolean* c, Float64* s, Boolean* l, Float64* a, Float64* e)
{ if (p) *p = gPlaying; if (c) *c = false; if (s) *s = gStamp0; if (l) *l = false; if (a) *a = 0; if (e) *e = 0; return noErr; }

static std::string xmlEsc (const std::string& s)
{ std::string o; for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c; } return o; }

struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName;
    std::map<std::string, std::pair<float,float>> byRange; double stamp = 0;
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        HostCallbackInfo cb {}; cb.beatAndTempoProc = beatAndTempo; cb.transportStateProc = transportState;
        AudioUnitSetProperty (au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &cb, sizeof cb);
        if (AudioUnitInitialize (au) != noErr) return false;
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char b[256] = {};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString, b, sizeof b, kCFStringEncodingUTF8);
            else snprintf (b, sizeof b, "%s", pi.name);
            byName[b] = id; byRange[b] = { pi.minValue, pi.maxValue };
        }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
    bool has (const std::string& n) const { return byName.count (n) > 0; }
    int  choiceCount (const std::string& n) const { auto it = byRange.find (n); return it == byRange.end() ? 0 : (int) (it->second.second - it->second.first) + 1; }
    bool set (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
      return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    /** a choice's scale is NOT 0..1 everywhere in this plugin — ask, never guess (tp83). */
    bool setIndex (const std::string& n, int idx, int count)
    { auto it = byRange.find (n); if (it == byRange.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
      const float lo = it->second.first, hi = it->second.second;
      return set (n, count > 1 ? lo + (hi - lo) * ((float) idx / (float) (count - 1)) : lo); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v > 0 ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void render (int blocks, double ppqStart, bool playing, std::vector<float>* keep = nullptr, std::vector<float>* keepR = nullptr)
    {
        std::vector<float> L (BLK), R (BLK); gPlaying = playing; double ppq = ppqStart;
        for (int b = 0; b < blocks; ++b)
        {
            gPpq0 = ppq; gStamp0 = stamp;
            AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = L.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = R.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            if (playing) ppq += BLK * BPM / 60.0 / SR;
            if (keep)  keep ->insert (keep ->end(), L.begin(), L.end());
            if (keepR) keepR->insert (keepR->end(), R.begin(), R.end());   // Widen's whole claim is the IMAGE
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.001, false);
        }
    }

    // ── THE STATE DOOR — exactly what a host does when it saves and loads a patch ──────────────
    //  JUCE's copyXmlToBinary: 'VC2!' · uint32 little-endian length · the XML · a trailing NUL.
    bool getStateXml (std::string& out)
    {
        CFPropertyListRef pl = nullptr; UInt32 s = sizeof pl;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &s) != noErr || ! pl) return false;
        CFDataRef blob = (CFDataRef) CFDictionaryGetValue ((CFDictionaryRef) pl, CFSTR ("jucePluginState"));
        if (! blob) { CFRelease (pl); return false; }
        const unsigned char* p = CFDataGetBytePtr (blob); const long n = CFDataGetLength (blob);
        if (n < 9 || memcmp (p, "VC2!", 4) != 0) { CFRelease (pl); return false; }
        out.assign ((const char*) p + 8, (size_t) (n - 9)); CFRelease (pl); return true;
    }
    bool setStateXml (const std::string& xml)
    {
        CFPropertyListRef pl = nullptr; UInt32 s = sizeof pl;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &s) != noErr || ! pl) return false;
        CFMutableDictionaryRef md = CFDictionaryCreateMutableCopy (nullptr, 0, (CFDictionaryRef) pl);
        std::vector<unsigned char> out (8 + xml.size() + 1);
        memcpy (out.data(), "VC2!", 4); const unsigned int L = (unsigned int) xml.size();
        out[4] = L & 0xff; out[5] = (L >> 8) & 0xff; out[6] = (L >> 16) & 0xff; out[7] = (L >> 24) & 0xff;
        memcpy (out.data() + 8, xml.data(), xml.size()); out[8 + xml.size()] = 0;
        CFDataRef nd = CFDataCreate (nullptr, out.data(), (CFIndex) out.size());
        CFDictionarySetValue (md, CFSTR ("jucePluginState"), nd);
        CFPropertyListRef np = md;
        const OSStatus e = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &np, sizeof np);
        CFRelease (nd); CFRelease (md); CFRelease (pl); pump (0.35); return e == noErr;
    }
    /** set one attribute on the root <Parameters> element, the way a saved patch carries it. */
    static void putAttr (std::string& xml, const std::string& key, const std::string& val)
    {
        const std::string attr = " " + key + "=\"" + xmlEsc (val) + "\"";
        const std::string pat = key + "=\"";
        const size_t a = xml.find (pat);
        if (a != std::string::npos) { const size_t e = xml.find ('"', a + pat.size()); xml = xml.substr (0, a - 1) + attr + xml.substr (e + 1); return; }
        const size_t t = xml.find ("<Parameters"); if (t == std::string::npos) return;
        xml = xml.substr (0, t + 11) + attr + xml.substr (t + 11);
    }
    static std::string getAttr (const std::string& xml, const std::string& key)
    { const std::string pat = key + "=\""; const size_t a = xml.find (pat); if (a == std::string::npos) return "\x01";   // \x01 = absent
      const size_t e = xml.find ('"', a + pat.size()); return xml.substr (a + pat.size(), e - a - pat.size()); }
    bool putShaper (const std::string& json)
    { std::string x; if (! getStateXml (x)) return false; putAttr (x, "shaperJson0", json); return setStateXml (x); }
};


static double rmsOf (const std::vector<float>& x, size_t a, size_t b)
{ double e = 0; for (size_t i = a; i < b && i < x.size(); ++i) e += (double) x[i] * x[i]; return std::sqrt (e / (double) std::max<size_t> (1, b - a)); }
struct Swing { double swingDb, meanB; };
/* tremolo: the loudness of 10.7 ms frames, p90/p10 in dB, and their mean */
static Swing loudness (const std::vector<float>& x, size_t A, size_t B)
{ std::vector<double> v; const size_t F = 512;
  for (size_t i = A; i + F <= B && i + F < x.size(); i += F) v.push_back (rmsOf (x, i, i + F));
  if (v.size() < 8) return { 0, 0 }; std::vector<double> s = v; std::sort (s.begin(), s.end()); double m = 0; for (double q : v) m += q; m /= (double) v.size();
  return { 20.0 * std::log10 (s[(size_t) (s.size() * 0.9)] / (s[(size_t) (s.size() * 0.1)] + 1e-12)), m }; }
static Swing brightness (const std::vector<float>& x, size_t A, size_t B)
{
    std::vector<double> v; const size_t F = 512;
    for (size_t i = A; i + F <= B && i + F < x.size(); i += F)
    { double e = 0, d = 0; for (size_t k = i + 1; k < i + F; ++k) { e += (double) x[k] * x[k]; const double q = (double) x[k] - x[k-1]; d += q * q; }
      if (e > 1e-9) v.push_back (std::sqrt (d / e)); }
    if (v.size() < 8) return { 0, 0 };
    std::vector<double> s = v; std::sort (s.begin(), s.end());
    double m = 0; for (double q : v) m += q; m /= (double) v.size();
    return { 20.0 * std::log10 (s[(size_t) (s.size() * 0.9)] / (s[(size_t) (s.size() * 0.1)] + 1e-12)), m };
}
int main()
{
    Au a; if (! a.open()) { printf ("  !! no Terrain AU\n"); return 2; }
    printf ("\ntp96 — the MOD page's Pro columns, heard (LFO 1 -> Osc A Level, a held chord)\n\n");
    a.pump (1.2); a.render (30, 0.0, false);
    /* ⚠️ filters start OFF (Type = NONE) and no oscillator is sent to them: the first run measured the SAME brightness at a
       20 kHz and a 200 Hz cutoff. Ladder LP 24, Osc A sent in, and the cutoff parked AMONG the chord's partials (the init
       chord is nearly pure sines at 131 / 196 / 262 Hz — a cutoff above them has nothing to act on). */
    a.setIndex ("Synth Filter 1 Type", 0, a.choiceCount ("Synth Filter 1 Type")); a.set ("Synth OSC A Filter 1 Send", 1.0f);
    a.set ("Synth Filter 1 Cutoff", 250.0f); a.set ("LFO 1 Rate", 2.0f); a.pump (0.5);
    auto take = [&] (const std::string& routes, int vel) -> Swing
    {
        std::string x; a.getStateXml (x); Au::putAttr (x, "synModJson", routes); a.setStateXml (x);
        a.set ("Synth Filter 1 Cutoff", 250.0f); a.pump (0.6); a.render (40, 0.0, false);
        a.note (48, vel); a.note (55, vel); a.note (60, vel);
        std::vector<float> l; a.render ((int) (48000.0 * 3.2 / 512), 0.0, true, &l);
        a.note (48, 0); a.note (55, 0); a.note (60, 0); a.render (60, 3.2, true);
        return loudness (l, (size_t) (48000 * 0.6), l.size());
    };

    if (getenv ("MODPRO_DIAG"))
    {
        std::string x; a.getStateXml (x); Au::putAttr (x, "synModJson", "[{\"s\":0,\"d\":0,\"v\":0.6}]"); a.setStateXml (x);
        std::string y; a.getStateXml (y); printf ("  round-trip synModJson = %s\n", Au::getAttr (y, "synModJson").c_str());
        for (float cut : { 20000.0f, 800.0f, 250.0f, 120.0f })
        { a.set ("Synth Filter 1 Cutoff", cut); a.pump (0.4); a.render (20, 0.0, false);
          a.note (48, 110); a.note (55, 110); a.note (60, 110); std::vector<float> l; a.render ((int) (48000.0 * 1.5 / 512), 0.0, true, &l);
          a.note (48, 0); a.note (55, 0); a.note (60, 0); a.render (60, 1.5, true);
          Swing w = brightness (l, 24000, l.size()); printf ("  cutoff %6.0f Hz: mean brightness %.4f  swing %.2f dB  rms %.4f\n", cut, w.meanB, w.swingDb, rmsOf (l, 24000, l.size())); }
        a.close(); return 0;
    }
    const std::string base = "{\"s\":0,\"d\":64,\"v\":0.6";   /* LFO 1 -> Osc A Level (per-voice): a tremolo, heard as loudness */
    const Swing A  = take ("[" + base + "}]", 110);
    const Swing Bo = take ("[" + base + ",\"o\":0.25}]", 110);
    const Swing C  = take ("[" + base + ",\"pl\":1}]", 110);
    const Swing Dh = take ("[" + base + ",\"x\":200}]", 127), Ds = take ("[" + base + ",\"x\":200}]", 25);
    const Swing Eh = take ("[" + base + ",\"x\":200,\"ai\":1}]", 127), Es = take ("[" + base + ",\"x\":200,\"ai\":1}]", 25);
    const Swing F  = take ("[]", 110);
    const Swing Gs = take ("[" + base + ",\"x\":200}]", 72), Gb = take ("[" + base + ",\"x\":200,\"ac\":0.8}]", 72);   /* aux CRV: a mid velocity, straight vs bent */
    int pass = 0, fail = 0; auto chk = [&] (bool ok, const char* w, const std::string& d) { printf ("  %s  %s\n        %s\n", ok ? "PASS" : "FAIL", w, d.c_str()); ok ? ++pass : ++fail; };
    char b[260];
    snprintf (b, sizeof b, "swing %.2f dB with the route, %.2f dB without", A.swingDb, F.swingDb);                  chk (A.swingDb > F.swingDb + 3.0, "A  LFO 1 moves Osc A's level (tremolo)", b);
    snprintf (b, sizeof b, "swing %.2f dB at Output 25 %% against %.2f dB at 100 %%", Bo.swingDb, A.swingDb);        chk (Bo.swingDb < 0.6 * A.swingDb, "B  OUTPUT trims the route", b);
    snprintf (b, sizeof b, "mean level %.4f Uni against %.4f Bi", C.meanB, A.meanB);                               chk (C.meanB > A.meanB * 1.03, "C  POL Uni pushes one way (the level only goes up from the knob)", b);
    snprintf (b, sizeof b, "swing %.2f dB at velocity 127, %.2f dB at 25", Dh.swingDb, Ds.swingDb);                    chk (Dh.swingDb > Ds.swingDb + 3.0, "D  AUX = velocity scales the route", b);
    snprintf (b, sizeof b, "swing %.2f dB at velocity 127, %.2f dB at 25", Eh.swingDb, Es.swingDb);                    chk (Es.swingDb > Eh.swingDb + 3.0, "E  INV flips it: the soft note swings more", b);
    snprintf (b, sizeof b, "swing %.2f dB with the aux bent +0.8, %.2f dB straight, both at velocity 72", Gb.swingDb, Gs.swingDb); chk (Gb.swingDb < Gs.swingDb - 3.0, "F  aux CRV bends the aux (a bent-down curve holds a mid velocity back)", b);
    printf ("\n  %d passed, %d failed\n\n", pass, fail);
    a.close(); return fail ? 1 : 0;
}
