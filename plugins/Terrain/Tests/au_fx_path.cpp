// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb439 — DOES THE PLUGIN REACH THE ENGINE?  The first harness in this tree that renders the
//  REAL, INSTALLED plugin instead of an engine header behind a shim.
//
//    clang++ -std=c++17 -O2 -I Tests/shim -I Source Tests/au_fx_path.cpp -o /tmp/au_fx_path \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//    /tmp/au_fx_path                 the FULL run: the 28 fb439-447 gates + fb453's 184-cell matrix
//    /tmp/au_fx_path --quick         one representative knob per kind instead of all 184
//    /tmp/au_fx_path --kind 14       one device
//    /tmp/au_fx_path --mutate        aim every route at the ADJACENT knob: every cell MUST go red
//    /tmp/au_fx_path --no-matrix     everything except the matrix
//
//  🔑 WHY THIS EXISTS (fb373, restated by Max at fb439): every one of the four fx4 devices passed
//     its own engine cert — the Equalizer's 147/147 among them — and all four were stone dead in
//     the plugin, because the chain never emitted an entry for their kind. An engine harness
//     CANNOT see that. This one can: it loads the installed AU, plays a note, and measures the
//     OUTPUT with the device out of the chain vs. in it with its controls pushed.
//
//  It reports, per device, the three numbers an ear actually integrates — level, spectrum and
//  stereo — so "it's in the chain" and "it does something" are separate, visible verdicts.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <chrono>
#include <complex>

// fb453 — the GENERATED map of every rack dial's parameter, [kind][knob]. The matrix below walks
// exactly this table, so the harness and the plugin cannot disagree about which knob is which.
#include "SynthModConfig.h"    // wc::fxModDest — the SHIPPED destination arithmetic, not a copy
#include "fx_mod_ids.inc"

static const double SR = 48000.0;
static const int    BLK = 512;
static const int    NBLK = 90;          // ~0.96 s
static const int    SKIP = 20;          // drop the attack/settling blocks

// ── tiny iterative radix-2 FFT ────────────────────────────────────────────────────────────────
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

struct Render { std::vector<float> L, R; };

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::vector<AudioUnitParameterID> idList;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;

    bool open()
    {
        AudioComponentDescription d {};
        d.componentType         = kAudioUnitType_MusicDevice;
        d.componentSubType      = 'Tern';
        d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c) { printf ("  !! AU aumu/Tern/Wvcr not found — is it installed?\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { printf ("  !! instantiate failed\n"); return false; }

        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4;
        f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { printf ("  !! AudioUnitInitialize failed\n"); return false; }

        UInt32 sz = 0; Boolean w = false;
        if (AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w) != noErr || sz == 0)
        { printf ("  !! no parameter list\n"); return false; }
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        idList = ids;
        for (auto id : ids) {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            std::string nm;
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) {
                char buf[256] = {0};
                CFStringGetCString (pi.cfNameString, buf, sizeof buf, kCFStringEncodingUTF8);
                nm = buf;
            } else nm = pi.name;
            byName[nm] = id; info[id] = pi;
        }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }

    bool has (const std::string& n) const { return byName.count (n) > 0; }
    // norm 0..1 mapped onto the parameter's own reported range
    bool set (const std::string& n, float norm)
    {
        auto it = byName.find (n); if (it == byName.end()) return false;
        const auto& pi = info.at (it->second);
        const float v = pi.minValue + norm * (pi.maxValue - pi.minValue);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    float get (const std::string& n)
    {
        auto it = byName.find (n); if (it == byName.end()) return NAN;
        AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v);
        return v;
    }

    // ══ fb453 — ADDRESSING A PARAMETER BY ITS OWN ID, not by its display name ══════════════════
    // Everything above this line talks to the plugin through the host-visible NAME ("Utility
    // Gain"). The modulation matrix cannot: its map (`fx_mod_ids.inc`) is authored in parameter
    // IDs ("SYN_UTL_GAIN"), because that is what `cacheFxModRefs()` resolves and what
    // `instPrefix()` builds. Re-typing 1,104 display names here would be 1,104 chances to gate the
    // wrong knob — the exact fault the generated table exists to prevent.
    //
    // JUCE's AU wrapper derives the AudioUnitParameterID from the id string and nothing else:
    //   generateAUParameterID() = String::hashCode() with the sign bit cleared
    //   (juce_audio_plugin_client_AU_1.mm:2387; JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS = 1),
    //   String::hashCode() = HashGenerator<uint32> = `r = 31*r + codepoint`.
    // So the id IS the address, and "does this id resolve?" becomes a question the host can ask —
    // which is how gate D reads the plugin's own parameter registry without a Source change.
    static AudioUnitParameterID pid (const std::string& juceParamId)
    {
        uint32_t r = 0;
        for (unsigned char ch : juceParamId) r = 31u * r + (uint32_t) ch;
        return (AudioUnitParameterID) (r & 0x7FFFFFFFu);
    }
    bool  hasP (const std::string& id) const { return info.count (pid (id)) > 0; }
    // The AU value, verbatim: for a JUCE float parameter that is the NORMALISED 0..1 value (and
    // every one of the 184 rack dials is declared NormalisableRange<float>(0,1), so normalised ==
    // the value the rack reads); for a choice parameter it is the INDEX (max = numSteps-1).
    bool  setP (const std::string& id, float v)
    { return AudioUnitSetParameter (au, pid (id), kAudioUnitScope_Global, 0, v, 0) == noErr; }
    float getP (const std::string& id)
    { AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, pid (id), kAudioUnitScope_Global, 0, &v); return v; }
    float maxP (const std::string& id) const
    { auto it = info.find (pid (id)); return it == info.end() ? 0.0f : it->second.maxValue; }

    // ══ THE ROUTE, INSTALLED THROUGH THE PLUGIN'S OWN setSynthMod BRIDGE ══════════════════════
    // `setSynthModMatrix()` is reachable from exactly two places: the WebView's `setSynthMod`
    // native function, and `setStateInformation()` — which reads the SAME JSON out of the saved
    // state under "synModJson" and hands it to the SAME parser (PluginProcessor.cpp:12396). A
    // host cannot call a WebView native fn; it CAN restore state, and that is a real user path
    // (open a project that has FX-rack routes in it). So: read the AU's ClassInfo, rewrite one
    // attribute of the state XML, write it back. Nothing here reaches around the bridge.
    //
    // The blob under "jucePluginState" is copyXmlToBinary()'s: [magic 0x21324356][len][XML][NUL].
    bool setRoutes (const std::string& json)
    {
        CFPropertyListRef pl = nullptr; UInt32 psz = sizeof (pl);
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &psz) != noErr || pl == nullptr)
            return false;
        CFDictionaryRef dict = (CFDictionaryRef) pl;
        CFStringRef key = CFSTR ("jucePluginState");           // JUCE_STATE_DICTIONARY_KEY
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

    // ══ RUN THE HOST'S RUN LOOP ══════════════════════════════════════════════════════════════
    // Several engines in this rack are ALLOCATED ON THE MESSAGE THREAD the first time the audio
    // thread asks for one — the granular rings (fb362), the tape transports (fb365), the pooled
    // reverbs (fb352). The audio thread raises `*WantBuild_` and `timerCallback()` (60 Hz) builds.
    // A harness that renders once and measures therefore measures the PASS-THROUGH slot forever:
    // the whole Granular and Tape rows read Δ = 0.00 dB and every gate on them is vacuous. That is
    // not a plugin fault, it is a HOST fault — a real host runs a run loop. So this one does too.
    void pumpHost (double seconds)
    { const double t1 = seconds; double t = 0.0;
      while (t < t1) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.02, false); t += 0.02; } }

    Render render (int nblk = NBLK, int skip = SKIP)
    {
        MusicDeviceMIDIEvent (au, 0x90, 48, 100, 0);      // a low-ish note: energy in every band
        MusicDeviceMIDIEvent (au, 0x90, 55, 100, 0);
        MusicDeviceMIDIEvent (au, 0x90, 64, 100, 0);
        Render out;
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = 0;
        for (int b = 0; b < nblk; ++b) {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            if (AudioUnitRender (au, &fl, &ts, 0, BLK, abl) != noErr) break;
            ts.mSampleTime += BLK;
            if (b >= skip) { out.L.insert (out.L.end(), bl.begin(), bl.end());
                             out.R.insert (out.R.end(), br.begin(), br.end()); }
        }
        free (abl);
        return out;
    }
};

