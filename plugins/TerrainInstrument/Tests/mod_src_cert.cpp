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
//   1  NEGATIVE CONTROL — Level A at 0 with NO route is silent; with an UNKNOWN wire code (229) it
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
    // fb566 — by JUCE parameter ID (au_fx_path.cpp's idiom): generateAUParameterID() = String::hashCode() with the sign bit cleared
    static AudioUnitParameterID pid (const std::string& id) { uint32_t r = 0; for (unsigned char ch : id) r = 31u * r + (uint32_t) ch; return (AudioUnitParameterID) (r & 0x7FFFFFFFu); }
    bool hasId (const std::string& id) const { return info.count (pid (id)) > 0; }
    bool setId (const std::string& id, float norm)
    {
        auto it = info.find (pid (id)); if (it == info.end()) return false;
        const auto& pi = it->second;
        return AudioUnitSetParameter (au, it->first, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
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
static double pearson (const std::vector<double>& a, const std::vector<double>& b)   // fb572 — two Random routes must not move together
{
    const size_t n = std::min (a.size(), b.size()); if (n < 3) return 0.0;
    double ma = 0, mb = 0; for (size_t i = 0; i < n; ++i) { ma += a[i] / (double) n; mb += b[i] / (double) n; }
    double sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    return (saa > 0 && sbb > 0) ? sab / std::sqrt (saa * sbb) : 0.0;
}
static double spreadOf (const std::vector<double>& a) { double lo = 1e9, hi = -1e9; for (double x : a) { lo = std::min (lo, x); hi = std::max (hi, x); } return a.empty() ? 0.0 : hi - lo; }
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
    au.setRoutes ("[{\"s\":229,\"d\":64,\"v\":1.0}]");   // fb565 — 228 is Macro 9 now; 229 is the gap before the wheel
    const double unknown = rmsDb (au.note (60));
    chk (silent < -70 && unknown < -70, "1  Level A at 0: silent with no route, still silent with an UNKNOWN wire code (229)", fmt ("%.1f dB / %.1f dB", silent, unknown));

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
    // Level A = knob (0) + Random × depth (1.0), so each note's RMS IS the note's random value in
    // dB: uniform on 0..1 lands half the draws within 6 dB of full, and in dB two of six will often
    // sit a tenth apart. Judge it back in LINEAR, against the macro-100 level (= rand 1.0): a
    // spread over 0.3 and a standard deviation over 0.1 (uniform 0..1 has 0.29) — the failure this
    // guards against is every note landing on the SAME value (no per-note draw at all).
    std::vector<double> lin; for (double x : rr) lin.push_back (std::pow (10.0, (x - m1) / 20.0));
    double lmin = 1e9, lmax = -1e9, mean = 0; for (double x : lin) { lmin = std::min (lmin, x); lmax = std::max (lmax, x); mean += x / (double) lin.size(); }
    double var = 0; for (double x : lin) var += (x - mean) * (x - mean) / (double) lin.size();
    std::string rs; for (double x : lin) rs += fmt ("%.2f ", x);
    chk (lmax - lmin > 0.3 && std::sqrt (var) > 0.1, "6  RANDOM 1 → Level A: six notes, six draws (linear spread > 0.3, sd > 0.1)", rs + fmt ("· spread %.2f sd %.2f", lmax - lmin, std::sqrt (var)));

    // ── 7 · ALT ──
    au.setRoutes ("[{\"s\":244,\"d\":64,\"v\":1.0}]");
    std::vector<double> al; for (int i = 0; i < 6; ++i) al.push_back (rmsDb (au.note (60)));
    bool alternates = true;
    for (size_t i = 0; i + 1 < al.size(); ++i) { const bool lo = al[i] < -60, lo2 = al[i + 1] < -60; if (lo == lo2) alternates = false; }
    std::string as; for (double x : al) as += fmt ("%.1f ", x);
    chk (alternates, "7  ALT → Level A: six notes alternate loud / silent / loud …", as + "dB");

    // ── 7b · ONE RANDOM, INDEPENDENT PER ROUTE (fb572) ──
    //  Max: "every time we put the parameter on there, it should just be random... they shouldn't be linked to each
    //  other." Two routes from the ONE Random — Level A (64) and Coarse A (42, ±24 st at full depth) — over 24 notes:
    //  the level draw and the pitch draw must be UNCORRELATED. Before fb572 one draw (rand_[0]) drove both and this
    //  read r = +1.00. 24 notes, not 12: P(|r| > 0.5) for independent draws is ~1.3 % at n = 24 (~10 % at 12 — a bar
    //  that reds one run in ten is a bar nobody trusts, fb570's lesson). Pearson is scale-free, so the pitch rides in
    //  raw semitones; a draw near 0 is near silence and cannot be pitched, so those notes are dropped and counted.
    au.set (LVL, 0.0f); au.pump (0.2);
    au.setRoutes ("[{\"s\":240,\"d\":64,\"v\":1.0},{\"s\":240,\"d\":42,\"v\":1.0}]");
    std::vector<double> lv9, pt9; int quiet9 = 0;
    for (int i = 0; i < 24; ++i)
    {
        const auto body = au.note (60, 40, 12, 40);
        const double l = std::pow (10.0, (rmsDb (body) - m1) / 20.0);
        if (l < 0.03) { ++quiet9; continue; }
        lv9.push_back (l); pt9.push_back (12.0 * std::log2 (std::max (1.0, f0Hz (body)) / 261.63));
    }
    const double r9 = pearson (lv9, pt9);
    chk (lv9.size() >= 16 && std::abs (r9) < 0.5, "7b ONE RANDOM: two routes (Level A, Coarse A) over 24 notes draw INDEPENDENTLY (|r| < 0.5; read +1.00 before)",
         fmt ("r = %+.2f over %.0f notes (%.0f too quiet to pitch)", r9, (double) lv9.size(), (double) quiet9));
    chk (spreadOf (lv9) > 0.3 && spreadOf (pt9) > 6.0, "7b … and each route still scatters on its own (level spread > 0.3, pitch spread > 6 st)",
         fmt ("level spread %.2f · pitch spread %.1f st", spreadOf (lv9), spreadOf (pt9)));

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

    // ── 9 · BYPASS (Phase 3) — the route stays in the list, the processor skips it ──
    au.set (LVL, 0.0f); au.set (MAC1, 1.0f); au.pump (0.2);
    au.setRoutes ("[{\"s\":220,\"d\":64,\"v\":1.0,\"b\":1}]");
    const double by1 = rmsDb (au.note (60));
    au.setRoutes ("[{\"s\":220,\"d\":64,\"v\":1.0,\"b\":0}]");
    const double by0 = rmsDb (au.note (60));
    chk (by1 < -70 && by0 > -30, "9  BYPASS: the same Macro 1 → Level A route is silent bypassed, loud un-bypassed", fmt ("%.1f dB / %.1f dB", by1, by0));

    // ── 10 · SCALE BY (Phase 3) — Macro 2 scales the depth of Macro 1 → Level A ──
    const std::string MAC2 = au.find ("Macro 2");
    au.setRoutes ("[{\"s\":220,\"d\":64,\"v\":1.0,\"x\":221}]");
    au.set (MAC2, 0.0f); au.pump (0.2); const double sx0 = rmsDb (au.note (60));
    au.set (MAC2, 0.5f); au.pump (0.2); const double sx5 = rmsDb (au.note (60));
    au.set (MAC2, 1.0f); au.pump (0.2); const double sx1 = rmsDb (au.note (60));
    chk (sx0 < -70 && sx1 > -30 && (sx1 - sx5) > 3.0 && (sx1 - sx5) < 9.0,
         "10 SCALE BY: Macro 1 → Level A × Macro 2: aux 0 silent, aux 50 % about −6 dB, aux 100 full", fmt ("%.1f / %.1f / %.1f dB", sx0, sx5, sx1));
    au.set (MAC2, 0.0f); au.set (MAC1, 0.0f); au.pump (0.2);

    // ── 12 · A MACRO REACHES THE RACK (fb566) — Max's chain: wheel → Macro 1 → the reverb's MIX ──
    //  The rack's walk knew LFO and envelope routes only; a macro (or the wheel, velocity, a follower)
    //  into any rack knob was silent. Measured on the wet TAIL after note-off: dry-only (mix 0) dies with
    //  the note; a mix opened by the macro rings on. The reverb is a real rack device here (ACTIVE +
    //  POWER + the osc A send), its MIX knob at 0 so the route is the only way to a tail.
    {
        const bool haveRvb = au.hasId ("SYN_RVB_MIX") && au.hasId ("SYN_RVB_POWER") && au.hasId ("SYN_RVB_ACTIVE") && au.hasId ("SYN_RVB_SRC_A");
        chk (haveRvb, "12 the AU exposes the reverb's Mix, Power, Active and Source A (by parameter id)");
        au.set (LVL, 1.0f); au.setId ("SYN_RVB_ACTIVE", 1.0f); au.setId ("SYN_RVB_POWER", 1.0f); au.setId ("SYN_RVB_SRC_A", 1.0f); au.setId ("SYN_RVB_MIX", 0.0f);
        au.pump (0.6); au.midi (0x90, 60, 100); au.render (20); au.midi (0x80, 60, 0); au.render (40); au.pump (0.6);   // a throw-away note + run loop: the pooled reverb is built on the message thread (fb352)
        auto tailDb = [&] () { au.midi (0x90, 60, 100); au.render (30); au.midi (0x80, 60, 0); au.render (40); return rmsDb (au.render (30)); };   // 0.4 s after the release ends
        au.setRoutes ("[{\"s\":220,\"d\":697,\"v\":1.0}]");   // Macro 1 → dest 697 = fxModDest (reverb, inst 1, MIX)
        au.set (MAC1, 0.0f); au.pump (0.2); const double rt0 = tailDb();
        au.set (MAC1, 1.0f); au.pump (0.2); const double rt1 = tailDb();
        chk (rt0 < -60 && rt1 > -45 && (rt1 - rt0) > 15, "12 MACRO 1 → Reverb Mix (knob at 0): macro 0 → no tail, macro 100 → the reverb rings after note-off", fmt ("%.1f dB → %.1f dB", rt0, rt1));
        au.set (MAC1, 0.0f); au.pump (0.2);
        au.setRoutes ("[{\"s\":230,\"d\":1878,\"v\":1.0},{\"s\":220,\"d\":697,\"v\":1.0}]");   // the whole chain: wheel → Macro 1 → Reverb Mix
        au.midi (0xB0, 1, 0);   au.render (30); const double rc0 = tailDb();
        au.midi (0xB0, 1, 127); au.render (30); const double rc1 = tailDb();
        chk (rc0 < -60 && rc1 > -45 && (rc1 - rc0) > 15, "12 THE CHAIN: wheel → Macro 1 → Reverb Mix: wheel 0 → no tail, wheel 127 → the tail", fmt ("%.1f dB → %.1f dB", rc0, rc1));
        au.midi (0xB0, 1, 0); au.render (30);
        au.setRoutes ("[]"); au.setId ("SYN_RVB_ACTIVE", 0.0f); au.setId ("SYN_RVB_POWER", 0.0f); au.setId ("SYN_RVB_SRC_A", 0.0f); au.setId ("SYN_RVB_MIX", 0.35f); au.set (LVL, 0.0f); au.pump (0.4);
    }

    // ── 13 · THE FREE LFO PARKS IN SILENCE (fb566) — Max: "when the MIDI is done, everything stops" ──
    //  LFO 1, Ramp, on Level A (knob at 0.5, depth 0.5). RUNS while a note sounds: two windows of one
    //  held note differ. PARKS between notes: a long silence moves it by nothing — the second note's
    //  window matches the first's within the LFO's own travel during the sounding time.
    {
        const std::string LR = au.find ("LFO 1 Rate"), LS = au.find ("LFO 1 Shape"), LD = au.find ("LFO 1 Depth"), LSY = au.find ("LFO 1 Sync");
        chk (! LR.empty() && ! LS.empty() && ! LD.empty(), "13 the AU exposes LFO 1 Rate, Shape and Depth", LR + " / " + LS + " / " + LD);
        // ⚠️ the Shape choice runs 0..10 on the AU (eleven shapes), so the index is normalised against the
        //    AU's OWN max — 3/6 landed on S&H (random steps: the RUNS bar read a coin toss and the
        //    PARKS bar a hold). Ramp = 3, monotone within a cycle, the honest ruler for both.
        const float shapeMax = au.info.at (au.byName.at (LS)).maxValue;
        au.set (LVL, 0.5f); au.set (LS, 3.0f / shapeMax); au.set (LD, 1.0f); if (! LSY.empty()) au.set (LSY, 0.0f);   // Ramp, master depth 1, free rate
        au.setRoutes ("[{\"s\":0,\"d\":64,\"v\":0.5}]");
        auto windowDb = [&] (int fromBlk, int toBlk) { std::vector<float> all; for (int b = 0; b < toBlk; ++b) { auto x = au.render (1); if (b >= fromBlk) all.insert (all.end(), x.begin(), x.end()); } return rmsDb (all); };
        // RUNS: 0.5 Hz (normalised 0.267 on the 0.01..40 Hz skew-0.3 range) — 0.8 s apart is 40 % of a ramp cycle
        au.set (LR, 0.267f); au.pump (0.2);
        au.midi (0x90, 60, 100); const double w1 = windowDb (19, 28); const double w2 = windowDb (56, 65); au.midi (0x80, 60, 0); au.render (40);
        chk (std::abs (w2 - w1) > 2.0, "13 LFO 1 RUNS while the note sounds: two windows 0.8 s apart differ", fmt ("%.1f dB vs %.1f dB", w1, w2));
        // PARKS — A/B: the SAME note after 1 s of silence and after 5 s of silence. The release tail's
        // travel (the voice sounds ~0.3 s past note-off) is in BOTH; only the silence differs. Ramp at
        // 0.03 Hz (normalised 0.102): parked, the two windows sit ~0.02 cycles apart (< 0.5 dB); free-
        // running, the extra 4 s is 0.12 cycles of ramp (~2.4 dB, more across a wrap).
        au.set (LR, 0.102f); au.pump (0.2);
        auto noteWin = [&] () { au.midi (0x90, 60, 100); const double w = windowDb (37, 47); au.midi (0x80, 60, 0); au.render (40); return w; };
        noteWin();
        au.render (94);  const double pa = noteWin();   // 1.0 s of silence, then the note
        au.render (469); const double pb = noteWin();   // 5.0 s of silence, then the note
        chk (std::abs (pb - pa) < 1.5, "13 LFO 1 PARKS in silence: the note after 5 s of silence matches the note after 1 s (free-running would drift ~2.4 dB)", fmt ("%.1f dB vs %.1f dB (Δ %.2f)", pa, pb, pb - pa));
        au.setRoutes ("[]"); au.set (LVL, 0.0f); au.set (LR, 0.5f); au.set (LS, 0.0f); au.pump (0.2);
    }

    // ── 11 · A MACRO IS A DESTINATION (fb565) — the wheel drives Macro 1 (knob at 0), Macro 1 drives Level A ──
    au.set (LVL, 0.0f); au.set (MAC1, 0.0f); au.pump (0.2);
    au.setRoutes ("[{\"s\":230,\"d\":1878,\"v\":1.0},{\"s\":220,\"d\":64,\"v\":1.0}]");
    au.midi (0xB0, 1, 0);   au.render (30); const double md0 = rmsDb (au.note (60));
    au.midi (0xB0, 1, 127); au.render (30); const double md1 = rmsDb (au.note (60));
    chk (md0 < -70 && md1 > -30 && (md1 - md0) > 40, "11 MACRO AS A DESTINATION: wheel → Macro 1 (knob at 0) → Level A: wheel 0 silent, wheel 127 loud", fmt ("%.1f dB → %.1f dB", md0, md1));
    au.midi (0xB0, 1, 0); au.render (30);
    // the route removed, the knob's own value is back in charge (macroModded_ clears)
    au.setRoutes ("[{\"s\":220,\"d\":64,\"v\":1.0}]");
    au.midi (0xB0, 1, 127); au.render (30); const double md2 = rmsDb (au.note (60));
    chk (md2 < -70, "11 … and with the wheel → Macro 1 route removed, wheel 127 no longer reaches Level A (the knob at 0 rules again)", fmt ("%.1f dB", md2));
    au.midi (0xB0, 1, 0); au.render (30);
    // the ninth macro exists on the wire (228 is Macro 9 now, not an unknown)
    const std::string MAC9 = au.find ("Macro 9");
    au.setRoutes ("[{\"s\":228,\"d\":64,\"v\":1.0}]");
    if (! MAC9.empty()) { au.set (MAC9, 0.0f); au.pump (0.2); }
    const double n9a = rmsDb (au.note (60));
    if (! MAC9.empty()) { au.set (MAC9, 1.0f); au.pump (0.2); }
    const double n9b = rmsDb (au.note (60));
    chk (! MAC9.empty() && n9a < -70 && n9b > -30, "11 MACRO 9 (wire 228) → Level A: the parameter exists, 0 silent, 100 loud", fmt ("%.1f dB → %.1f dB", n9a, n9b));
    if (! MAC9.empty()) { au.set (MAC9, 0.0f); au.pump (0.2); }

    au.close();
    std::printf ("\n  %d pass · %d fail\n\n", PASS, FAIL);
    return FAIL ? 1 : 0;
}
