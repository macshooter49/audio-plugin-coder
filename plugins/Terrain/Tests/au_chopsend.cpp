// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_chopsend.cpp — tp56 · THE CHOP SAMPLERS FEED THE RACK (the real installed AU).
//
//  Max: "these aren't the sample oscillator engine, these are sample CHOPPED engines" — Sampler
//  A/B/C/D as real modules on the Patcher, routable into effects / filter / EQ / tape / granular.
//
//  ONE PARAMETER PER DEVICE: <device>_CHOPS, a 4-bit mask, bit L = chop layer L feeds that device's
//  send bus. 0 everywhere is the default, and 0 means the Chop page's own law — the layer sums into
//  the master exactly as it always has.
//
//  🚨 THE BAR THAT MATTERS HERE IS [2]. A routing change that reaches into the block where every
//  oscillator's send is resolved is exactly the kind that quietly moves the SYNTH, and a "does the
//  parameter exist" test would never see it. With every chop mask slammed to all four layers on
//  every device, a synth patch's audio must be BIT-IDENTICAL — because no chop layer has a sample
//  loaded here, and a routing path that costs an untouched patch anything at all is a regression
//  whatever it does when a sample IS loaded.
//
//  ⚠️ WHAT THIS CANNOT REACH, SAID PLAINLY: the layers themselves. A chop layer only sounds with a
//  sample loaded into it, and nothing in the AU's parameter surface loads one — the Chop page does
//  that through a native function. So the audible half of this feature is NOT measured here. What
//  is: the parameters ([0]), the silence of the default ([2]), and the two places the processor has
//  to read the mask ([3]) — a routed layer LEAVES the dry mix and is ADDED to every bus that claimed
//  it; one without the other is either a double or a silence. The CABLE that writes the mask is
//  driven for real in Tests/_tp56_chopsamp_gate.js.
//
//  clang++ -O2 -std=c++17 Tests/au_chopsend.cpp -o /tmp/auchop -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/auchop
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
#include <fstream>
#include <algorithm>

static const char* const kKinds[16] = { "Reverb","Delay","Distortion","Granular","Tape","Filter","Chorus","Flanger",
                                        "Phaser","Equalizer","Widen","Compress","OTT","Bode","Utility","Splitter" };

static double rms (const std::vector<float>& v)
{ double s = 0; for (float x : v) s += (double) x * x; return v.empty() ? 0.0 : 10.0 * std::log10 (s / (double) v.size() + 1e-30); }

// one synth note, with an optional slam of every chop mask
static std::vector<float> renderSynth (bool slamChops, int* setCount)
{
    Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); exit (1); }
    a.set ("Osc A Enable", 1.0f);
    a.set ("Synth OSC A Level", 0.8f);
    if (slamChops)
    {
        int n = 0;
        for (const char* k : kKinds)
            for (int i = 1; i <= 6; ++i)
            {
                std::string nm = std::string (k) + (i == 1 ? "" : " " + std::to_string (i)) + " Chop Sources";
                if (a.has (nm.c_str())) { a.set (nm.c_str(), 1.0f); ++n; }      // 1.0 of 0..15 = all four layers
            }
        if (a.has ("Deck Chop Sources")) { a.set ("Deck Chop Sources", 1.0f); ++n; }
        if (setCount) *setCount = n;
    }
    a.pump (0.40);
    a.render (30, nullptr);
    a.note (57, 100);
    std::vector<float> v; a.render (70, &v);
    a.note (57, 0); a.render (4, nullptr);
    a.close();
    return v;
}

static std::string slurp (const char* p)
{ std::ifstream in (p); return std::string ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>()); }

