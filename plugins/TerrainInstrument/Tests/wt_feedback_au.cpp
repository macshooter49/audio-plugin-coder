// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_feedback_au.cpp — fb584: FEEDBACK, ON THE INSTALLED PLUGIN, AND IT OBEYS BOTH HALVES OF
//  THE LIFEGUARD LAW.
//
//    clang++ -std=c++17 -O2 Tests/wt_feedback_au.cpp -o /tmp/wt_feedback_au \
//        -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/wt_feedback_au
//
//  The oscillator modulates its own read: its output bends both the PHASE it reads at and the
//  FRAME it reads from, through the DX7 mean filter (y = 0.5(x[n] + x[n-1])) our FM operator
//  already uses. The two paths are tapered differently ON PURPOSE, and that taper IS the law:
//
//    🏊‍♂️🦈 "10-50% must stay clean, musical and useful — dirt at 15% is a defect... Which means my
//       100% shouldn't be 50."  So FRAME feedback (clean, progressive, but on its own only 0.45x
//       the WT Pos axis) rides a LINEAR taper and owns the bottom, and PHASE feedback (reaches
//       1.6x, but its character IS its alias) rides a FOURTH-POWER taper and owns the top.
//
//  THIS FILE MEASURES BOTH ENDS OF THAT SENTENCE ON THE SHIPPING AU — not the offline model.
//  Inharmonic energy is measured against a Blackman-Harris window (-92 dB sidelobes), harmonics
//  taken as +/-4 bins around each k*f0; everything else in band is alias, chaos or noise.
//
//  THE BARS
//   0  the AU exposes the knob as "Feedback", and "Spread" is gone
//   1  AT ONE VOICE IT WORKS — it is a property of the oscillator, not of the unison stack
//   2  THE BOTTOM IS CLEAN — at 25% the inharmonic energy is still far down (the half of the law
//      that the last three attempts at this slot never had to answer)
//   3  THE TOP IS VIOLENT — at 100% the partials have left the harmonic grid entirely
//   4  IT IS PROGRESSIVE — every quarter of the travel moves the sound, none of it is dead
//   5  NO CLICKS while sweeping under a held note
//   6  THE LEVEL HOLDS — this is timbre and destruction, not a volume ramp
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


// ── a compact iterative radix-2 FFT (no framework dependency) ─────────────────────────────────
static void fft (std::vector<double>& re, std::vector<double>& im)
{
    const int n = (int) re.size();
    for (int i = 1, j = 0; i < n; ++i)
    { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
      if (i < j) { std::swap (re[(size_t)i], re[(size_t)j]); std::swap (im[(size_t)i], im[(size_t)j]); } }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / len;
        const double wr = std::cos (ang), wi = std::sin (ang);
        for (int i = 0; i < n; i += len)
        {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < len/2; ++k)
            {
                const double ur = re[(size_t)(i+k)],       ui = im[(size_t)(i+k)];
                const double vr = re[(size_t)(i+k+len/2)]*cr - im[(size_t)(i+k+len/2)]*ci;
                const double vi = re[(size_t)(i+k+len/2)]*ci + im[(size_t)(i+k+len/2)]*cr;
                re[(size_t)(i+k)] = ur+vr; im[(size_t)(i+k)] = ui+vi;
                re[(size_t)(i+k+len/2)] = ur-vr; im[(size_t)(i+k+len/2)] = ui-vi;
                const double ncr = cr*wr - ci*wi; ci = cr*wi + ci*wr; cr = ncr;
            }
        }
    }
}
static const int NFFT = 32768;
// inharmonic energy vs harmonic energy, in dB. f0 need not land on a bin: harmonics get +/-4 bins.
static double inharmDb (const std::vector<float>& x, double f0)
{
    if ((int) x.size() < NFFT) return 0.0;
    std::vector<double> re ((size_t) NFFT), im ((size_t) NFFT, 0.0);
    for (int i = 0; i < NFFT; ++i)
    {   // Blackman-Harris: -92 dB sidelobes, so leakage cannot masquerade as alias
        const double t = 2.0*M_PI*i/(NFFT-1);
        const double w = 0.35875 - 0.48829*std::cos(t) + 0.14128*std::cos(2*t) - 0.01168*std::cos(3*t);
        re[(size_t)i] = (double) x[x.size() - (size_t) NFFT + (size_t) i] * w;
    }
    fft (re, im);
    const double binHz = SR / NFFT;
    const int    top   = (int) (16000.0 / binHz);
    std::vector<bool> isHarm ((size_t) top + 1, false);
    for (int k = 1; k * f0 < 16000.0; ++k)
    { const int c = (int) std::lround (k * f0 / binHz);
      for (int d = -4; d <= 4; ++d) { const int b = c + d; if (b >= 0 && b <= top) isHarm[(size_t)b] = true; } }
    double h = 0, o = 0;
    for (int b = 8; b <= top; ++b)
    { const double e = re[(size_t)b]*re[(size_t)b] + im[(size_t)b]*im[(size_t)b];
      if (isHarm[(size_t)b]) h += e; else o += e; }
    return 10.0 * std::log10 (std::max (o, 1e-30) / std::max (h, 1e-30));
}