static double rmsdb (const std::vector<float>& v)
{
    double a = 0; for (float x : v) a += (double) x * x;
    return 20.0 * std::log10 (std::sqrt (a / std::max<size_t> (1, v.size())) + 1e-12);
}
static double crest (const std::vector<float>& v)
{
    double a = 0, pk = 0; for (float x : v) { a += (double) x * x; pk = std::max (pk, (double) std::fabs (x)); }
    const double r = std::sqrt (a / std::max<size_t> (1, v.size()));
    return 20.0 * std::log10 ((pk + 1e-12) / (r + 1e-12));
}
static double sideRatio (const Render& r)
{
    double m = 0, s = 0;
    for (size_t i = 0; i < r.L.size(); ++i) {
        const double mid = 0.5 * (r.L[i] + r.R[i]), sd = 0.5 * (r.L[i] - r.R[i]);
        m += mid * mid; s += sd * sd;
    }
    return 10.0 * std::log10 ((s + 1e-15) / (m + 1e-15));
}
// average magnitude spectrum in dB over 32 log bands, 20 Hz .. 18 kHz
static std::vector<double> spec (const std::vector<float>& v)
{
    const size_t N = 4096;
    std::vector<double> acc (N / 2, 0.0); int frames = 0;
    for (size_t off = 0; off + N <= v.size(); off += N) {
        std::vector<std::complex<double>> a (N);
        for (size_t i = 0; i < N; ++i) {
            const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) (N - 1));
            a[i] = std::complex<double> (v[off + i] * w, 0.0);
        }
        fft (a);
        for (size_t i = 0; i < N / 2; ++i) acc[i] += std::abs (a[i]);
        ++frames;
    }
    if (frames == 0) frames = 1;
    std::vector<double> band (32, 0.0);
    for (int b = 0; b < 32; ++b) {
        const double f0 = 20.0 * std::pow (900.0, (double) b / 32.0);
        const double f1 = 20.0 * std::pow (900.0, (double) (b + 1) / 32.0);
        const size_t i0 = (size_t) std::max (1.0, f0 / (SR / (double) N));
        const size_t i1 = (size_t) std::min ((double) (N / 2 - 1), f1 / (SR / (double) N));
        double e = 0; int n = 0;
        for (size_t i = i0; i <= i1; ++i) { e += acc[i] / frames; ++n; }
        band[b] = 20.0 * std::log10 (e / std::max (1, n) + 1e-12);
    }
    return band;
}
static double specDist (const std::vector<double>& a, const std::vector<double>& b)
{
    double s = 0; int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) { s += std::fabs (a[i] - b[i]); ++n; }
    return s / std::max (1, n);
}

static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& detail)
{
    if (ok) { ++npass; printf ("  ok   %-58s %s\n", what, detail.c_str()); }
    else    { ++nfail; printf ("  FAIL %-58s %s\n", what, detail.c_str()); }
}

struct Dev { const char* label; const char* pfx; const char* knobs[4]; };

// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb453 · TASK 5 — THE EQUIVALENCE MATRIX.  Below this line is the only thing in this tree that
//  can say the FX rack's modulation is REAL: for every live (kind, knob) cell, a route that
//  supplies +x must be INDISTINGUISHABLE from turning that knob by +x, on the installed AU.
//
//  A mis-mapped knob, a hole in the generated table, a wrong DestInfo scale, a sign error, a knob
//  that resolved to nullptr — every one of them shows up here as two waveforms that do not null,
//  and nowhere else. (fb373: a green engine harness proves the ENGINE works and never that the
//  plugin REACHES it. fb425: sweep the full matrix, not a sample.)
// ══════════════════════════════════════════════════════════════════════════════════════════════

static int juceClampInst (int i) { return i < 0 ? 0 : (i > 5 ? 5 : i); }

static double nowSec()
{ using namespace std::chrono; return duration<double> (steady_clock::now().time_since_epoch()).count(); }

// The destination integer comes from the PLUGIN'S OWN expression. It was a hand-copied
// `694 + (kind*6 + inst)*12 + knob` here, which is a second authority on the one number the whole
// matrix pivots on — and `694` is itself a derived constant (ModDest::FxModBase). This harness
// already builds with `-I Source`, so it can just call the function the rack calls.
static int destOf (int kind, int inst0, int knob) { return wc::fxModDest (kind, inst0, knob); }
// ...and pinned, so "use the shipped expression" cannot quietly become "use a different one".
// 694 is ModDest::FxModBase, the anchor every saved project's route integers are relative to.
static_assert (wc::fxModDest (0, 0, 0) == 694,      "FxModBase moved — saved FX-rack routes would shift");
static_assert (wc::fxModDest (14, 0, 0) == 1702,    "the kind/instance stride moved");
static_assert (wc::fxModDest (15, 5, 11) == 693 + 16 * 6 * 12, "the last cell is the last dest");

// 🚨 EVERY NUMBER IN THIS EXPERIMENT IS AN EXACT BINARY FRACTION, and that is the whole design.
//    The route's depth reaches the plugin as DECIMAL TEXT inside the state blob and comes back as
//    a double; the knob's value goes in as a float through the host. If the two paths do not agree
//    to the last bit, the null is measuring the harness's own rounding instead of the plugin.
//    0.35f + 0.25f is 0.599999994 and the literal 0.60f is 0.600000024 — three parts in a hundred
//    million, invisible on most knobs (−230 dB) and NOT invisible on a knob with gain behind it:
//    the Delay's Time is exp(ln(8000)·x) inside a feedback loop, so that same 3e-8 comes back out
//    at −60 dB. Measured, not hypothesised. 0.25 / 0.25 / 0.50 are exact in binary AND print and
//    parse exactly, so route-and-knob differ by the CODE PATH and by nothing else.
static const float  kBase = 0.25f;      // where every rack dial sits for the sweep
static const float  kOff  = 0.25f;      // what the route supplies
static const float  kTop  = 0.50f;      // kBase + kOff — where the knob goes instead
static const double kEqGate  = -100.0;  // dB below the render's own RMS: "the route EQUALS the knob"
// ── "it did something at all", and why it is TWO numbers rather than a threshold ──────────────
// The equivalence half is vacuous on its own: a dial that does nothing at the point being probed
// gives B = C = A and a perfect null for the wrong reason. What makes a cell INFORMATIVE is not
// that the move was loud, it is that the null sits far BELOW the move — so the gate is a margin,
// with a floor under it so a genuinely dead cell can never sneak through. A dead cell measures
// −190…−225 dBr here (that is the render's own numerical floor), so a floor at −100 dBr is still
// ninety-odd dB clear of "nothing happened". cmp.ROUND is the case that made this the rule: it
// moves the sound by −61.8 dBr and nulls at −215.8 dBr — 154 dB of margin, an entirely proven
// mapping, and a flat −60 dB threshold would have called it a failure for being 1.8 dB too quiet.
static const double kAudFloor = -100.0;  // the mechanism must plainly engage
static const double kAudMargin =  60.0;  // ...and the null must sit this far below the move
// Many back-panel slots are bound PER TYPE — the Reverb's Mod pair is only real on the types that
// define modulation, the Splitter's lane-3 slots are unbound in a 2-lane Type (fb447, "the slots
// that go unbound"). Sampling 4 of a device's 8 or 16 Types is exactly the mistake fb425 is about,
// and it cost three Splitter cells a red row on the run before this one. Sweep them all.
static const int    kTypeTries = 16;

// ── THE OPERATING POINTS, in the order a cell tries them. Only a cell that stayed INERT at one
//    reaches the next, so 182 of the 184 never leave op 0 and the extra stages cost nothing.
struct MtxOp { float base; bool stereo; const char* note; };
static const MtxOp kMtxOps[3] = {
    { kBase, false, "" },
    { 0.0f,  false, "   [probed 0.00->0.25: it is on a plateau at 0.25]" },
    { kBase, true,  "   [needed a STEREO-ASYMMETRIC probe: a mono source cannot excite it]" },
};
   // saturate.SIG

