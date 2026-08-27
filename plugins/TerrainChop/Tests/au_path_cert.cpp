// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_path_cert.cpp — DOES TERRAIN CHOP (the plugin) REACH FlowChop (the engine)?
//  Waves Crate · Lane C
//
//    clang++ -std=c++17 -O2 -I ../TerrainInstrument/Source Tests/au_path_cert.cpp \
//            -o /tmp/tchp_cert -framework AudioToolbox -framework CoreFoundation
//    /tmp/tchp_cert
//
//  VERIFY THE PATH, NOT JUST THE ENGINE (fb373): FlowChop has its own offline certs in the
//  Terrain tree; a green engine proves nothing about THIS plugin. This harness loads the
//  INSTALLED AU (aumf / Tchp / Wvcr), streams a deterministic input through it as a real
//  host would, and measures the OUTPUT:
//
//    G0  the component exists, initializes, and exposes a parameter list.
//    G1  the JUCE param ids resolve as AU parameter addresses (id -> hashCode31, the same
//        derivation the JUCE AU wrapper uses) — a sample across every param family.
//    G2  BLEND = 0 -> dry-transparent: residual vs the bit-identical input < -60 dBr.
//        (fb373's exact failure class: a param that LOOKS wired but never reaches the DSP.)
//    G3  FLOW_SEQ_RATE audibly changes the chop grid: with staccato settings the output
//        envelope is periodic at the step rate; the AUTOCORRELATION period must track the
//        engine's own rate ladder (arpBeatsPerStepRich — included from the Terrain tree,
//        which also proves FlowChop.h standalone-compiles with -I TerrainInstrument/Source)
//        at BOTH tested knob values, free-running at the 120 BPM fallback.
//    G4  CATCH works from the host's MIDI lane (the reason this plugin is an aumf):
//        CATCH on + no note  -> transparent;  note-on held -> chopping engages;
//        note-off -> disengages again. This dies if the held-note stack, setCatchHeld or
//        noteOnRoot wiring is missing.
//
//  A GATE THAT HAS NEVER FAILED HAS NEVER BEEN TESTED: the recorded fail-first run is this
//  binary executed BEFORE the AU was ever built/installed — G0 goes red and the exit code
//  is 1 (see the lane report). G2/G3/G4 additionally fail by construction against any build
//  whose processor drops the BLEND read, the FLOW_SEQ_RATE read, or the MIDI held-stack.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// The engine's own truth for the rate ladder — INCLUDED from the Terrain tree, never copied.
#include "FlowChop.h"

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what)
{ ++g_checks; std::printf ("  %s  %s\n", ok ? "PASS " : "FAIL:", what); if (! ok) ++g_fail; }

static const double SR  = 48000.0;
static const int    BLK = 512;

// deterministic input: a steady 220 Hz tone — flat envelope, so any AM in the
// output is the chop's doing. Pure function of the absolute sample index.
static float inputSample (long long n)
{ return 0.5f * std::sin (2.0 * M_PI * 220.0 * (double) n / SR); }

static OSStatus inputCb (void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* ts,
                         UInt32, UInt32 nFrames, AudioBufferList* io)
{
    const long long base = (long long) ts->mSampleTime;
    for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
    {
        float* d = (float*) io->mBuffers[b].mData;
        for (UInt32 i = 0; i < nFrames; ++i) d[i] = inputSample (base + (long long) i);
    }
    return noErr;
}

struct AU
{
    AudioUnit au = nullptr;

    bool open()
    {
        AudioComponentDescription d {};
        d.componentType         = kAudioUnitType_MusicEffect;   // 'aumf'
        d.componentSubType      = 'Tchp';
        d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c) { std::printf ("  !! AU aumf/Tchp/Wvcr not found — is Terrain Chop installed?\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { std::printf ("  !! instantiate failed\n"); return false; }

        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4;
        f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,  0, &f, sizeof f);
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        AURenderCallbackStruct cb { inputCb, nullptr };
        AudioUnitSetProperty (au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        if (AudioUnitInitialize (au) != noErr) { std::printf ("  !! AudioUnitInitialize failed\n"); return false; }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }

    // JUCE's AU wrapper derives the AudioUnitParameterID from the id string and nothing else:
    // String::hashCode (r = 31*r + codepoint) with the sign bit cleared. The id IS the address.
    static AudioUnitParameterID pid (const std::string& juceParamId)
    {
        uint32_t r = 0;
        for (unsigned char ch : juceParamId) r = 31u * r + (uint32_t) ch;
        return (AudioUnitParameterID) (r & 0x7FFFFFFFu);
    }
    bool hasP (const std::string& id) const
    {
        AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
        return AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                                     pid (id), &pi, &s) == noErr;
    }
    bool setP (const std::string& id, float v)
    { return AudioUnitSetParameter (au, pid (id), kAudioUnitScope_Global, 0, v, 0) == noErr; }

    void note (bool on, int nn)
    { MusicDeviceMIDIEvent (au, on ? 0x90 : 0x80, (UInt32) nn, on ? 100 : 0, 0); }

