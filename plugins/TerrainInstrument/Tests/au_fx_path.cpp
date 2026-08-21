// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb439 — DOES THE PLUGIN REACH THE ENGINE?  The first harness in this tree that renders the
//  REAL, INSTALLED plugin instead of an engine header behind a shim.
//
//    clang++ -O2 -std=c++17 Tests/au_fx_path.cpp -o /tmp/aufx \
//            -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreAudio
//
//  🔑 WHY THIS EXISTS (fb373, restated by Max at fb439): every one of the four fx4 devices passed
//     its own engine cert — the Equalizer's 147/147 among them — and all four were stone dead in
//     the plugin, because the chain never emitted an entry for their kind. An engine harness
//     CANNOT see that. This one can: it loads the installed AU, plays a note, and measures the
//     OUTPUT with the device out of the chain vs. in it with its controls pushed.
//
//  It reports, per device, the three numbers an ear actually integrates — level, spectrum and
//  stereo — so "it's in the chain" and "it does something" are separate, visible verdicts.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <complex>

static const double SR = 48000.0;
static const int    BLK = 512;
static const int    NBLK = 90;          // ~0.96 s
static const int    SKIP = 20;          // drop the attack/settling blocks

// ── tiny iterative radix-2 FFT ────────────────────────────────────────────────────────────────
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}

struct Render { std::vector<float> L, R; };

struct AU
{
    AudioUnit au = nullptr;
    std::map<std::string, AudioUnitParameterID> byName;
    std::map<AudioUnitParameterID, AudioUnitParameterInfo> info;

    bool open()
    {
        AudioComponentDescription d {};
        d.componentType         = kAudioUnitType_MusicDevice;
        d.componentSubType      = 'Tern';
        d.componentManufacturer = 'Wvcr';
        AudioComponent c = AudioComponentFindNext (nullptr, &d);
        if (! c) { printf ("  !! AU aumu/Tern/Wvcr not found — is it installed?\n"); return false; }
        if (AudioComponentInstanceNew (c, &au) != noErr) { printf ("  !! instantiate failed\n"); return false; }

        AudioStreamBasicDescription f {};
        f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4;
        f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
        AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &f, sizeof f);
        UInt32 mx = BLK;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mx, sizeof mx);
        if (AudioUnitInitialize (au) != noErr) { printf ("  !! AudioUnitInitialize failed\n"); return false; }

        UInt32 sz = 0; Boolean w = false;
        if (AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w) != noErr || sz == 0)
        { printf ("  !! no parameter list\n"); return false; }
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids) {
            AudioUnitParameterInfo pi {}; UInt32 s = sizeof pi;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &pi, &s) != noErr) continue;
            std::string nm;
            if ((pi.flags & kAudioUnitParameterFlag_HasCFNameString) && pi.cfNameString) {
                char buf[256] = {0};
                CFStringGetCString (pi.cfNameString, buf, sizeof buf, kCFStringEncodingUTF8);
                nm = buf;
            } else nm = pi.name;
            byName[nm] = id; info[id] = pi;
        }
        return true;
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }

    bool has (const std::string& n) const { return byName.count (n) > 0; }
    // norm 0..1 mapped onto the parameter's own reported range
    bool set (const std::string& n, float norm)
    {
        auto it = byName.find (n); if (it == byName.end()) return false;
        const auto& pi = info.at (it->second);
        const float v = pi.minValue + norm * (pi.maxValue - pi.minValue);
        return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr;
    }
    float get (const std::string& n)
    {
        auto it = byName.find (n); if (it == byName.end()) return NAN;
        AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, it->second, kAudioUnitScope_Global, 0, &v);
        return v;
    }

    Render render()
    {
        MusicDeviceMIDIEvent (au, 0x90, 48, 100, 0);      // a low-ish note: energy in every band
        MusicDeviceMIDIEvent (au, 0x90, 55, 100, 0);
        MusicDeviceMIDIEvent (au, 0x90, 64, 100, 0);
        Render out;
        std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer));
        abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = 0;
        for (int b = 0; b < NBLK; ++b) {
            abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() };
            abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
            AudioUnitRenderActionFlags fl = 0;
            if (AudioUnitRender (au, &fl, &ts, 0, BLK, abl) != noErr) break;
            ts.mSampleTime += BLK;
            if (b >= SKIP) { out.L.insert (out.L.end(), bl.begin(), bl.end());
                             out.R.insert (out.R.end(), br.begin(), br.end()); }
        }
        free (abl);
        return out;
    }
};

