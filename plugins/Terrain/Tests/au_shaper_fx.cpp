// ══════════════════════════════════════════════════════════════════════════════════════════════
//  au_shaper_fx.cpp — tp83 · SOUND-CERTIFYING THE SHAPER'S BORROWED EFFECTS, IN THE INSTALLED AU.
//
//  Nine of the Shaper's seventeen lanes are rack engines it borrows — Reverb, Delay, Chorus, Widen,
//  Multiband, Tape, Granular, Bode, Noise. Until tp83 none of them could be certified at all: the
//  chain lived in the shaperJson blob, so no harness could PLACE a kind. The eight positions are
//  parameters now (Shaper Slot 1..8), and this is what that bought.
//
//  WHAT IS BEING CERTIFIED, and why in this form:
//   · IT DOES SOMETHING — the lane's output against the same render with the lane off, measured as a
//     MAGNITUDE-SPECTRUM change over a log-spaced Goertzel bank. Magnitude, not samples: a phase-only
//     change gives an enormous sample difference and is inaudible, which is the trap fb283 was written
//     about (Plate Dispersion measured "102 % divergence" and Max heard nothing). Sample-difference RMS
//     is not used here as evidence of anything.
//   · THE DRAWN CURVE IS WHAT MOVES IT — the same measurement taken where the shape sits in its trough
//     and where it sits at its peak. An effect that is merely always-on is not a SHAPER lane, and this
//     is the bar that tells the difference.
//   · and the three that have an unmistakable signature of their own get it named directly: Bode moves
//     a pure tone's energy OFF its own frequency, Widen raises side against mid, Noise makes sound out
//     of silence — the only lane that adds signal rather than processing it.
//
//  WHAT IT DOES NOT CERTIFY: taste. It says each lane is audible, rhythmic and doing its own job. Whether
//  a Plate at Size 0.45 sounds GOOD is Max's ear, and this is the point that hands it to him.
//
//  clang++ -O2 -std=c++17 Tests/au_shaper_fx.cpp -o /tmp/aufx -framework AudioToolbox \
//          -framework AudioUnit -framework CoreFoundation -framework CoreAudio && /tmp/aufx
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
    std::map<std::string, std::pair<float,float>> byRange;   // tp83 — a choice's scale differs per parameter; never guess it
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
            byName[b] = id; byRange[b] = { pi.minValue, pi.maxValue };
        }
        return true;
    }
    /** tp83 — a choice parameter's scale is NOT 0..1 everywhere in this plugin (the existing cert sets one
        as 4/117 and another as a raw 2). Ask the AU what the range is rather than guessing it. */
    bool setIndex (const std::string& n, int idx, int count)
    {
        auto it = byRange.find (n); if (it == byRange.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; }
        const float lo = it->second.first, hi = it->second.second;
        const float v = (count > 1) ? lo + (hi - lo) * ((float) idx / (float) (count - 1)) : lo;
        return set (n, v);
    }
    bool set (const std::string& n, float v) { auto it = byName.find (n); if (it == byName.end()) { printf ("  !! no param '%s'\n", n.c_str()); return false; } return AudioUnitSetParameter (au, it->second, kAudioUnitScope_Global, 0, v, 0) == noErr; }
    void pump (double sec) { const double t0 = CFAbsoluteTimeGetCurrent(); while (CFAbsoluteTimeGetCurrent() - t0 < sec) CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.01, false); }
    void note (int n, int v) { MusicDeviceMIDIEvent (au, v > 0 ? 0x90 : 0x80, (UInt32) n, (UInt32) v, 0); }
    // render `blocks` with the transport at ppq (advancing when playing)
    void render (int blocks, double ppqStart, bool playing, std::vector<float>* keep = nullptr, std::vector<float>* keepR = nullptr)
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
            if (keep)  keep ->insert (keep ->end(), L.begin(), L.end());
            if (keepR) keepR->insert (keepR->end(), R.begin(), R.end());   // tp83 — Widen's claim is about the IMAGE
            CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.001, false);
        }
    }
    void close() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); au = nullptr; } }
};
static double rmsDb (const std::vector<float>& x, size_t a, size_t b) { double e = 0; size_t n = 0; for (size_t i = a; i < b && i < x.size(); ++i) { e += (double) x[i] * x[i]; ++n; } return 20 * std::log10 (std::sqrt (e / std::max<size_t> (1, n)) + 1e-12); }
static const size_t STEP = 6000;   // a sixteenth at 120 BPM, 48 kHz


