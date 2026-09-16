// tp12c — EVERY OSCILLATOR IS 180 DEGREES, ALIGNED (Max: "I shouldn't see an oscillator that isn't 180").
// Build: clang++ -std=c++17 -O2 Tests/au_phase_default.cpp -o /tmp/au_phase_default -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
// Reads a FRESH instance of the installed AU: Phase must be 0.5 (180 deg) and Mode 0 (Manual/aligned) on A-D.
// tp12 — THE RACK IS AFTER THE FILTER, FOR EVERY MODEL. Build: clang++ -std=c++17 -O2 Tests/au_rack_order.cpp -o /tmp/au_rack_order -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
// Every "rack cut the highs by" line must read ~90 dB (before tp12 the Ladder rows read −2.8: the leak).
// tp12 — IS THE RACK AFTER THE FILTER? Renders the INSTALLED AU: osc A → Filter 1 (HP) → rack Filter card (LP, insert).
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <complex>
static const double SR = 48000.0;
static const int    BLK = 512;
static const int    NBLK = 160;
static const int    SKIP = 60;
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
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.011, false);   // tp12 — a REAL host's message thread runs between blocks (the timer builds pooled send filters)
            ts.mSampleTime += BLK;
            if (b >= skip) { out.L.insert (out.L.end(), bl.begin(), bl.end());
                             out.R.insert (out.R.end(), br.begin(), br.end()); }
        }
        free (abl);
        return out;
    }
};
// tp12c — a FRESH instance must show every oscillator at PHASE 0.5 (180 deg), Rand 1.0, MODE 0 (Manual/aligned).
int main()
{
    AU a; if (! a.open()) return 1;
    static const char* const OSC[4] = { "A", "B", "C", "D" };
    int bad = 0;
    for (auto* o : OSC)
    {
        const std::string p = std::string ("SYN_OSC_") + o + "_";
        const float ph = a.getP (p + "PHASE"), am = a.getP (p + "PHASE_AMT"), md = a.getP (p + "PHASE_MODE");
        const char* mn = (md < .5f) ? "Manual/aligned" : (md < 1.5f) ? "Free" : (md < 2.5f) ? "Random" : "Spread";
        const bool ok = std::fabs (ph - 0.5f) < 1e-3 && md < 0.5f;
        if (! ok) ++bad;
        printf ("  OSC %s   Phase %.3f (%3.0f deg)   Rand %.2f   Mode %.0f = %-14s %s\n", o, ph, ph * 360.0f, am, md, mn, ok ? "ok" : "<-- NOT 180 ALIGNED");
    }
    printf ("  %s\n", bad ? "FAIL" : "every oscillator on a fresh instance: 180 deg, aligned");
    a.close(); return bad ? 1 : 0;
}