static double rmsdb (const std::vector<float>& v)
{
    double a = 0; for (float x : v) a += (double) x * x;
    return 20.0 * std::log10 (std::sqrt (a / std::max<size_t> (1, v.size())) + 1e-12);
}
static double crest (const std::vector<float>& v)
{
    double a = 0, pk = 0; for (float x : v) { a += (double) x * x; pk = std::max (pk, (double) std::fabs (x)); }
    const double r = std::sqrt (a / std::max<size_t> (1, v.size()));
    return 20.0 * std::log10 ((pk + 1e-12) / (r + 1e-12));
}
static double sideRatio (const Render& r)
{
    double m = 0, s = 0;
    for (size_t i = 0; i < r.L.size(); ++i) {
        const double mid = 0.5 * (r.L[i] + r.R[i]), sd = 0.5 * (r.L[i] - r.R[i]);
        m += mid * mid; s += sd * sd;
    }
    return 10.0 * std::log10 ((s + 1e-15) / (m + 1e-15));
}
// average magnitude spectrum in dB over 32 log bands, 20 Hz .. 18 kHz
static std::vector<double> spec (const std::vector<float>& v)
{
    const size_t N = 4096;
    std::vector<double> acc (N / 2, 0.0); int frames = 0;
    for (size_t off = 0; off + N <= v.size(); off += N) {
        std::vector<std::complex<double>> a (N);
        for (size_t i = 0; i < N; ++i) {
            const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) (N - 1));
            a[i] = std::complex<double> (v[off + i] * w, 0.0);
        }
        fft (a);
        for (size_t i = 0; i < N / 2; ++i) acc[i] += std::abs (a[i]);
        ++frames;
    }
    if (frames == 0) frames = 1;
    std::vector<double> band (32, 0.0);
    for (int b = 0; b < 32; ++b) {
        const double f0 = 20.0 * std::pow (900.0, (double) b / 32.0);
        const double f1 = 20.0 * std::pow (900.0, (double) (b + 1) / 32.0);
        const size_t i0 = (size_t) std::max (1.0, f0 / (SR / (double) N));
        const size_t i1 = (size_t) std::min ((double) (N / 2 - 1), f1 / (SR / (double) N));
        double e = 0; int n = 0;
        for (size_t i = i0; i <= i1; ++i) { e += acc[i] / frames; ++n; }
        band[b] = 20.0 * std::log10 (e / std::max (1, n) + 1e-12);
    }
    return band;
}
static double specDist (const std::vector<double>& a, const std::vector<double>& b)
{
    double s = 0; int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) { s += std::fabs (a[i] - b[i]); ++n; }
    return s / std::max (1, n);
}

static int npass = 0, nfail = 0;
static void chk (bool ok, const char* what, const std::string& detail)
{
    if (ok) { ++npass; printf ("  ok   %-58s %s\n", what, detail.c_str()); }
    else    { ++nfail; printf ("  FAIL %-58s %s\n", what, detail.c_str()); }
}

struct Dev { const char* label; const char* pfx; const char* knobs[4]; };

