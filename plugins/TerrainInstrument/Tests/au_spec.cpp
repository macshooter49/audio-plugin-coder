// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_spec.cpp — fb467. DOES THE SPECTRAL OVERPASS REACH THE PLUGIN?
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/au_spec.cpp -o /tmp/au_spec \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation \
//            -framework CoreAudio -framework Accelerate
//
//  fb373's law, and it is the reason this file exists: a green offline harness proves the ENGINE
//  works and never that the plugin REACHES it. Tests/spec_cert.cpp calls SpectralMorph::apply
//  directly — it would stay green if the new mode never appeared in the dropdown, if the window
//  params were never registered, if the rebuild gate ignored them, or if picking "Disperse" landed
//  on "Spectral Phaser" (which is EXACTLY what fb373 was: a choice normalised by the dropdown's
//  option count instead of the parameter's).
//
//  Everything below drives the INSTALLED AudioUnit through its own parameter list and its own
//  saved-state blob, and measures the audio that comes out.
//
//  🚨 THE FLOOR IS MEASURED, NOT ASSUMED (fb456/fb462). The first thing this does is render the
//  SAME patch twice and report how far apart the two renders are. Every "these differ" gate must
//  clear that floor by a wide margin, and every "these are the same" gate must sit inside it.
//  Without it, "0.8 dB of change" is a number with no scale.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include "WtFft.h"
#include "SynthModConfig.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static const double SR = 48000.0;
static const int BLK = 512, WARM = 12, MEAS = 120;      // ~1.3 s of measurement after ~0.13 s warm-up
static const int NFFT = 4096;

static int gPass = 0, gFail = 0;
static void gate (bool c, const char* n, const char* d = "")
{ c ? ++gPass : ++gFail; printf ("  %-5s %-56s %s\n", c ? "ok" : "FAIL", n, d); }

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    bool missing = false;

    bool open()
    {
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice;
        d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c || AudioComponentInstanceNew (c, &au) != noErr) { printf ("  !! AU not found\n"); return false; }
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { printf ("  !! init failed\n"); return false; }
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids) { AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char b[256] = {0};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString, b, sizeof b, kCFStringEncodingUTF8);
            else snprintf (b, sizeof b, "%s", pi.name);
            byName[b] = id; info[id] = pi; }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }

    bool has (const std::string& n) const { return byName.count (n) != 0; }
    // set by NORMALISED position (0..1 of the parameter's own declared range)
    void setNorm (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); missing = true; return; }
      const auto& pi = info.at (it->second);
      AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + v * (pi.maxValue - pi.minValue), 0); }
    // set by RAW value in the parameter's own units (harmonic index, mode index, ...)
    void setRaw (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); missing = true; return; }
      AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0); }
    float getRaw (const std::string& n)
    { auto it = byName.find (n); if (it == byName.end()) { missing = true; return -1e9f; }
      AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v); return v; }
    void pump (double secs) { double t = 0; while (t < secs) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }

    bool setRoutes (const std::string& json)
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr) return false;
        CFDictionaryRef dict = (CFDictionaryRef) pl;
        CFStringRef key = CFSTR ("jucePluginState");
        CFDataRef dd = (CFDataRef) CFDictionaryGetValue (dict, key);
        if (dd == nullptr) { CFRelease (pl); return false; }
        const UInt8* p = CFDataGetBytePtr (dd);
        uint32_t magic = 0, len = 0; memcpy (&magic, p, 4); memcpy (&len, p + 4, 4);
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
        memcpy (blob.data(), &m, 4); memcpy (blob.data() + 4, &l, 4); memcpy (blob.data() + 8, xml.data(), xml.size());
        CFMutableDictionaryRef nd = CFDictionaryCreateMutableCopy (nullptr, 0, dict);
        CFDataRef ndata = CFDataCreate (kCFAllocatorDefault, blob.data(), (CFIndex) blob.size());
        CFDictionarySetValue (nd, key, ndata);
        CFPropertyListRef npl = (CFPropertyListRef) nd;
        const OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &npl, sizeof (npl));
        CFRelease (ndata); CFRelease (nd); CFRelease (pl);
        return st == noErr;
    }

    // Hold one note, return the AVERAGE magnitude spectrum (Hann-windowed, 50% overlap).
    std::vector<double> spectrum (int note = 45)
    {
        MusicDeviceMIDIEvent (au, 0x90, note, 100, 0);
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
        std::vector<double> acc ((size_t) (NFFT/2 + 1), 0.0), win ((size_t) NFFT), re ((size_t) (NFFT/2+1)), im ((size_t) (NFFT/2+1));
        std::vector<double> ring; ring.reserve ((size_t) (MEAS * BLK));
        for (int b = 0; b < WARM + MEAS; ++b)
        {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            ts.mSampleTime += BLK;
            if (b % 4 == 0) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.0, false);   // let the 60 Hz morph timer run
            if (b >= WARM) for (int i = 0; i < BLK; ++i) ring.push_back ((double) bl[(size_t) i]);
        }
        MusicDeviceMIDIEvent (au, 0x80, note, 0, 0);
        for (int b = 0; b < 8; ++b)   // release the voice so the next case starts clean
        { abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
          abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
          AudioUnitRenderActionFlags fl = 0; AudioUnitRender (au, &fl, &ts, 0, BLK, abl); ts.mSampleTime += BLK; }
        free (abl);
        int frames = 0;
        for (size_t off = 0; off + (size_t) NFFT <= ring.size(); off += (size_t) NFFT / 2)
        {
            for (int i = 0; i < NFFT; ++i)
                win[(size_t) i] = ring[off + (size_t) i] * (0.5 - 0.5 * std::cos (2.0 * M_PI * i / (NFFT - 1)));
            tw::wtfft::forwardReal (win.data(), NFFT, re.data(), im.data());
            for (int k = 0; k <= NFFT/2; ++k) acc[(size_t) k] += std::hypot (re[(size_t) k], im[(size_t) k]);
            ++frames;
        }
        if (frames > 0) for (auto& v : acc) v /= frames;
        return acc;
    }
};

