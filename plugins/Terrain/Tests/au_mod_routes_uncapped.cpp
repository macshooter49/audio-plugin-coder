// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_mod_routes_uncapped.cpp — tp101 · THE MOD MATRIX HAS NO ROUTE MAX (installed AU).
//
//  Max: the MOD page said "4 routes · 32 max" and he has built more than 32 before. The page capped
//  at 32, the drag adders at 128, and the C++ (wc::MAX_ASSIGNMENTS) at 128 — a route past the bound
//  was accepted by nobody and heard by nobody. The bound is now 256 (preallocated, real-time safe),
//  and the page shows only "N routes".
//
//  THE PROOF: the audible route is LFO 1 -> Osc A Level (a tremolo, heard as loudness) placed at
//  POSITION N of the patch's route list, behind N-1 filler routes (LFO 2 -> Osc A Level at depth 0:
//  real, valid, counted routes that contribute nothing). If the engine drops routes past a cap, the
//  tremolo at that position is gone.
//    0  a 256-route patch round-trips through the saved state
//    A  position 1 (control)  ·  B  position 41 (past the old page cap of 32)
//    C  position 129 (past the old C++ cap of 128)  ·  D  position 256 (the new bound, last slot)
//    E  255 fillers and no audible route: no more swing than an empty matrix (the chord beats on its own).
//
//  clang++ -O2 -std=c++17 Tests/au_mod_routes_uncapped.cpp -o /tmp/aumodroutes -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/aumodroutes
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
/* tremolo: the loudness of 10.7 ms frames, p90/p10 in dB */
static double swingDb (const std::vector<float>& x, size_t A, size_t B)
{ std::vector<double> v; const size_t F = 512;
  for (size_t i = A; i + F <= B && i + F < x.size(); i += F) v.push_back (rmsOf (x, i, i + F));
  if (v.size() < 8) return 0; std::sort (v.begin(), v.end());
  return 20.0 * std::log10 (v[(size_t) (v.size() * 0.9)] / (v[(size_t) (v.size() * 0.1)] + 1e-12)); }

/* the audible route at position `pos` behind pos-1 inert fillers; pos = 0: `fillers` fillers and nothing audible. */
static std::string routes (int pos, int fillers)
{
    std::string j = "[";
    const int total = pos > 0 ? pos : fillers;
    for (int i = 1; i <= total; ++i)
    {
        if (i > 1) j += ",";
        j += (i == pos) ? "{\"s\":0,\"d\":64,\"v\":0.6}" : "{\"s\":1,\"d\":64,\"v\":0}";
    }
    return j + "]";
}
static size_t countRoutes (const std::string& s)
{ size_t n = 0; for (size_t p = s.find ("\"d\""); p != std::string::npos; p = s.find ("\"d\"", p + 1)) ++n;
  if (n == 0) for (size_t p = s.find ("&quot;d&quot;"); p != std::string::npos; p = s.find ("&quot;d&quot;", p + 1)) ++n;
  return n; }

int main()
{
    Au a; if (! a.open()) { printf ("  !! no Terrain AU\n"); return 2; }
    printf ("\ntp101 — a route's POSITION in the list must not decide whether it is heard (LFO 1 -> Osc A Level)\n\n");
    a.pump (1.2); a.render (30, 0.0, false);
    a.set ("LFO 1 Rate", 2.0f); a.pump (0.5);
    auto take = [&] (const std::string& js) -> double
    {
        std::string x; a.getStateXml (x); Au::putAttr (x, "synModJson", js); a.setStateXml (x);
        a.pump (0.6); a.render (40, 0.0, false);
        a.note (48, 110); a.note (55, 110); a.note (60, 110);
        std::vector<float> l; a.render ((int) (48000.0 * 3.2 / 512), 0.0, true, &l);
        a.note (48, 0); a.note (55, 0); a.note (60, 0); a.render (60, 3.2, true);
        return swingDb (l, (size_t) (48000 * 0.6), l.size());
    };
    char b[200];
    {
        std::string x; a.getStateXml (x); Au::putAttr (x, "synModJson", routes (256, 0)); a.setStateXml (x);
        std::string y; a.getStateXml (y); const size_t n = countRoutes (Au::getAttr (y, "synModJson"));
        snprintf (b, sizeof b, "%zu of 256 routes came back from the saved state", n); chk (n == 256, "0  a 256-route patch round-trips through the state", b);
    }
    const double A = take (routes (1, 0)), B = take (routes (41, 0)), C = take (routes (129, 0)), D = take (routes (256, 0)), E = take (routes (0, 255)), F = take ("[]");
    snprintf (b, sizeof b, "swing %.2f dB at position 1, %.2f dB with no audible route", A, E);   chk (A > E + 3.0, "A  control: LFO 1 -> Osc A Level is a tremolo", b);
    snprintf (b, sizeof b, "swing %.2f dB at position 41 (control %.2f dB)", B, A);             chk (B > E + 3.0 && std::fabs (B - A) < 1.5, "B  route 41 modulates (past the old page cap of 32)", b);
    snprintf (b, sizeof b, "swing %.2f dB at position 129 (control %.2f dB)", C, A);            chk (C > E + 3.0 && std::fabs (C - A) < 1.5, "C  route 129 modulates (past the old C++ cap of 128)", b);
    snprintf (b, sizeof b, "swing %.2f dB at position 256 (control %.2f dB)", D, A);            chk (D > E + 3.0 && std::fabs (D - A) < 1.5, "D  route 256 modulates (the last preallocated slot)", b);
    snprintf (b, sizeof b, "swing %.2f dB with 255 depth-0 fillers, %.2f dB with no routes at all (the chord's own beating)", E, F);
    chk (std::fabs (E - F) < 1.5, "E  fillers alone add no tremolo", b);
    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    a.close(); return nfail ? 1 : 0;
}