    // render nblk blocks continuing the timeline from tsSample; appends L channel to out
    long long tsSample = 0;
    void render (int nblk, std::vector<float>* outL = nullptr)
    {
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (::AudioBuffer));
        abl->mNumberBuffers = 2;
        for (int b = 0; b < nblk; ++b)
        {
            AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid;
            ts.mSampleTime = (Float64) tsSample;
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            if (AudioUnitRender (au, &fl, &ts, 0, BLK, abl) != noErr) break;
            if (outL) outL->insert (outL->end(), bl.begin(), bl.end());
            tsSample += BLK;
        }
        free (abl);
    }
};

// residual of out vs the regenerated input over [n0, n0+len), in dBr
static double residualDb (const std::vector<float>& out, long long absStart, size_t n0, size_t len)
{
    double se = 0.0, si = 0.0;
    for (size_t i = n0; i < n0 + len && i < out.size(); ++i)
    {
        const double ref = (double) inputSample (absStart + (long long) i);
        const double d   = (double) out[i] - ref;
        se += d * d; si += ref * ref;
    }
    if (si <= 0.0) return 0.0;
    return 10.0 * std::log10 (std::max (1e-30, se / si));
}

// dominant envelope period (samples) via autocorrelation of the rectified,
// hop-averaged envelope; searched in [lo, hi] samples. Returns {period, corr, depth}.
struct Period { double samples = 0, corr = 0, depth = 0; };
static Period envPeriod (const std::vector<float>& x, size_t skip, double lo, double hi)
{
    const int hop = 50;
    std::vector<double> env;
    for (size_t i = skip; i + (size_t) hop <= x.size(); i += (size_t) hop)
    {
        double a = 0.0;
        for (int k = 0; k < hop; ++k) a += std::fabs ((double) x[i + (size_t) k]);
        env.push_back (a / hop);
    }
    const size_t n = env.size();
    Period out;
    if (n < 32) return out;
    double mean = 0.0; for (double v : env) mean += v; mean /= (double) n;
    double var = 0.0;  for (double& v : env) { v -= mean; var += v * v; }
    if (var <= 0.0 || mean <= 1e-9) return out;
    out.depth = std::sqrt (var / (double) n) / mean;
    const int loL = std::max (2, (int) std::floor (lo / hop));
    const int hiL = std::min ((int) n / 2, (int) std::ceil (hi / hop));
    double best = -1.0; int bestLag = 0;
    for (int lag = loL; lag <= hiL; ++lag)
    {
        double c = 0.0;
        for (size_t i = 0; i + (size_t) lag < n; ++i) c += env[i] * env[i + (size_t) lag];
        c /= var;
        if (c > best) { best = c; bestLag = lag; }
    }
    out.samples = (double) bestLag * hop;
    out.corr    = best;
    return out;
}

// one G3 rate probe: fresh instance, staccato settings, measure the grid period
static Period probeRate (float rateKnob, double expectSamples, bool& openOk)
{
    AU au;
    openOk = au.open();
    Period p;
    if (! openOk) return p;
    au.setP ("FLOW_SEQ_RATE", rateKnob);
    au.setP ("FLOW_SEQ_GATE", 0.30f);       // staccato — strong AM at the grid
    au.setP ("FLOW_CHOP_T_LEN", 0.35f);     // deep trim — silence between slices
    std::vector<float> out;
    const long long abs0 = au.tsSample;
    au.render (750, &out);                  // 8 s
    (void) abs0;
    p = envPeriod (out, (size_t) (2.0 * SR), expectSamples * 0.55, expectSamples * 1.60);
    au.close();
    return p;
}