// relative spectral distance, in dB: 20log10( S|A-B| / S|B| ). 0 dB = the change is as big as the
// signal; -60 dB = one part in a thousand. Level-independent enough for A/B of the same patch.
static double dist (const std::vector<double>& a, const std::vector<double>& b)
{
    double d = 0.0, t = 0.0;
    const size_t lo = (size_t) (60.0 / (SR / NFFT));            // ignore DC / sub-60 Hz rumble
    for (size_t k = lo; k < b.size(); ++k) { d += std::abs (a[k] - b[k]); t += b[k]; }
    return 20.0 * std::log10 (std::max (1e-30, d) / std::max (1e-30, t));
}

// 🚨 JUCE's AU wrapper reports CHOICE parameters in raw units (Spectral Type is 0..8, WT Preset is
//    0..29) but FLOAT parameters NORMALISED 0..1 — "OSC A Spectral Low" advertises 0..1, not 1..512.
//    Setting it with a raw harmonic number clamps to 1.0 = harmonic 512, which is the wide-open
//    default, which is why an early run of this file had every window gate sitting exactly on the
//    repeatability floor. Harmonics go through the parameter's own skew, mirrored here.
static const double SPECWIN_SKEW = std::log (0.5) / std::log ((32.0 - 1.0) / (512.0 - 1.0));
static float harmToNorm (double h)
{ const double p = (std::min (512.0, std::max (1.0, h)) - 1.0) / 511.0;
  return (float) (p <= 0.0 ? 0.0 : std::pow (p, SPECWIN_SKEW)); }

// A bare wavetable oscillator on a RICH table: no fold, no warp, no blur — what we hear is the
// TABLE. 🚨 THE PRESET MATTERS: the boot default is preset 0, a SINE. One harmonic cannot be
// dispersed, stretched or windowed, so every spectral gate below reads exactly its own noise floor
// on it — a whole page of green-looking numbers proving nothing (they read FAIL here, which is how
// this was caught). Preset 4 is Prophet Saw.
static const float RICH_PRESET = 4.0f;
static void plainPatch (AU& au)
{
    au.setRaw  ("Synth OSC A WT Preset", RICH_PRESET);
    au.setNorm ("Synth OSC A WT Frame", 0.5f);
    au.setNorm ("Synth OSC A Warp Amount", 0.0f);
    au.setNorm ("OSC A Fold Amount", 0.0f);
    au.setNorm ("OSC A Blur", 0.0f);
    au.setRaw  ("OSC A Spectral Type", 0.0f);
    au.setNorm ("OSC A Spectral Amount", 0.0f);
    au.setNorm ("OSC A Spectral Low", harmToNorm (1));
    au.setNorm ("OSC A Spectral High", harmToNorm (512));
    au.pump (0.30);
}

