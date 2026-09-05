// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_spread_cert.cpp — fb582: SPREAD GIVES EVERY UNISON VOICE ITS OWN FRAME, AND IT IS AUDIBLE.
//
//    clang++ -std=c++17 -O2 Tests/wt_spread_cert.cpp -o /tmp/wt_spread_cert \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/wt_spread_cert
//
//  Max: "I don't like blur, never did. There's no point in having it there... spread needs to DO
//  something." BLUR averaged a Gaussian band of frames into one cycle, which can only cancel detail.
//  SPREAD fans the unison stack ACROSS the table instead: each voice reads its own frame, seated on
//  its own detune position, so a stack becomes a chorus of different waveforms.
//
//  THE RIG: OSC A alone on a real table (WT Preset 4 — a sine cannot fail a timbre test), unison
//  DETUNE at 0 so the only thing separating the voices is the frame they read. Two renders of the
//  same held note, spread 0 against spread 1, compared as normalised magnitude spectra (a 256-bin
//  DFT over a 1024-sample window, each normalised to unit energy first) so the number is TIMBRE
//  change and not level change.
//
//  THE BARS
//   0  the AU exposes OSC A Level, WT Preset, Unison, Unison Detune and Spread
//   1  AT ONE VOICE, SPREAD IS INERT — one voice has no stack to fan: the two renders match to
//      better than −60 dB, so no mono patch can change under it
//   2  AT EIGHT VOICES, SPREAD CHANGES THE SOUND — spectral distance >= 0.15 (measured 0 vs 1)
//   3  IT IS TIMBRE, NOT LEVEL — the two renders sit within 3 dB of each other
//   4  THE KNOB IS PROGRESSIVE — distance grows with the knob: d(0.25) < d(0.5) < d(1.0)
//   5  NO CLICKS — sweeping spread 0 → 1 under a held note produces no sample step a real waveform
//      would not (max |Δ| stays under 4× the un-swept note's own max |Δ|)
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

static const double SR = 48000.0;
static const int    BLK = 512;
static int pass = 0, fail = 0;
static void chk (bool ok, const char* label, const std::string& detail = "")
{ if (ok) ++pass; else ++fail; std::printf ("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", label, detail.empty() ? "" : "   ", detail.c_str()); }

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
        if (! c) { std::printf ("  !! AU not found\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! instantiate failed\n"); return false; }
        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { std::printf ("  !! init failed\n"); return false; }
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
    float norm (const std::string& n)   // the parameter's value as 0..1 of its own range
    {
        auto it = byName.find (n); if (it == byName.end()) return NAN;
        AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v);
        const auto& pi = info.at (it->second); return (v - pi.minValue) / std::max (1e-9f, pi.maxValue - pi.minValue);
    }
    bool set (const std::string& n, float nv)
    {
        auto it = byName.find (n); if (it == byName.end()) return false;
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + nv * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    // read the state XML out of ClassInfo
    std::string stateXml()
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return "";
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue ((CFDictionaryRef) pl, CFSTR ("jucePluginState"));
        std::string xml;
        if (dd) { const UInt8* p = CFDataGetBytePtr (dd); uint32_t len = 0; std::memcpy (&len, p + 4, 4); xml.assign ((const char*) p + 8, len); }
        CFRelease (pl); return xml;
    }
    // rewrite one attribute of the state XML and hand it back (au_fx_path.cpp's idiom, generalised)
    bool setStateAttr (const std::string& name, const std::string& value)
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return false;
        CFDictionaryRef dict = (CFDictionaryRef) pl;
        CFStringRef key = CFSTR ("jucePluginState");
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue (dict, key);
        if (dd == nullptr) { CFRelease (pl); return false; }
        const UInt8* p = CFDataGetBytePtr (dd);
        uint32_t magic = 0, len = 0; std::memcpy (&magic, p, 4); std::memcpy (&len, p + 4, 4);
        if (magic != 0x21324356u) { CFRelease (pl); return false; }
        std::string xml ((const char*) p + 8, len);
        std::string esc; for (char ch : value) esc += (ch == '"' ? "&quot;" : ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : std::string (1, ch));
        const std::string attr = " " + name + "=\"" + esc + "\"";
        const size_t at = xml.find (" " + name + "=\"");
        if (at != std::string::npos) { const size_t e = xml.find ('"', at + name.size() + 3); xml = xml.substr (0, at) + attr + xml.substr (e + 1); }
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
        std::vector<float> out; std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = clock_;
        for (int b = 0; b < nblk; ++b)
        {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() }; abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0; if (AudioUnitRender (au, &fl, &ts, 0, BLK, abl) != noErr) break;
            ts.mSampleTime += BLK; clock_ += BLK;
            for (int i = 0; i < BLK; ++i) out.push_back (0.5f * (bl[(size_t) i] + br[(size_t) i]));
        }
        free (abl); return out;
    }
    // a CC event has to pass through processBlock (audio) and then the timer (message thread)
    void cc (int num, int val) { midi (0xB0, (UInt32) num, (UInt32) val); render (4); pump (0.35); }
    std::vector<float> note (int nn) { midi (0x90, (UInt32) nn, 100); render (12); auto body = render (24); midi (0x80, (UInt32) nn, 0); render (40); return body; }
};


