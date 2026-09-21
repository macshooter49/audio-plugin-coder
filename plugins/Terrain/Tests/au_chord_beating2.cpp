// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_chord_beating2.cpp — tp69 · PART 2: the limiter over time, the start phase, Serum's own sine
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

// ── envelope helpers ──
static std::vector<double> envRms (const std::vector<float>& x, int win)
{ std::vector<double> e; for (size_t i = 0; i + (size_t) win <= x.size(); i += (size_t) win) { double s = 0; for (int k = 0; k < win; ++k) s += (double) x[i + (size_t) k] * x[i + (size_t) k]; e.push_back (std::sqrt (s / win)); } return e; }
static double peakIn (const std::vector<float>& x, size_t a, size_t b) { double p = 0; for (size_t i = a; i < b && i < x.size(); ++i) p = std::max (p, (double) std::fabs (x[i])); return p; }

int main()
{
    const int chord[6] = { 60, 62, 63, 65, 67, 70 };
    const int single[1] = { 60 };
    const size_t from = (size_t) (SR * 1.5);
    auto play = [&] (Au& a, const int* notes, int n, double sec, std::vector<float>& out)
    { a.reset(); a.render (10); for (int i = 0; i < n; ++i) a.note (notes[i], 100); a.render ((int) (sec * SR / BLK), &out); for (int i = 0; i < n; ++i) a.note (notes[i], 0); a.render (40); };
    printf ("\ntp69 — PART 2: THE LIMITER, THE PHASE, AND SERUM'S OWN SINE\n");

    // ── an ideal reference: six pure sines at Terrain's single-note amplitude (-15.1 dBFS peak) ──
    {
        const double A = std::pow (10.0, -15.14 / 20.0); double f[6]; for (int i = 0; i < 6; ++i) f[i] = hzOf (chord[i]);
        auto crestOf = [&] (const double* ph) { double pk = 0, e = 0; const size_t N = (size_t) (SR * 3.0); for (size_t n = 0; n < N; ++n) { double v = 0; for (int i = 0; i < 6; ++i) v += A * std::sin (2 * M_PI * f[i] * (double) n / SR + ph[i]); pk = std::max (pk, std::fabs (v)); e += v * v; } return std::make_pair (20 * std::log10 (pk), 20 * std::log10 (pk) - 20 * std::log10 (std::sqrt (e / N))); };
        double z[6] = { 0, 0, 0, 0, 0, 0 }; auto r0 = crestOf (z);
        double pkSum = 0, crSum = 0; unsigned s = 12345;
        for (int t = 0; t < 12; ++t) { double ph[6]; for (int i = 0; i < 6; ++i) { s = s * 1103515245u + 12345u; ph[i] = 2 * M_PI * ((s >> 8) & 0xffff) / 65536.0; } auto r = crestOf (ph); pkSum += r.first; crSum += r.second; }
        printf ("\n  ── IDEAL: six pure sines at -15.1 dBFS each ──\n");
        printf ("     all six start at phase 0 (Terrain's Manual):  peak %+.2f dBFS  crest %.2f dB   ← over the -0.9 dBFS limiter threshold\n", r0.first, r0.second);
        printf ("     random start phases, 12 trials (Serum's law): peak %+.2f dBFS  crest %.2f dB  (mean)\n", pkSum / 12, crSum / 12);
        printf ("     (the steady-state peak of a beating chord is set by the notes, not the start phases — the START is what differs)\n");
    }

    { Au t; if (! t.open ('Tern', 'Wvcr')) { printf ("  !! no Terrain AU\n"); return 2; }
      t.pump (1.0); t.render (20);
      std::vector<float> x05, x025;
      play (t, chord, 6, 4.0, x05);
      t.set ("Output Gain", 0.25f); play (t, chord, 6, 4.0, x025); t.set ("Output Gain", 0.5f);
      // the limiter's footprint: what the 0.5 render lost against the 0.25 render doubled, 5 ms windows, steady state
      const int W = 240; auto e05 = envRms (x05, W), e025 = envRms (x025, W);
      double grMin = 0, grMax = -99, grSum = 0; int n = 0, busy = 0; const size_t k0 = from / W;
      for (size_t k = k0; k < e05.size() && k < e025.size(); ++k) { if (e025[k] < 1e-6) continue; const double gr = 20 * std::log10 (e05[k] / (2.0 * e025[k])); grMin = std::min (grMin, gr); grMax = std::max (grMax, gr); grSum += gr; ++n; if (gr < -0.5) ++busy; }
      printf ("\n  ── TERRAIN — the master limiter on the Cm11 (0.5 gain vs 0.25 gain ×2, 5 ms windows, steady state) ──\n");
      printf ("     gain reduction: deepest %.2f dB, shallowest %.2f dB, mean %.2f dB; swinging by %.2f dB; below -0.5 dB %d %% of the time\n", grMin, grMax, grSum / std::max (1, n), grMax - grMin, 100 * busy / std::max (1, n));
      printf ("     attack: the chord's first 30 ms peak %.2f dBFS (0.5 gain) — the six start together\n", 20 * std::log10 (peakIn (x05, (size_t) (10 * BLK), (size_t) (10 * BLK + SR * 0.03)) + 1e-12));
      // the phase law: Random start phases (mode 2) — the attack and the pre-limiter peak
      for (const char* o : { "Synth OSC A Phase Mode" }) t.set (o, 2.0f);
      std::vector<float> xr, xr025; play (t, chord, 6, 4.0, xr); t.set ("Output Gain", 0.25f); play (t, chord, 6, 4.0, xr025); t.set ("Output Gain", 0.5f);
      auto er = envRms (xr, W), er025 = envRms (xr025, W); double grMinR = 0; int busyR = 0, nR = 0;
      for (size_t k = k0; k < er.size() && k < er025.size(); ++k) { if (er025[k] < 1e-6) continue; const double gr = 20 * std::log10 (er[k] / (2.0 * er025[k])); grMinR = std::min (grMinR, gr); ++nR; if (gr < -0.5) ++busyR; }
      Meas mr = measure (xr, chord, 6, from);
      printf ("     with Phase Mode = Random: attack peak %.2f dBFS, steady peak %.2f dBFS, limiter deepest %.2f dB, busy %d %%; non-note content %.1f dB down\n",
              20 * std::log10 (peakIn (xr, (size_t) (10 * BLK), (size_t) (10 * BLK + SR * 0.03)) + 1e-12), mr.peak, grMinR, 100 * busyR / std::max (1, nR), -mr.spurEnergyDb);
      t.set ("Synth OSC A Phase Mode", 0.0f);
      t.close(); }

    { Au s; if (! s.open ('Xf2X', 'XFER')) { printf ("\n  !! no Serum 2 AU\n"); return 3; }
      s.pump (1.5); s.render (20);
      s.set ("A Enable", 0.0f); s.set ("Sub Enable", 1.0f); s.pump (0.3); s.render (20);
      std::vector<float> x;
      play (s, single, 1, 4.0, x); Meas m1 = measure (x, single, 1, from); report ("SERUM 2 — C4 alone on the SUB oscillator (its sine)", m1, single, 1);
      x.clear(); play (s, chord, 6, 4.0, x); Meas m6 = measure (x, chord, 6, from); report ("SERUM 2 — the Cm11 on the sub sine", m6, chord, 6);
      printf ("     attack: first 30 ms peak %.2f dBFS\n", 20 * std::log10 (peakIn (x, (size_t) (10 * BLK), (size_t) (10 * BLK + SR * 0.03)) + 1e-12));
      // is there a limiter? the chord's peak vs six times one note
      printf ("     six notes vs one: %.2f dB apart (six coherent sines would be +15.6 dB; a limiter would hold it under 0)\n", m6.peak - m1.peak);
      s.close(); }
    printf ("\n"); return 0;
}
