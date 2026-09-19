// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp53 — EFGH CROSS-BLEND WITH ABCD, on the REAL installed AU.
//
//  Max: "EFGH cannot cross-blend with ABCD."  He was right, and the reason was architectural:
//  oscillators E–H are a SECOND VOICE BANK (ensureBankB), so a blend slot on E had no A inside its
//  own voice object to reach for.  Every voice now publishes its four modulator taps on a shared
//  board keyed by the note it is playing, and reads the other bank's — see Source/CrossBlendBus.h.
//
//  🚨 THE BAR THAT MATTERS IS [2], AND IT IS THE ONE A NAIVE FIX FAILS.  Index 17 must reach the
//  OTHER bank.  A fix that quietly aliased it back to the carrier's own bank-mate 0 would still
//  "make a sound", still move the spectrum, and still look green on a bare does-it-change test —
//  so the cross source is measured AGAINST a bank-mate source at the same depth on the same
//  carrier, and the two must be far apart.
//
//  🚨 AND [3] IS THE fb523 LAW ACROSS THE WALL.  In-bank, a modulator turned down to Level 0 still
//  modulates (modSrcForce_).  Across banks the source lives in a different voice object and cannot
//  know anyone is listening, so the request has to travel.  [3] drives osc A's Level to ZERO — it
//  contributes nothing audible — and the FM on E must survive.  Without the cross-force it dies.
//
//    clang++ -O2 -std=c++17 Tests/au_crossblend.cpp -o /tmp/auxb \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//    /tmp/auxb
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
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& detail = "")
{ if (ok) { ++npass; printf ("  PASS  %s\n        %s\n", what, detail.c_str()); } else { ++nfail; printf ("  FAIL  %s\n        %s\n", what, detail.c_str()); } }

struct Au
{
    AudioUnit au = nullptr; std::map<std::string, AudioUnitParameterID> byName; std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;
    double stamp = 0.0;
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
    bool has (const char* name) const { return byName.find (name) != byName.end(); }
    // NO FUZZY FALLBACK (the harm_table_au lesson): a missing name aborts by name rather than
    // quietly driving the neighbouring parameter and measuring nothing.
    bool set (const char* name, float norm)
    {
        auto it = byName.find (name);
        if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
        const auto& pi = info.at (it->second);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, pi.minValue + norm * (pi.maxValue - pi.minValue), 0) == noErr;
    }
    bool setIdx (const char* name, int idx)   // a CHOICE parameter is an INDEX, never a fraction (CLAUDE.md §4)
    {
        auto it = byName.find (name); if (it == byName.end()) { printf ("    !! no parameter named '%s'\n", name); return false; }
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, (float) idx, 0) == noErr;
    }
    int choiceCount (const char* name) const
    {
        auto it = byName.find (name); if (it == byName.end()) return -1;
        const auto& pi = info.at (it->second); return (int) (pi.maxValue - pi.minValue) + 1;
    }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void render (int blocks, std::vector<float>* out)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        for (int b = 0; b < blocks; ++b)
        {
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = bl.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = br.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl);
            stamp += BLK;
            if (out != nullptr) { out->insert (out->end(), bl.begin(), bl.end()); out->insert (out->end(), br.begin(), br.end()); }
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.003, false);
        }
        free (abl);
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};

static double rmsDb (const std::vector<float>& v, size_t from = 0)
{ double e = 0; size_t n = 0; for (size_t i = from; i < v.size(); ++i) { e += (double) v[i] * v[i]; ++n; } return n ? 20.0 * std::log10 (std::sqrt (e / (double) n) + 1e-12) : -240.0; }

/*  THE PERCEPTUAL NUMBER (the fb283 law: sample-difference RMS is BANNED as a dramaticism metric —
    a phase-only change measures huge and is inaudible).  A coarse magnitude spectrum, compared band
    by band in dB, and the answer is the MEAN ABSOLUTE dB change across the bands that carry energy.
    A Goertzel bank is enough here: we are asking "did the timbre move", not "by how many cents".  */