// RMS and null-depth over BOTH channels — a stereo-only move (Utility Image, Widen) is invisible
// on L alone, and half the rack's knobs are stereo.
static double rms2 (const Render& r)
{ double a = 0; for (float x : r.L) a += (double) x * x; for (float x : r.R) a += (double) x * x;
  return 20.0 * std::log10 (std::sqrt (a / std::max<size_t> (1, r.L.size() + r.R.size())) + 1e-12); }
static double null2 (const Render& a, const Render& b)
{ if (a.L.size() != b.L.size() || a.L.empty()) return 999.0;
  double s = 0; for (size_t i = 0; i < a.L.size(); ++i) { const double d1 = (double) a.L[i] - b.L[i], d2 = (double) a.R[i] - b.R[i]; s += d1 * d1 + d2 * d2; }
  return 20.0 * std::log10 (std::sqrt (s / (double) (2 * a.L.size())) + 1e-12); }
// the number the gates actually read: how far the difference sits BELOW the signal itself
static double rel (const Render& a, const Render& b) { return null2 (a, b) - rms2 (b); }

// One measurement.  🔑 A FRESH AU EVERY TIME, and that is not caution — it is the only way this
// gate can exist. Measured on this build: two renders of an IDENTICAL configuration on ONE
// instance diverge by as little as −58 dB and as much as −8 dB (i.e. a different sound; something
// the AU's own Reset does not clear survives between renders). Two FRESH instances of an identical
// configuration are BIT-IDENTICAL — exact zeros. The equivalence gate compares waveforms sample by
// sample, so it stands on that determinism, and the harness gates the determinism first.
struct MtxCfg
{
    int   kind = 0;
    int   inst = 0;                       // 0-based instance; ids take "" then "2".."6"
    int   type = 0;                       // the device's TYPE choice index
    bool  stereoUp = false;               // put a stereo-MAKER ahead of the device under test
    bool  mixFull = true;                 // Mix (knob 3) at 1.0 rather than kBase
    std::map<int, float> knobs;           // knob index -> value, overriding the default
    int   probe = -1;                     // the knob this measurement is ABOUT (setup decisions only)
    std::vector<std::pair<std::string, float>> extra;   // any other parameter
    std::string routes = "[]";            // the synModJson blob
    bool  warmup = true;                  // throw-away pass + run loop, so lazy engines exist
    int   nblk = NBLK, skip = SKIP;
};

static std::map<AudioUnitParameterID, AudioUnitParameterInfo> gInfo;   // one snapshot, for gate D
static bool gInfoDone = false;

static Render mtxRender (const MtxCfg& c)
{
    AU a;
    if (! a.open()) { printf ("  !! could not instantiate the AU for a matrix render\n"); exit (2); }
    if (! gInfoDone) { gInfo = a.info; gInfoDone = true; }

    // ── LFO 1, PARKED. Square at phase 0 is +1.0 exactly and STAYS +1.0: the plateau means the
    //    0.01 Hz rate floor (0.0096 of a cycle across the render) cannot move it, and the shape
    //    has no slope to interpolate. Depth 1.0 is the per-LFO master, which multiplies the route
    //    (`r.depth * master`, PluginProcessor.cpp:~7985). So a route of v = 0.25 delivers exactly
    //    +0.25 to the knob — and the calibration gate below MEASURES that rather than trusting it.
    a.setP ("LFO1_SHAPE", 4.0f);   // choice(11): 4 = SQUARE
    a.setP ("LFO1_SYNC",  0.0f);
    a.setP ("LFO1_RATE",  0.0f);   // normalised 0 = the 0.01 Hz floor
    a.setP ("LFO1_PHASE", 0.0f);
    a.setP ("LFO1_DEPTH", 1.0f);

    // The instance suffix is the plugin's own rule (instPrefix(), index.html; `p = "SYN_UTL" + sfx
    // + "_"`, PluginProcessor.cpp): instance 1 is bare, 2..6 carry their number.
    const std::string T = std::string (kFxModTag[c.kind]) + (c.inst == 0 ? std::string() : std::to_string (c.inst + 1));
    a.setP (T + "_ACTIVE", 1.0f);
    a.setP (T + "_POWER",  1.0f);
    static const char* const SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
    for (auto* sc : SRC) a.setP (T + "_" + sc, 1.0f);     // fb362 — devices arrive UNROUTED
    a.setP (T + "_TYPE", (float) c.type);
    // ── the per-kind toggles that decide whether a whole GROUP of knobs is live at all. Without
    //    these the Delay's Time is on the tempo grid (the knob is ignored) and the Tape's echo
    //    section is switched off entirely (fb366, "the echo is opt-in") — a dead knob for a
    //    reason that has nothing to do with modulation.
    if (c.kind == 1)
    {
        a.setP (T + "_SYNC", 0.0f);                          // otherwise Time is on the tempo grid
        // L/R LINK is the Delay's own fork: LINKED, "Time R" is not read at all and "Spread" is
        // what offsets the two sides; UNLINKED it is the other way round. So the link follows the
        // knob under test — the shipped default for every dial except the one that only exists
        // when the sides are free.
        a.setP (T + "_LINK", c.probe == 11 ? 0.0f : 1.0f);
    }
    if (c.kind == 4) { a.setP (T + "_DELAY", 1.0f); a.setP (T + "_SYNC", 0.0f); }
    // 🚨 RESOLVE BY PARAMETER, NOT BY DIAL. The Delay's front "Time" (knob 0) and its back
    //    "Time L" (knob 10) ARE one parameter, so writing the dials in index order writes
    //    SYN_DLY_TIME twice and the SECOND write wins — which silently threw away the override
    //    on knob 0 and made its "moved the knob" render identical to the flat one. (It cost this
    //    harness a red cell to find, which is the point of a matrix that sweeps every cell.)
    {
        std::map<std::string, float> want;
        for (int n = 0; n < 12; ++n)
            if (kFxModLeaf[c.kind][n]) want[kFxModLeaf[c.kind][n]] = (n == 3 && c.mixFull) ? 1.0f : kBase;
        for (const auto& kv : c.knobs)
            if (kv.first >= 0 && kv.first < 12 && kFxModLeaf[c.kind][kv.first]) want[kFxModLeaf[c.kind][kv.first]] = kv.second;
        for (const auto& kv : want) a.setP (T + "_" + kv.first, kv.second);
    }
    // ── THE MONO BLIND SPOT ───────────────────────────────────────────────────────────────────
    // 🚨 A DIAL WHOSE ONLY EFFECT IS ON THE INPUT'S L/R ASYMMETRY IS INVISIBLE TO A MONO PROBE,
    //    and this probe is exactly mono: the pre-existing fb439 rows show it without meaning to —
    //    Δside comes out as the exact negative of Δlevel on five of the seven devices, which is
    //    what you get when the dry side channel is a constant floor because L and R are identical.
    //    cmp.TIE is the one dial of the 184 that landed in that spot. It is the compressor's
    //    detector STEREO LINK: `mx = max(aL,aR2); d0 = tie*mx + (1-tie)*aL; d1 = tie*mx +
    //    (1-tie)*aR2` (TerrainCompressFx.h:571-573). With aL == aR2, mx == aL == aR2 and
    //    d0 = d1 = aL FOR EVERY VALUE OF TIE — an algebraic identity, not a dead knob. So a cell
    //    that survives both mono operating points gets a stereo-asymmetric source: a Widen (or,
    //    when Widen itself is under test, a Utility on Rotate + Haas) placed AHEAD in the chain.
    if (c.stereoUp)
    {
        const std::string U = (c.kind == 10) ? "SYN_UTL" : "SYN_WID";
        a.setP (U + "_ACTIVE", 1.0f); a.setP (U + "_POWER", 1.0f); a.setP (U + "_MIX", 1.0f);
        for (auto* sc : SRC) a.setP (U + "_" + sc, 1.0f);
        a.setP (U + "_RANK", 0.20f);                       // upstream of...
        a.setP (T + "_RANK", 0.80f);                       // ...the device under test
        if (c.kind == 10) { a.setP ("SYN_UTL_B6", 0.90f); a.setP ("SYN_UTL_B7", 0.90f); }   // Rotate + Haas
        else { a.setP ("SYN_WID_AMOUNT", 0.90f); a.setP ("SYN_WID_WIDTH", 0.90f); a.setP ("SYN_WID_SPREAD", 0.90f); }
    }
    for (const auto& e : c.extra) a.setP (e.first, e.second);
    a.setRoutes (c.routes);
    if (c.warmup) { a.render (24, 24); a.pumpHost (0.10); }   // raise *WantBuild_, let the timer build
    Render r = a.render (c.nblk, c.skip);
    a.close();
    return r;
}