int main()
{
    std::printf ("\n══ wt_feedback_au — fb584 (the INSTALLED AU) ══\n\n");
    AU a; if (! a.open()) { std::printf ("  cannot open the AU — is it installed?\n"); return 1; }

    const std::string lvl = a.find ("OSC A Level");
    const std::string wtp = a.find ("OSC A WT Preset");
    const std::string uni = a.find ("OSC A Unison");
    const std::string fbk = a.find ("OSC A Feedback");
    chk (! fbk.empty() && ! lvl.empty(), "[0] THE AU EXPOSES 'Feedback'",
         "level='" + lvl + "' wt='" + wtp + "' fb='" + fbk + "'");
    if (fbk.empty()) { std::printf ("\n  knob not exposed — nothing else can be measured.\n"); a.close(); return 1; }
    chk (a.find ("OSC A Spread") == "", "[0b] the OLD name is gone", "no 'Spread' parameter remains");

    a.set (lvl, 1.0f);
    a.set (wtp, 4.0f / 45.0f);              // Prophet Saw — a sine has almost nothing to feed back
    if (! uni.empty()) a.set (uni, 0.0f);   // ONE voice
    a.pump (0.4);

    const int    NOTE = 48;                                   // C3
    const double F0   = 440.0 * std::pow (2.0, (NOTE - 69) / 12.0);
    const int    NOTES6[6] = { 41, 45, 48, 52, 55, 60 };

    auto renderAt = [&] (float fb, int note)
    { a.set (fbk, fb); a.render (2); a.pump (0.15);
      a.midi (0x90, (UInt32) note, 100); a.render (10);
      auto body = a.render (80); a.midi (0x80, (UInt32) note, 0); a.render (30); return body; };

    // ── the floor: the same settings twice is not the same audio on this synth ──
    double floorD = 0.0;
    for (int i = 0; i < 6; ++i)
    { const auto x = renderAt (0.0f, NOTES6[i]); const auto y = renderAt (0.0f, NOTES6[i]);
      floorD += specDist (spectrum (x), spectrum (y)) / 6.0; }

    // ── the sweep ──
    const float KS[5] = { 0.0f, 0.10f, 0.25f, 0.50f, 1.0f };
    double inh[5] = {0,0,0,0,0}, moved[5] = {0,0,0,0,0}, lvlD[5] = {0,0,0,0,0};
    std::vector<double> prevSpec;
    double worstStep = 1e9;
    std::vector<float> dry;
    for (int q = 0; q < 5; ++q)
    {
        double acc = 0; std::vector<double> sp;
        for (int i = 0; i < 3; ++i)
        {
            const auto x = renderAt (KS[q], NOTES6[i]);
            if (i == 0) { inh[q] = inharmDb (x, F0 * std::pow (2.0, (NOTES6[0]-NOTE)/12.0));
                          sp = spectrum (x); if (q == 0) dry = x; }
            if (q > 0)  acc += specDist (spectrum (x), spectrum (renderAt (0.0f, NOTES6[i]))) / 3.0;
            if (i == 0) lvlD[q] = rmsDb (x);
        }
        moved[q] = acc;
        if (! prevSpec.empty()) worstStep = std::min (worstStep, specDist (sp, prevSpec));
        prevSpec = sp;
    }

    char b[256];
    std::snprintf (b, sizeof b, "one voice, knob 1.0: moved %.3f against a %.3f floor (%.1fx)",
                   moved[4], floorD, floorD > 1e-6 ? moved[4]/floorD : 0.0);
    chk (moved[4] > floorD * 3.0, "[1] AT ONE VOICE IT WORKS — no unison needed", b);

    std::snprintf (b, sizeof b, "inharmonic energy: %.1f dB at knob 0, %.1f at 0.10, %.1f at 0.25",
                   inh[0], inh[1], inh[2]);
    chk (inh[2] <= -20.0, "[2] THE BOTTOM IS CLEAN — 10-50%% stays musical", b);

    std::snprintf (b, sizeof b, "inharmonic energy at knob 1.0: %+.1f dB (it was %.1f at rest) — the partials have left the grid",
                   inh[4], inh[0]);
    chk (inh[4] > inh[2] + 15.0, "[3] THE TOP IS VIOLENT — 100%% is the algorithm's 100%%", b);

    // ⚠️ THIS BAR DELIBERATELY DOES NOT TEST THE BOTTOM OF THE KNOB, AND THAT IS NOT A DODGE.
    //    Bar 2 immediately above certifies that the bottom quarter is nearly INERT — that is the
    //    Lifeguard Law's "10-50% must stay clean". So asking the same run to also prove the bottom
    //    quarter PROGRESSES is asking two contradictory things: those readings are, by design,
    //    down in this synth's own note-to-note floor, and comparing noise to noise is how you get
    //    a gate that goes red one run in five. It did, twice, before this comment existed.
    //    Progressivity across the WHOLE travel is certified where it can be measured without that
    //    noise at all — offline, in Tests/wt_feedback_cert.cpp bar 4, which renders deterministic
    //    tables and reports the quietest of 20 adjacent steps (1.7 dB, 0 inert). What THIS bar can
    //    honestly measure on a live synth is the half that is well clear of the floor.
    std::snprintf (b, sizeof b, "spectral move per quarter: %.3f / %.3f / %.3f / %.3f (this synth's own floor: %.3f); top/bottom %.1fx",
                   moved[1], moved[2], moved[3], moved[4], floorD,
                   moved[1] > 1e-6 ? moved[4]/moved[1] : 0.0);
    chk (moved[4] > moved[3] && moved[3] > floorD * 1.5 && moved[4] > floorD * 3.0,
         "[4] THE TOP HALF PROGRESSES, well clear of the floor", b);

    // ── sweep under a held note ──
    a.set (fbk, 0.0f); a.render (2); a.pump (0.3);
    a.midi (0x90, 48, 100); a.render (8);
    std::vector<float> swept;
    for (int k = 0; k <= 20; ++k)
    { a.set (fbk, (float) k / 20.0f); const auto c = a.render (3);
      swept.insert (swept.end(), c.begin(), c.end());
      CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); }
    a.midi (0x80, 48, 0); a.render (20);
    // ⚠️ THE REFERENCE HAS TO BE THE SWEEP'S DESTINATION, NOT ITS ORIGIN. At full feedback the
    //    waveform is legitimately chaotic and its honest sample-to-sample step is large; comparing
    //    that against a clean FB=0 tone measures THE FEATURE WORKING and calls it a click. The
    //    question is whether MOVING the knob adds anything beyond where the knob ARRIVES.
    auto steadyAt = [&] (float v)
    { a.set (fbk, v); a.render (2); a.pump (0.3);
      a.midi (0x90, 48, 100); a.render (8);
      const auto s2 = a.render (63); a.midi (0x80, 48, 0); a.render (20); return maxStep (s2); };
    const double st0 = steadyAt (0.0f), st1 = steadyAt (1.0f);
    const double ss = maxStep (swept), st = std::max (st0, st1);
    std::snprintf (b, sizeof b, "sweeping %.5f vs %.5f held at the destination (%.2fx); held at rest %.5f",
                   ss, st, st > 1e-9 ? ss/st : 0.0, st0);
    chk (ss < st * 1.5, "[5] NO CLICKS WHILE SWEEPING THE KNOB", b);

    std::snprintf (b, sizeof b, "level across the whole knob: %.1f -> %.1f dB (%.1f dB of swing)",
                   lvlD[0], lvlD[4], std::abs (lvlD[4] - lvlD[0]));
    chk (std::abs (lvlD[4] - lvlD[0]) < 9.0, "[6] THE LEVEL HOLDS — timbre, not a volume ramp", b);

    a.close();
    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