int main()
{
    printf ("\n══ fb467 — THE SPECTRAL OVERPASS, ON THE INSTALLED AU ══\n\n");
    AU au;
    if (! au.open()) return 1;

    // ── the params must EXIST under the names the code uses ───────────────────────────────────
    const char* need[] = { "OSC A Spectral Type", "OSC A Spectral Amount",
                           "OSC A Spectral Low", "OSC A Spectral High" };
    bool allThere = true; std::string miss;
    for (const char* n : need) if (! au.has (n)) { allThere = false; miss += std::string (" ") + n; }
    gate (allThere, "P0  the four spectral parameters are on the AU", allThere ? "" : ("missing:" + miss).c_str());
    if (! allThere) { au.close(); return 1; }

    {   // the choice param must carry NINE options, or picking Disperse lands somewhere else
        const auto& pi = au.info.at (au.byName.at ("OSC A Spectral Type"));
        char d[120]; snprintf (d, sizeof d, "declared range %.0f..%.0f", pi.minValue, pi.maxValue);
        gate ((int) pi.maxValue == 8, "P1  Spectral Type has 9 options (None..Disperse)", d);
    }
    {   // the defaults, in the AU's normalised convention: 0 = harmonic 1, 1 = harmonic 512
        const float dLo = au.getRaw ("OSC A Spectral Low"), dHi = au.getRaw ("OSC A Spectral High");
        char d[180]; snprintf (d, sizeof d, "normalised defaults Low %.3f High %.3f (0 = harmonic 1, 1 = harmonic 512)", dLo, dHi);
        gate (std::abs (dLo - 0.0f) < 1e-5f && std::abs (dHi - 1.0f) < 1e-5f,
              "P2  the window boots WIDE OPEN, so no saved patch moves", d);
    }

    // ── F — THE FLOOR. The same patch, rendered twice. ────────────────────────────────────────
    plainPatch (au);
    const auto ref1 = au.spectrum();
    const auto ref2 = au.spectrum();
    const double FLOOR = dist (ref1, ref2);
    { char d[140]; snprintf (d, sizeof d, "the same patch twice reads %+.1f dB — every bar below is set against THIS", FLOOR);
      gate (FLOOR < -20.0, "F0  the render is repeatable enough to compare against", d); }
    const double MOVED = FLOOR + 12.0;      // "changed" must clear the floor by 12 dB (4x)

    // ── D — DISPERSE ──────────────────────────────────────────────────────────────────────────
    plainPatch (au);
    au.setNorm ("OSC A Fold Amount", 0.5f);          // the route by which a crest change becomes audible
    au.pump (0.25);
    const auto foldDry = au.spectrum();

    au.setRaw ("OSC A Spectral Type", 8.0f);          // Disperse
    au.setNorm ("OSC A Spectral Amount", 1.0f);
    au.pump (0.35);
    const auto disp = au.spectrum();
    { char d[140]; snprintf (d, sizeof d, "%+.1f dB vs the same patch with the morph off (floor %+.1f)", dist (disp, foldDry), FLOOR);
      gate (dist (disp, foldDry) > MOVED, "D1  DISPERSE reaches the plugin and changes the sound", d); }

    au.setRaw ("OSC A Spectral Type", 7.0f);          // its NEIGHBOUR in the menu
    au.pump (0.35);
    const auto phaser = au.spectrum();
    { char d[140]; snprintf (d, sizeof d, "mode 8 vs mode 7 = %+.1f dB (floor %+.1f)", dist (disp, phaser), FLOOR);
      gate (dist (disp, phaser) > MOVED, "D2  mode 8 is NOT mode 7 (the fb373 off-by-one)", d); }

    // ── W — THE PARTIAL WINDOW ────────────────────────────────────────────────────────────────
    plainPatch (au);
    au.setRaw  ("OSC A Spectral Type", 1.0f);         // Harmonic Stretch — a big, obvious morph
    au.setNorm ("OSC A Spectral Amount", 1.0f);
    au.pump (0.35);
    const auto wide = au.spectrum();

    au.setNorm ("OSC A Spectral High", harmToNorm (16));
    au.pump (0.35);
    const auto narrow = au.spectrum();
    { char d[140]; snprintf (d, sizeof d, "High 512 vs High 16 = %+.1f dB (floor %+.1f)", dist (narrow, wide), FLOOR);
      gate (dist (narrow, wide) > MOVED, "W1  the HIGH edge reaches the plugin", d); }

    au.setNorm ("OSC A Spectral High", harmToNorm (512));
    au.setNorm ("OSC A Spectral Low", harmToNorm (12));
    au.pump (0.35);
    const auto lowShut = au.spectrum();
    { char d[140]; snprintf (d, sizeof d, "Low 400 vs High 512 wide = %+.1f dB (floor %+.1f)", dist (lowShut, wide), FLOOR);
      gate (dist (lowShut, wide) > MOVED, "W2  the LOW edge reaches the plugin", d); }

    // W3 — the DIRECTIONAL one, and the only gate here that could not pass by accident: a window
    // that excludes every partial the table actually has must be the SAME SOUND as no morph at all.
    plainPatch (au);
    const auto noMorph = au.spectrum();
    au.setRaw  ("OSC A Spectral Type", 1.0f);
    au.setNorm ("OSC A Spectral Amount", 1.0f);
    au.setNorm ("OSC A Spectral Low", harmToNorm (500));
    au.setNorm ("OSC A Spectral High", harmToNorm (512));
    au.pump (0.35);
    const auto shut = au.spectrum();
    // 🚨 TWO conditions, because the first one alone passes on ANY patch where the morph does
    //    nothing — including a sine, which is what the boot preset is. The pair is the gate: the
    //    morph must be doing something wide open, AND the window must take it away.
    const double wideMoved = dist (wide, noMorph), shutMoved = dist (shut, noMorph);
    { char d[190]; snprintf (d, sizeof d, "wide open moves %+.1f dB; windowed to 500..512 moves %+.1f dB (floor %+.1f)",
                             wideMoved, shutMoved, FLOOR);
      gate (wideMoved > MOVED && shutMoved < FLOOR + 6.0,
            "W3  a window that excludes everything IS no morph", d); }

    // ── M — THE WINDOW AS A MODULATION DESTINATION ────────────────────────────────────────────
    //     dest ints come from SynthModConfig.h, so a renumber breaks this test loudly.
    {
        plainPatch (au);
        au.setRaw  ("OSC A Spectral Type", 1.0f);
        au.setNorm ("OSC A Spectral Amount", 1.0f);
        au.setNorm ("OSC A Spectral High", harmToNorm (24));   // a base the LFO can sweep UP from
        au.pump (0.25);
        char j0[128], j1[128];
        snprintf (j0, sizeof j0, "[{\"s\":0,\"d\":%d,\"v\":0.000000}]", (int) wc::ModDest::SpecHiA);
        snprintf (j1, sizeof j1, "[{\"s\":0,\"d\":%d,\"v\":0.900000}]", (int) wc::ModDest::SpecHiA);
        au.setRoutes (j0); au.pump (0.35);
        const auto d0 = au.spectrum();
        au.setRoutes (j1); au.pump (0.35);
        const auto d9 = au.spectrum();
        au.setRoutes ("[]"); au.pump (0.2);
        char d[180]; snprintf (d, sizeof d, "dest %d: depth 0.9 vs depth 0 = %+.1f dB (floor %+.1f)",
                               (int) wc::ModDest::SpecHiA, dist (d9, d0), FLOOR);
        gate (dist (d9, d0) > MOVED, "M1  an LFO on the window's HIGH edge moves the sound", d);
    }

    // ══ fb469 — THE BLUR TWIN, ON THE INSTALLED PLUGIN ═══════════════════════════════════════
    //  Offline, blur on Square used to drag the spectral centroid from 13.97 down to 6.44 — it was
    //  HOLLOWING the sound, which is what Max heard as "blur really doesn't do much". With the
    //  phase-aligned twin the same blur takes it to 17.55. The SIGN of that move is the test: it is
    //  a 2.7x swing, so no amount of colouring from the unison, the filters or the amp envelope can
    //  flip it. The two tables the twin is REFUSED on must be untouched.
    {
        auto centroidNow = [&] (int preset, float blurNorm) {
            plainPatch (au);
            au.setRaw  ("Synth OSC A WT Preset", (float) preset);
            au.setNorm ("Synth OSC A WT Frame", 0.5f);
            au.setNorm ("OSC A Blur", blurNorm);
            au.pump (0.45);                                  // the twin is built on the 60 Hz timer
            const auto sp = au.spectrum (45);
            double num = 0.0, den = 0.0;
            const double f0 = 110.0, bin = SR / NFFT;        // note 45 = 110 Hz
            for (int h = 1; h <= 60; ++h)
            { const int k = (int) std::lround (h * f0 / bin);
              if (k <= 0 || k >= (int) sp.size()) continue;
              double a = 0.0; for (int j = -2; j <= 2; ++j) if (k+j > 0 && k+j < (int) sp.size()) a = std::max (a, sp[(size_t) (k+j)]);
              num += h * a; den += a; }
            return den > 0 ? num / den : 0.0; };

        const double sqDry = centroidNow (2, 0.0f), sqBlur = centroidNow (2, 1.0f);
        char d[220]; snprintf (d, sizeof d, "Square: centroid %.2f dry -> %.2f blurred (it used to fall to about half)", sqDry, sqBlur);
        gate (sqBlur > sqDry * 0.95, "T1  blur no longer HOLLOWS a table that cancels (Square)", d);

        const double sdDry = centroidNow (22, 0.0f), sdBlur = centroidNow (22, 1.0f);
        snprintf (d, sizeof d, "SpectralDrift: centroid %.2f dry -> %.2f blurred, and blur still moves it", sdDry, sdBlur);
        gate (std::abs (sdBlur - sdDry) > 0.05, "T2  the tables the twin is REFUSED on still blur", d);
    }

    gate (! au.missing, "P3  every parameter this test asked for exists by name");
    au.close();
    printf ("\n  PASS %d   FAIL %d\n\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