int main()
{
    printf ("\n══ fb439 — REAL-PLUGIN PATH TEST (the installed AU, not an engine header) ══\n\n");

    // ── the control render: no fx4 device in the chain at all ─────────────────────────────────
    AU ctl; if (! ctl.open()) return 2;
    const Render dry = ctl.render();
    const double dryRms = rmsdb (dry.L);
    const std::vector<double> drySpec = spec (dry.L);
    chk (dry.L.size() > 1000 && dryRms > -60.0, "the probe itself makes sound (harness is not measuring silence)",
         "rms=" + std::to_string (dryRms) + " dB, " + std::to_string (dry.L.size()) + " samples");
    chk (ctl.has ("Equalizer In Chain") && ctl.has ("Widen In Chain")
      && ctl.has ("Compress In Chain") && ctl.has ("Multiband In Chain"),
         "all four devices expose In Chain / Power to the host", "");
    ctl.close();
    if (dryRms <= -60.0) { printf ("\n  probe is silent — cannot measure. STOP.\n"); return 2; }

    // ── each device: put it in the chain, power it, push its controls, measure the output ─────
    const Dev devs[7] = {
        { "Equalizer", "Equalizer", { "Equalizer Amount", "Equalizer Low",  "Equalizer Body",  "Equalizer Slant" } },
        { "Widen",     "Widen",     { "Widen Amount",     "Widen Width",    "Widen Spread",    nullptr } },
        { "Compress",  "Compress",  { "Compress Push",    "Compress Ratio", "Compress Lift",   nullptr } },
        { "Multiband", "Multiband", { "Multiband Amount", "Multiband Raise","Multiband Press", nullptr } },
        // fb444 — Bode. Shift is the hero; Fdbk and Direction are the two that most change the
        // output, so if this row measures Delta 0.00 the device is in the chain and dead.
        { "Bode",      "Bode",      { "Bode Shift",       "Bode Fdbk",      "Bode Direction",  "Bode Blur" } },
        // fb444 — Utility. Gain is the hero; Image and Strain are the two that most change the
        // output. Note Gain's UNITY is 0.667, so pushing it to 1.0 is a real +30 dB change.
        { "Utility",   "Utility",   { "Utility Gain",     "Utility Image",  "Utility Strain",  "Utility Twist" } },
        // fb444 — Splitter. With no lane devices it still shapes: per-lane gains and Balance.
        { "Splitter",  "Splitter",  { "Splitter Balance", "Splitter Lane 1 Gain", "Splitter Spread", "Splitter Split" } },
    };
    for (const auto& d : devs)
    {
        AU a; if (! a.open()) return 2;
        const std::string P = d.pfx;
        bool okSet = true;
        okSet &= a.set (P + " In Chain", 1.0f);
        okSet &= a.set (P + " Power",    1.0f);
        okSet &= a.set (P + " Mix",      1.0f);
        // 🔑 THE ROUTE. Devices arrive UNROUTED by design (fb362 "unrouted on arrival"), and
        //    TW_FX4_APPLY gates on `poolRouteAny_[BASE + inst0]`, so a device with no source
        //    selected returns its input BIT-IDENTICALLY. The card's A/B/C/D/Sub/Noise row is what
        //    normally sets this; the harness must do the same or it is measuring a device that was
        //    never asked to listen to anything.
        const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
        int routed = 0;
        for (int k = 0; k < 6; ++k) if (a.set (P + " " + SRC[k], 1.0f)) ++routed;
        chk (routed == 6, (std::string (d.label) + ": all six route sources are host-writable").c_str(),
             std::to_string (routed) + "/6");
        for (int k = 0; k < 4; ++k) if (d.knobs[k]) a.set (d.knobs[k], 1.0f);
        chk (okSet, (std::string (d.label) + ": host could write In Chain / Power / Mix").c_str(), "");

        const Render wet = a.render();
        const double wRms = rmsdb (wet.L);
        const double dS   = specDist (drySpec, spec (wet.L));
        const double dSide= sideRatio (wet) - sideRatio (dry);
        const double dCr  = crest (wet.L) - crest (dry.L);
        char det[256];
        snprintf (det, sizeof det, "Δlevel=%+.2f dB  Δspectrum=%.2f dB/band  Δside=%+.2f dB  Δcrest=%+.2f dB",
                  wRms - dryRms, dS, dSide, dCr);
        // "does the mechanism engage" (fb432): ANY of the three axes moving is proof of arrival.
        const bool moved = std::fabs (wRms - dryRms) > 0.35 || dS > 0.35
                        || std::fabs (dSide) > 0.35 || std::fabs (dCr) > 0.35;
        chk (moved, (std::string (d.label) + ": the audio actually REACHES the device").c_str(), det);
        a.close();
    }

    // ══ fb444 — THE SPLITTER'S ONE INDISPENSABLE PROPERTY, MEASURED IN THE PLUGIN ══════════
    // Its engine cert nulls reconstruction at -122 dB, but fb373 is the whole law of this file:
    // a green engine says nothing about whether the plugin reaches it, or reaches it CORRECTLY.
    // A splitter that combs at its crossovers is useless no matter how good the UI is, and a
    // comb would be INAUDIBLE as "broken" - it just sounds slightly thin, forever. So: put a
    // Splitter in the chain at its DEFAULTS, touch nothing, and require the output to be the
    // same sound. Every OTHER row in this file asserts Delta != 0; this one asserts Delta ~ 0,
    // and that asymmetry is the point.
    {
        AU a;
        if (a.open())
        {
            a.set ("Splitter In Chain", 1.0f);
            a.set ("Splitter Power",    1.0f);
            const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
            for (int k = 0; k < 6; ++k) a.set (std::string ("Splitter ") + SRC[k], 1.0f);
            const Render w = a.render();
            const double dL = rmsdb (w.L) - dryRms;
            const double dS = specDist (drySpec, spec (w.L));
            char det[200];
            snprintf (det, sizeof det, "Δlevel=%+.3f dB  Δspectrum=%.3f dB/band  (a comb here would be permanent and inaudible as a fault)", dL, dS);
            chk (std::fabs (dL) < 0.60 && dS < 1.20,
                 "Splitter at DEFAULTS is transparent: split and rejoin do not comb", det);
            a.close();
        }
    }

    // ══ fb446 — A DEVICE IN A BAND IS POWERED BY THE BAND ═══════════════════════════════════
    // Lane cards carry NO route row (they are fed by the Splitter's band), so nothing lights their
    // SRC_* pills — and every pooled apply routine gates on poolRouteAny_. Without the lane-power
    // rule in resolveLanes(), a Distortion dropped into the Mid band would return its input forever,
    // bit-identical, with a full green build (fb435's exact shape). So: Splitter + a Distortion in
    // band 2 with Drive up, NO Distortion routes, versus the Splitter alone — the spectrum must move.
    {
        AU a;
        if (a.open())
        {
            const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
            a.set ("Splitter In Chain", 1.0f); a.set ("Splitter Power", 1.0f); a.set ("Splitter Chain Rank", 0.30f);
            for (int k = 0; k < 6; ++k) a.set (std::string ("Splitter ") + SRC[k], 1.0f);
            const Render base = a.render();
            const auto baseSpec = spec (base.L);
            a.set ("Distortion In Chain", 1.0f); a.set ("Distortion Power", 1.0f); a.set ("Distortion Chain Rank", 0.60f);
            a.set ("Distortion Lane", 2.0f / 7.0f);      // choice(8): index 2 = "Lane 2" (Mid), never lround(raw*(N-1))
            a.set ("Distortion Drive", 1.0f); a.set ("Distortion Mix", 1.0f);
            // deliberately NO "Distortion SRC_*" — a lane card has no route row
            const Render w = a.render();
            const double dS = specDist (baseSpec, spec (w.L));
            const double dL = rmsdb (w.L) - rmsdb (base.L);
            char det[200];
            snprintf (det, sizeof det, "Distortion in band 2, unrouted: Δspectrum=%.2f dB/band  Δlevel=%+.2f dB  (0.00 = the lane device is dead)", dS, dL);
            chk (dS > 0.35 || std::fabs (dL) > 0.35, "a LEGACY device (Distortion) in a Splitter band PROCESSES the band with NO routes lit", det);
            a.close();
        }
        // ... and a POOLED kind (Bode, whose power gate is poolRouteAny_), same shape
        AU b2;
        if (b2.open())
        {
            const char* SRC[6] = { "SRC_A","SRC_B","SRC_C","SRC_D","SRC_SUB","SRC_NOISE" };
            b2.set ("Splitter In Chain", 1.0f); b2.set ("Splitter Power", 1.0f); b2.set ("Splitter Chain Rank", 0.30f);
            for (int k = 0; k < 6; ++k) b2.set (std::string ("Splitter ") + SRC[k], 1.0f);
            const Render base = b2.render(); const auto baseSpec = spec (base.L);
            b2.set ("Bode In Chain", 1.0f); b2.set ("Bode Power", 1.0f); b2.set ("Bode Chain Rank", 0.60f);
            b2.set ("Bode Lane", 2.0f / 7.0f); b2.set ("Bode Shift", 0.92f); b2.set ("Bode Mix", 1.0f);
            const Render w = b2.render();
            const double dS = specDist (baseSpec, spec (w.L)), dL = rmsdb (w.L) - rmsdb (base.L);
            char det[200]; snprintf (det, sizeof det, "Bode in band 2, unrouted, Shift 92%%: Δspectrum=%.2f dB/band  Δlevel=%+.2f dB", dS, dL);
            chk (dS > 0.35 || std::fabs (dL) > 0.35, "a POOLED device (Bode) in a Splitter band PROCESSES the band with NO routes lit", det);
            b2.close();
        }
    }

    printf ("\n  PASS %d   FAIL %d\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
