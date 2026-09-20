// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_tape_types.cpp — tp64 · THE FIVE TAPE TYPES REACH THE ENGINE (the installed AU).
//
//  Max: "these three don't need a back panel … take the DSP from the old three and just put it there."
//  The rack's Tape card declared five types since tp60, and tapemodes_cert.cpp proved the ENGINE
//  renders five different machines — but PluginProcessor::applyTpe clamped the choice with
//  `(ty >= 0 && ty <= 1) ? ty : 0` from the day the card was born, so Reel / Porta / Wire ran as
//  Studio in every host. This is the bar that was missing: through the AU, with the card powered and
//  in the chain, each type's spectrum must differ from every other's — and 2 / 3 / 4 from 0 most of
//  all, because before tp64 those three WERE type 0.
//
//  clang++ -O2 -std=c++17 Tests/au_tape_types.cpp -o /tmp/autape -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/autape
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>

static const double SR = 48000.0; static const int BLK = 512;
static int npass = 0, nfail = 0;
static void chk (bool ok, const std::string& what, const std::string& d = "")
{ printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what.c_str()); if (! d.empty()) printf ("        %s\n", d.c_str()); ok ? ++npass : ++nfail; }

struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; double stamp = 0;
    bool open()
    {
        setenv ("TERRAIN_DETERMINISTIC", "1", 1);
        AudioComponentDescription d {}; d.componentType = kAudioUnitType_MusicDevice; d.componentSubType = 'Tern'; d.componentManufacturer = 'Wvcr';
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
    bool set (const std::string& n, float v) { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; } return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v > 0 ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void render (int blocks, std::vector<float>* keep = nullptr)
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
            if (keep) keep->insert (keep->end(), L.begin(), L.end());
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.002, false);
        }
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};

// 32 log bands, 80 Hz .. 16 kHz, mean power in dB over 2048-sample windows (the same measure
// tapemodes_cert.cpp uses on the bare engine, so the numbers read against its 6.37 dB floor).
static std::vector<double> bands (const std::vector<float>& x)
{
    const int N = 2048, NB = 32; std::vector<double> acc (NB, 0.0); int wins = 0;
    std::vector<double> edges (NB + 1); for (int b = 0; b <= NB; ++b) edges[(size_t) b] = 80.0 * std::pow (16000.0 / 80.0, (double) b / NB);
    for (size_t s = 0; s + N <= x.size(); s += N)
    {
        for (int b = 0; b < NB; ++b)
        {
            const int k0 = (int) std::floor (edges[(size_t) b] * N / SR), k1 = std::max (k0 + 1, (int) std::floor (edges[(size_t) b + 1] * N / SR));
            double p = 0;
            for (int k = k0; k < k1; ++k)
            {
                double re = 0, im = 0; const double w = 2.0 * M_PI * k / N;
                for (int n = 0; n < N; ++n) { const double v = x[s + (size_t) n] * (0.5 - 0.5 * std::cos (2.0 * M_PI * n / N)); re += v * std::cos (w * n); im -= v * std::sin (w * n); }
                p += re * re + im * im;
            }
            acc[(size_t) b] += p / (k1 - k0);
        }
        ++wins;
    }
    for (auto& v : acc) v = 10.0 * std::log10 (v / std::max (1, wins) + 1e-12);
    return acc;
}
static double dist (const std::vector<double>& a, const std::vector<double>& b)
{ double d = 0; int n = 0; for (size_t i = 0; i < a.size(); ++i) { if (a[i] < -90 && b[i] < -90) continue; d += std::fabs (a[i] - b[i]); ++n; } return n ? d / n : 0; }