// ── the metrics ───────────────────────────────────────────────────────────────────────────────
//  Goertzel: one bin's magnitude, mean removed. Phase-independent by construction, which is the whole
//  reason it is here rather than a sample difference.
static double goertzel (const std::vector<float>& x, size_t a, size_t n, double f)
{
    if (a + n > x.size()) return 0.0;
    double mean = 0; for (size_t i = a; i < a + n; ++i) mean += x[i]; mean /= (double) n;
    const double w = 2.0 * M_PI * f / SR, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (size_t i = a; i < a + n; ++i) { const double s0 = ((double) x[i] - mean) + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) n;
}
static const int NB = 24;
static double binHz (int b) { return 60.0 * std::pow (16000.0 / 60.0, (double) b / (NB - 1)); }   // 60 Hz .. 16 kHz, log
//  the largest magnitude change, in dB, across the bank — counting only bins that carry real energy in
//  one render or the other, so a silent corner of the spectrum cannot invent a number.
static double specChangeDb (const std::vector<float>& on, const std::vector<float>& off, size_t a, size_t n)
{
    double worst = 0, ref = 0;
    for (int b = 0; b < NB; ++b) { ref = std::max (ref, std::max (goertzel (on, a, n, binHz (b)), goertzel (off, a, n, binHz (b)))); }
    if (ref < 1e-7) return 0.0;
    for (int b = 0; b < NB; ++b)
    {
        const double u = goertzel (on, a, n, binHz (b)), v = goertzel (off, a, n, binHz (b));
        if (std::max (u, v) < ref * 0.02) continue;                       // below the floor: not evidence
        worst = std::max (worst, std::fabs (20.0 * std::log10 ((u + 1e-9) / (v + 1e-9))));
    }
    return worst;
}
//  a band TILT is invisible to specChangeDb: tilted one way and tilted the other are both "changed" by
//  about the same amount. The ratio itself is the measurement.
static double bandRatio (const std::vector<float>& x, size_t a, size_t n)
{
    double lo = 0, hi = 0;
    for (double f = 80.0;   f < 500.0;   f *= 1.12) lo += goertzel (x, a, n, f);
    for (double f = 2000.0; f < 12000.0; f *= 1.12) hi += goertzel (x, a, n, f);
    return (hi + 1e-9) / (lo + 1e-9);
}
//  tape's signature is what it ADDS to a tone: harmonics that were not there.
static double thd (const std::vector<float>& x, size_t a, size_t n, double f0)
{
    const double f = goertzel (x, a, n, f0);
    double h = 0; for (int k = 2; k <= 6; ++k) h += goertzel (x, a, n, f0 * k);
    return (h + 1e-9) / (f + 1e-9);
}
static double sideOverMid (const std::vector<float>& l, const std::vector<float>& r, size_t a, size_t n)
{
    double m = 0, s = 0;
    for (size_t i = a; i < a + n && i < l.size() && i < r.size(); ++i)
    { const double mm = 0.5 * (l[i] + r[i]), ss = 0.5 * (l[i] - r[i]); m += mm * mm; s += ss * ss; }
    return std::sqrt (s / (m + 1e-12));
}