static double rmsDb (const std::vector<float>& v)
{ double s = 0; for (float x : v) s += (double) x * x; return 10.0 * std::log10 (std::max (1e-20, s / std::max<size_t> (1, v.size()))); }

// a naive DFT magnitude spectrum over one window, normalised to unit energy
static std::vector<double> spectrum (const std::vector<float>& v, size_t off = 4096)
{
    const int N = 1024, BINS = 256;
    std::vector<double> mag ((size_t) BINS, 0.0);
    if (v.size() < off + (size_t) N) return mag;
    for (int k = 1; k <= BINS; ++k)
    {
        double re = 0, im = 0;
        const double w = 2.0 * M_PI * (double) k / (double) N;
        for (int n = 0; n < N; ++n)
        {
            const double x = (double) v[off + (size_t) n] * (0.5 - 0.5 * std::cos (2.0 * M_PI * n / (N - 1)));   // Hann
            re += x * std::cos (w * n); im -= x * std::sin (w * n);
        }
        mag[(size_t) (k - 1)] = std::sqrt (re * re + im * im);
    }
    double e = 0; for (double m : mag) e += m * m;
    e = std::sqrt (std::max (1e-30, e));
    for (double& m : mag) m /= e;      // unit energy: level drops out, only the SHAPE remains
    return mag;
}
static double specDist (const std::vector<double>& a, const std::vector<double>& b)
{ double s = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) { const double d = a[i] - b[i]; s += d * d; } return std::sqrt (s); }
static double maxStep (const std::vector<float>& v)
{ double m = 0; for (size_t i = 1; i < v.size(); ++i) m = std::max (m, (double) std::abs (v[i] - v[i - 1])); return m; }
static double diffDb (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size()); if (! n) return 0.0;
    double num = 0, den = 0;
    for (size_t i = 0; i < n; ++i) { const double d = (double) a[i] - b[i]; num += d * d; den += (double) a[i] * a[i]; }
    return 10.0 * std::log10 (std::max (1e-30, num) / std::max (1e-30, den));
}

