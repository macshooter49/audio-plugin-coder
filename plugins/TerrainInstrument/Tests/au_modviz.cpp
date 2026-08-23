// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb461 — DO A REAL LFO AND A REAL ENVELOPE ACTUALLY MOVE THE THINGS WE JUST MADE VISIBLE?
//
//    clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/au_modviz.cpp -o /tmp/au_modviz \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  Max: "we need to make sure that our LFOS AND ENV can visually move our params as well."
//
//  THE HOLE THIS FILLS. fb457-460 made the rack cards and the waterfall draw the EFFECTIVE value
//  instead of the knob, and every gate for that drove a SYNTHETIC feed — a hand-written
//  __fxModEff / __wtDisp object. Not one of them ever pushed a REAL LFO or a REAL envelope
//  through the plugin. That is precisely fb373's law: a green harness proves the ENGINE works and
//  never that the plugin REACHES it.
//
//  WHAT IS PROVED HERE, AND WHAT IS NOT. The displayed number is the voice's own member
//  (framePos_, warpAmount_, foldAmountA_, and the spectral amount the morph rebuilds from) —
//  SynthVoice::getWtDisplay() returns those exact members and the processor publishes them. Those
//  members are also what SHAPES THE SOUND. So if a real route moves the sound, it moved the number
//  the display draws. This measures the sound; the publish reading the same member is structural
//  and is asserted by grep in the sweep, not here.
//
//  METHOD. Install a real route by rewriting the AU's own state blob (the same path a saved
//  project takes — au_fx_path.cpp's trick), hold a chord, and measure a LEVEL-INDEPENDENT timbre
//  descriptor (high-band energy ratio) across 16 windows. A static patch barely moves it; a
//  modulated one sweeps it. The control is the SAME render with NO route: it must sit still, or
//  the metric is measuring the amp envelope and would pass on a dead route.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include "SynthModConfig.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static const double SR = 48000.0; static const int BLK = 512;
// 🚨 WARM IS 2, NOT 40. fb179's law: an OWNING envelope drives its destination from ZERO up to
// the knob. With a 5 ms attack the sweep is OVER 5 ms after note-on, so a window that opened at
// 0.43 s measured a CONSTANT and every envelope case failed — my test, not the plugin. The
// measurement now starts at note-on and the modulating envelope is SLOW (see patch()).
static const int WARM = 2, WINS = 16, BLK_PER_WIN = 24;   // 16 x 24 x 512 ≈ 4.1 s from note-on

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
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
    void setNorm (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return; }
      const auto& pi = info.at (it->second);
      AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + v * (pi.maxValue - pi.minValue), 0); }
    static float toNorm (float v, float lo, float hi) { const float p = (v - lo) / (hi - lo); return p <= 0.f ? 0.f : std::pow (p, 0.3f); }

    void pumpHost (double secs) { double t = 0; while (t < secs) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }

    // Install a real route by rewriting the AU's own saved state — the SAME path a project takes.
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

    // hold a chord and return a LEVEL-INDEPENDENT timbre descriptor per window
    std::vector<double> timbreOverTime()
    {
        const int NOTES[3] = { 48, 55, 60 };
        for (int n = 0; n < 3; ++n) MusicDeviceMIDIEvent (au, 0x90, NOTES[n], 100, 0);
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
        std::vector<double> out;
        double hi = 0.0, lo = 0.0; int wblk = 0; float prev = 0.0f;
        for (int b = 0; b < WARM + WINS * BLK_PER_WIN; ++b)
        {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            ts.mSampleTime += BLK;
            if (b % 6 == 0) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.0, false);   // let the morph timer run
            if (b < WARM) { prev = bl[(size_t) BLK - 1]; continue; }
            for (int i = 0; i < BLK; ++i)
            {   // one-pole HP as the "high band": d = x[n]-x[n-1] emphasises harmonics
                const float x = bl[(size_t) i], d = x - prev; prev = x;
                hi += (double) d * d; lo += (double) x * x;
            }
            if (++wblk == BLK_PER_WIN)
            {   // ratio of high-band to total energy — independent of overall level
                out.push_back (lo > 1e-20 ? std::sqrt (hi / lo) : 0.0);
                hi = lo = 0.0; wblk = 0;
            }
        }
        free (abl);
        return out;
    }
};

static std::string routeJson (int src, int dest, float depth)
{ char b[128]; snprintf (b, sizeof b, "[{\"s\":%d,\"d\":%d,\"v\":%.6f}]", src, dest, depth); return b; }

struct Case { const char* label; int src; int dest; int prep; };

