// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb464 — WHAT DOES BLUR ACTUALLY DO?  Measured on the installed AU, before touching any DSP.
//
//    clang++ -O2 -std=c++17 Tests/blur_audit.cpp -o /tmp/blur_audit \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  Max: "blur isn't really doing much, like not at all… whatever blur does I just think it's kind
//  of boring. I don't think it needs to have that parameter space unless it does something
//  incredible… we can use audio harnesses to really see that."
//
//  So: no DSP opinions until there are numbers. For each (preset × WT position) this sweeps Blur
//  0 → 100 % and reports, against the SAME render at Blur 0:
//    · Δ harmonics  — the summed change across the first 24 harmonics, in dB relative to the
//                     un-blurred harmonic energy. This is the honest "did the TIMBRE change".
//    · centroid     — where the spectral energy sits (harmonic number). Blur is a weighted MEAN
//                     of neighbouring frames, so if it does anything, the centroid should drift.
//    · null         — plain RMS difference of the waveform, for reference.
//  Harmonic magnitudes are computed by direct correlation at k·f0, so no FFT window games and no
//  sensitivity to phase.
//
//  THE HYPOTHESIS BEING TESTED (from reading Wavetable::renderBlend): blur's Gaussian width is
//  sigma = 0.0001 + blur²·9 measured in ABSOLUTE FRAMES. A factory table has 16 frames, so blur
//  100 % (sigma 9) smears across the whole table — but an imported 256-frame table would see
//  ±36 of 256, i.e. almost nothing. And renderBlend RMS-matches its output to the un-blurred
//  frame, so it cannot change loudness either. A weighted mean of similar frames ≈ the original.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static const double SR = 48000.0; static const int BLK = 512;
static const int WARM = 60, MEAS = 90;          // ~1 s measured, well after the attack
static const int NH = 24;                        // harmonics analysed

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
    bool has (const std::string& n) { return byName.count (n) > 0; }
    void setNorm (const std::string& n, float v)
    { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return; }
      const auto& pi = info.at (it->second);
      AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + v * (pi.maxValue - pi.minValue), 0); }
    static float toNorm (float v, float lo, float hi) { const float p = (v - lo) / (hi - lo); return p <= 0.f ? 0.f : std::pow (p, 0.3f); }

    std::vector<float> renderNote (int note)
    {
        MusicDeviceMIDIEvent (au, 0x90, note, 100, 0);
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK), out;
        out.reserve ((size_t) MEAS * BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
        for (int b = 0; b < WARM + MEAS; ++b)
        {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            ts.mSampleTime += BLK;
            if (b % 6 == 0) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.0, false);
            if (b >= WARM) out.insert (out.end(), bl.begin(), bl.end());
        }
        free (abl);
        return out;
    }
};

// magnitude at k*f0 by direct correlation — phase-independent, no window artefacts
static void harmonics (const std::vector<float>& x, double f0, double* h)
{
    const int N = (int) x.size();
    for (int k = 1; k <= NH; ++k)
    {
        const double w = 2.0 * M_PI * (f0 * k) / SR;
        double re = 0, im = 0;
        for (int n = 0; n < N; ++n) { re += x[(size_t) n] * std::cos (w * n); im += x[(size_t) n] * std::sin (w * n); }
        h[k - 1] = 2.0 * std::sqrt (re * re + im * im) / N;
    }
}
static double dbOf (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

int main()
{
    printf ("\n══ fb464 — WHAT DOES BLUR ACTUALLY DO? (installed AU, OSC A, unison 1) ══\n");
    const int NOTE = 48; const double F0 = 440.0 * std::pow (2.0, (NOTE - 69) / 12.0);
    printf ("   note %d = %.2f Hz · %d harmonics · factory tables are 16 frames\n\n", NOTE, F0, NH);

    struct Preset { const char* name; float norm; };
    // preset choice is exposed with its real index range; these are fractions of it
    const Preset PRESETS[] = { { "Sine", 0.00f }, { "Prophet Saw", 0.20f }, { "Jupiter PWM", 0.26f } };
    const float POS[]  = { 0.0f, 0.35f, 0.70f };
    const float BLUR[] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.00f };

    for (const auto& pr : PRESETS)
      for (float pos : POS)
      {
        double h0[NH] = {0}; std::vector<float> ref;
        printf ("  ── %-12s  WT Pos %.2f\n", pr.name, pos);
        for (float bl : BLUR)
        {
            AU a; if (! a.open()) return 2;
            a.setNorm ("Synth Amp Attack",  AU::toNorm (5.f, 1.f, 10000.f));
            a.setNorm ("Synth Amp Decay",   AU::toNorm (9000.f, 1.f, 10000.f));
            a.setNorm ("Synth Amp Sustain", 1.0f);
            a.setNorm ("Synth OSC A WT Preset", pr.norm);
            a.setNorm ("Synth OSC A WT Frame",  pos);
            a.setNorm ("OSC A Blur",            bl);
            auto x = a.renderNote (NOTE); a.close();
            double h[NH]; harmonics (x, F0, h);

            if (bl == 0.0f) { std::copy (h, h + NH, h0); ref = x; printf ("      blur   0%%   (reference)\n"); continue; }

            double num = 0, den = 0, cN = 0, cD = 0, c0N = 0, c0D = 0;
            for (int k = 0; k < NH; ++k)
            { const double d = h[k] - h0[k]; num += d * d; den += h0[k] * h0[k];
              cN += h[k] * (k + 1); cD += h[k]; c0N += h0[k] * (k + 1); c0D += h0[k]; }
            double dn = 0, rf = 0;
            const size_t n = std::min (x.size(), ref.size());
            for (size_t i = 0; i < n; ++i) { const double d = x[i] - ref[i]; dn += d * d; rf += (double) ref[i] * ref[i]; }
            printf ("      blur %3.0f%%   Δharm %7.2f dB   centroid %5.2f → %5.2f   null %7.2f dB\n",
                    bl * 100.0, dbOf (std::sqrt (num / std::max (den, 1e-30))),
                    c0D > 0 ? c0N / c0D : 0.0, cD > 0 ? cN / cD : 0.0,
                    dbOf (std::sqrt (dn / std::max (rf, 1e-30))));
        }
        printf ("\n");
      }
    return 0;
}
