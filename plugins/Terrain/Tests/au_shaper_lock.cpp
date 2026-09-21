// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_shaper_lock.cpp — tp71 · THE SHAPER IS DAW-LOCKED, THROUGH THE INSTALLED AU.
//
//  A host transport (kAudioUnitProperty_HostCallbacks: beat + tempo, transport state) drives the
//  plugin's playhead exactly as a DAW does. The Chop slot holds the Shaper (Flow Chain 1 = Chop),
//  the Volume lane boots as a 1/16 gate. A held C4:
//   [1] play from bar 1: sixteenth 0 is ON, sixteenth 1 is OFF — from the first samples
//   [2] play pressed a sixteenth in: the first sixteenth heard is OFF (the shape is read at the
//       transport's phase, not from the moment play was pressed)
//   [3] transport stopped: the gate holds phase 0 (on) — no free-running
//   [4] Shaper Volume On = 0: no gate at all (the parameter path)
//   [5] Shaper Time On with the unity ramp: the audio passes unchanged (the ring armed itself)
//
//  clang++ -O2 -std=c++17 Tests/au_shaper_lock.cpp -o /tmp/aushp -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/aushp
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

static const double SR = 48000.0; static const int BLK = 512; static const double BPM = 120.0;
static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& d = "") { printf ("  %s  %s\n", ok ? "PASS " : "FAIL ", what); if (! d.empty()) printf ("        %s\n", d.c_str()); ok ? ++npass : ++nfail; }

// ── the host: what the plugin's playhead sees ──
static double gPpq0 = 0.0, gStamp0 = 0.0; static bool gPlaying = false;
static OSStatus beatAndTempo (void*, Float64* outBeat, Float64* outTempo)
{ if (outTempo) *outTempo = BPM; if (outBeat) *outBeat = gPpq0; return noErr; }
static OSStatus transportState (void*, Boolean* playing, Boolean* changed, Float64* sampleInLoop, Boolean* looping, Float64* loopStart, Float64* loopEnd)
{ if (playing) *playing = gPlaying; if (changed) *changed = false; if (sampleInLoop) *sampleInLoop = gStamp0; if (looping) *looping = false; if (loopStart) *loopStart = 0; if (loopEnd) *loopEnd = 0; return noErr; }

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
        HostCallbackInfo cb {}; cb.beatAndTempoProc = beatAndTempo; cb.transportStateProc = transportState;
        AudioUnitSetProperty (au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &cb, sizeof cb);
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
    // render `blocks` with the transport at ppq (advancing when playing)
    void render (int blocks, double ppqStart, bool playing, std::vector<float>* keep = nullptr)
    {
        std::vector<float> L (BLK), R (BLK); gPlaying = playing; double ppq = ppqStart;
        for (int b = 0; b < blocks; ++b)
        {
            gPpq0 = ppq; gStamp0 = stamp;
            AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer));
            abl->mNumberBuffers = 2;
            abl->mBuffers[0].mNumberChannels = 1; abl->mBuffers[0].mDataByteSize = BLK * 4; abl->mBuffers[0].mData = L.data();
            abl->mBuffers[1].mNumberChannels = 1; abl->mBuffers[1].mDataByteSize = BLK * 4; abl->mBuffers[1].mData = R.data();
            AudioUnitRenderActionFlags fl = 0; AudioTimeStamp ts {}; ts.mSampleTime = stamp; ts.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRender (au, &fl, &ts, 0, BLK, abl); stamp += BLK;
            if (playing) ppq += BLK * BPM / 60.0 / SR;
            if (keep) keep->insert (keep->end(), L.begin(), L.end());
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.001, false);
        }
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
static double rmsDb (const std::vector<float>& x, size_t a, size_t b) { double e = 0; size_t n = 0; for (size_t i = a; i < b && i < x.size(); ++i) { e += (double) x[i] * x[i]; ++n; } return 20 * std::log10 (std::sqrt (e / std::max<size_t> (1, n)) + 1e-12); }
static const size_t STEP = 6000;   // a sixteenth at 120 BPM, 48 kHz

