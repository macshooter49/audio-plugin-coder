// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp22 — THE FILTER TABLE, on the installed AU, judged the way this project requires: PERCEPTUALLY.
//  The house law (fb283) bans sample-difference RMS as a dramaticism metric — an allpass measures
//  huge and sounds like nothing. So every claim here is a MAGNITUDE-SPECTRUM claim:
//      · spectral CENTROID (brightness) and the band energies themselves
//      · magnitude change in dB between two settings, which is what an ear actually tracks
//
//  What it must prove, and what "it works" means:
//    1. selecting the type CHANGES the sound            (it is not a silent no-op)
//    2. DIFFERENT TABLES sound DIFFERENT                 (the whole feature — Max's "we should hear
//                                                         something different, correct?")
//    3. RESONANCE 0 is flat and 100 % is DRAMATIC        (the lifeguard law: the knob's 100 % is the
//                                                         algorithm's 100 %, no unused headroom)
//    4. the FRAME scan moves the curve                   (a modulatable parameter that really sweeps)
//    5. CUTOFF slides the whole curve                    (harmonic 24 rides the knob)
//
//    clang++ -O2 -std=c++17 Tests/au_filter_table.cpp -o /tmp/auft \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
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

static const double SR = 48000.0; static const int BLK = 512, BLOCKS = 128, NFFT = 16384;
static const int FILTER_TABLE_TYPE = 118;

struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK; AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) return false;
        UInt32 sz = 0; Boolean w = false;
        AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w);
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            char nm[256] = {}; if (pi.cfNameString) CFStringGetCString (pi.cfNameString, nm, sizeof nm, kCFStringEncodingUTF8); else std::strncpy (nm, pi.name, 255);
            byName[nm] = id; info[id] = pi;
        }
        return true;
    }
    bool set (const char* name, float norm)
    {
        auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter '%s'\n", name); return false; }
        const auto& pi = info[it->second];
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    bool setRaw (const char* name, float v)
    {
        auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter '%s'\n", name); return false; }
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    void chord() { MusicDeviceMIDIEvent (au, 0x90, 36, 100, 0); MusicDeviceMIDIEvent (au, 0x90, 48, 100, 0); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    std::vector<float> render()
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK), all;
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        for (int b = 0; b < BLOCKS; ++b)
        {
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = (double) b * BLK; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            if (b >= BLOCKS / 4) all.insert (all.end(), bl.begin(), bl.end());
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false);
        }
        free (abl); return all;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        { std::complex<double> w (1.0, 0.0);
          for (size_t k = 0; k < len / 2; ++k)
          { const auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}

/** The magnitude spectrum in dB per bin — the only currency this file trades in. */
static std::vector<double> spectrum (const std::vector<float>& x)
{
    std::vector<std::complex<double>> a ((size_t) NFFT);
    const size_t n = std::min ((size_t) NFFT, x.size());
    for (size_t i = 0; i < n; ++i)
    { const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) NFFT); a[i] = { x[i] * w, 0.0 }; }
    fft (a);
    std::vector<double> m ((size_t) NFFT / 2);
    for (int k = 0; k < NFFT / 2; ++k) m[(size_t) k] = 20.0 * std::log10 (std::abs (a[(size_t) k]) + 1e-12);
    return m;
}
/** Mean |dB| difference across the audible band — "how differently does this sound".
 *  ⚠️ ONLY over bins that CARRY SOMETHING. A raw mean over every bin is dominated by the noise
 *  floor, where -100 vs -110 dB reads as a 10 dB "difference" nobody can hear — measured: it
 *  reported 9.5 dB between two renders that are mathematically identical. The mask is the
 *  reference's own peak minus 60 dB, so the metric answers the question an ear would ask. */
static double specDiffDb (const std::vector<double>& a, const std::vector<double>& b)
{
    const int lo = (int) (40.0 / (SR / NFFT)), hi = (int) (16000.0 / (SR / NFFT));
    double pk = -1e9;
    for (int k = lo; k < hi && k < (int) b.size(); ++k) pk = std::max (pk, b[(size_t) k]);
    const double floorDb = pk - 60.0;
    double s = 0.0; int n = 0;
    for (int k = lo; k < hi && k < (int) a.size(); ++k)
    { if (b[(size_t) k] < floorDb) continue;
      const double d = a[(size_t) k] - b[(size_t) k]; if (std::isfinite (d)) { s += std::fabs (d); ++n; } }
    return n ? s / (double) n : 0.0;
}
/** Spectral centroid in Hz — brightness. */
static double centroid (const std::vector<double>& m)
{
    double num = 0.0, den = 0.0;
    for (int k = 1; k < (int) m.size(); ++k)
    { const double lin = std::pow (10.0, m[(size_t) k] / 20.0), f = (double) k * SR / NFFT;
      if (f > 40.0 && f < 16000.0) { num += lin * f; den += lin; } }
    return den > 0 ? num / den : 0.0;
}