int main()
{
    std::printf ("══ TERRAIN CHOP · AU PATH CERT (aumf/Tchp/Wvcr) ══\n");

    // ── G0 · the component ────────────────────────────────────────────────────
    std::printf ("── G0 · component ──\n");
    {
        AU au;
        const bool ok = au.open();
        check (ok, "AU aumf/Tchp/Wvcr found + initialized");
        if (! ok)
        {
            std::printf ("══ %d/%d — the plugin is not installed; every path gate is unreachable ══\n",
                         g_checks - g_fail, g_checks);
            return 1;
        }
        // ── G1 · param ids resolve (the JUCE hash IS the AU address) ─────────
        std::printf ("── G1 · param registry ──\n");
        check (au.hasP ("FLOW_SEQ_RATE"),     "FLOW_SEQ_RATE resolves");
        check (au.hasP ("FLOW_SEQ_MORPH"),    "FLOW_SEQ_MORPH resolves");
        check (au.hasP ("FLOW_CHOP_BLEND"),   "FLOW_CHOP_BLEND resolves");
        check (au.hasP ("FLOW_CHOP_CATCH"),   "FLOW_CHOP_CATCH resolves");
        check (au.hasP ("FLOW_CHOP_SLICES"),  "FLOW_CHOP_SLICES resolves");
        check (au.hasP ("FLOW_CHOP_T_LEN"),   "FLOW_CHOP_T_LEN resolves");
        check (au.hasP ("FLOW_CHOP_D_TONE"),  "FLOW_CHOP_D_TONE resolves");
        check (! au.hasP ("FLOW_CHOP_NOPE"),  "a nonsense id does NOT resolve (registry is real)");
        au.close();
    }

    // ── G2 · BLEND=0 -> dry-transparent ──────────────────────────────────────
    std::printf ("── G2 · BLEND=0 dry-transparent ──\n");
    {
        AU au;
        if (check (au.open(), "instance"), au.au != nullptr)
        {
            au.setP ("FLOW_CHOP_BLEND", 0.0f);
            std::vector<float> out;
            const long long abs0 = au.tsSample;
            au.render (280, &out);   // 3 s
            const double db = residualDb (out, abs0, (size_t) (1.0 * SR), (size_t) (1.5 * SR));
            std::printf ("       residual %.2f dBr (gate < -60)\n", db);
            check (db < -60.0, "BLEND=0 output == input");
            au.close();
        }
    }

    // ── G3 · FLOW_SEQ_RATE moves the grid (ladder-true, free-run @120) ───────
    std::printf ("── G3 · rate grid ──\n");
    {
        const float rA = 0.6111f;                 // shipped default = 1/16
        float rB = 0.30f;
        const double beatsA = (double) wc::arpBeatsPerStepRich (rA);
        double beatsB = (double) wc::arpBeatsPerStepRich (rB);
        if (std::fabs (beatsB - beatsA) < 1e-6)   // same ladder rung? walk until it differs
            for (float cand : { 0.20f, 0.40f, 0.90f })
                if (std::fabs ((double) wc::arpBeatsPerStepRich (cand) - beatsA) > 1e-6)
                { rB = cand; beatsB = (double) wc::arpBeatsPerStepRich (rB); break; }
        const double expA = beatsA * (60.0 / 120.0) * SR;   // free-run fallback = 120 BPM
        const double expB = beatsB * (60.0 / 120.0) * SR;
        bool okA = false, okB = false;
        const Period pA = probeRate (rA, expA, okA);
        const Period pB = probeRate (rB, expB, okB);
        std::printf ("       rate %.4f: ladder %.4f beats -> expect %.0f smp, measured %.0f (corr %.2f, depth %.2f)\n",
                     rA, beatsA, expA, pA.samples, pA.corr, pA.depth);
        std::printf ("       rate %.4f: ladder %.4f beats -> expect %.0f smp, measured %.0f (corr %.2f, depth %.2f)\n",
                     rB, beatsB, expB, pB.samples, pB.corr, pB.depth);
        check (okA && okB, "instances");
        check (pA.depth > 0.15, "rate A: output IS gated (envelope moves)");
        check (pB.depth > 0.15, "rate B: output IS gated (envelope moves)");
        check (pA.samples > 0 && std::fabs (pA.samples - expA) / expA < 0.12, "rate A period tracks the ladder");
        check (pB.samples > 0 && std::fabs (pB.samples - expB) / expB < 0.12, "rate B period tracks the ladder");
        if (expA > 0 && expB > 0 && pA.samples > 0 && pB.samples > 0)
        {
            const double ratioLadder = expB / expA, ratioMeas = pB.samples / pA.samples;
            check (std::fabs (ratioMeas - ratioLadder) / ratioLadder < 0.15,
                   "the TWO rates differ by the ladder ratio (the knob moves the grid)");
        }
    }

    // ── G4 · CATCH from the host MIDI lane (the aumf point) ──────────────────
    std::printf ("── G4 · CATCH via MIDI ──\n");
    {
        AU au;
        if (check (au.open(), "instance"), au.au != nullptr)
        {
            au.setP ("FLOW_CHOP_CATCH", 1.0f);
            au.setP ("FLOW_SEQ_GATE", 0.30f);
            au.setP ("FLOW_CHOP_T_LEN", 0.35f);

            std::vector<float> idle;
            long long abs0 = au.tsSample;
            au.render (280, &idle);   // 3 s, no note
            const double dbIdle = residualDb (idle, abs0, (size_t) (1.0 * SR), (size_t) (1.5 * SR));

            au.note (true, 60);       // hold C3 on the host note lane
            std::vector<float> held;
            abs0 = au.tsSample;
            au.render (280, &held);
            const double dbHeld = residualDb (held, abs0, (size_t) (1.0 * SR), (size_t) (1.5 * SR));

            au.note (false, 60);      // release
            std::vector<float> rel;
            abs0 = au.tsSample;
            au.render (280, &rel);
            const double dbRel = residualDb (rel, abs0, (size_t) (1.5 * SR), (size_t) (1.0 * SR));

            std::printf ("       idle %.2f dBr · held %.2f dBr · released %.2f dBr\n", dbIdle, dbHeld, dbRel);
            check (dbIdle < -50.0, "CATCH + no note   -> transparent");
            check (dbHeld > -30.0, "CATCH + note held -> chopping ENGAGES (MIDI stack wired)");
            check (dbRel  < -40.0, "note released     -> disengages again");
            au.close();
        }
    }

    std::printf ("══ %d/%d gates green ══\n", g_checks - g_fail, g_checks);
    return g_fail == 0 ? 0 : 1;
}