int main()
{
    printf ("\n══ fb461 — a REAL LFO and a REAL ENVELOPE, measured on the installed AU ══\n\n");
    const int LFO1 = 0;                    // sources 0..9 are the LFOs
    // Env 2 (the FILTER envelope), not Env 1: the AMP envelope has to stay fast or the note has no
    // attack, and a fast envelope is a STEP, not a sweep. Env 2 is given a 3 s attack below purely
    // so the modulation has somewhere to travel while we watch.
    const int ENV2 = wc::kEnvSrcBase + 1;  // blob encoding is 100 + (envNum-1)

    const Case CASES[] = {
        { "LFO 1 -> WT Position", LFO1, (int) wc::ModDest::Frame,     0 },
        { "ENV 2 -> WT Position", ENV2, (int) wc::ModDest::Frame,     0 },
        { "LFO 1 -> Warp",        LFO1, (int) wc::ModDest::Warp,      1 },
        { "ENV 2 -> Warp",        ENV2, (int) wc::ModDest::Warp,      1 },
        { "LFO 1 -> Fold",        LFO1, (int) wc::ModDest::Fold,      2 },
        { "ENV 2 -> Fold",        ENV2, (int) wc::ModDest::Fold,      2 },
        { "LFO 1 -> Spectral",    LFO1, (int) wc::ModDest::SpectralA, 3 },
        { "ENV 2 -> Spectral",    ENV2, (int) wc::ModDest::SpectralA, 3 },
    };

    // 🚨 EACH CASE GETS ITS OWN PATCH AND ITS OWN CONTROL. The first version enabled Sync warp,
    //    fold AND spectral for every case — and those dominate the timbre, so the FRAME cases were
    //    being measured through a haze of other shaping and read as "barely moves". A shared
    //    control is also the wrong reference: the noise floor of a synced-warp patch is not the
    //    noise floor of a clean one. So: minimal patch per case, and the SAME patch with NO route
    //    is that case's control. That A/B is the only thing that isolates the route.
    enum Prep { P_FRAME = 0, P_WARP, P_FOLD, P_SPEC };
    auto patch = [] (AU& a, int prep) {
        a.setNorm ("Synth Amp Attack",  AU::toNorm (5.f,    1.f, 10000.f));
        a.setNorm ("Synth Amp Decay",   AU::toNorm (2000.f, 1.f, 10000.f));
        a.setNorm ("Synth Amp Sustain", 1.0f);
        a.setNorm ("Synth Amp Release", AU::toNorm (300.f,  1.f, 10000.f));
        // an owning envelope travels 0 -> the KNOB, so every modulated dial needs a non-zero base
        a.setNorm ("Synth OSC A WT Frame",      0.55f);
        a.setNorm ("Synth Filter Env Attack",   AU::toNorm (3000.f, 1.f, 10000.f));   // slow: the sweep must be IN the window
        a.setNorm ("Synth Filter Env Sustain",  1.0f);
        // only the shaping this case is about — anything else masks it
        a.setNorm ("Synth OSC A Warp Mode",     (prep == P_WARP) ? (2.0f / 10.0f) : 0.0f);
        a.setNorm ("Synth OSC A Warp Amount",   (prep == P_WARP) ? 0.35f : 0.0f);
        a.setNorm ("OSC A Fold Shape",          0.0f);
        a.setNorm ("OSC A Fold Amount",         (prep == P_FOLD) ? 0.30f : 0.0f);
        a.setNorm ("OSC A Spectral Type",       (prep == P_SPEC) ? 0.30f : 0.0f);
        a.setNorm ("OSC A Spectral Amount",     (prep == P_SPEC) ? 0.30f : 0.0f);
    };

    int pass = 0, fail = 0;
    for (const auto& c : CASES)
    {
        // three renders: NO route · the route at DEPTH 0 · the route at depth 0.9.
        // The depth-0 render is the negative control that makes the bar defensible — it proves the
        // metric reacts to the MODULATION and not merely to a route existing, and it is what the
        // 4x bar sits above. Without it the bar was an arbitrary constant I had guessed (and an
        // arbitrary absolute floor of 0.002 was failing a case that moved 5x its own control).
        double v[3] = { 0, 0, 0 };
        for (int routed = 0; routed < 3; ++routed)
        {
            AU a; if (! a.open()) return 2;
            patch (a, c.prep);
            const bool ok = a.setRoutes (routed == 0 ? std::string ("[]")
                                                     : routeJson (c.src, c.dest, routed == 1 ? 0.0f : 0.9f));
            if (! ok) { printf ("  FAIL  %s — route not installed\n", c.label); a.close(); ++fail; goto next; }
            a.pumpHost (0.25);
            {
                auto t = a.timbreOverTime(); a.close();
                const double mn = *std::min_element (t.begin(), t.end()), mx = *std::max_element (t.begin(), t.end());
                v[routed] = mx - mn;
            }
        }
        {
            const double ctl = v[0], zero = v[1], mod = v[2];
            const bool moves   = mod  > ctl * 4.0;    // the modulation moves the timbre
            const bool inertAt0 = zero < ctl * 2.0;   // and a depth-0 route does NOT
            const bool ok = moves && inertAt0;
            printf ("  %s  %-22s  none %.5f · depth0 %.5f · depth.9 %.5f   (%.1fx / %.1fx)%s\n",
                    ok ? "ok  " : "FAIL", c.label, ctl, zero, mod,
                    mod / std::max (ctl, 1e-9), zero / std::max (ctl, 1e-9),
                    ok ? "" : (moves ? "   <- depth-0 route MOVED it" : "   <- no movement"));
            ok ? ++pass : ++fail;
        }
        next: ;
    }
    printf ("\n  PASS %d   FAIL %d\n\n", pass, fail);
    return fail ? 1 : 0;
}