typedef std::vector<std::pair<std::string, float>> Sets;
static std::vector<float> run (bool tableType, const Sets& sets, const std::vector<std::pair<std::string,float>>& raws = {})
{
    Au a; a.open();
    a.set ("Synth Filter 1 Source A", 1.0f);
    a.set ("Synth OSC A Filter 1 Send", 1.0f);
    if (tableType) a.setRaw ("Synth Filter 1 Type", (float) FILTER_TABLE_TYPE);
    for (auto& r : raws) a.setRaw (r.first.c_str(), r.second);
    for (auto& s : sets) a.set (s.first.c_str(), s.second);
    a.pump (1.2);                       // the bake is a message-thread job — let the timer run it
    a.chord(); auto out = a.render(); a.close(); return out;
}

int main()
{
    int fails = 0;
    auto expect = [&] (bool ok, const char* what) { printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; };
    printf ("\n== tp22 - THE FILTER TABLE, perceptually (magnitude spectrum; RMS is banned here) ==\n\n");

    const auto none = spectrum (run (false, {}));
    // resonance 1.0 so the curve is at full prominence for the "does it do anything" questions
    const Sets full = { { "Synth Filter 1 Resonance", 1.0f }, { "Synth Filter 1 Cutoff", 0.5f } };
    const auto tbl  = spectrum (run (true, full));
    printf ("   no filter            centroid %7.0f Hz\n", centroid (none));
    printf ("   FILTER TABLE         centroid %7.0f Hz   spectrum change %5.2f dB\n", centroid (tbl), specDiffDb (tbl, none));
    expect (specDiffDb (tbl, none) > 2.0, "1. selecting FILTER TABLE changes the sound");

    // 2 · different tables must sound different — the feature's whole claim
    const auto t0 = spectrum (run (true, full, { { "Synth Filter 1 Table", 0.0f } }));
    const auto t9 = spectrum (run (true, full, { { "Synth Filter 1 Table", 9.0f } }));
    const auto t30= spectrum (run (true, full, { { "Synth Filter 1 Table", 30.0f } }));
    printf ("\n   table 0              centroid %7.0f Hz\n", centroid (t0));
    printf ("   table 9              centroid %7.0f Hz   vs table 0: %5.2f dB\n", centroid (t9), specDiffDb (t9, t0));
    printf ("   table 30             centroid %7.0f Hz   vs table 0: %5.2f dB\n", centroid (t30), specDiffDb (t30, t0));
    expect (specDiffDb (t9, t0)  > 1.5, "2. table 9 sounds different from table 0");
    expect (specDiffDb (t30, t0) > 1.5, "2. table 30 sounds different from table 0");

    // 3 · resonance: 0 flat, 100% dramatic (the lifeguard law)
    const auto r0 = spectrum (run (true, { { "Synth Filter 1 Resonance", 0.0f }, { "Synth Filter 1 Cutoff", 0.5f } }));
    const auto r1 = tbl;
    printf ("\n   resonance 0          vs no filter: %5.2f dB   (should be ~0 - a flat curve)\n", specDiffDb (r0, none));
    printf ("   resonance 100%%       vs resonance 0: %5.2f dB\n", specDiffDb (r1, r0));
    expect (specDiffDb (r0, none) < 1.5, "3. resonance 0 is FLAT (the filter does nothing)");
    expect (specDiffDb (r1, r0)   > 4.0, "3. resonance 100% is DRAMATIC (no unused headroom)");

    // 4 · the frame scan moves the curve
    const auto f0 = spectrum (run (true, { { "Synth Filter 1 Resonance", 1.0f }, { "Synth Filter 1 Cutoff", 0.5f }, { "Synth Filter 1 Table Frame", 0.0f } }));
    const auto f1 = spectrum (run (true, { { "Synth Filter 1 Resonance", 1.0f }, { "Synth Filter 1 Cutoff", 0.5f }, { "Synth Filter 1 Table Frame", 1.0f } }));
    printf ("\n   frame 0 vs frame 1   %5.2f dB\n", specDiffDb (f1, f0));
    expect (specDiffDb (f1, f0) > 1.0, "4. scanning the FRAME moves the curve");

    // 5 · cutoff slides the whole curve (harmonic 24 rides the knob)
    const auto c_lo = spectrum (run (true, { { "Synth Filter 1 Resonance", 1.0f }, { "Synth Filter 1 Cutoff", 0.25f } }));
    const auto c_hi = spectrum (run (true, { { "Synth Filter 1 Resonance", 1.0f }, { "Synth Filter 1 Cutoff", 0.85f } }));
    printf ("   cutoff low  centroid %7.0f Hz\n", centroid (c_lo));
    printf ("   cutoff high centroid %7.0f Hz   (the curve rides the knob)\n", centroid (c_hi));
    expect (centroid (c_hi) > centroid (c_lo) * 1.15, "5. CUTOFF slides the whole curve upward");

    printf ("\n%s (%d failed)\n\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
