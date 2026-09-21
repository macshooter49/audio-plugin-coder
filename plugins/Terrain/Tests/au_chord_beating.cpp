// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_chord_beating.cpp — tp69 · WHY DOES A SINE CHORD CLIP AND BEAT IN TERRAIN AND NOT IN SERUM 2?
//
//  Max: "play a C minor 11th, all the notes, even the D next to the D sharp … you can hear the
//  beating, you can hear the clipping … do the same thing in Serum 2, nothing clips."
//  Same close-voiced Cm11 (C4 D4 Eb4 F4 G4 Bb4, velocity 100, 4 s) through the installed Terrain
//  AU on its default patch (OSC A wavetable, preset 0 = Sine) and through Serum 2's own init patch
//  (Basic Shapes frame 1 = a sine). Measured on the steady state: peak / RMS / crest, and a 65536-
//  point spectrum: the six notes' lines, and EVERY line that is not one of them (a linear sum of
//  six sines has NO other lines — beating is amplitude modulation, not a spectral line — so every
//  extra line is a nonlinearity: clipping, saturation, an oscillator that is not a sine, aliasing).
//  Also one note alone (harmonics of the "sine") and the chord at two output gains.
//
//  clang++ -O2 -std=c++17 Tests/au_chord_beating.cpp -o /tmp/auchord -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/auchord
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

static const double SR = 48000.0; static const int BLK = 512;
struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; double stamp = 0; std::string name;
    bool open (OSType sub, OSType manu)
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = sub; d.componentManufacturer = manu;
        AudioComponent c = AudioComponentFindNext (nullptr, &d); if (! c || AudioComponentInstanceNew (c, &au) != noErr) return false;
        AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
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
            char b[256] = {};
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) CFStringGetCString (pi.cfNameString, b, sizeof b, kCFStringEncodingUTF8);
            else snprintf (b, sizeof b, "%s", pi.name);
            byName[b] = id;
        }
        return true;
    }
    bool set (const std::string& n, float v) { auto it = byName.find (n); if (it == byName.end()) return false; return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v > 0 ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void render (int blocks, std::vector<float>* keepL = nullptr, std::vector<float>* keepR = nullptr)
    {
        std::vector<float> L (BLK), R (BLK);
        for (int b = 0; b < blocks; ++b)
        {
            AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = L.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = R.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            if (keepL) keepL->insert (keepL->end(), L.begin(), L.end());
            if (keepR) keepR->insert (keepR->end(), R.begin(), R.end());
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.001, false);
        }
    }
    void reset() { AudioUnitReset (au, kAudioUnitScope_Global, 0); }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * M_PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1, 0); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}