int main()
{
    Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); return 2; }
    printf ("\ntp64 — THE FIVE TAPE TYPES, THROUGH THE INSTALLED AU\n\n");
    a.pump (0.8); a.render (20);
    // the rack's Tape card (instance 1): powered, in the chain, fed by OSC A, fully wet so the type is all we hear
    bool okp = true;
    okp &= a.set ("Tape Power", 1); okp &= a.set ("Tape In Chain", 1); okp &= a.set ("Tape Mix", 1.0f);
    okp &= a.set ("Tape SRC_A", 1);   // the card's source pill: the engine is built lazily only once the slot is powered AND routed
    a.pump (1.5); a.render (40);   // the engine is built lazily on the timer once the slot is powered and routed
    chk (okp, "[0] the Tape card's Power / In Chain / Mix / SRC_A parameters exist on the AU");

    const char* names[5] = { "Studio", "Cassette", "Reel", "Porta", "Wire" };
    std::vector<std::vector<double>> spec (5);
    double rms[5] = {};
    for (int t = 0; t < 5; ++t)
    {
        a.set ("Tape Type", (float) t);
        a.note (48, 100); a.note (55, 100); a.note (60, 100);
        a.render (140);                       // 1.5 s: the type re-seat (a ~20 ms duck + the 75 ms crossfade) is long gone
        std::vector<float> x; a.render (94, &x);   // 1.0 s measured
        a.note (48, 0); a.note (55, 0); a.note (60, 0); a.render (60); a.pump (0.2);
        double e = 0; for (float v : x) e += (double) v * v; rms[t] = 20.0 * std::log10 (std::sqrt (e / (double) x.size()) + 1e-12);
        spec[(size_t) t] = bands (x);
        printf ("  type %d %-9s rms %6.1f dBFS\n", t, names[t], rms[t]);
    }
    bool loud = true; for (int t = 0; t < 5; ++t) loud &= rms[t] > -50.0;
    chk (loud, "[1] every type sounds through the card (all five above -50 dBFS)");

    double worst = 1e9, worstVs0 = 1e9; std::string pairs;
    for (int i = 0; i < 5; ++i) for (int j = i + 1; j < 5; ++j)
    {
        const double d = dist (spec[(size_t) i], spec[(size_t) j]);
        char b[96]; snprintf (b, sizeof b, "%s/%s %.2f dB  ", names[i], names[j], d); pairs += b;
        worst = std::min (worst, d);
        if (i == 0) worstVs0 = std::min (worstVs0, d);
    }
    printf ("  %s\n", pairs.c_str());
    // 1.0 dB of mean band distance is well above the run-to-run noise of a deterministic render
    // (two renders of the same type agree to < 0.1 dB) and well under the bare engine's closest
    // shipped pair (6.37 dB, tapemodes_cert.cpp): the transport, the echo and the mix sit between.
    chk (worstVs0 > 1.0, "🚨 [2] Reel, Porta and Wire each differ from Studio (they WERE Studio before tp64: the applyTpe clamp)",
         "closest to Studio: " + std::to_string (worstVs0) + " dB");
    chk (worst > 1.0, "[3] all ten pairs differ — five types, five sounds", "closest pair: " + std::to_string (worst) + " dB");

    // and the Reel's front row is the Sculptor's: its Tilt moves the sound, its Wow does not need to
    a.set ("Tape Type", 2);
    auto grab = [&] { a.note (48, 100); a.note (55, 100); a.note (60, 100); a.render (100); std::vector<float> x; a.render (94, &x); a.note (48, 0); a.note (55, 0); a.note (60, 0); a.render (60); return bands (x); };
    a.set ("Tape Tilt", 0.5f); auto mid = grab();
    a.set ("Tape Tilt", 1.0f); auto hi  = grab();
    a.set ("Tape Tilt", 0.0f); auto lo  = grab();
    a.set ("Tape Tilt", 0.5f);
    const double dTilt = std::max (dist (mid, hi), dist (mid, lo));
    chk (dTilt > 0.5, "[4] the Reel's TILT (Timbre) reaches the Harmonic Sculptor", "tilt 0.5 → 1.0 / 0.0: " + std::to_string (dTilt) + " dB");

    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    a.close(); return nfail == 0 ? 0 : 1;
}