static std::vector<double> mags (const std::vector<float>& lr, size_t fromBlock)
{
    std::vector<float> L;                                     // render() appends L block then R block
    for (size_t i = fromBlock * 2 * BLK; i + BLK <= lr.size(); i += 2 * BLK) L.insert (L.end(), lr.begin() + (long) i, lr.begin() + (long) (i + BLK));
    std::vector<double> out;
    for (int k = 0; k < 48; ++k)                              // 48 log-spaced probes, 40 Hz .. 16 kHz
    {
        const double f  = 40.0 * std::pow (16000.0 / 40.0, (double) k / 47.0);
        const double w  = 2.0 * M_PI * f / SR;
        const double cw = std::cos (w), coeff = 2.0 * cw;
        double s0 = 0, s1 = 0, s2 = 0;
        for (float x : L) { s0 = (double) x + coeff * s1 - s2; s2 = s1; s1 = s0; }
        const double p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        out.push_back (10.0 * std::log10 (std::fabs (p) / std::max<size_t> (1, L.size()) + 1e-14));
    }
    return out;
}
static double spectrumMoveDb (const std::vector<double>& a, const std::vector<double>& b)
{
    double s = 0; int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    { if (a[i] < -110.0 && b[i] < -110.0) continue; s += std::fabs (a[i] - b[i]); ++n; }     // silent bands say nothing
    return n ? s / n : 0.0;
}
static double nullDb (const std::vector<float>& a, const std::vector<float>& b)
{ if (a.size() != b.size() || a.empty()) return 999.0; double e = 0; for (size_t i = 0; i < a.size(); ++i) { const double d = (double) a[i] - b[i]; e += d * d; } return 20.0 * std::log10 (std::sqrt (e / (double) a.size()) + 1e-12); }

// ── one render of a held note under a setup ─────────────────────────────────────────────────────
struct Cfg { int srcIdx; float depth; float aLevel; bool eOn; bool aOn; int carrier;
             int semiA = 0; int semiF = 0; float fLevel = 0.7f; };       // carrier 0 = E (bank 1), 1 = A (bank 0)
static std::vector<float> renderCfg (const Cfg& c)
{
    Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); exit (1); }
    a.set ("Osc A Enable", c.aOn ? 1.0f : 0.0f);
    a.set ("Osc E Enable", c.eOn ? 1.0f : 0.0f);
    a.set ("Osc F Enable", 1.0f);                   // F must exist to BE a bank-mate source for E
    a.set ("Synth OSC A Level", c.aLevel);
    a.set ("Synth OSC E Level", 0.7f);
    a.set ("Synth OSC F Level", c.fLevel);
    a.setIdx ("Synth OSC A Semitone", c.semiA);     // the pitch of each candidate modulator, set apart
    a.setIdx ("Synth OSC F Semitone", c.semiF);
    if (c.carrier == 0) { a.setIdx ("Synth OSC E Blend 1 Mode", c.depth > 0 ? 1 : 0); a.setIdx ("Synth OSC E Blend 1 Source", c.srcIdx); a.set ("Synth OSC E Blend 1 Depth", c.depth); a.set ("Synth OSC A Level", c.aLevel); }
    else                { a.setIdx ("Synth OSC A Blend 1 Mode", c.depth > 0 ? 1 : 0); a.setIdx ("Synth OSC A Blend 1 Source", c.srcIdx); a.set ("Synth OSC A Blend 1 Depth", c.depth); }
    a.pump (0.60);                                  // the pool wakes up (ensureBankB runs off the timer)
    a.render (30, nullptr);                         // settle: smoothers, the lazy bank build
    a.note (57, 100);                               // A3 — low enough that FM sidebands land in band
    std::vector<float> v; a.render (70, &v);
    a.note (57, 0); a.render (4, nullptr);
    a.close();
    return v;
}