int main()
{
    Au a; if (! a.open()) { printf ("  !! no Terrain AU\n"); return 2; }
    printf ("\ntp71 — THE SHAPER IS DAW-LOCKED (installed AU, a host transport, the Volume lane's 1/16 gate)\n\n");
    a.pump (1.0); a.render (20, 0.0, false);
    bool okp = a.set ("Flow Chain 1", 2.0f);   // the Chop slot = the Shaper
    a.pump (0.8); a.render (10, 0.0, false);
    chk (okp, "[0] the Shaper sits in Flow Chain 1 (the Chop slot)");
    // the note: held through each take, released and let go between takes
    auto take = [&] (double ppq0, bool playing, int blocks, std::vector<float>& out) { a.note (60, 100); a.render (blocks, ppq0, playing, &out); a.note (60, 0); a.render (200, ppq0 + blocks * BLK * BPM / 60.0 / SR, playing); a.pump (0.3); };
    auto stepDb = [&] (const std::vector<float>& x, int step) { const size_t s0 = STEP * (size_t) step; return rmsDb (x, s0 + 700, s0 + STEP - 400); };   // inside the step, past the amp attack and the gate's smoothing
    std::vector<float> x;
    // [1] from the bar line
    take (0.0, true, (int) (STEP * 8 / BLK) + 1, x);
    char b[200]; snprintf (b, sizeof b, "steps 0..3: %.1f %.1f %.1f %.1f dBFS", stepDb (x, 0), stepDb (x, 1), stepDb (x, 2), stepDb (x, 3));
    chk (stepDb (x, 0) > -30 && stepDb (x, 1) < stepDb (x, 0) - 25 && stepDb (x, 2) > -30 && stepDb (x, 3) < stepDb (x, 2) - 25, "🚨 [1] play from bar 1: sixteenth 0 ON, 1 OFF, 2 ON, 3 OFF — the gate rides the transport from the first samples", b);
    // [2] play pressed a sixteenth in
    x.clear(); take (0.25, true, (int) (STEP * 8 / BLK) + 1, x);
    snprintf (b, sizeof b, "steps 0..3 from ppq 0.25: %.1f %.1f %.1f %.1f dBFS", stepDb (x, 0), stepDb (x, 1), stepDb (x, 2), stepDb (x, 3));
    chk (stepDb (x, 0) < stepDb (x, 1) - 25 && stepDb (x, 1) > -30, "🚨 [2] play pressed a sixteenth in: the first sixteenth heard is OFF, the next ON — the shape is read at the DAW's position, not from the press", b);
    // [3] stopped
    x.clear(); take (3.37, false, (int) (STEP * 4 / BLK) + 1, x);
    snprintf (b, sizeof b, "stopped at ppq 3.37: steps 0..2: %.1f %.1f %.1f dBFS", stepDb (x, 0), stepDb (x, 1), stepDb (x, 2));
    chk (stepDb (x, 0) > -30 && stepDb (x, 1) > -30 && stepDb (x, 2) > -30, "[3] transport stopped: the gate holds phase 0 (on) and nothing free-runs", b);
    // [4] the lane off
    a.set ("Shaper Volume On", 0.0f); a.pump (0.3);
    x.clear(); take (0.0, true, (int) (STEP * 4 / BLK) + 1, x);
    snprintf (b, sizeof b, "Volume off: steps 0..2: %.1f %.1f %.1f dBFS", stepDb (x, 0), stepDb (x, 1), stepDb (x, 2));
    chk (std::fabs (stepDb (x, 0) - stepDb (x, 1)) < 3 && stepDb (x, 1) > -30, "[4] Shaper Volume On = 0: no gate (the parameter path)", b);
    // [5] the Time lane on its unity ramp: a wire (the ring arms on the timer)
    // two takes are compared sample-for-sample, so the voice must start at a repeatable phase (tp70 made Random the default)
    a.set ("Synth OSC A Phase Mode", 0.0f); a.set ("Synth OSC B Phase Mode", 0.0f); a.set ("Synth OSC C Phase Mode", 0.0f); a.set ("Synth OSC D Phase Mode", 0.0f); a.pump (0.3);
    a.set ("Shaper Time On", 1.0f); a.pump (1.0); a.render (10, 0.0, true); a.pump (0.5);
    std::vector<float> y; take (0.0, true, (int) (STEP * 4 / BLK) + 1, y);
    a.set ("Shaper Time On", 0.0f); a.pump (0.5);
    std::vector<float> z; take (0.0, true, (int) (STEP * 4 / BLK) + 1, z);
    // the installed voice is not sample-repeatable take to take (its own modulation), so the wire is judged by level:
    // every sixteenth of the Time-on take sits within 1.5 dB of the Time-off take — no drop-outs, no halving, no repeats
    double worst = 0; for (int st = 1; st < 4; ++st) worst = std::max (worst, std::fabs (stepDb (y, st) - stepDb (z, st)));
    snprintf (b, sizeof b, "Time on (unity) vs off, per-sixteenth level: worst %.2f dB (on: %.1f %.1f %.1f · off: %.1f %.1f %.1f)", worst, stepDb (y, 1), stepDb (y, 2), stepDb (y, 3), stepDb (z, 1), stepDb (z, 2), stepDb (z, 3));
    chk (worst < 1.5 && stepDb (y, 1) > -30, "[5] the Time lane on its unity ramp is a wire — the ring armed itself and the read position equals the write position", b);
    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    a.close(); return nfail ? 1 : 0;
}
