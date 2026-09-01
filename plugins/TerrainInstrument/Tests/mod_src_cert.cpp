// ══════════════════════════════════════════════════════════════════════════════════════════════
//  mod_src_cert.cpp — fb563 PHASE 2: the new modulation sources REACH THE AUDIO, on the real AU.
//
//    clang++ -std=c++17 -O2 Tests/mod_src_cert.cpp -o /tmp/mod_src_cert \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/mod_src_cert
//
//  THE LAW THIS EXISTS FOR (fb552 / fb373): a mod source has to pass four doors and three of them
//  fail silently. The UI can draw a route, save it, and the processor can throw it away at the door
//  — the underline moves and the audio does not. So every one of the six new families is proven by
//  RENDERING through the installed AU with a route installed through the plugin's own state path
//  (synModJson → setSynthModMatrix, the same parser the WebView feeds) and MIDI sent through the
//  host's own MusicDeviceMIDIEvent. Level A is the destination and its knob sits at ZERO, so the
//  source is the ONLY thing that can make a sound: silence = the door dropped it.
//
//  THE BARS
//   1  NEGATIVE CONTROL — Level A at 0 with NO route is silent; with an UNKNOWN wire code (228) it
//      stays silent (the door drops it, never invents a source).
//   2  MACRO — Macro 1 → Level A: macro at 0 is silent, at 100 it is loud (> 40 dB apart).
//   3  MOD WHEEL — CC 1 → Level A: wheel 0 silent, wheel 127 loud.
//   4  AFTERTOUCH — channel pressure → Level A: 0 silent, 127 loud.
//   5  PITCH BEND, hard-wired — no route: bend centre vs bend max moves the fundamental by the
//      range (2 semitones by default → ×1.1225), within 1 %. Range 12 → ×2 (an octave).
//   6  RANDOM — Random 1 → Level A: six separate notes land at DIFFERENT levels (spread > 6 dB
//      between the loudest and the quietest, and no two within 0.1 dB of each other).
//   7  ALT — Alt → Level A: six separate notes ALTERNATE loud / silent / loud …
//   8  DEPTH SIGN — Macro 1 → Level A at depth −1 with Level A at 1.0 turns it DOWN (the velocity
//      law: additive, signed), and a route CURVE y = 1 − x inverts the macro (macro 0 → loud).
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
#include <complex>
#include <algorithm>

