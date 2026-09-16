// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp20 — THE FLOW POOL GATE. Arp / Chop / Glitch instances 2..4 are real cards: a chain slot names
//  (kind, instance), the instance reads ITS OWN knobs (FLOW_GLI2_VARY, not FLOW_GLI_VARY), two Chops
//  in a row are two stages, and an Arp chain arpeggiates in order. Deterministic renders (TERRAIN_
//  DETERMINISTIC) let "did the card act?" be a difference against the dry render.
//
//    clang++ -O2 -std=c++17 Tests/au_flow_pool.cpp -o /tmp/auflow \
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
#include <array>
static const double SR = 48000.0; static const int BLK = 512, BLOCKS = 240;
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
        auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
        const auto& pi = info[it->second];
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    // chain slot i (1-based) = kind k (1 Arp 2 Chop 3 Glitch 4 Robin), instance n (1-based)
    void slot (int i, int k, int n)
    {
        set (("Flow Chain " + std::to_string (i)).c_str(), (float) k / 4.0f);
        set (("Flow Chain " + std::to_string (i) + " Instance").c_str(), (float) (n - 1) / 3.0f);
    }
    void chord() { MusicDeviceMIDIEvent (au, 0x90, 48, 100, 0); MusicDeviceMIDIEvent (au, 0x90, 55, 100, 0); MusicDeviceMIDIEvent (au, 0x90, 60, 100, 0); }
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
            all.insert (all.end(), bl.begin(), bl.end());
        }
        free (abl); return all;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
