// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp20 — THE OSCILLATOR POOL GATE. Oscillators E–H are a second voice bank built lazily on the
//  message thread. This proves, on the installed AU, that (1) a patch that never touches E–H is
//  untouched (see Tests/pool_identity.cpp for the bit-identity half), (2) switching E on makes it
//  sound, (3) E reads ITS OWN knobs (level, octave), (4) E reaches the filters and the rack sends,
//  and (5) without a message thread the bank never appears (the tp19 lesson, documented not hidden).
//
//    clang++ -O2 -std=c++17 Tests/au_osc_pool.cpp -o /tmp/aupool \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    /tmp/aupool            -> the scenarios, exit 1 on any failed expectation
//    /tmp/aupool list <sub> -> print every parameter whose name contains <sub>
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
static const double SR = 48000.0; static const int BLK = 512;
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
    bool set (const char* name, float norm)   // NORMALISED 0..1 — the AU exposes every JUCE parameter over [minValue, maxValue]
    {
        auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
        const auto& pi = info[it->second];
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    void chord() { MusicDeviceMIDIEvent (au, 0x90, 48, 100, 0); MusicDeviceMIDIEvent (au, 0x90, 60, 100, 0); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    // renders `blocks` blocks, pumping the run loop between them when asked; returns RMS dBFS and the zero-crossing rate (per second) of L
    void render (int blocks, bool pumpLoop, double& rmsDb, double& zcr)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        double e = 0.0; long n = 0; long zc = 0; float prev = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = (double) b * BLK; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            if (b >= blocks / 2)   // measure the second half (envelopes settled)
                for (int i = 0; i < BLK; ++i) { e += (double) bl[i] * bl[i]; ++n; if ((bl[i] >= 0.0f) != (prev >= 0.0f)) ++zc; prev = bl[i]; }
            if (pumpLoop) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.004, false);
        }
        free (abl);
        rmsDb = n > 0 ? 20.0 * std::log10 (std::sqrt (e / (double) n) + 1e-12) : -200.0;
        zcr   = n > 0 ? (double) zc / ((double) n / SR) : 0.0;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