static const double SR = 48000.0;
static const int    BLK = 512;
static int PASS = 0, FAIL = 0;
static void chk (bool ok, const char* label, const std::string& detail = "")
{
    if (ok) { ++PASS; std::printf ("  ok    %s%s%s\n", label, detail.empty() ? "" : "   ", detail.c_str()); }
    else    { ++FAIL; std::printf ("  FAIL  %s%s%s\n", label, detail.empty() ? "" : "   ", detail.c_str()); }
}
static std::string fmt (const char* f, double a, double b = 0, double c = 0)
{ char buf[256]; std::snprintf (buf, sizeof buf, f, a, b, c); return buf; }

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    bool open()
    {
        AudioComponentDescription d {};
        d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c) { std::printf ("  !! AU aumu/Tern/Wvcr not found\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! instantiate failed\n"); return false; }
        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { std::printf ("  !! AudioUnitInitialize failed\n"); return false; }
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            std::string nm;
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString)
            { char buf[256] = {0}; CFStringGetCString (pi.cfNameString, buf, sizeof buf, kCFStringEncodingUTF8); nm = buf; }
            else nm = pi.name;
            byName[nm] = id; info[id] = pi;
        }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
    std::string find (const std::string& needle) const
    { for (auto& kv : byName) if (kv.first.find (needle) != std::string::npos) return kv.first; return ""; }
    bool set (const std::string& n, float norm)
    {
        auto it = byName.find (n); if (it == byName.end()) return false;
        const auto& pi = info.at (it->second);
        const float v = pi.minValue + norm * (pi.maxValue - pi.minValue);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    // the plugin's own state path: rewrite synModJson in the saved XML and hand it back (au_fx_path.cpp's idiom)
    bool setRoutes (const std::string& json)
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return false;
        CFDictionaryRef dict = (CFDictionaryRef) pl;
        CFStringRef key = CFSTR ("jucePluginState");
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue (dict, key);
        if (dd == nullptr) { CFRelease (pl); return false; }
        const UInt8* p = CFDataGetBytePtr (dd);
        uint32_t magic = 0, len = 0; std::memcpy (&magic, p, 4); std::memcpy (&len, p + 4, 4);
        if (magic != 0x21324356u || (CFIndex) (len + 8) > CFDataGetLength (dd)) { CFRelease (pl); return false; }
        std::string xml ((const char*) p + 8, len);
        std::string esc; for (char ch : json) esc += (ch == '"' ? "&quot;" : ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : std::string (1, ch));
        const std::string attr = " synModJson=\"" + esc + "\"";
        const size_t at = xml.find (" synModJson=\"");
        if (at != std::string::npos) { const size_t e = xml.find ('"', at + 13); xml = xml.substr (0, at) + attr + xml.substr (e + 1); }
        else { const size_t r = xml.find ("<Parameters"); if (r == std::string::npos) { CFRelease (pl); return false; }
               xml = xml.substr (0, r + 11) + attr + xml.substr (r + 11); }
        std::vector<UInt8> blob (8 + xml.size() + 1, 0);
        const uint32_t m = 0x21324356u, l = (uint32_t) xml.size() + 1;
        std::memcpy (blob.data(), &m, 4); std::memcpy (blob.data() + 4, &l, 4); std::memcpy (blob.data() + 8, xml.data(), xml.size());
        CFMutableDictionaryRef nd = CFDictionaryCreateMutableCopy (nullptr, 0, dict);
        CFDataRef ndata = CFDataCreate (kCFAllocatorDefault, blob.data(), (CFIndex) blob.size());
        CFDictionarySetValue (nd, key, ndata);
        CFPropertyListRef npl = (CFPropertyListRef) nd;
        const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &npl, sizeof (npl));
        CFRelease (ndata); CFRelease (nd); CFRelease (pl);
        pump (0.3);
        return st == noErr;
    }
    void pump (double seconds) { double t = 0; while (t < seconds) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }
    void midi (UInt32 status, UInt32 d1, UInt32 d2) { MusicDeviceMIDIEvent (au, status, d1, d2, 0); }
    // render nblk blocks into out (both channels summed to L) — no note handling here
    std::vector<float> render (int nblk)
    {
        std::vector<float> out; out.reserve ((size_t) nblk * BLK);
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = clock_;
        for (int b = 0; b < nblk; ++b)
        {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            if (AudioUnitRender (au, &fl, &ts, 0, BLK, abl) != noErr) break;
            ts.mSampleTime += BLK; clock_ += BLK;
            for (int i = 0; i < BLK; ++i) out.push_back (0.5f * (bl[(size_t) i] + br[(size_t) i]));
        }
        free (abl);
        return out;
    }
    double clock_ = 0;
    // one note: note-on, settle, measure the sustained part, note-off, let the release die
    std::vector<float> note (int nn, int sustainBlk = 24, int settleBlk = 12, int tailBlk = 40)
    {
        midi (0x90, (UInt32) nn, 100);
        render (settleBlk);
        auto body = render (sustainBlk);
        midi (0x80, (UInt32) nn, 0);
        render (tailBlk);
        return body;
    }
};

static double rmsDb (const std::vector<float>& v)
{ double s = 0; for (float x : v) s += (double) x * x; return 10.0 * std::log10 (s / std::max<size_t> (1, v.size()) + 1e-30); }

// fundamental by FFT peak with parabolic interpolation, on a Hann window
static double f0Hz (const std::vector<float>& v)
{
    const size_t N = 16384; if (v.size() < N) return 0;
    std::vector<std::complex<double>> a (N);
    for (size_t i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * M_PI * (double) i / (double) (N - 1)); a[i] = v[i] * w; }
    for (size_t i = 1, j = 0; i < N; ++i) { size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= N; len <<= 1)
    { const double ang = -2 * M_PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
      for (size_t i = 0; i < N; i += len) { std::complex<double> w (1, 0);
        for (size_t k = 0; k < len / 2; ++k) { const auto u = a[i + k], t = a[i + k + len / 2] * w; a[i + k] = u + t; a[i + k + len / 2] = u - t; w *= wl; } } }
    size_t best = 1; double bm = 0;
    for (size_t k = 2; k < N / 2; ++k) { const double m = std::abs (a[k]); if (m > bm) { bm = m; best = k; } }
    const double l = std::abs (a[best - 1]), c = std::abs (a[best]), r = std::abs (a[best + 1]);
    const double d = (l - r) / (2 * (l - 2 * c + r) + 1e-30);
    return ((double) best + d) * SR / (double) N;
}