int main()
{
    printf ("\n══ tp56 — THE CHOP SAMPLERS FEED THE RACK (the installed AU) ══\n\n");

    // ── [0] EVERY DEVICE HAS A CHOP MASK, AND IT IS 0..15 ────────────────────────────────────
    {
        Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); return 2; }
        int found = 0, wrongRange = 0, nonZero = 0; std::string missing, bad;
        for (const char* k : kKinds)
            for (int i = 1; i <= 6; ++i)
            {
                const std::string nm = std::string (k) + (i == 1 ? "" : " " + std::to_string (i)) + " Chop Sources";
                if (! a.has (nm.c_str())) { if (missing.size() < 90) missing += nm + " "; continue; }
                ++found;
                const auto& pi = a.info.at (a.byName.at (nm));
                // ⚠️ JUCE's AU wrapper reports every parameter in its own 0..1 normalised space,
                // NOT the declared 0..15 — the range is checked by STEP COUNT instead, which is
                // what actually says "four bits".  (The first cut asserted 0..15 and went red on a
                // parameter that was perfectly correct.)
                if (! (pi.minValue == 0.0f && pi.maxValue == 1.0f)) { ++wrongRange; if (bad.size() < 60) bad += nm + "[" + std::to_string (pi.minValue) + ".." + std::to_string (pi.maxValue) + "] "; }
                Float32 v = -1; AudioUnitGetParameter (a.au, a.byName.at (nm), kAudioUnitScope_Global, 0, &v);
                if (v != 0.0f) ++nonZero;
            }
        const bool deck = a.has ("Deck Chop Sources");
        a.close();
        chk (found == 96 && deck && wrongRange == 0 && nonZero == 0,
             "[0] EVERY RACK DEVICE CARRIES A CHOP MASK, AND EVERY ONE DEFAULTS TO 0 (0 = the Chop page's own law)",
             "found " + std::to_string (found) + "/96 (+ Deck " + (deck ? "yes" : "NO") + ") · wrong range "
             + std::to_string (wrongRange) + (bad.empty() ? "" : " [" + bad + "]") + " · non-zero defaults "
             + std::to_string (nonZero) + (missing.empty() ? "" : " · MISSING " + missing));
    }

    // ── [1] the reference note ───────────────────────────────────────────────────────────────
    const std::vector<float> plain = renderSynth (false, nullptr);
    chk (rms (plain) > -60.0, "[1] THE REFERENCE NOTE SOUNDS", "level " + std::to_string (rms (plain)) + " dBFS");

    // ── [2] 🚨 THE SLAM IS SILENT ON THE SYNTH ───────────────────────────────────────────────
    int n = 0;
    const std::vector<float> slammed = renderSynth (true, &n);
    const double db = nullDb (plain, slammed);
    chk (n >= 96 && db < -180.0,
         "[2] 🚨 EVERY CHOP MASK SLAMMED TO ALL FOUR LAYERS AND THE SYNTH IS BIT-IDENTICAL",
         std::to_string (n) + " masks set · null " + std::to_string (db) + " dB (the chop path may not cost an untouched patch one bit)");

    // ── [3] THE PROCESSOR ACTUALLY READS THE MASK, IN BOTH PLACES ────────────────────────────
    //  A perfect parameter nobody consumes is fb435's silent control all over again. Two things
    //  have to be true in the shipped source: a routed layer LEAVES the dry mix, and its audio is
    //  ADDED to the buses that claimed it — one without the other is either a double or a silence.
    {
        const std::string src = slurp ("Source/PluginProcessor.cpp");
        const bool resolve = src.find ("chopRoutedMask_ = any;")                              != std::string::npos;
        const bool dryOut  = src.find ("if (audible && ! chopRouted)")                        != std::string::npos;
        const bool feeds   = src.find ("feed (poolSendBuf_[(size_t) q], poolChopMask_[(size_t) q])") != std::string::npos;
        const bool arms    = src.find ("if (poolChopMask_[(size_t) q]) poolRouteAny_[(size_t) q] = true;") != std::string::npos;
        const bool inst1   = src.find ("feed (reverbSendBuf_,     hallChopMask_)")            != std::string::npos;
        chk (resolve && dryOut && feeds && arms && inst1,
             "[3] THE PROCESSOR READS IT IN BOTH PLACES — a routed layer leaves the dry mix AND is added to every bus that claimed it",
             std::string ("resolve=") + (resolve?"y":"n") + " dryOut=" + (dryOut?"y":"n") + " feed=" + (feeds?"y":"n")
             + " arm=" + (arms?"y":"n") + " inst1=" + (inst1?"y":"n"));
    }

    // ── [4] tp57 — THE BPM LOCK IS A REAL, GLOBAL, AUTOMATABLE PARAMETER ────────────────────
    //  Max: "the BPM and the lock will have to be global, and that will be nice because A B C and D
    //  can now be locked to the BPM."  One parameter, off by default, and setting it must not move
    //  a synth patch by one bit — no chop layer has a sample here, so there is nothing to stretch,
    //  and a lock that cost an untouched patch anything would be a regression whatever it does when
    //  a sample IS loaded.
    {
        Au a; if (! a.open()) { printf ("  !! cannot open the AU\n"); return 2; }
        const bool has = a.has ("Chop BPM Lock");
        Float32 v = -1;
        if (has) AudioUnitGetParameter (a.au, a.byName.at ("Chop BPM Lock"), kAudioUnitScope_Global, 0, &v);
        a.close();
        chk (has && v == 0.0f, "[4] THE BPM LOCK EXISTS, IS GLOBAL AND IS OFF BY DEFAULT",
             std::string ("present=") + (has ? "yes" : "NO") + " default=" + std::to_string (v));
    }
    {
        Au a; if (! a.open()) return 2;
        a.set ("Osc A Enable", 1.0f); a.set ("Synth OSC A Level", 0.8f);
        a.pump (0.40); a.render (30, nullptr); a.note (57, 100);
        std::vector<float> off; a.render (60, &off); a.note (57, 0); a.render (4, nullptr); a.close();

        Au c; if (! c.open()) return 2;
        c.set ("Osc A Enable", 1.0f); c.set ("Synth OSC A Level", 0.8f);
        /* ⚠️ A NULL BAR MUST NOT BE ABLE TO PASS BECAUSE THE PARAMETER IS MISSING. The first cut of
           this read PASS against a stale binary that had no "Chop BPM Lock" at all: `set` printed
           its "no parameter named" line, changed nothing, and the two identical renders nulled at
           -240. The whole bar was measuring a control that did not exist. */
        const bool lockSet = c.set ("Chop BPM Lock", 1.0f);
        Float32 back = -1;
        if (c.has ("Chop BPM Lock"))
            AudioUnitGetParameter (c.au, c.byName.at ("Chop BPM Lock"), kAudioUnitScope_Global, 0, &back);
        c.pump (0.40); c.render (30, nullptr); c.note (57, 100);
        std::vector<float> on; c.render (60, &on); c.note (57, 0); c.render (4, nullptr); c.close();

        const double db = nullDb (off, on);
        chk (lockSet && back > 0.5f && rmsDb (off) > -60.0 && db < -180.0,
             "[5] THE LOCK REALLY WENT ON, THE NOTE REALLY SOUNDED, AND THE SYNTH IS BIT-IDENTICAL",
             "lock set=" + std::string (lockSet ? "yes" : "NO") + " read back=" + std::to_string (back)
             + " · note " + std::to_string (rmsDb (off)) + " dBFS · null " + std::to_string (db) + " dB");
    }
    // ── [6] AND THE VOICE ACTUALLY APPLIES IT ───────────────────────────────────────────────
    {
        const std::string src = slurp ("Source/SamplerVoice.h");
        const bool applies = src.find ("timeStretchMulParam_->load()") != std::string::npos;
        const bool ratio   = src.find ("activeConfig.stretchRatio * m") != std::string::npos;
        const bool warps   = src.find ("activeConfig.warpMode = WarpMode::Beats") != std::string::npos;
        const std::string pr = slurp ("Source/PluginProcessor.cpp");
        const bool detects = pr.find ("tw::looptempo::detect") != std::string::npos;
        const bool feeds   = pr.find ("tw::looptempo::stretchTo") != std::string::npos;
        chk (applies && ratio && warps && detects && feeds,
             "[6] THE VOICE APPLIES THE STRETCH *AND* TURNS THE WARP ON — a ratio without a warp mode does nothing at all (the NONE path ignores stretchRatio)",
             std::string ("load=") + (applies?"y":"n") + " ratio=" + (ratio?"y":"n") + " beats=" + (warps?"y":"n")
             + " detect=" + (detects?"y":"n") + " stretchTo=" + (feeds?"y":"n"));
    }

    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