int main (int argc, char** argv)
{
    if (argc > 2 && ! std::strcmp (argv[1], "list"))
    {
        Au a; if (! a.open()) { printf ("no AU\n"); return 2; }
        for (auto& kv : a.byName) if (kv.first.find (argv[2]) != std::string::npos) printf ("  %s\n", kv.first.c_str());
        printf ("  (%zu parameters)\n", a.byName.size()); return 0;
    }
    if (argc > 3 && ! std::strcmp (argv[1], "try"))   // try <A|E> name norm [name norm ...] -> RMS + zcr, with the parameter infos printed
    {
        Au a; if (! a.open()) { printf ("no AU\n"); return 2; }
        const bool useE = argv[2][0] == 'E';
        if (useE) { a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); }
        for (int i = 3; i + 1 < argc; i += 2)
        {
            auto it = a.byName.find (argv[i]);
            if (it != a.byName.end()) { const auto& pi = a.info[it->second]; printf ("  %-32s min %g max %g def %g unit %u -> norm %s\n", argv[i], pi.minValue, pi.maxValue, pi.defaultValue, (unsigned) pi.unit, argv[i + 1]); }
            a.set (argv[i], (float) atof (argv[i + 1]));
        }
        a.pump (1.2); a.chord(); double r = 0, z = 0; a.render (240, true, r, z); a.close();
        printf ("  -> %7.2f dBFS  zcr %.0f/s\n", r, z); return 0;
    }
    if (argc > 1 && ! std::strcmp (argv[1], "seq"))   // instance 1: E only, level 0 · instance 2: E only (default) — does anything leak between instances?
    {
        double r = 0, z = 0;
        { Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); a.set ("Synth OSC E Level", 0.0f); a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close(); printf ("  #1 E level 0: %7.2f dBFS\n", r); }
        { Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close(); printf ("  #2 E default: %7.2f dBFS  zcr %.0f\n", r, z); }
        { Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close(); printf ("  #3 E default: %7.2f dBFS  zcr %.0f\n", r, z); }
        for (int k = 4; k <= 6; ++k)
        { Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); for (int i = 2; i + 1 < argc; i += 2) a.set (argv[i], (float) atof (argv[i + 1]));
          a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close(); printf ("  #%d E + args: %7.2f dBFS  zcr %.0f\n", k, r, z); }
        return 0;
    }
    int fails = 0;
    auto expect = [&] (bool ok, const char* what) { printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; };
    double rA = 0, zA = 0, r = 0, z = 0;
    {   // S0 — the default patch: oscillator A alone
        Au a; a.open(); a.chord(); a.render (240, true, rA, zA); a.close();
        printf ("S0 A only                 %7.2f dBFS  zcr %.0f/s\n", rA, zA);
        expect (rA > -40.0, "A sounds");
    }
    {   // S1 — E switched on WITHOUT a message thread: the bank cannot build, so the sound is A alone (documented)
        Au a; a.open(); a.set ("Osc E Enable", 1.0f); a.chord(); a.render (240, false, r, z); a.close();
        printf ("S1 A + E, no message thread %5.2f dBFS\n", r);
        expect (std::abs (r - rA) < 0.05, "no message thread -> no bank 1 (A alone)");
    }
    {   // S2 — E switched on WITH the message thread: the bank builds on the timer and E joins A
        Au a; a.open(); a.set ("Osc E Enable", 1.0f); a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close();
        printf ("S2 A + E (bank built)     %7.2f dBFS\n", r);
        expect (r > rA + 0.5, "E adds level on top of A");
    }
    double rE = 0, zE = 0;
    {   // S3 — E alone
        Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); a.pump (1.2); a.chord(); a.render (240, true, rE, zE); a.close();
        printf ("S3 E only                 %7.2f dBFS  zcr %.0f/s\n", rE, zE);
        expect (rE > -40.0, "E sounds on its own");
    }
    {   // S4 — E alone, its LEVEL at zero: bank 1 reads E's knobs, not A's
        Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); a.set ("Synth OSC E Level", 0.0f); a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close();
        printf ("S4 E only, E level 0      %7.2f dBFS\n", r);
        expect (r < -70.0, "E's own level knob silences E");
    }
    {   // S5 — E alone, one octave up: E's own tuning (zero-crossing rate roughly doubles)
        Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); a.set ("Synth OSC E Octave", 4.0f / 6.0f); /* -3..+3 -> +1 */ a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close();
        printf ("S5 E only, +1 octave      %7.2f dBFS  zcr %.0f/s (E at 0 oct: %.0f/s)\n", r, z, zE);
        expect (z > zE * 1.5 && z < zE * 3.0, "E's own octave knob moves E's pitch one octave");
    }
    {   // S6 — E alone through filter 1 with the cutoff closed: bank 1's filter send works
        Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f); a.set ("Synth OSC E Filter 1 Send", 1.0f); a.set ("Synth Filter 1 Type", 0.01f); a.set ("Synth Filter 1 Cutoff", 0.0f);   // type 27 = NONE by default
        a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close();
        printf ("S6 E only -> filter 1 closed %5.2f dBFS\n", r);
        expect (r < rE - 12.0, "E through a closed filter 1 is much quieter");
    }
    {   // S7 — A alone with the SAME state as S3 but E off: identical to S0 (E's presence never changes A)
        Au a; a.open(); a.set ("Osc E Enable", 1.0f); a.pump (1.2); a.set ("Osc E Enable", 0.0f); a.chord(); a.render (240, true, r, z); a.close();
        printf ("S7 A only, bank built idle %6.2f dBFS\n", r);
        expect (std::abs (r - rA) < 0.05, "a built but silent bank 1 leaves A exactly alone");
    }
    {   // S8 — E alone, routed into a powered Tape card (cassette) via ITS pill: the tp19 law holds for bank 1 — the
        //      pulled source comes back through the running send (no gate), so the output stays within a few dB of dry E
        Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f);
        a.set ("Tape In Chain", 1.0f); a.set ("Tape Power", 1.0f); a.set ("Tape SRC_E", 1.0f);
        a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close();
        printf ("S8 E only -> Tape (powered, SRC_E) %5.2f dBFS\n", r);
        expect (r > rE - 6.0 && r < rE + 6.0, "E pulled into a powered Tape card is not gated");
    }
    {   // S9 — E alone, routed into a powered Multiband card via ITS pill
        Au a; a.open(); a.set ("Osc A Enable", 0.0f); a.set ("Osc E Enable", 1.0f);
        a.set ("Multiband In Chain", 1.0f); a.set ("Multiband Power", 1.0f); a.set ("Multiband SRC_E", 1.0f);
        a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close();
        printf ("S9 E only -> Multiband (powered, SRC_E) %5.2f dBFS\n", r);
        expect (r > rE - 6.0 && r < rE + 6.0, "E pulled into a powered Multiband card is not gated");
    }
    {   // S10 — A + E, only E routed into a powered Tape card: A stays exactly as in S0's dry path plus E through the card
        Au a; a.open(); a.set ("Osc E Enable", 1.0f);
        a.set ("Tape In Chain", 1.0f); a.set ("Tape Power", 1.0f); a.set ("Tape SRC_E", 1.0f);
        a.pump (1.2); a.chord(); a.render (240, true, r, z); a.close();
        printf ("S10 A dry + E -> Tape           %5.2f dBFS\n", r);
        expect (r > rA + 0.5, "A dry and E through the card both reach the output");
    }
    printf ("%s (%d failed)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