int main()
{
    setvbuf (stdout, nullptr, _IOLBF, 0);
    std::printf ("\n══ fb563 PHASE 2 — THE NEW SOURCES REACH THE AUDIO (installed AU) ══\n");
    AU au; if (! au.open()) return 2;
    const std::string LVL = au.find ("OSC A Level"), MAC1 = au.find ("Macro 1"), BEND = au.find ("Bend Range");
    std::printf ("   params: level='%s' macro='%s' bend='%s'\n", LVL.c_str(), MAC1.c_str(), BEND.c_str());
    chk (! LVL.empty() && ! MAC1.empty() && ! BEND.empty(), "0  the AU exposes OSC A Level, Macro 1 and Bend Range");
    if (LVL.empty() || MAC1.empty() || BEND.empty()) { au.close(); return 1; }
    // Osc A only, level knob at ZERO: the source is the only way to a sound
    const std::string LB = au.find ("OSC B Level"), LC = au.find ("OSC C Level"), LD = au.find ("OSC D Level");
    au.set (LVL, 0.0f); if (! LB.empty()) au.set (LB, 0.0f); if (! LC.empty()) au.set (LC, 0.0f); if (! LD.empty()) au.set (LD, 0.0f);
    au.pump (0.5);

    // ── 1 · NEGATIVE CONTROL ──
    au.setRoutes ("[]");
    const double silent = rmsDb (au.note (60));
    au.setRoutes ("[{\"s\":228,\"d\":64,\"v\":1.0}]");
    const double unknown = rmsDb (au.note (60));
    chk (silent < -70 && unknown < -70, "1  Level A at 0: silent with no route, still silent with an UNKNOWN wire code (228)", fmt ("%.1f dB / %.1f dB", silent, unknown));

    // ── 2 · MACRO ──
    au.setRoutes ("[{\"s\":220,\"d\":64,\"v\":1.0}]");
    au.set (MAC1, 0.0f); au.pump (0.2); const double m0 = rmsDb (au.note (60));
    au.set (MAC1, 1.0f); au.pump (0.2); const double m1 = rmsDb (au.note (60));
    chk (m0 < -70 && m1 > -30 && (m1 - m0) > 40, "2  MACRO 1 → Level A: macro 0 silent, macro 100 loud", fmt ("%.1f dB → %.1f dB", m0, m1));
    au.set (MAC1, 0.0f); au.pump (0.2);

    // ── 3 · MOD WHEEL ──
    au.setRoutes ("[{\"s\":230,\"d\":64,\"v\":1.0}]");
    au.midi (0xB0, 1, 0); au.render (30); const double w0 = rmsDb (au.note (60));
    au.midi (0xB0, 1, 127); au.render (30); const double w1 = rmsDb (au.note (60));
    chk (w0 < -70 && w1 > -30 && (w1 - w0) > 40, "3  MOD WHEEL (CC 1) → Level A: wheel 0 silent, wheel 127 loud", fmt ("%.1f dB → %.1f dB", w0, w1));
    au.midi (0xB0, 1, 0); au.render (30);

    // ── 4 · AFTERTOUCH ──
    au.setRoutes ("[{\"s\":231,\"d\":64,\"v\":1.0}]");
    au.midi (0xD0, 0, 0); au.render (30); const double a0 = rmsDb (au.note (60));
    au.midi (0xD0, 127, 0); au.render (30); const double a1 = rmsDb (au.note (60));
    chk (a0 < -70 && a1 > -30 && (a1 - a0) > 40, "4  AFTERTOUCH (channel pressure) → Level A: 0 silent, 127 loud", fmt ("%.1f dB → %.1f dB", a0, a1));
    au.midi (0xD0, 0, 0); au.render (30);

    // ── 5 · PITCH BEND, hard-wired ──
    au.setRoutes ("[]"); au.set (LVL, 1.0f); au.pump (0.2);
    au.midi (0xE0, 0, 64); au.render (30);                          // centre (8192)
    const double fc = f0Hz (au.note (60, 40, 12, 40));
    au.midi (0xE0, 127, 127); au.render (30);                       // max (16383)
    const double fu = f0Hz (au.note (60, 40, 12, 40));
    au.set (BEND, 12.0f / 24.0f); au.pump (0.2);
    const double fo = f0Hz (au.note (60, 40, 12, 40));
    au.midi (0xE0, 0, 64); au.render (30); au.set (BEND, 2.0f / 24.0f); au.pump (0.2);
    const double r2 = fu / std::max (1.0, fc), r12 = fo / std::max (1.0, fc);
    chk (fc > 200 && fc < 300 && std::abs (r2 - 1.12246) < 0.012, "5  PITCH BEND max at range 2 → the fundamental rises 2 semitones (×1.1225)", fmt ("%.1f Hz → %.1f Hz (×%.4f)", fc, fu, r2));
    chk (std::abs (r12 - 2.0) < 0.02, "5  … and at range 12 → an octave (×2.000)", fmt ("×%.4f", r12));

    // ── 6 · RANDOM ──
    au.set (LVL, 0.0f); au.pump (0.2);
    au.setRoutes ("[{\"s\":240,\"d\":64,\"v\":1.0}]");
    std::vector<double> rr; for (int i = 0; i < 6; ++i) rr.push_back (rmsDb (au.note (60)));
    double rmin = 1e9, rmax = -1e9; bool distinct = true;
    for (size_t i = 0; i < rr.size(); ++i) { rmin = std::min (rmin, rr[i]); rmax = std::max (rmax, rr[i]);
        for (size_t j = 0; j < i; ++j) if (std::abs (rr[i] - rr[j]) < 0.1) distinct = false; }
    std::string rs; for (double x : rr) rs += fmt ("%.1f ", x);
    chk (rmax - rmin > 6.0 && distinct, "6  RANDOM 1 → Level A: six notes, six different levels", rs + "dB");

    // ── 7 · ALT ──
    au.setRoutes ("[{\"s\":244,\"d\":64,\"v\":1.0}]");
    std::vector<double> al; for (int i = 0; i < 6; ++i) al.push_back (rmsDb (au.note (60)));
    bool alternates = true;
    for (size_t i = 0; i + 1 < al.size(); ++i) { const bool lo = al[i] < -60, lo2 = al[i + 1] < -60; if (lo == lo2) alternates = false; }
    std::string as; for (double x : al) as += fmt ("%.1f ", x);
    chk (alternates, "7  ALT → Level A: six notes alternate loud / silent / loud …", as + "dB");

    // ── 8 · DEPTH SIGN + CURVE ──
    au.set (LVL, 1.0f); au.pump (0.2);
    au.setRoutes ("[{\"s\":220,\"d\":64,\"v\":-1.0}]");
    au.set (MAC1, 0.0f); au.pump (0.2); const double n0 = rmsDb (au.note (60));
    au.set (MAC1, 1.0f); au.pump (0.2); const double n1 = rmsDb (au.note (60));
    chk (n0 > -30 && n1 < -60, "8  MACRO 1 → Level A at depth −1 (knob at 1): macro 100 turns it DOWN (additive, signed)", fmt ("%.1f dB → %.1f dB", n0, n1));
    au.set (LVL, 0.0f); au.pump (0.2);
    std::string curve; for (int k = 0; k < 129; ++k) { if (k) curve += ","; curve += fmt ("%.4f", 1.0 - (double) k / 128.0); }
    au.setRoutes ("[{\"s\":220,\"d\":64,\"v\":1.0,\"c\":\"" + curve + "\"}]");
    au.set (MAC1, 0.0f); au.pump (0.2); const double c0 = rmsDb (au.note (60));
    au.set (MAC1, 1.0f); au.pump (0.2); const double c1 = rmsDb (au.note (60));
    chk (c0 > -30 && c1 < -60, "8  … and a connection curve y = 1 − x inverts it: macro 0 loud, macro 100 silent", fmt ("%.1f dB → %.1f dB", c0, c1));

    au.close();
    std::printf ("\n  %d pass · %d fail\n\n", PASS, FAIL);
    return FAIL ? 1 : 0;
}