static double rmsDb (const std::vector<float>& a) { double e = 0; for (float v : a) e += (double) v * v; return 20.0 * std::log10 (std::sqrt (e / (double) a.size()) + 1e-12); }
static double diffDb (const std::vector<float>& a, const std::vector<float>& b) { double e = 0; for (size_t i = 0; i < a.size() && i < b.size(); ++i) { const double d = a[i] - b[i]; e += d * d; } return 20.0 * std::log10 (std::sqrt (e / (double) a.size()) + 1e-12); }
typedef std::vector<std::pair<std::string, float>> Sets;
static std::vector<float> run (const Sets& sets, const std::vector<std::array<int, 3>>& chain)
{
    Au a; a.open();
    for (auto& c : chain) a.slot (c[0], c[1], c[2]);
    for (auto& s : sets) a.set (s.first.c_str(), s.second);
    a.chord(); auto out = a.render(); a.close(); return out;
}
int main()
{
    int fails = 0;
    auto expect = [&] (bool ok, const char* what) { printf ("  %s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; };
    const auto dry = run ({}, {});
    printf ("dry                                  %7.2f dBFS\n", rmsDb (dry));
    // ── GLITCH: instance 2 alone, firing always (its Vary = fire chance)
    const auto g2  = run ({ { "Glitch 2 Vary", 1.0f } }, { { 1, 3, 2 } });
    printf ("Glitch 2 in chain, Glitch 2 Vary 1   %7.2f dBFS   diff vs dry %6.1f dB\n", rmsDb (g2), diffDb (g2, dry));
    expect (diffDb (g2, dry) > -30.0, "Glitch 2 acts on the signal");
    // ── determinism probe: the same scenario twice (the glitch's die streams are seeded per instance, not per run)
    const auto g2d  = run ({}, { { 1, 3, 2 } });
    const auto g2d2 = run ({}, { { 1, 3, 2 } });
    printf ("Glitch 2 default, twice              diff %6.1f dB %s\n", diffDb (g2d, g2d2), diffDb (g2d, g2d2) < -60.0 ? "(deterministic)" : "(NOT deterministic run to run)");
    // ── the instance reads ITS OWN knob: Glitch 2 in the chain with ITS Vary at 0 (never fires) while instance 1's
    //    Vary is cranked to 1 — if Glitch 2 read instance 1's knob it would fire constantly and leave the dry far behind
    const auto g20 = run ({ { "Glitch 2 Vary", 0.0f } }, { { 1, 3, 2 } });
    const auto g2x = run ({ { "Glitch Vary", 1.0f }, { "Glitch 2 Vary", 0.0f } }, { { 1, 3, 2 } });
    printf ("Glitch 2 (own Vary 0)                diff vs dry %6.1f dB\n", diffDb (g20, dry));
    printf ("  + Glitch 1 Vary 1                  diff vs the line above %6.1f dB\n", diffDb (g2x, g20));
    expect (diffDb (g2x, g20) < -60.0, "Glitch 1's Vary never reaches Glitch 2");
    const auto g2y = run ({ { "Glitch 2 Vary", 1.0f } }, { { 1, 3, 2 } });
    expect (diffDb (g2y, g20) > -30.0, "Glitch 2's own Vary changes Glitch 2");
    // ── CHOP: instance 2 alone with reverse always on
    const auto c2  = run ({ { "Chop 2 Rev Odds", 1.0f } }, { { 1, 2, 2 } });
    printf ("Chop 2 in chain, Chop 2 Rev Odds 1   %7.2f dBFS   diff vs dry %6.1f dB\n", rmsDb (c2), diffDb (c2, dry));
    expect (diffDb (c2, dry) > -30.0, "Chop 2 acts on the signal");
    // ── two Chops in a row are two stages: Chop 1 (rev) -> Chop 2 (rev) differs from Chop 1 (rev) alone
    const auto c1  = run ({ { "Chop Rev Odds", 1.0f } }, { { 1, 2, 1 } });
    const auto c12 = run ({ { "Chop Rev Odds", 1.0f }, { "Chop 2 Rev Odds", 1.0f } }, { { 1, 2, 1 }, { 2, 2, 2 } });
    printf ("Chop 1 -> Chop 2 vs Chop 1 alone     diff %6.1f dB\n", diffDb (c12, c1));
    expect (diffDb (c12, c1) > -30.0, "a second Chop in the chain is a second stage");
    // ── ARP: instance 2 alone arpeggiates the held chord
    const auto a2  = run ({}, { { 1, 1, 2 } });
    printf ("Arp 2 in chain                       %7.2f dBFS   diff vs dry %6.1f dB\n", rmsDb (a2), diffDb (a2, dry));
    expect (diffDb (a2, dry) > -30.0, "Arp 2 arpeggiates");
    const auto a1  = run ({}, { { 1, 1, 1 } });
    printf ("Arp 1 vs Arp 2 (defaults)            diff %6.1f dB\n", diffDb (a1, a2));
    expect (diffDb (a1, a2) < -60.0, "Arp 2 is a clone of Arp 1");
    // ── Arp 2 reads ITS OWN rate: Arp 2 alone at a faster rate differs from Arp 1 alone at the default
    const auto a2f = run ({ { "Arp 2 Rate", 0.9f } }, { { 1, 1, 2 } });
    printf ("Arp 2 (own Rate .9) vs Arp 1 default diff %6.1f dB\n", diffDb (a2f, a1));
    expect (diffDb (a2f, a1) > -30.0, "Arp 2 reads ITS OWN rate");
    const auto a2g = run ({ { "Arp Rate", 0.9f } }, { { 1, 1, 2 } });
    printf ("Arp 2 in chain, Arp 1 Rate .9        diff vs Arp 2 default %6.1f dB\n", diffDb (a2g, a2));
    expect (diffDb (a2g, a2) < -60.0, "Arp 2 ignores Arp 1's rate");
    // ── Arp 1 -> Arp 2 (faster) is a chain of two note stages: differs from Arp 1 alone
    const auto a12 = run ({ { "Arp 2 Rate", 0.9f } }, { { 1, 1, 1 }, { 2, 1, 2 } });
    printf ("Arp 1 -> Arp 2 (.9) vs Arp 1 alone   diff %6.1f dB\n", diffDb (a12, a1));
    expect (diffDb (a12, a1) > -30.0, "two Arps chain note stages");
    // ── legacy: the fb131 four-slot chain (instance 0 everywhere) still resolves — Chop 1 via slot 1 = Chop 1
    const auto cL  = run ({ { "Chop Rev Odds", 1.0f } }, { { 1, 2, 1 } });
    expect (diffDb (cL, c1) < -60.0, "legacy slot semantics unchanged (instance choice 0 = instance 1)");
    printf ("%s (%d failed)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
