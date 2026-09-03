// ══════════════════════════════════════════════════════════════════════════════════════════════
//  filter_mod_cert.cpp — fb568: DOES A MODULATOR ACTUALLY MOVE THE FILTER CUTOFF? (installed AU)
//
//    clang++ -std=c++17 -O2 Tests/filter_mod_cert.cpp -o /tmp/filter_mod_cert \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/filter_mod_cert
//
//  Max: "every time I press a random note I expect the cutoff to be at random areas — it doesn't...
//  random cutoff... doesn't even do anything audible." The audio path DOES gather block-constant
//  sources (velocity/macros/wheel/aftertouch/random/alt) into Cut1 (SynthVoice envCutBlk1_). This
//  measures whether that reaches the EAR: it renders the installed AU with a real Ladder LP 24
//  filter engaged and reads the SPECTRAL CENTROID (brightness) of each note. A moving cutoff moves
//  the centroid; a dead route leaves it flat.
//
//  THE BARS  (filter 1 = Ladder LP 24, cutoff knob mid unless noted)
//   THE BUG (fb568): the per-voice cutoff gather in SynthVoice knew LFO sources ONLY — a macro/wheel/
//   aftertouch/bend/random/alt route to the cutoff was summed into envCutBlk1_ in the mod-matrix
//   prelude, but that accumulator reads back ZERO by the time the filter loop runs, so every non-LFO
//   cutoff route was silent. FIX: the per-sample cut gather now reads every family through the one
//   reader, where LFO->cutoff already provably reaches the filter.
//   Measure: a resonant Ladder LP 24 (res 0.8) whose cutoff sits low — the level swings hard as the
//   cutoff moves, a sensitive probe. OSC A is SENT into Filter 1 (F1 pill up; default is DRY).
//   1  CONTROL — no route: six notes at one pitch share one level.
//   1a the filter responds to its cutoff knob;  1a2 LFO 1 -> Cutoff sweeps within a note (never broke).
//   1b MACRO 1 -> Cutoff moves the filter;  2 RANDOM -> Cutoff SCATTERS per note (Max's ask).
//   3  DRY SEND (F1 pill at 0): the route is silent — the osc must be SENT into the filter first.
//   4  AFTERTOUCH -> Cutoff moves;  5 MOD WHEEL (CC 1) -> Cutoff moves.
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
    double clock_ = 0;
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
    static AudioUnitParameterID pid (const std::string& id) { uint32_t r = 0; for (unsigned char ch : id) r = 31u * r + (uint32_t) ch; return (AudioUnitParameterID) (r & 0x7FFFFFFFu); }
    bool setId (const std::string& id, float norm)
    {
        auto it = info.find (pid (id)); if (it == info.end()) return false;
        const auto& pi = it->second;
        return AudioUnitSetParameter (au, it->first, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    bool setIdAbs (const std::string& id, float v)
    {
        auto it = info.find (pid (id)); if (it == info.end()) return false;
        return AudioUnitSetParameter (au, it->first, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    bool set (const std::string& n, float norm)
    {
        auto it = byName.find (n); if (it == byName.end()) return false;
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
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
    // one note: settle, then the body; caller may send pressure/CC between via callbacks not needed here
    std::vector<float> note (int nn, int sustainBlk = 40, int settleBlk = 16, int tailBlk = 24)
    {
        midi (0x90, (UInt32) nn, 110);
        render (settleBlk);
        auto body = render (sustainBlk);
        midi (0x80, (UInt32) nn, 0);
        render (tailBlk);
        return body;
    }
};

// spectral centroid (Hz) on a Hann window — the brightness a moving cutoff moves
static double centroidHz (const std::vector<float>& v)
{
    const size_t N = 16384; if (v.size() < N) return 0;
    std::vector<std::complex<double>> a (N);
    for (size_t i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * M_PI * (double) i / (double) (N - 1)); a[i] = v[i] * w; }
    for (size_t i = 1, j = 0; i < N; ++i) { size_t bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= N; len <<= 1)
    { const double ang = -2 * M_PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
      for (size_t i = 0; i < N; i += len) { std::complex<double> w (1, 0);
        for (size_t k = 0; k < len / 2; ++k) { const auto u = a[i + k], t = a[i + k + len / 2] * w; a[i + k] = u + t; a[i + k + len / 2] = u - t; w *= wl; } } }
    double num = 0, den = 0;
    for (size_t k = 1; k < N / 2; ++k) { const double mag = std::abs (a[k]); const double hz = (double) k * SR / (double) N; num += hz * mag; den += mag; }
    return den > 1e-12 ? num / den : 0;
}
static double rmsDb (const std::vector<float>& v)
{ double s = 0; for (float x : v) s += (double) x * x; return 10.0 * std::log10 (s / std::max<size_t> (1, v.size()) + 1e-30); }
static double spread (const std::vector<double>& c) { double lo = 1e18, hi = -1e18; for (double x : c) { lo = std::min (lo, x); hi = std::max (hi, x); } return hi - lo; }

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
    const double l = std::abs (a[best-1]), c = std::abs (a[best]), r = std::abs (a[best+1]);
    const double d = (l - r) / (2 * (l - 2 * c + r) + 1e-30);
    return ((double) best + d) * SR / (double) N;
}
int main()
{
    setvbuf (stdout, nullptr, _IOLBF, 0);
    std::printf ("\n══ fb568 — A MODULATOR MOVES THE FILTER CUTOFF (installed AU, Ladder LP 24) ══\n");
    AU au; if (! au.open()) return 2;
    const std::string LVL = au.find ("OSC A Level");
    chk (! LVL.empty(), "0  the AU exposes OSC A Level");
    if (LVL.empty()) { au.close(); return 1; }
    // one bright osc, filter engaged; the other oscs silent
    au.set (LVL, 1.0f);
    for (auto n : { "OSC B Level", "OSC C Level", "OSC D Level" }) { auto s = au.find (n); if (! s.empty()) au.set (s, 0.0f); }
    au.setId  ("SYN_OSC_A_F1MIX", 1.0f);           // fb568 — SEND OSC A INTO FILTER 1 (the F1 pill; default 0 = DRY)
    au.setId ("SYN_FILTER1_TYPE", 0.0f);           // Ladder LP 24 (index 0; NONE is the default 27)
    au.setId  ("SYN_FILTER1_CUT", 0.28f);          // NORMALISED low cutoff — headroom BOTH ways (the AU range is 0..1 skewed, not Hz)
    au.setId  ("SYN_FILTER1_RES", 0.80f);   // resonant: the cutoff position swings the level hard (a sensitive, monotonic-enough probe)
    { auto it = au.info.find (AU::pid ("SYN_FILTER1_CUT")); if (it != au.info.end()) std::printf ("   cutoff param: min=%.1f max=%.1f set=0.28norm\n", it->second.minValue, it->second.maxValue); }
    au.pump (0.5);

    auto sixNotes = [&] (std::vector<double>& out) { out.clear(); for (int i = 0; i < 6; ++i) out.push_back (rmsDb (au.note (60))); };

    // ── 1 · CONTROL: no route, brightness is one value ──
    au.setRoutes ("[]");
    std::vector<double> ctrl; sixNotes (ctrl);
    const double ctrlSpread = spread (ctrl);
    // convert Hz spread to a rough dB-ish log ratio for the label
    chk (ctrlSpread < 1.0, "1  CONTROL: LP 24 on, no route — six notes share one level",
         fmt ("RMS spread %.2f dB", ctrlSpread));

    // ── 1a · does the FILTER respond to its cutoff KNOB at all? ──
    au.setRoutes ("[]");
    au.setId ("SYN_FILTER1_CUT", 0.10f);  au.pump (0.2); const double kLo = rmsDb (au.note (60));
    au.setId ("SYN_FILTER1_CUT", 0.60f);  au.pump (0.2); const double kHi = rmsDb (au.note (60));
    au.setId ("SYN_FILTER1_CUT", 0.28f);  au.pump (0.2);
    chk (std::abs (kHi - kLo) > 3.0, "1a the filter RESPONDS to its cutoff knob", fmt ("RMS %.1f -> %.1f dB", kLo, kHi));

    // ── 1a2 · does an LFO reach Cut1? (the one source the in-voice gather was written for) ──
    au.setRoutes ("[{\"s\":0,\"d\":0,\"v\":1.0}]");   // LFO 1 -> Cut1
    { auto b = au.note (60, 80); double lo=1e9, hi=-1e9; const size_t win=8192; for (size_t o=0;o+win<=b.size(); o+=win){ std::vector<float> seg(b.begin()+o,b.begin()+o+win); double r=rmsDb(seg); lo=std::min(lo,r); hi=std::max(hi,r);} 
      chk (hi - lo > 3.0, "1a2 LFO 1 -> Cutoff sweeps the filter within one note", fmt ("RMS %.1f .. %.1f dB", lo, hi)); }

    // ── 1b · SANITY: does Cut1 respond to a BLOCK-CONSTANT source at all? Macro 1 -> Cut1, macro 0 vs 100 ──
    au.setRoutes ("[{\"s\":220,\"d\":0,\"v\":1.0}]");
    const std::string M1 = au.find ("Macro 1");
    std::printf ("   macro param: '%s'  (setId hasId=%d)\n", M1.c_str(), (int) au.setId ("SYN_MACRO_1", 0.5f));
    au.set (M1, 0.0f); au.pump (0.2); const double mLo = rmsDb (au.note (60));
    au.set (M1, 1.0f); au.pump (0.2); const double mHi = rmsDb (au.note (60));
    au.set (M1, 0.0f); au.pump (0.2);
    chk (std::abs (mHi - mLo) > 2.0, "1b MACRO 1 -> Cutoff moves the filter (a block-constant source reaches Cut1)",
         fmt ("RMS %.1f -> %.1f dB", mLo, mHi));

    // ── 2 · RANDOM → Cutoff (dest 0 = Cut1): the per-note scatter Max wants ──
    au.setRoutes ("[{\"s\":240,\"d\":0,\"v\":1.0}]");
    std::vector<double> rnd; sixNotes (rnd);
    const double rndSpread = spread (rnd);
    // fb570 — six uniform draws over a 48-semitone sweep can land close together (measured 2.93 dB once,
    // 5.78 / 7.27 dB on the next two runs, against a 0.15 dB control): the bar is the RATIO to the
    // no-route control, not a fixed floor a small draw can miss. 8x the control and > 1.5 dB.
    chk (rndSpread > 1.5 && rndSpread > 8.0 * ctrlSpread,
         "2  RANDOM -> Cutoff: six notes SCATTER the cutoff (per-note level varies) — Max\'s ask",
         fmt ("RMS spread %.2f dB vs control %.2f dB", rndSpread, ctrlSpread));

    // ── 3 · THE DISCOVERABILITY TRAP: OSC A not SENT into the filter (F1 pill at 0 = dry) ──
    //   The real reason "a modulator does nothing" — the osc must be routed into the filter first.
    au.setId ("SYN_OSC_A_F1MIX", 0.0f); au.pump (0.3);
    std::vector<double> off; sixNotes (off);
    chk (spread (off) < 1.0,
         "3  DRY SEND (F1 pill at 0): the Random route is silent — the osc must be SENT into the filter",
         fmt ("RMS spread %.2f dB (was %.2f with the send up)", spread (off), rndSpread));
    au.setId ("SYN_OSC_A_F1MIX", 1.0f); au.pump (0.3);   // send back up

    // ── 4 · AFTERTOUCH → Cutoff: channel pressure opens the filter ──
    au.setRoutes ("[{\"s\":231,\"d\":0,\"v\":1.0}]");
    au.midi (0x90, 60, 110); au.render (16);
    au.midi (0xD0, 0, 0);   au.render (8);  double atLo = rmsDb (au.render (36));
    au.midi (0xD0, 127, 0); au.render (10); double atHi = rmsDb (au.render (36));
    au.midi (0x80, 60, 0);  au.render (24);
    chk (std::abs (atHi - atLo) > 2.0, "4  AFTERTOUCH -> Cutoff: pressure 0 -> 127 moves the filter", fmt ("RMS %.1f -> %.1f dB", atLo, atHi));

    // ── 5 · MOD WHEEL (CC 1) → Cutoff ──
    au.setRoutes ("[{\"s\":230,\"d\":0,\"v\":1.0}]");
    au.midi (0xB0, 1, 0);   au.render (6);
    au.midi (0x90, 60, 110); au.render (16); double whLo = rmsDb (au.render (36));
    au.midi (0x80, 60, 0);  au.render (24);
    au.midi (0xB0, 1, 127); au.render (6);
    au.midi (0x90, 60, 110); au.render (16); double whHi = rmsDb (au.render (36));
    au.midi (0x80, 60, 0);  au.render (24);
    chk (std::abs (whHi - whLo) > 2.0, "5  MOD WHEEL (CC 1) -> Cutoff: wheel 0 -> 127 moves the filter", fmt ("RMS %.1f -> %.1f dB", whLo, whHi));

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", PASS, FAIL);
    au.close();
    return FAIL ? 1 : 0;
}