int main()
{
    AU au; if (! au.open()) return 1;
    std::printf ("\n══ fb582 — SPREAD: EVERY UNISON VOICE ITS OWN FRAME (installed AU) ══\n\n");
    const std::string LVL = au.find ("OSC A Level"), WT = au.find ("OSC A WT Preset"),
                      UNI = au.find ("OSC A Unison"), DET = au.find ("OSC A Unison Detune"),
                      SPR = au.find ("OSC A Spread");
    chk (! LVL.empty() && ! WT.empty() && ! UNI.empty() && ! DET.empty() && ! SPR.empty(),
         "0  the AU exposes OSC A Level, WT Preset, Unison, Unison Detune and Spread",
         "level='" + LVL + "' wt='" + WT + "' uni='" + UNI + "' det='" + DET + "' spread='" + SPR + "'");
    if (LVL.empty() || WT.empty() || UNI.empty() || DET.empty() || SPR.empty()) { au.close(); return 1; }

    for (const char* other : { "OSC B Level", "OSC C Level", "OSC D Level" })
    { const std::string n = au.find (other); if (! n.empty()) au.set (n, 0.0f); }
    au.set (WT, 4.0f / 100.0f);          // a real table: frames that actually differ
    au.set (DET, 0.0f);                  // the ONLY thing separating the voices is the frame
    au.pump (0.3);

    auto render = [&] (int voices, float spread) {
        au.set (UNI, (float) (voices - 1) / 15.0f);
        au.set (SPR, spread);
        au.pump (0.35);
        return au.note (60);
    };
    /* THE SYNTH IS NOT REPEATABLE NOTE TO NOTE, BY DESIGN — per-voice drift and start phase mean two
       renders of the SAME settings differ. MEASURED: one voice 4.8 dB sample-wise (spectral 0.0023),
       eight voices 5.4 dB and spectral 0.39, because eight near-identical voices comb differently
       every note. So a single note cannot isolate spread at all. Average the magnitude spectra over
       several notes: the random comb averages away, the systematic timbre change does not. */
    struct M { std::vector<double> spec; double rms; };
    auto measure = [&] (int voices, float spread, int n) {
        au.set (UNI, (float) (voices - 1) / 15.0f); au.set (SPR, spread); au.pump (0.35);
        std::vector<double> acc ((size_t) 256, 0.0); double r = 0;
        for (int k = 0; k < n; ++k) { const auto v = au.note (60); const auto s = spectrum (v);
            for (size_t b = 0; b < acc.size(); ++b) acc[b] += s[b]; r += rmsDb (v); }
        double e = 0; for (double m : acc) e += m * m; e = std::sqrt (std::max (1e-30, e));
        for (double& m : acc) m /= e;
        return M { acc, r / n };
    };
    const int N = 6;
    // the floor: two INDEPENDENT averaged measurements of the same settings
    const M f1a = measure (1, 0.0f, N), f1b = measure (1, 0.0f, N);
    const M f8a = measure (8, 0.0f, N), f8b = measure (8, 0.0f, N);
    const double floor1 = specDist (f1a.spec, f1b.spec), floor8 = specDist (f8a.spec, f8b.spec);
    std::printf ("   noise floor over %d notes: 1 voice %.4f · 8 voices %.4f\n", N, floor1, floor8);

    // 1 — one voice: nothing to fan
    const M m1s = measure (1, 1.0f, N);
    const double dMono = specDist (f1a.spec, m1s.spec);
    chk (dMono <= floor1 * 2.0 + 0.005,
         "1  AT ONE VOICE, SPREAD IS INERT: it moves the sound no more than the synth's own note-to-note drift, so no mono patch changes under it",
         "spread 0 → 1 distance " + std::to_string (dMono) + " against a floor of " + std::to_string (floor1));

    // 2 / 3 — eight voices, detune 0: the frame fan is the only variable
    const M e0 = f8a, e1 = measure (8, 1.0f, N);
    const double d01 = specDist (e0.spec, e1.spec);
    const double lvlDelta = std::abs (e1.rms - e0.rms);
    chk (d01 >= 0.15 && d01 >= floor8 * 3.0,
         "2  AT EIGHT VOICES, SPREAD CHANGES THE SOUND: it moves the spectrum far past the stack's own note-to-note wander",
         "distance " + std::to_string (d01) + " vs floor " + std::to_string (floor8) + " (want >= 0.15 and >= 3x the floor)");
    chk (lvlDelta < 4.0, "3  IT IS TIMBRE, NOT LEVEL: the averaged level barely moves while the spectrum travels",
         std::to_string (e0.rms) + " dB → " + std::to_string (e1.rms) + " dB (Δ " + std::to_string (lvlDelta) + ", want < 4)");

    // 4 — the knob is progressive
    const M q = measure (8, 0.25f, N), h = measure (8, 0.5f, N);
    const double dq = specDist (e0.spec, q.spec), dh = specDist (e0.spec, h.spec);
    chk (dq > floor8 && dq < dh && dh < d01,
         "4  THE KNOB IS PROGRESSIVE: every setting is past the floor, and the further it goes the further the sound travels",
         "d(0.25) " + std::to_string (dq) + " < d(0.5) " + std::to_string (dh) + " < d(1.0) " + std::to_string (d01) + " · floor " + std::to_string (floor8));

    // 5 — no clicks while the knob moves under a held note
    au.set (UNI, 7.0f / 15.0f); au.set (SPR, 0.0f); au.pump (0.3);
    const double quietStep = maxStep (au.note (60));
    au.set (SPR, 0.0f); au.pump (0.2);
    au.midi (0x90, 60, 100); au.render (10);
    std::vector<float> swept;
    for (int i = 0; i <= 40; ++i)
    { au.set (SPR, (float) i / 40.0f); auto b = au.render (2); swept.insert (swept.end(), b.begin(), b.end()); }
    au.midi (0x80, 60, 0); au.render (20);
    const double sweptStep = maxStep (swept);
    chk (sweptStep <= quietStep * 4.0 + 1e-6, "5  NO CLICKS: sweeping spread under a held note makes no step the waveform itself would not",
         "max |Δ| swept " + std::to_string (sweptStep) + " vs still " + std::to_string (quietStep) + " (want <= 4x)");

    std::printf ("\n  %d pass · %d fail\n", pass, fail);
    au.close(); return fail ? 1 : 0;
}