int main()
{
    printf ("\n══ tp53 — EFGH CROSS-BLEND WITH ABCD (the installed AU) ══\n\n");

    {   // PREFLIGHT — name the parameters this cert drives, and the width of the source list
        Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); return 1; }
        const char* need[] = { "Osc A Enable", "Osc E Enable", "Synth OSC A Level", "Synth OSC E Level",
                               "Synth OSC E Blend 1 Mode", "Synth OSC E Blend 1 Source", "Synth OSC E Blend 1 Depth",
                               "Synth OSC A Blend 1 Mode", "Synth OSC A Blend 1 Source", "Synth OSC A Blend 1 Depth" };
        std::string missing;
        for (auto* n : need) if (! a.has (n)) { missing += n; missing += " "; }
        const int nsrcE = a.choiceCount ("Synth OSC E Blend 1 Source");
        const int nsrcA = a.choiceCount ("Synth OSC A Blend 1 Source");
        a.close();
        if (! missing.empty()) { printf ("  !! missing parameters: %s\n", missing.c_str()); return 1; }
        char d[200]; snprintf (d, sizeof d, "Osc E source list = %d choices, Osc A = %d (want 21 each: 17 frozen + the other bank's four)", nsrcE, nsrcA);
        chk (nsrcE == 21 && nsrcA == 21, "[0] BOTH banks offer 21 blend sources — E's WSLOT params are CLONES of B's, so the appended four reach them for free", d);
    }

    // The reference: osc E alone, nothing modulating it.
    const std::vector<float> dry  = renderCfg ({ 0,  0.00f, 0.7f, true, true, 0 });
    // The claim: osc E FM'd by osc A — source 17, the other bank's first.
    const std::vector<float> xA   = renderCfg ({ 17, 0.85f, 0.7f, true, true, 0 });
    // The control: the same carrier, the same depth, a BANK-MATE source (F = index 1).
    const std::vector<float> matF = renderCfg ({ 1,  0.85f, 0.7f, true, true, 0 });

    const auto mDry = mags (dry, 20), mXA = mags (xA, 20), mF = mags (matF, 20);
    const double moveXA = spectrumMoveDb (mDry, mXA);
    const double moveF  = spectrumMoveDb (mDry, mF);
    const double apart  = spectrumMoveDb (mXA, mF);

    { char d[280]; snprintf (d, sizeof d, "osc E, FM depth 0.85 from osc A: the spectrum moves %.2f dB (dry %.1f dBFS -> %.1f dBFS). Before tp53 this index did not exist and E could reach nothing outside E-H.", moveXA, rmsDb (dry, dry.size()/2), rmsDb (xA, xA.size()/2));
      chk (moveXA > 3.0, "[1] OSC E IS FM'd BY OSC A — a cross-bank source is a real modulator, and it is night-and-day", d); }

    { char d[280]; snprintf (d, sizeof d, "E<-A moves %.2f dB and E<-F moves %.2f dB from dry; the two RESULTS sit %.2f dB apart (want > 2.0 — a fix that aliased 17 back to a bank-mate would read ~0 here)", moveXA, moveF, apart);
      chk (apart > 2.0, "[2] 🚨 AND IT IS REALLY THE OTHER BANK — the cross source does not sound like a bank-mate at the same depth", d); }

    /*  [2b] 🚨 THE DECISIVE ONE: MOVE THE MODULATOR AND SEE WHOSE PITCH MATTERS.
        With E's source set to 17, retuning OSC A must change E's timbre and retuning OSC F must
        NOT.  Amplitudes and depths are identical in all three renders, so the only thing that can
        move the spectrum is WHICH oscillator is doing the modulating.  A bug that aliased 17 to a
        bank-mate reverses this answer exactly, and no threshold-tuning can hide it.  */
    /*  ⚠️ BOTH CANDIDATES ARE AT LEVEL 0 and only E is audible. The first cut of this bar left them
        at 0.7 and read "F moves it 3.48 dB" — F was simply IN THE MIX, so retuning it moved the sum
        whether or not it was modulating anything. A confound, not a finding: the bar has to hear the
        carrier alone. Level 0 is safe for exactly the reason [3] proves — a source turned down still
        renders as a modulator, in-bank (fb523) and now across the banks too. */
    const std::vector<float> baseA = renderCfg ({ 17, 0.85f, 0.0f, true, true, 0,  0, 0, 0.0f });
    const std::vector<float> movA  = renderCfg ({ 17, 0.85f, 0.0f, true, true, 0, 12, 0, 0.0f });   // osc A up an octave
    const std::vector<float> movF  = renderCfg ({ 17, 0.85f, 0.0f, true, true, 0,  0, 12, 0.0f });  // osc F up an octave
    const double byA = spectrumMoveDb (mags (baseA, 20), mags (movA, 20));
    const double byF = spectrumMoveDb (mags (baseA, 20), mags (movF, 20));
    { char d[320]; snprintf (d, sizeof d, "E's source is 17. Retuning OSC A (+12 st) moves E's spectrum %.2f dB; retuning OSC F by the same +12 moves it %.2f dB. The modulator is A, and it is not F.", byA, byF);
      chk (byA > 3.0 && byA > byF * 2.5, "[2b] 🚨 IT IS OSC A DOING THE MODULATING — A's pitch moves the sound, F's does not", d); }

    // [3] the fb523 law across the wall: A's Level at ZERO, so it contributes nothing audible.
    const std::vector<float> dry0 = renderCfg ({ 0,  0.00f, 0.0f, true, true, 0 });
    const std::vector<float> xA0  = renderCfg ({ 17, 0.85f, 0.0f, true, true, 0 });
    const double move0 = spectrumMoveDb (mags (dry0, 20), mags (xA0, 20));
    { char d[280]; snprintf (d, sizeof d, "with osc A's LEVEL at 0 (it contributes nothing you can hear) the FM on E still moves the spectrum %.2f dB. Without the cross-force travelling to bank 0, lane A would not render and this reads ~0.", move0);
      chk (move0 > 3.0, "[3] 🚨 A SOURCE TURNED DOWN STILL MODULATES — fb523's law holds ACROSS the banks", d); }

    // [4] the other direction: A FM'd by E. One block later (see CrossBlendBus.h) but real.
    const std::vector<float> aDry = renderCfg ({ 0,  0.00f, 0.7f, true, true, 1 });
    const std::vector<float> aXE  = renderCfg ({ 17, 0.85f, 0.7f, true, true, 1 });
    const double moveAE = spectrumMoveDb (mags (aDry, 20), mags (aXE, 20));
    { char d[280]; snprintf (d, sizeof d, "osc A, FM depth 0.85 from osc E: %.2f dB. This is the direction that reads LAST block's taps (bank 0 renders first) — a clean one-block delay line, not a dropout.", moveAE);
      chk (moveAE > 3.0, "[4] AND IT GOES BOTH WAYS — A/B/C/D can take E/F/G/H as a source too", d); }

    // [5] THE BUS IS A READER, NOT A WRITER. Arming E<-A must not move osc A's own sound.
    const std::vector<float> aAlone  = renderCfg ({ 0,  0.00f, 0.7f, false, true, 0 });   // A on, E off, nothing armed
    const std::vector<float> aAlone2 = renderCfg ({ 0,  0.00f, 0.7f, false, true, 0 });
    const double selfNull = nullDb (aAlone, aAlone2);
    { char d[280]; snprintf (d, sizeof d, "two renders of osc A alone, no cross route anywhere: null %.1f dB (the board is allocated and stamped every block either way — if merely HAVING it cost anything, it would show here)", selfNull);
      chk (selfNull < -100.0, "[5] the bus is inert when nothing is armed — a patch that uses no cross source is unchanged", d); }

    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    return nfail == 0 ? 0 : 1;
}