int main()
{
    Au a; if (! a.open()) { printf ("  !! no Terrain AU\n"); return 2; }
    printf ("\ntp83 — SOUND-CERTIFYING THE SHAPER'S BORROWED EFFECTS (installed AU, a host transport)\n\n");
    a.pump (1.0); a.render (20, 0.0, false);
    const bool okp = a.set ("Flow Chain 1", 2.0f);      // the Chop slot = the Shaper
    a.pump (0.8); a.render (10, 0.0, false);
    chk (okp, "[0] the Shaper sits in Flow Chain 1 (the Chop slot)");

    static const char* const kAll[18] = { "Volume","Time","Filter","Pan","Repeat","Drive","Phaser","Crush",
                                          "Reverb","Delay","Chorus","Widen","Multiband","Tape","Granular","Bode","Noise", "Flanger" };
    // every lane dark to begin with, and the chain back on the tile order
    auto reset = [&] {
        for (int q = 0; q < 18; ++q) a.set (std::string ("Shaper ") + kAll[q] + " On", 0.0f);
        for (int q = 0; q < 8; ++q)  a.setIndex (std::string ("Shaper Slot ") + std::to_string (q + 1), q, 18);
        a.pump (0.4); a.render (20, 0.0, false);
    };
    chk (a.setIndex ("Shaper Slot 1", 8, 18), "[1] the chain is PARAMETERS — a kind can be placed at a position from outside the interface (this is what unlocked everything below)");

    // ── the battery ───────────────────────────────────────────────────────────────────────────
    //  Two bars at 120 BPM with a held C4. The borrowed lanes boot on a SINE over one bar, so the shape
    //  sits in its trough at the bar line and at its peak half a bar later — which is what lets the same
    //  measurement answer "does it do anything" and "does the CURVE move it".
    const int BARS = 2, BLOCKS = (int) (SR * 4.0 / BLK);
    const size_t TROUGH = (size_t) (SR * 0.06), PEAK = (size_t) (SR * 1.0), WIN = (size_t) (SR * 0.35);
    (void) BARS;
    struct Res { double change; double trough; double peak; };
    auto measure = [&] (int kind) -> Res
    {
        const std::string on = std::string ("Shaper ") + kAll[kind] + " On";
        reset (); a.setIndex ("Shaper Slot 1", kind, 18); a.pump (0.5);
        std::vector<float> wet, wetR, dry, dryR;
        a.set (on, 1.0f); a.pump (0.5);
        a.note (60, 100); a.render (BLOCKS, 0.0, true, &wet, &wetR); a.note (60, 0); a.render (120, 4.0, true);
        a.set (on, 0.0f); a.pump (0.5);
        a.note (60, 100); a.render (BLOCKS, 0.0, true, &dry, &dryR); a.note (60, 0); a.render (120, 4.0, true);
        Res r {};
        r.change = specChangeDb (wet, dry, PEAK, WIN);
        r.trough = specChangeDb (wet, dry, TROUGH, WIN);
        r.peak   = r.change;
        return r;
    };

    for (int k = 8; k <= 16; ++k)
    {
        /* Noise, Multiband and Tape are certified on their own terms below. Noise ADDS signal, so "against dry"
           is the wrong question; Multiband TILTS, and tilted-one-way and tilted-the-other look equally "changed"
           to a magnitude metric; Tape's claim is the harmonics it adds, not the level it sits at. Using the
           generic bar on those three would be measuring the wrong thing and calling it a pass or a fail. */
        if (k == 16 || k == 12 || k == 13) continue;
        const Res r = measure (k);
        char b[260];
        std::snprintf (b, sizeof b, "[%d] %s: the spectrum moves %.1f dB against the same render with the lane dark, and %.1f dB more at the shape's peak than in its trough",
                       k - 6, kAll[k], r.change, r.peak - r.trough);
        chk (r.change > 1.5 && r.peak > r.trough + 0.5, b);
    }

    // ── the three with a signature of their own ───────────────────────────────────────────────
    {   // MULTIBAND tilts the bands — the ratio is the claim, not "the spectrum changed"
        reset (); a.setIndex ("Shaper Slot 1", 12, 18); a.set ("Shaper Multiband On", 1.0f); a.pump (0.6);
        std::vector<float> x; a.note (60, 100); a.render (BLOCKS, 0.0, true, &x); a.note (60, 0); a.render (120, 4.0, true);
        const double t = bandRatio (x, TROUGH, WIN), pk = bandRatio (x, PEAK, WIN);
        char b[230]; std::snprintf (b, sizeof b, "[10] Multiband TILTS WITH THE SHAPE: high-against-low %.4f in the shape's trough against %.4f at its peak (ratio %.2fx)",
                                    t, pk, (pk + 1e-9) / (t + 1e-9));
        chk (pk > t * 1.25 || t > pk * 1.25, b);
    }
    {   // TAPE's signature is the harmonics it adds, not the level it sits at
        reset (); a.setIndex ("Shaper Slot 1", 13, 18); a.set ("Shaper Tape On", 1.0f); a.pump (0.6);
        std::vector<float> x; a.note (60, 100); a.render (BLOCKS, 0.0, true, &x); a.note (60, 0); a.render (120, 4.0, true);
        const double t = thd (x, TROUGH, WIN, 261.63), pk = thd (x, PEAK, WIN, 261.63);
        char b[230]; std::snprintf (b, sizeof b, "[11] Tape COLOURS WITH THE SHAPE: harmonics-against-fundamental %.4f in the shape's trough against %.4f at its peak", t, pk);
        chk (pk > t * 1.2, b);
    }
    {   // BODE moves a pure tone OFF its own frequency — no other lane here can do that
        reset (); a.setIndex ("Shaper Slot 1", 15, 18); a.set ("Shaper Bode On", 1.0f);
        a.set ("Shaper Bode Mode", 0.0f); a.pump (0.6);
        std::vector<float> x; a.note (60, 100); a.render (BLOCKS, 0.0, true, &x); a.note (60, 0); a.render (120, 4.0, true);
        const double at261 = goertzel (x, PEAK, WIN, 261.63);          // C4, the note being held
        double off = 0; for (double f = 300.0; f < 900.0; f += 7.0) off = std::max (off, goertzel (x, PEAK, WIN, f));
        char b[220]; std::snprintf (b, sizeof b, "[12] Bode SHIFTS: with C4 held, %.1f dB of energy sits off the note's own frequency (fundamental %.5f, strongest elsewhere %.5f)",
                                    20.0 * std::log10 ((off + 1e-9) / (at261 + 1e-9)), at261, off);
        chk (off > at261 * 0.25, b);
    }
    {   // WIDEN raises the sides against the middle — the one claim it makes
        reset (); a.setIndex ("Shaper Slot 1", 11, 18);
        std::vector<float> wl, wr, dl, dr;
        a.set ("Shaper Widen On", 1.0f); a.pump (0.5);
        a.note (60, 100); a.render (BLOCKS, 0.0, true, &wl, &wr); a.note (60, 0); a.render (120, 4.0, true);
        a.set ("Shaper Widen On", 0.0f); a.pump (0.5);
        a.note (60, 100); a.render (BLOCKS, 0.0, true, &dl, &dr); a.note (60, 0); a.render (120, 4.0, true);
        const double sw = sideOverMid (wl, wr, PEAK, WIN), sd = sideOverMid (dl, dr, PEAK, WIN);
        char b[200]; std::snprintf (b, sizeof b, "[13] Widen OPENS THE IMAGE: side-against-mid %.4f lit against %.4f dark", sw, sd);
        chk (sw > sd + 0.02, b);
    }
    {   // NOISE makes sound out of SILENCE — the only lane that adds signal rather than processing it
        reset (); a.setIndex ("Shaper Slot 1", 16, 18);
        std::vector<float> off, on;
        a.render (BLOCKS, 0.0, true, &off);                      // no note at all, lane dark
        a.set ("Shaper Noise On", 1.0f); a.pump (0.6);
        a.render (BLOCKS, 0.0, true, &on);                       // no note at all, lane lit
        const double q = rmsDb (off, PEAK, PEAK + WIN), l = rmsDb (on, PEAK, PEAK + WIN);
        char b[220]; std::snprintf (b, sizeof b, "[14] Noise SOUNDS WITH NOTHING PLAYING: %.1f dBFS lit against %.1f dBFS dark, with no note held — the only lane that adds signal", l, q);
        chk (l > q + 20.0, b);
    }
    {   /* tp91 — FLANGER: THE COMB MOVES WITH THE LINE. The generic bar asks for more effect at the shape's peak
           than in its trough, which is the wrong claim here — the comb is full-strength at both ends of the drawing;
           what the line does is MOVE it. So: the lit render's spectrum in the trough against its spectrum at the
           peak, beyond the held note's own drift between the same two windows (the dark render's). */
        reset (); a.setIndex ("Shaper Slot 1", 17, 18); a.pump (0.5);
        std::vector<float> wet, dry;
        a.set ("Shaper Flanger On", 1.0f); a.pump (0.6);
        a.note (60, 100); a.render (BLOCKS, 0.0, true, &wet); a.note (60, 0); a.render (120, 4.0, true);
        a.set ("Shaper Flanger On", 0.0f); a.pump (0.5);
        a.note (60, 100); a.render (BLOCKS, 0.0, true, &dry); a.note (60, 0); a.render (120, 4.0, true);
        std::vector<float> wT (wet.begin() + (long) TROUGH, wet.begin() + (long) (TROUGH + WIN)), wP (wet.begin() + (long) PEAK, wet.begin() + (long) (PEAK + WIN));
        std::vector<float> dT (dry.begin() + (long) TROUGH, dry.begin() + (long) (TROUGH + WIN)), dP (dry.begin() + (long) PEAK, dry.begin() + (long) (PEAK + WIN));
        const double moveW = specChangeDb (wT, wP, 0, WIN), moveD = specChangeDb (dT, dP, 0, WIN), lit = specChangeDb (wet, dry, PEAK, WIN);
        char b[260]; std::snprintf (b, sizeof b, "[15] Flanger's COMB MOVES WITH THE LINE: trough against peak the lit spectrum moves %.1f dB (the note alone drifts %.1f), and the lane is %.1f dB from dark",
                                    moveW, moveD, lit);
        chk (moveW > moveD + 6.0 && lit > 1.5, b);
    }
    reset ();
    printf ("\n  %d passed, %d failed\n\n", npass, nfail);
    return nfail ? 1 : 0;
}