struct Line { double hz, db; };
struct Spec { std::vector<double> mag; double binHz; };
static Spec spectrum (const std::vector<float>& x, size_t from)
{
    const size_t N = 65536; Spec s; s.binHz = SR / (double) N; s.mag.assign (N / 2, -200.0);
    std::vector<double> acc (N / 2, 0.0); int wins = 0;
    for (size_t st = from; st + N <= x.size(); st += N / 2)
    {
        std::vector<std::complex<double>> a (N);
        for (size_t i = 0; i < N; ++i) a[i] = x[st + i] * (0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) N));
        fft (a);
        for (size_t k = 0; k < N / 2; ++k) acc[k] += std::norm (a[k]);
        ++wins;
    }
    const double ref = (double) N * (double) N * 0.25 * 0.25;
    for (size_t k = 0; k < N / 2; ++k) s.mag[k] = 10.0 * std::log10 (acc[k] / std::max (1, wins) / ref + 1e-30);
    return s;
}
static std::vector<Line> peaks (const Spec& s, double minHz, double floorDb)
{
    std::vector<Line> out; const size_t k0 = (size_t) (minHz / s.binHz);
    for (size_t k = std::max<size_t> (k0, 2); k + 2 < s.mag.size(); ++k)
        if (s.mag[k] > floorDb && s.mag[k] >= s.mag[k - 1] && s.mag[k] >= s.mag[k + 1] && s.mag[k] > s.mag[k - 2] && s.mag[k] > s.mag[k + 2])
        { const double a = s.mag[k - 1], b = s.mag[k], c = s.mag[k + 1]; const double d = 0.5 * (a - c) / (a - 2 * b + c + 1e-12);
          out.push_back ({ ((double) k + d) * s.binHz, b }); }
    return out;
}
static double hzOf (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }
struct Meas { double peak, rms, crest; double fund[12]; std::vector<Line> spur; double spurEnergyDb; double imLowDb; };
static Meas measure (const std::vector<float>& x, const int* notes, int nNotes, size_t from)
{
    Meas m {}; double pk = 0, e = 0; size_t n = 0;
    for (size_t i = from; i < x.size(); ++i) { const double v = std::fabs (x[i]); if (v > pk) pk = v; e += v * v; ++n; }
    m.peak = 20 * std::log10 (pk + 1e-12); m.rms = 20 * std::log10 (std::sqrt (e / std::max<size_t> (1, n)) + 1e-12); m.crest = m.peak - m.rms;
    Spec s = spectrum (x, from);
    auto lines = peaks (s, 20.0, -110.0);
    for (int i = 0; i < 12; ++i) m.fund[i] = -200;
    double top = -200;
    for (int i = 0; i < nNotes; ++i) { const double f = hzOf (notes[i]); for (auto& L : lines) if (std::fabs (L.hz - f) < 2.5) { m.fund[i] = std::max (m.fund[i], L.db); top = std::max (top, L.db); } }
    double spurE = 0, lowE = 0;
    for (auto& L : lines)
    {
        bool isF = false; for (int i = 0; i < nNotes; ++i) if (std::fabs (L.hz - hzOf (notes[i])) < 2.5) isF = true;
        if (isF) continue;
        if (L.db > top - 90) m.spur.push_back ({ L.hz, L.db - top });
        spurE += std::pow (10.0, L.db / 10.0); if (L.hz < 150.0) lowE += std::pow (10.0, L.db / 10.0);
    }
    std::sort (m.spur.begin(), m.spur.end(), [] (const Line& a, const Line& b) { return a.db > b.db; });
    m.spurEnergyDb = 10 * std::log10 (spurE + 1e-30) - top; m.imLowDb = 10 * std::log10 (lowE + 1e-30) - top;
    return m;
}
static void report (const char* title, const Meas& m, const int* notes, int nNotes)
{
    printf ("\n  ── %s ──\n", title);
    printf ("     peak %6.2f dBFS   rms %6.2f dBFS   crest %5.2f dB%s\n", m.peak, m.rms, m.crest, m.peak > -0.05 ? "   ⚠️ AT/OVER FULL SCALE" : "");
    printf ("     notes:"); for (int i = 0; i < nNotes; ++i) printf ("  %.1f Hz %6.1f dB", hzOf (notes[i]), m.fund[i]); printf ("\n");
    printf ("     everything that is NOT a note: %.1f dB below the loudest note (below 150 Hz: %.1f dB)\n", -m.spurEnergyDb, -m.imLowDb);
    printf ("     loudest non-note lines:"); int c = 0; for (auto& L : m.spur) { if (c++ >= 10) break; printf ("  %.1f Hz %+.1f", L.hz, L.db); } printf ("\n");
}
int main()
{
    const int chord[6] = { 60, 62, 63, 65, 67, 70 };
    const int single[1] = { 60 };
    const size_t from = (size_t) (SR * 1.5);
    auto play = [&] (Au& a, const int* notes, int n, double sec, std::vector<float>& out)
    { a.reset(); a.render (10); for (int i = 0; i < n; ++i) a.note (notes[i], 100); a.render ((int) (sec * SR / BLK), &out); for (int i = 0; i < n; ++i) a.note (notes[i], 0); a.render (40); };
    printf ("\ntp69 — A SINE CHORD: TERRAIN vs SERUM 2 (installed AUs, Cm11 C4 D4 Eb4 F4 G4 Bb4, vel 100, steady state)\n");
    { Au t; if (! t.open ('Tern', 'Wvcr')) { printf ("  !! no Terrain AU\n"); return 2; }
      t.pump (1.0); t.render (20); std::vector<float> x;
      play (t, single, 1, 4.0, x); report ("TERRAIN — C4 alone (default patch: OSC A wavetable, Sine)", measure (x, single, 1, from), single, 1);
      x.clear(); play (t, chord, 6, 4.0, x); report ("TERRAIN — the Cm11", measure (x, chord, 6, from), chord, 6);
      t.set ("Output Gain", 0.25f); x.clear(); play (t, chord, 6, 4.0, x); report ("TERRAIN — the Cm11 with Output Gain at 0.25 (was 0.5)", measure (x, chord, 6, from), chord, 6);
      t.close(); }
    { Au s; if (! s.open ('Xf2X', 'XFER')) { printf ("\n  !! no Serum 2 AU (Xf2X/XFER)\n"); return 3; }
      s.pump (1.5); s.render (20); std::vector<float> x;
      play (s, single, 1, 4.0, x); report ("SERUM 2 — C4 alone (init patch)", measure (x, single, 1, from), single, 1);
      x.clear(); play (s, chord, 6, 4.0, x); report ("SERUM 2 — the Cm11", measure (x, chord, 6, from), chord, 6);
      s.close(); }
    printf ("\n"); return 0;
}