static std::string routeJson (int dest, float depth, int src = 0)
{ char b[96]; snprintf (b, sizeof b, "[{\"s\":%d,\"d\":%d,\"v\":%.6f}]", src, dest, depth); return b; }

struct CellFail { int kind, knob; std::string leaf; double eq, aud; };

// the canonical kind order (PluginProcessor.h:1660) — the same order kFxModTag/kFxModLeaf carry
static const char* const kKindName[16] = { "reverb","delay","saturate","granular","tape","flt","cho",
                                           "fla","pha","eqz","wid","cmp","ott","bod","utl","spl" };

int main (int argc, char** argv)
{
    // the full run is minutes long and is normally piped to a log; block buffering would hide
    // every line of it until the process exits.
    setvbuf (stdout, nullptr, _IOLBF, 0);
    printf ("\n══ fb439 — REAL-PLUGIN PATH TEST (the installed AU, not an engine header) ══\n\n");

    // fb453 · Task 5 switches.  The FULL 184-cell matrix is the DEFAULT (fb425 — sweep the whole
    // matrix, not a sample); the rest exist to shorten a debug loop or to prove the gate's teeth.
    bool  mtxRun = true, mtxQuick = false, mtxMutate = false;
    int   mtxOnlyKind = -1;
    int   mtxInst = 0;                                       // 0-based; --inst is 1-based like the UI
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if      (a == "--no-matrix") mtxRun = false;
        else if (a == "--quick")     mtxQuick = true;            // one knob per kind (16 cells)
        else if (a == "--mutate")    mtxMutate = true;           // route the ADJACENT knob: must FAIL
        else if (a == "--kind" && i + 1 < argc) mtxOnlyKind = atoi (argv[++i]);
        else if (a == "--inst" && i + 1 < argc) mtxInst = juceClampInst (atoi (argv[++i]) - 1);   // 1..6 -> 0..5
        else if (a == "--help") { printf ("  [--no-matrix] [--quick] [--kind N] [--inst 1..6] [--mutate]\n"); return 0; }
    }
    const double t5t0 = nowSec();

    // ── the control render: no fx4 device in the chain at all ─────────────────────────────────
    AU ctl; if (! ctl.open()) return 2;
    const Render dry = ctl.render();
    const double dryRms = rmsdb (dry.L);
    const std::vector<double> drySpec = spec (dry.L);
    chk (dry.L.size() > 1000 && dryRms > -60.0, "the probe itself makes sound (harness is not measuring silence)",
         "rms=" + std::to_string (dryRms) + " dB, " + std::to_string (dry.L.size()) + " samples");
    chk (ctl.has ("Equalizer In Chain") && ctl.has ("Widen In Chain")
      && ctl.has ("Compress In Chain") && ctl.has ("Multiband In Chain"),
         "all four devices expose In Chain / Power to the host", "");
    ctl.close();
    if (dryRms <= -60.0) { printf ("\n  probe is silent — cannot measure. STOP.\n"); return 2; }

    // ── each device: put it in the chain, power it, push its controls, measure the output ─────
    const Dev devs[7] = {
        { "Equalizer", "Equalizer", { "Equalizer Amount", "Equalizer Low",  "Equalizer Body",  "Equalizer Slant" } },
        { "Widen",     "Widen",     { "Widen Amount",     "Widen Width",    "Widen Spread",    nullptr } },
        { "Compress",  "Compress",  { "Compress Push",    "Compress Ratio", "Compress Lift",   nullptr } },
        { "Multiband", "Multiband", { "Multiband Amount", "Multiband Raise","Multiband Press", nullptr } },
        // fb444 — Bode. Shift is the hero; Fdbk and Direction are the two that most change the
        // output, so if this row measures Delta 0.00 the device is in the chain and dead.
        { "Bode",      "Bode",      { "Bode Shift",       "Bode Fdbk",      "Bode Direction",  "Bode Diffusion" } },   // fb447 — Blur → Diffusion
        // fb444 — Utility. Gain is the hero; Image and Strain are the two that most change the
        // output. Note Gain's UNITY is 0.667, so pushing it to 1.0 is a real +30 dB change.
        { "Utility",   "Utility",   { "Utility Gain",     "Utility Width",  "Utility High Pass", "Utility Drive" } },   // fb450 — the channel strip: Gain · Width · High Pass · Drive
        // fb444 — Splitter. With no lane devices it still shapes: per-lane gains and Balance.
        { "Splitter",  "Splitter",  { "Splitter Balance", "Splitter Lane 1 Gain", "Splitter Spread", "Splitter Split" } },
    };
    for (const auto& d : devs)
    {
        AU a; if (! a.open()) return 2;
        const std::string P = d.pfx;
        bool okSet = true;
        okSet &= a.set (P + " In Chain", 1.0f);
        okSet &= a.set (P + " Power",    1.0f);
        okSet &= a.set (P + " Mix",      1.0f);
        // 🔑 THE ROUTE. Devices arrive UNROUTED by design (fb362 "unrouted on arrival"), and
        //    TW_FX4_APPLY gates on `poolRouteAny_[BASE + inst0]`, so a device with no source
        //    selected returns its input BIT-IDENTICALLY. The card's A/B/C/D/Sub/Noise row is what
        //    normally sets this; the harness must do the same or it is measuring a device that was
        //    never asked to listen to anything.
        const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
        int routed = 0;
        for (int k = 0; k < 6; ++k) if (a.set (P + " " + SRC[k], 1.0f)) ++routed;
        chk (routed == 6, (std::string (d.label) + ": all six route sources are host-writable").c_str(),
             std::to_string (routed) + "/6");
        for (int k = 0; k < 4; ++k) if (d.knobs[k]) a.set (d.knobs[k], 1.0f);
        chk (okSet, (std::string (d.label) + ": host could write In Chain / Power / Mix").c_str(), "");

        const Render wet = a.render();
        const double wRms = rmsdb (wet.L);
        const double dS   = specDist (drySpec, spec (wet.L));
        const double dSide= sideRatio (wet) - sideRatio (dry);
        const double dCr  = crest (wet.L) - crest (dry.L);
        char det[256];
        snprintf (det, sizeof det, "Δlevel=%+.2f dB  Δspectrum=%.2f dB/band  Δside=%+.2f dB  Δcrest=%+.2f dB",
                  wRms - dryRms, dS, dSide, dCr);
        // "does the mechanism engage" (fb432): ANY of the three axes moving is proof of arrival.
        const bool moved = std::fabs (wRms - dryRms) > 0.35 || dS > 0.35
                        || std::fabs (dSide) > 0.35 || std::fabs (dCr) > 0.35;
        chk (moved, (std::string (d.label) + ": the audio actually REACHES the device").c_str(), det);
        a.close();
    }

    // ══ fb444 — THE SPLITTER'S ONE INDISPENSABLE PROPERTY, MEASURED IN THE PLUGIN ══════════
    // Its engine cert nulls reconstruction at -122 dB, but fb373 is the whole law of this file:
    // a green engine says nothing about whether the plugin reaches it, or reaches it CORRECTLY.
    // A splitter that combs at its crossovers is useless no matter how good the UI is, and a
    // comb would be INAUDIBLE as "broken" - it just sounds slightly thin, forever. So: put a
    // Splitter in the chain at its DEFAULTS, touch nothing, and require the output to be the
    // same sound. Every OTHER row in this file asserts Delta != 0; this one asserts Delta ~ 0,
    // and that asymmetry is the point.
    {
        AU a;
        if (a.open())
        {
            a.set ("Splitter In Chain", 1.0f);
            a.set ("Splitter Power",    1.0f);
            const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
            for (int k = 0; k < 6; ++k) a.set (std::string ("Splitter ") + SRC[k], 1.0f);
            const Render w = a.render();
            const double dL = rmsdb (w.L) - dryRms;
            const double dS = specDist (drySpec, spec (w.L));
            char det[200];
            snprintf (det, sizeof det, "Δlevel=%+.3f dB  Δspectrum=%.3f dB/band  (a comb here would be permanent and inaudible as a fault)", dL, dS);
            chk (std::fabs (dL) < 0.60 && dS < 1.20,
                 "Splitter at DEFAULTS is transparent: split and rejoin do not comb", det);
            a.close();
        }
    }

    // ══ fb446 — A DEVICE IN A BAND IS POWERED BY THE BAND ═══════════════════════════════════
    // Lane cards carry NO route row (they are fed by the Splitter's band), so nothing lights their
    // SRC_* pills — and every pooled apply routine gates on poolRouteAny_. Without the lane-power
    // rule in resolveLanes(), a Distortion dropped into the Mid band would return its input forever,
    // bit-identical, with a full green build (fb435's exact shape). So: Splitter + a Distortion in
    // band 2 with Drive up, NO Distortion routes, versus the Splitter alone — the spectrum must move.
    {
        AU a;
        if (a.open())
        {
            const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
            a.set ("Splitter In Chain", 1.0f); a.set ("Splitter Power", 1.0f); a.set ("Splitter Chain Rank", 0.30f);
            for (int k = 0; k < 6; ++k) a.set (std::string ("Splitter ") + SRC[k], 1.0f);
            const Render base = a.render();
            const auto baseSpec = spec (base.L);
            a.set ("Distortion In Chain", 1.0f); a.set ("Distortion Power", 1.0f); a.set ("Distortion Chain Rank", 0.60f);
            a.set ("Distortion Lane", 2.0f / 7.0f);      // choice(8): index 2 = "Lane 2" (Mid), never lround(raw*(N-1))
            a.set ("Distortion Drive", 1.0f); a.set ("Distortion Mix", 1.0f);
            // deliberately NO "Distortion SRC_*" — a lane card has no route row
            const Render w = a.render();
            const double dS = specDist (baseSpec, spec (w.L));
            const double dL = rmsdb (w.L) - rmsdb (base.L);
            char det[200];
            snprintf (det, sizeof det, "Distortion in band 2, unrouted: Δspectrum=%.2f dB/band  Δlevel=%+.2f dB  (0.00 = the lane device is dead)", dS, dL);
            chk (dS > 0.35 || std::fabs (dL) > 0.35, "a LEGACY device (Distortion) in a Splitter band PROCESSES the band with NO routes lit", det);
            a.close();
        }
        // ... and a POOLED kind (Bode, whose power gate is poolRouteAny_), same shape
        AU b2;
        if (b2.open())
        {
            const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
            b2.set ("Splitter In Chain", 1.0f); b2.set ("Splitter Power", 1.0f); b2.set ("Splitter Chain Rank", 0.30f);
            for (int k = 0; k < 6; ++k) b2.set (std::string ("Splitter ") + SRC[k], 1.0f);
            const Render base = b2.render(); const auto baseSpec = spec (base.L);
            b2.set ("Bode In Chain", 1.0f); b2.set ("Bode Power", 1.0f); b2.set ("Bode Chain Rank", 0.60f);
            b2.set ("Bode Lane", 2.0f / 7.0f); b2.set ("Bode Shift", 0.92f); b2.set ("Bode Mix", 1.0f);
            const Render w = b2.render();
            const double dS = specDist (baseSpec, spec (w.L)), dL = rmsdb (w.L) - rmsdb (base.L);
            char det[200]; snprintf (det, sizeof det, "Bode in band 2, unrouted, Shift 92%%: Δspectrum=%.2f dB/band  Δlevel=%+.2f dB", dS, dL);
            chk (dS > 0.35 || std::fabs (dL) > 0.35, "a POOLED device (Bode) in a Splitter band PROCESSES the band with NO routes lit", det);
            b2.close();
        }
        // ── fb447 — THE RELABEL LAW, BOUND FOR REAL (TerrainSplitterFx.h "THE SLOTS THAT GO UNBOUND"): in a
        //    2-lane Type b3 ("Lane 3 Gain" to the DAW, "Low Pan" on the card) is lane 1's PAN. Low/High, b3 hard
        //    left: the RIGHT channel's low band must drop and the left's must not — and in Low/Mid/High the
        //    same knob is Lane 3 Gain again, so the right low band must NOT move (the binding is per Type).
        AU pn;
        if (pn.open())
        {
            const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
            auto lowMean = [] (const std::vector<double>& sp) { double a = 0; for (int b = 0; b < 15; ++b) a += sp[(size_t) b]; return a / 15.0; };   // bands 0..14 = 20 Hz .. ~500 Hz (the 2-lane default split)
            pn.set ("Splitter In Chain", 1.0f); pn.set ("Splitter Power", 1.0f); pn.set ("Splitter Chain Rank", 0.30f);
            for (int k = 0; k < 6; ++k) pn.set (std::string ("Splitter ") + SRC[k], 1.0f);
            pn.set ("Splitter Type", 0.0f);                                   // Low / High
            const Render b0 = pn.render();
            pn.set ("Splitter Lane 3 Gain", 0.0f);                            // = Low PAN, hard left, in this Type
            const Render p1 = pn.render();
            const double dR = lowMean (spec (p1.R)) - lowMean (spec (b0.R)), dLft = lowMean (spec (p1.L)) - lowMean (spec (b0.L));
            char det[240]; snprintf (det, sizeof det, "Low/High, b3 hard left: right low band %+.1f dB, left low band %+.1f dB", dR, dLft);
            chk (dR < -6.0 && dLft > -2.5 && dLft < 5.0, "Splitter Low/High: b3 is the LOW band's PAN (right drops, left holds) — the relabel law is bound, not a label", det);
            pn.set ("Splitter Lane 3 Gain", 0.5f);
            pn.set ("Splitter Type", 1.0f / 7.0f);                            // Low / Mid / High: b3 is Lane 3 Gain again
            const Render b1 = pn.render();
            pn.set ("Splitter Lane 3 Gain", 0.0f);
            const Render p2 = pn.render();
            const double dR3 = lowMean (spec (p2.R)) - lowMean (spec (b1.R));
            snprintf (det, sizeof det, "Low/Mid/High, b3 = 0: right low band %+.1f dB (a pan here would be the wrong binding)", dR3);
            chk (std::fabs (dR3) < 1.5, "Splitter Low/Mid/High: the same knob is Lane 3 Gain again — the low band's right channel does NOT move", det);
            pn.close();
        }
    }


    // ══════════════════════════════════════════════════════════════════════════════════════════
    //  fb453 · TASK 5 — IS THE FX RACK'S MODULATION REAL?
    // ══════════════════════════════════════════════════════════════════════════════════════════
    printf ("\n══ fb453 · TASK 5 — THE FX RACK AS A MODULATION DESTINATION, ON THE INSTALLED AU ══\n\n");
    if (! gInfoDone) { gInfo = ctl.info; gInfoDone = true; }

    // ── GATE D — THE fb373 DEBT: 1,104 RESOLVED POINTERS ──────────────────────────────────────
    // `cacheFxModRefs()` (PluginProcessor.cpp:5332) builds an id per (kind, instance, knob) and
    // resolves it with `apvts.getRawParameterValue()`; it keeps the tally in `fxModRefsResolved_`
    // and asserts 184 × 6 = 1,104. Nothing READS that member, and its jassert is a no-op in
    // Release — so an id that silently fails to resolve is a knob that can never modulate, with
    // every gate in this file still green. That is fb373 exactly.
    //
    // The count is surfaced here over a path that already exists and needs no Source change:
    // kAudioUnitProperty_ParameterList. A JUCE parameter's AU id IS the hash of its APVTS id
    // (AU::pid above), so "is this id in the host's parameter list" answers the same question
    // `getRawParameterValue()` answers — read out of the plugin's own registry, on the installed
    // binary, using the SAME generated table and the SAME suffix rule cacheFxModRefs() uses.
    {
        int resolved = 0; std::set<AudioUnitParameterID> distinct; std::vector<std::string> missing;
        for (int k = 0; k < 16; ++k)
          for (int i = 0; i < 6; ++i)
            for (int n = 0; n < 12; ++n)
            {
                if (kFxModLeaf[k][n] == nullptr) continue;                 // a hole — the Filter's 8
                const std::string id = std::string (kFxModTag[k])
                                     + (i == 0 ? std::string() : std::to_string (i + 1))
                                     + "_" + kFxModLeaf[k][n];
                if (gInfo.count (AU::pid (id))) { ++resolved; distinct.insert (AU::pid (id)); }
                else if (missing.size() < 12)   missing.push_back (id);
            }
        char det[300];
        // the membership test is only sound if the host's list has no two parameters on one id
        chk (ctl.idList.size() == ctl.info.size(),
             "the AU's parameter ids are unique (the id->parameter lookup is sound)",
             std::to_string (ctl.idList.size()) + " ids, " + std::to_string (ctl.info.size()) + " distinct");
        snprintf (det, sizeof det, "resolved %d of %d  (kFxModLive %d x 6 instances)%s%s",
                  resolved, kFxModLive * 6, kFxModLive,
                  missing.empty() ? "" : "   first missing: ", missing.empty() ? "" : missing[0].c_str());
        chk (resolved == kFxModLive * 6,
             "GATE D: every one of the 1,104 rack-mod cells resolves to a real plugin parameter", det);
        snprintf (det, sizeof det, "%zu distinct ids over %d cells (the 1 deliberate alias x 6: SYN_DLY_TIME)",
                  distinct.size(), kFxModLive * 6);
        chk ((int) distinct.size() == kFxModDistinct * 6,
             "...over exactly 1,098 distinct parameters — the alias is the ONLY duplicate", det);
    }

    // ── THE HARNESS'S OWN TWO ASSUMPTIONS, MEASURED (fb393: a harness kinder than reality) ────
    {
        MtxCfg u; u.kind = 14;                                   // Utility — Gain is the loudest knob
        const Render d1 = mtxRender (u), d2 = mtxRender (u);
        char det[220];
        snprintf (det, sizeof det, "two fresh instances, identical settings: null=%.1f dB at rms %.2f dB",
                  null2 (d1, d2), rms2 (d1));
        chk (null2 (d1, d2) < -180.0,
             "the render is DETERMINISTIC (a bit-exact null is what the equivalence gate stands on)", det);

        // and the offset itself: the LFO is PARKED, so v = 0.25 must land the knob on 0.35 + 0.25.
        // Scanned, not assumed — if the park were wrong every cell below would fail for one reason.
        MtxCfg b = u; b.routes = routeJson (destOf (14, 0, 0), kOff);
        const Render B = mtxRender (b);
        double best = 1e9, bestX = 0.0;
        for (int i = 0; i <= 4; ++i)
        {
            const float x = 0.125f + 0.0625f * (float) i;   // 0.125 .. 0.375, all exact
            MtxCfg c = u; c.knobs[0] = kBase + x;
            const double e = rel (B, mtxRender (c));
            if (e < best) { best = e; bestX = x; }
        }
        snprintf (det, sizeof det, "best null %.1f dBr at knob = base + %.4f (expected +%.4f)", best, bestX, kOff);
        chk (std::fabs (bestX - (double) kOff) < 1e-6 && best < kEqGate,
             "the parked LFO delivers EXACTLY the route's depth (calibrated, not assumed)", det);
    }

    // ── GATE A — FIRST BLOCK ──────────────────────────────────────────────────────────────────
    // The modulation map is built between the global LFO bank's advance and pushFx3Params(), the
    // one call where the rack reads its parameters. Built AFTER that call — the natural place,
    // next to flowKnob() where the math came from — every route would be one block late and dead
    // on block 1. That is inaudible in normal use and green on every other gate in this file.
    // So: render the AU's VERY FIRST block, with and without the route, and demand they differ.
    {
        MtxCfg f; f.kind = 14; f.warmup = false; f.nblk = 1; f.skip = 0;
        const Render u = mtxRender (f);
        f.routes = routeJson (destOf (14, 0, 0), kOff);
        const Render m = mtxRender (f);
        char det[220];
        snprintf (det, sizeof det, "block 1 alone: route vs no route = %.2f dBr (a map built one block late reads -240)",
                  rel (m, u));
        chk (rel (m, u) > -80.0,
             "GATE A: a route BITES ON BLOCK 1 (the map is built before the rack reads its params)", det);
    }

    // ── GATE B — ENVELOPE OWNERSHIP: THE KNOB IS THE PEAK, AND AT REST IT IS ZERO ─────────────
    // fb179. `monoEnvLevelOf()` returns level−1 and addEnv() adds the 1 back, so an OWNING
    // envelope crossfades the base away and drives the knob from ZERO up to the depth-scaled top.
    // At rest an owned knob reads 0, NOT its base — the opposite of the intuition, and an earlier
    // draft of this plan asserted the intuition, which would have blessed a HALVED implementation
    // while failing a correct one. Every gate here is therefore a THREE-way comparison: the
    // correct value, and the two wrong values the two plausible bugs produce.
    //
    // Env Mod 2 (source 104) is the vehicle: sustain 0 with a 1 ms attack/decay puts it at rest
    // while the note is still sounding, and sustain 1 pins it at its peak.
    {
        const int dst = destOf (14, 0, 0);                        // Utility Gain — monotonic, loud
        const std::vector<std::pair<std::string,float>> envRest = {
            { "SYN_ENV_M2_A", 0.0f }, { "SYN_ENV_M2_D", 0.0f }, { "SYN_ENV_M2_H", 0.0f },
            { "SYN_ENV_M2_DLY", 0.0f }, { "SYN_ENV_M2_CD", 0.5f }, { "SYN_ENV_M2_S", 0.0f } };
        std::vector<std::pair<std::string,float>> envPeak = envRest; envPeak.back().second = 1.0f;
        char det[300];
        {   // AT REST — the knob must read 0, not 0.35
            MtxCfg r; r.kind = 14; r.extra = envRest; r.knobs[0] = kBase;
            r.routes = routeJson (dst, 1.0f, 104);
            const Render R = mtxRender (r);
            MtxCfg z; z.kind = 14; z.extra = envRest; z.knobs[0] = 0.0f;   const Render Z = mtxRender (z);
            MtxCfg b; b.kind = 14; b.extra = envRest; b.knobs[0] = kBase;  const Render Bb = mtxRender (b);
            const double toZero = rel (R, Z), toBase = rel (R, Bb);
            snprintf (det, sizeof det, "vs knob 0.00 = %.1f dBr   vs knob 0.25 (its base) = %.1f dBr", toZero, toBase);
            chk (toZero < -60.0 && toBase > toZero + 40.0,
                 "GATE B1: an ENV route AT REST drives the knob to ZERO, not to its base (fb179)", det);
        }
        {   // AT PEAK, depth 1.0 — the knob must read 1.0.  A `* 0.5f` in addEnv() reads 0.5.
            MtxCfg r; r.kind = 14; r.extra = envPeak; r.knobs[0] = kBase;
            r.routes = routeJson (dst, 1.0f, 104);
            const Render R = mtxRender (r);
            MtxCfg o; o.kind = 14; o.extra = envPeak; o.knobs[0] = 1.0f;   const Render O = mtxRender (o);
            MtxCfg h; h.kind = 14; h.extra = envPeak; h.knobs[0] = 0.5f;   const Render Hh = mtxRender (h);
            const double toOne = rel (R, O), toHalf = rel (R, Hh);
            snprintf (det, sizeof det, "vs knob 1.00 = %.1f dBr   vs knob 0.50 (the halved bug) = %.1f dBr", toOne, toHalf);
            chk (toOne < -60.0 && toHalf > toOne + 40.0,
                 "GATE B2: an ENV route AT PEAK, depth 1.0, drives the knob to 1.0 — NOT halved", det);
        }
        {   // AT PEAK, depth 0.5 — base*(1−w) + w = 0.25*0.5 + 0.5 = 0.625. The halved bug: 0.375.
            MtxCfg r; r.kind = 14; r.extra = envPeak; r.knobs[0] = kBase;
            r.routes = routeJson (dst, 0.5f, 104);
            const Render R = mtxRender (r);
            MtxCfg g; g.kind = 14; g.extra = envPeak; g.knobs[0] = 0.625f; const Render G = mtxRender (g);
            MtxCfg w; w.kind = 14; w.extra = envPeak; w.knobs[0] = 0.375f; const Render W = mtxRender (w);
            const double toGood = rel (R, G), toWrong = rel (R, W);
            snprintf (det, sizeof det, "vs knob 0.625 (the crossfade) = %.1f dBr   vs 0.375 (halved) = %.1f dBr", toGood, toWrong);
            chk (toGood < -60.0 && toWrong > toGood + 40.0,
                 "GATE B3: depth 0.5 lands on base*(1-w)+w — the ownership CROSSFADE, to scale", det);
        }
    }

    // ── GATE C — THE ALIAS SUMS ───────────────────────────────────────────────────────────────
    // SYN_DLY_TIME sits behind TWO dials: the Delay's front "Time" (knob 0) and its back "Time L"
    // (knob 10), because of fb306-310's L/R link. The map is keyed by parameter POINTER, so both
    // destinations must land in ONE slot and ACCUMULATE. Keyed any other way they would fight and
    // the last one written would win.
    {
        const int d0 = destOf (1, 0, 0), d10 = destOf (1, 0, 10);
        MtxCfg base; base.kind = 1;
        const Render flat = mtxRender (base);
        MtxCfg f0 = base; f0.routes = routeJson (d0,  kOff);           const Render F0  = mtxRender (f0);
        MtxCfg f1 = base; f1.routes = routeJson (d10, kOff);           const Render F10 = mtxRender (f1);
        char hb[160]; snprintf (hb, sizeof hb, "[{\"s\":0,\"d\":%d,\"v\":0.125},{\"s\":0,\"d\":%d,\"v\":0.125}]", d0, d10);
        MtxCfg hf = base; hf.routes = hb;                              const Render HF  = mtxRender (hf);
        char det[260];
        snprintf (det, sizeof det, "front-Time vs back-Time L = %.1f dBr; and each moves the sound %.1f dBr",
                  rel (F0, F10), rel (F0, flat));
        chk (rel (F0, F10) < kEqGate && rel (F0, flat) > kAudFloor,
             "GATE C1: knob 0 and knob 10 ARE one parameter — the two dests are interchangeable", det);
        snprintf (det, sizeof det, "0.125 + 0.125 on the two dests vs 0.25 on one = %.1f dBr", rel (HF, F0));
        chk (rel (HF, F0) < kEqGate,
             "GATE C2: two half-depth routes on the alias SUM into one slot (they do not fight)", det);
    }

    // ── SIG-PARITY — THE GATE THAT USED TO BE "SIG-LAG" ───────────────────────────────────────
    // History, because it is the whole reason this gate exists. SIG (the Distortion's Knee) was
    // the ONE dial of the 184 that did not null: it carries both a legacy destination (DstMorph,
    // fb340) and a rack one, and its resolve was the only rack read site that ran BEFORE
    // buildFxMod(), so instance 1's Knee followed the matrix ONE BLOCK LATE. The matrix measured
    // it at -77.17 dBr where every other cell reached -178..-235, and this gate proved the
    // mechanism: block 1 alone differed by -17.8 dBr, and it did NOT wash out (-77.2 dBr from
    // block 20, -76.5 dBr from block 80). `ec97a46` moved the resolve below buildFxMod.
    //
    // So the gate is inverted: it now asserts the FIXED behaviour, and it is self-proving in both
    // directions because it compares INSTANCE 1 AGAINST INSTANCE 2. Instances 2..6 always read
    // `M (R.sig)` AFTER the build (PluginProcessor.cpp:~10442) and always nulled at the floor;
    // instance 1 was the only one that did not. Nothing except "instance 1 reads before the map is
    // built" predicts that asymmetry — so if the resolve ever moves back above buildFxMod(),
    // instance 1 breaks, instance 2 does not, and the SPREAD between them is what says so.
    {
        auto sigNull = [] (int inst, int nblk, int skip, bool warm) {
            MtxCfg base; base.kind = 2; base.inst = inst; base.probe = 1;
            base.nblk = nblk; base.skip = skip; base.warmup = warm;
            MtxCfg b = base; b.routes = routeJson (destOf (2, inst, 1), kOff);
            MtxCfg c = base; c.knobs[1] = kTop;
            return rel (mtxRender (b), mtxRender (c));
        };
        const double blk1 = sigNull (0, 1, 0, false);        // the AU's very first block
        const double i1   = sigNull (0, NBLK, SKIP, true);   // instance 1, steady state
        const double i2   = sigNull (1, NBLK, SKIP, true);   // instance 2, which never had the lag
        char det[300];
        snprintf (det, sizeof det, "block 1 %.1f dBr; steady state inst1 %.1f / inst2 %.1f dBr (spread %.1f dB)",
                  blk1, i1, i2, std::fabs (i1 - i2));
        chk (blk1 < -100.0 && i1 < -100.0 && std::fabs (i1 - i2) < 40.0,
             "SIG-PARITY: the Knee is CURRENT on block 1, and instance 1 now matches instance 2", det);
    }

    // ── THE WHOLE PANEL AT ONCE ───────────────────────────────────────────────────────────────
    // Every live knob of a device routed simultaneously, each with its OWN depth, versus every
    // knob moved by its own amount. Distinct depths are the point: a PERMUTATION of the
    // destinations passes a same-depth version of this and fails this one. It is also the only
    // gate here that exercises many routes at once — distinct slots, no crosstalk — and the
    // Delay's row re-proves the alias sum, because the two Time dests accumulate into one target.
    for (int k = 0; k < 16; ++k)
    {
        if (mtxOnlyKind >= 0 && k != mtxOnlyKind) continue;
        MtxCfg flat; flat.kind = k; flat.inst = mtxInst; flat.mixFull = false;
        std::string rj = "[";  std::map<std::string, float> sum;  int nRoutes = 0;
        for (int n = 0; n < 12; ++n)
        {
            if (kFxModLeaf[k][n] == nullptr) continue;
            const float d = 0.03125f * (float) (n + 1);   // 1/32 .. 12/32 — exact, and so are the sums
            char one[96]; snprintf (one, sizeof one, "%s{\"s\":0,\"d\":%d,\"v\":%.6f}", nRoutes ? "," : "", destOf (k, mtxInst, n), d);
            rj += one; ++nRoutes;
            sum[kFxModLeaf[k][n]] += d;                 // the alias accumulates, exactly as the map does
        }
        rj += "]";
        MtxCfg all = flat; all.routes = rj;
        MtxCfg mov = flat;
        for (int n = 0; n < 12; ++n) if (kFxModLeaf[k][n]) mov.knobs[n] = std::min (1.0f, kBase + sum[kFxModLeaf[k][n]]);
        const Render A0 = mtxRender (flat), Ball = mtxRender (all), Call = mtxRender (mov);
        char lbl[130];
        if (mtxInst == 0) snprintf (lbl, sizeof lbl, "%s: all %d knobs routed at once == all %d knobs moved", kKindName[k], nRoutes, nRoutes);
        else              snprintf (lbl, sizeof lbl, "%s%d: all %d knobs routed at once == all %d knobs moved", kKindName[k], mtxInst + 1, nRoutes, nRoutes);
        char det[220]; snprintf (det, sizeof det, "null=%7.2f dBr   panel moves the sound %7.2f dBr", rel (Ball, Call), rel (Ball, A0));
        chk (rel (Ball, Call) < kEqGate && rel (Ball, A0) > kAudFloor, lbl, det);
    }

    // ══ THE MATRIX ════════════════════════════════════════════════════════════════════════════
    if (mtxRun)
    {
        printf ("\n  ── the %s matrix, INSTANCE %d%s ──\n", mtxQuick ? "REPRESENTATIVE" : "FULL 184-cell",
                mtxInst + 1,
                mtxMutate ? "  [MUTATED: the route goes to the ADJACENT knob — every cell MUST fail]" : "");
        std::vector<CellFail> fails; std::vector<std::string> quietCells;
        int cells = 0, mutInert = 0, mutBroke = 0, mutHitMix = 0;
        for (int k = 0; k < 16; ++k)
        {
            if (mtxOnlyKind >= 0 && k != mtxOnlyKind) continue;
            const std::string T = kFxModTag[k];
            int typeMax = 0;
            { auto it = gInfo.find (AU::pid (T + "_TYPE")); if (it != gInfo.end()) typeMax = (int) it->second.maxValue; }
            // MUTATED runs take the FIRST measurement and nothing else. The retry exists to find a
            // Type where the dial is live; with the destination deliberately wrong the cell is going
            // to fail whatever Type it is on, so hunting is pure cost — and expensive cost, because
            // a mutated cell rarely satisfies the liveness test and therefore sweeps every Type and
            // both operating points: ~96 renders instead of 2, hours instead of minutes.
            const int nTypes = mtxMutate ? 1 : std::max (1, std::min (kTypeTries, typeMax + 1));
            std::map<int, Render> acache;                        // key = type*2 + (this is the Mix cell)
            for (int n = 0; n < 12; ++n)
            {
                if (kFxModLeaf[k][n] == nullptr) continue;        // a hole — the Filter has no back panel
                if (mtxQuick && n != 0) continue;
                const bool isMix = (n == 3);
                // MUTATION: aim the route at the device's NEXT live knob. Everything else is
                // identical, so a cell that still nulls would mean the matrix cannot see a
                // mis-mapped destination at all.
                int dn = n;
                if (mtxMutate) for (int q = 1; q < 12; ++q) { const int c2 = (n + q) % 12; if (kFxModLeaf[k][c2]) { dn = c2; break; } }
                double bEq = 999.0, bAud = -999.0; int bType = 0, bOp = 0;
                // TWO OPERATING POINTS, and the second is only ever reached by a dial that did
                // nothing at the first. 0.25 -> 0.50 is the sweep; some dials are on a PLATEAU
                // there and a plateau makes the null vacuous, not wrong. tape.DUCK is the worked
                // example: TapeFxEngine.h:713 clamps `duckEnv_ * 34 * duck` to 0.96, and on a
                // chord this loud the clamp is already saturated at duck = 0.25, so 0.25 and 0.50
                // are the SAME sound. Off the bottom of the range (0.00 -> 0.25) it is not — the
                // engine's `if (duck > 0.001f)` branch is not even entered at 0. So: if a cell is
                // inert everywhere at the normal base, probe it again from ZERO before calling it
                // unproven.
                for (int op = 0; op < (mtxMutate ? 1 : 3) && bAud <= kAudFloor; ++op)
                {
                  const float pBase = kMtxOps[op].base;
                  const float pTop  = pBase + kOff;
                  for (int t = 0; t < nTypes; ++t)
                  {
                    MtxCfg base; base.kind = k; base.inst = mtxInst; base.type = t; base.probe = n;
                    base.stereoUp = kMtxOps[op].stereo;
                    if (isMix || op != 0) base.knobs[n] = pBase;
                    Render A;
                    if (op == 0)
                    {
                        const int key = t * 4 + (isMix ? 1 : 0) + (n == 11 ? 2 : 0);   // n==11 can change the setup (Delay Link)
                        if (! acache.count (key)) acache[key] = mtxRender (base);
                        A = acache[key];
                    }
                    else A = mtxRender (base);     // ops 1-2 are per-cell, and only inert cells get here
                    MtxCfg bc = base; bc.routes = routeJson (destOf (k, mtxInst, dn), kOff);
                    MtxCfg cc = base; cc.knobs[n] = pTop;
                    const Render B = mtxRender (bc), C = mtxRender (cc);
                    const double eq = rel (B, C), aud = rel (B, A);
                    if (aud > bAud) { bAud = aud; bEq = eq; bType = t; bOp = op; }
                    // The search is for an operating point where the DIAL IS LIVE, and that is a
                    // question about `aud` alone. Folding the margin in here would make a cell whose
                    // route is WRONG (a large eq) keep hunting through all 16 Types and both
                    // operating points looking for a null it can never get — which is every cell of
                    // a --mutate run, and turns a 14-minute sweep into an overnight one.
                    if (aud > kAudFloor) break;   // live here — stop hunting
                  }
                }
                ++cells;
                const bool ok = (bEq < kEqGate) && (bAud > kAudFloor) && (bAud - bEq > kAudMargin);
                if (mtxMutate)
                {
                    // 🚨 DO NOT LET THE HEADLINE OUTRUN THE FINE PRINT. "184/184 red" is only
                    //    evidence of TEETH for the cells that went red because the null BROKE.
                    //    A mutated cell also goes red when the dial it aimed at is inert (the
                    //    null stays at the floor and the audibility clause fails instead), and
                    //    --mutate pins Type 0 and one operating point, so the rescues that save
                    //    those cells in a normal run are switched off. Counted, not glossed.
                    if (bAud <= kAudFloor) ++mutInert; else ++mutBroke;
                    if (dn == 3) ++mutHitMix;               // knob 2 always aims at Mix, clamped at 1.0
                }
                char lbl[140];
                if (mtxInst == 0) snprintf (lbl, sizeof lbl, "%s.%s (k%02d n%02d): route +0.25 == knob 0.50",
                                            kKindName[k], kFxModLeaf[k][n], k, n);
                else              snprintf (lbl, sizeof lbl, "%s%d.%s (k%02d i%d n%02d): route +0.25 == knob 0.50",
                                            kKindName[k], mtxInst + 1, kFxModLeaf[k][n], k, mtxInst, n);
                char ty[24] = ""; if (bType) snprintf (ty, sizeof ty, "   [Type %d]", bType);
                char det[300]; snprintf (det, sizeof det, "null=%7.2f dBr   moved %7.2f dBr%s%s%s",
                                         bEq, bAud, ty, kMtxOps[bOp].note, "");
                chk (ok, lbl, det);
                if (! ok) fails.push_back ({ k, n, kFxModLeaf[k][n], bEq, bAud });
                else if (bAud < -30.0) { char q[80]; snprintf (q, sizeof q, "%s.%s (%.1f dBr)", kKindName[k], kFxModLeaf[k][n], bAud); quietCells.push_back (q); }
            }
        }
        printf ("\n  %d cells swept.  %zu failed.\n", cells, fails.size());
        if (mtxMutate)
            printf ("  MUTATION BREAKDOWN: %d red because the NULL BROKE (the gate saw the mis-map),\n"
                    "                      %d red because the mis-aimed dial is INERT at Type 0 (the null held,\n"
                    "                        the audibility clause failed) — --mutate pins Type 0 and one\n"
                    "                        operating point, so the rescues a normal run uses are off here.\n"
                    "                      %d of them aimed at knob 3 = MIX, which the flat config pins at 1.0,\n"
                    "                        so the route clamps and moves nothing: a systematic artefact of\n"
                    "                        \"the ADJACENT knob\", not an independent detection.\n",
                    mutBroke, mutInert, mutHitMix);
        for (const auto& f : fails)
            printf ("    FAILED  %-8s %-9s (k%02d n%02d)  null=%7.2f dBr  moved=%7.2f dBr  %s\n",
                    kKindName[f.kind], f.leaf.c_str(), f.kind, f.knob, f.eq, f.aud,
                    f.aud <= kAudFloor ? "<- INERT at every Type and both operating points: the cell proves nothing"
                                       : "<- the route is NOT the knob");
        if (! quietCells.empty())
        {
            printf ("    subtle but live (< -30 dBr):");
            for (const auto& q : quietCells) printf (" %s", q.c_str());
            printf ("\n");
        }
    }
    printf ("\n  fb453 Task 5 wall clock: %.1f s\n", nowSec() - t5t0);

    printf ("\n  PASS %d   FAIL %d\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
