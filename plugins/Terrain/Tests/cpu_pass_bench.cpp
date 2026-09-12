// ══ fb636 — THE CPU PASS'S INSTRUMENTS, on the INSTALLED AU (the same door a DAW uses) ═══════════════════
//   c++ -std=c++17 -O2 -I Tests Tests/cpu_pass_bench.cpp -framework AudioToolbox -framework CoreFoundation -o cpu_pass_bench
//
//   golden  <outdir> <a.terrain ...>   one fixed performance per preset on a FRESH instance → <outdir>/<name>.f32
//                                      (L/R interleaved float32). The audio contract of the pass: render the bank
//                                      before a change and after it, then `compare`.
//   compare <dirA> <dirB>              per preset: bit-identical samples, max |diff|, null dB. Exit 1 if any differ.
//   cpu     <reps> <a.terrain ...>     steady CPU of a HELD 4-note chord + a GATED chord/s (releases overlap)
//   synth   <reps>                     the init patch with 1, 2, 3, 4 oscillators on — "10 % per oscillator"
//   spikes  <reps> <file|init:N>       chord/s with releases; the worst blocks and what just happened before each
//
//   CPU % = render wall time / audio time on one thread, block 512 @ 48 kHz — what a DAW's meter reads.
//   The harness thread asks for USER_INTERACTIVE QoS so it stays on a performance core.
#include "au_state_blob.h"
#include <chrono>
#include <algorithm>
#include <pthread.h>
#include <sys/stat.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/mach_time.h>
static void makeRealtime()
{
    if (! std::getenv ("CPB_RT")) return;   // opt-in: a time-constraint thread rendering flat out breaks its own constraint and is demoted (measured: 4x slower)
    mach_timebase_info_data_t tb; mach_timebase_info (&tb);
    const double nsPerTick = (double) tb.numer / tb.denom, periodNs = 1e9 * BLK / SR;
    thread_time_constraint_policy_data_t p;
    p.period = (uint32_t) (periodNs / nsPerTick); p.computation = (uint32_t) (0.5 * periodNs / nsPerTick);
    p.constraint = (uint32_t) (periodNs / nsPerTick); p.preemptible = 1;
    const kern_return_t r = thread_policy_set (mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t) &p, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    std::fprintf (stderr, "[rt] time-constraint policy %s\n", r == KERN_SUCCESS ? "ON" : "FAILED");
}

static std::string slurpBin (const std::string& p) { std::ifstream f (p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static uint32_t u32 (const std::string& s, size_t at) { return (uint32_t) (uint8_t) s[at] | ((uint32_t) (uint8_t) s[at+1] << 8) | ((uint32_t) (uint8_t) s[at+2] << 16) | ((uint32_t) (uint8_t) s[at+3] << 24); }
static std::string xmlOfTerrain (const std::string& path)
{
    const std::string f = slurpBin (path);
    if (f.size() < 12 || f.compare (0, 4, "TRN1") != 0) return {};
    const uint32_t ml = u32 (f, 4); if (8 + ml + 4 > f.size()) return {};
    const uint32_t cl = u32 (f, 8 + ml); const size_t c0 = 12 + ml; if (c0 + cl > f.size() || cl < 8) return {};
    const std::string chunk = f.substr (c0, cl);
    if (chunk.compare (0, 4, "VC2!") != 0) return {};
    std::string xml = chunk.substr (8); while (! xml.empty() && xml.back() == '\0') xml.pop_back();
    return xml;
}
static std::string xmlOrTerrain (const std::string& p) { if (p.size() > 4 && p.compare (p.size() - 4, 4, ".xml") == 0) return slurpBin (p); return xmlOfTerrain (p); }
static std::string baseName (const std::string& p) { size_t s = p.find_last_of ('/'); std::string b = s == std::string::npos ? p : p.substr (s + 1); size_t d = b.rfind ('.'); return d == std::string::npos ? b : b.substr (0, d); }


// ── a DAW's transport: 120 bpm, 4/4, playing. JUCE's AU wrapper builds the playhead from these; with
//    none, every tempo-synced LFO / flow card sees no host at all. CPB_NOHOST=1 turns it off.
static double g_hostSample = 0.0;
static OSStatus cbBeat (void*, Float64* beat, Float64* tempo)
{ if (beat) *beat = g_hostSample / SR * 2.0; if (tempo) *tempo = 120.0; return noErr; }
static OSStatus cbMusical (void*, UInt32* toNext, Float32* num, UInt32* den, Float64* downbeat)
{ const double b = g_hostSample / SR * 2.0; if (toNext) *toNext = (UInt32) ((std::ceil (b) - b) * SR / 2.0);
  if (num) *num = 4; if (den) *den = 4; if (downbeat) *downbeat = std::floor (b / 4.0) * 4.0; return noErr; }
static OSStatus cbTransport2 (void*, Boolean* playing, Boolean* recording, Boolean* changed, Float64* sampleInTL, Boolean* cycling, Float64* cs, Float64* ce)
{ if (playing) *playing = true; if (recording) *recording = false; if (changed) *changed = false; if (sampleInTL) *sampleInTL = g_hostSample;
  if (cycling) *cycling = false; if (cs) *cs = 0; if (ce) *ce = 0; return noErr; }
static void attachHost (AU& a)
{
    if (std::getenv ("CPB_NOHOST")) return;
    HostCallbackInfo h {}; h.hostUserData = nullptr; h.beatAndTempoProc = cbBeat; h.musicalTimeLocationProc = cbMusical; h.transportStateProc2 = cbTransport2;
    AudioUnitSetProperty (a.au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &h, sizeof h);
}

// stereo render; appends to out (L,R interleaved) and the per-block wall time to us
static void renderLR (AU& a, int nblk, std::vector<float>* out, std::vector<double>* us)
{
    std::vector<float> bl ((size_t) BLK), br ((size_t) BLK);
    AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
    AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = a.clock_;
    for (int b = 0; b < nblk; ++b)
    {
        abl->mBuffers[0] = { 1, (UInt32) (BLK * 4), bl.data() }; abl->mBuffers[1] = { 1, (UInt32) (BLK * 4), br.data() };
        AudioUnitRenderActionFlags fl = 0; g_hostSample = ts.mSampleTime;
        const auto t0 = std::chrono::steady_clock::now();
        if (AudioUnitRender (a.au, &fl, &ts, 0, BLK, abl) != noErr) break;
        if (us) us->push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count());
        ts.mSampleTime += BLK; a.clock_ += BLK;
        if (out) for (int i = 0; i < BLK; ++i) { out->push_back (bl[(size_t) i]); out->push_back (br[(size_t) i]); }
    }
    free (abl);
}
static const double kBudgetUs = 1e6 * (double) BLK / SR;
static double pct (const std::vector<double>& us) { double s = 0; for (double u : us) s += u; return us.empty() ? 0 : 100.0 * s / ((double) us.size() * kBudgetUs); }
static double worstPct (std::vector<double> us) { if (us.empty()) return 0; return 100.0 * *std::max_element (us.begin(), us.end()) / kBudgetUs; }
static double med (std::vector<double> v) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }
static const int CHORD[4] = { 48, 55, 60, 64 };

// A DAW runs the audio and message threads side by side. Pooled sends are published by the audio thread (the fb414
// route push) and BUILT by the processor timer (fb631 buildPoolFilters), so a send lit one timer tick after the
// first block. A harness that pumps only BEFORE rendering never let that tick happen, and an osc routed into a
// rack device rendered silent (found fb636: 10 of Max's 52 presets). fb636 bugA closed that window in the plugin —
// setStateInformation/prepareToPlay/the UI/learned CCs now build a lit send's pair before the audio can ask
// (Tests/pool_send_first_block_au.cpp) — but pooled reverb 2-6, granular and tape ENGINES are still timer-built,
// and the goldens must stay comparable with the reference, so the warm-up stays: a few blocks, a pump, again.
static bool loadInto (AU& a, const std::string& xml)
{
    attachHost (a); if (! a.writeXml (xml)) return false; a.pump (1.0);
    for (int k = 0; k < 3; ++k) { renderLR (a, 4, nullptr, nullptr); a.pump (0.25); }
    return true;
}

static int golden (const std::string& outDir, const std::vector<std::string>& files)
{
    mkdir (outDir.c_str(), 0755);
    for (const auto& f : files)
    {
        const std::string xml = xmlOrTerrain (f), name = baseName (f);
        if (xml.empty()) { std::printf ("  %-26s UNREADABLE\n", name.c_str()); continue; }
        AU a; if (! a.open()) return 2;
        if (! loadInto (a, xml)) { std::printf ("  %-26s load failed\n", name.c_str()); a.close(); continue; }
        std::vector<float> out; std::vector<double> us;
        for (int n : CHORD) a.midi (0x90, (UInt32) n, 100);
        renderLR (a, 141, &out, &us);                        // 1.5 s held
        for (int n : CHORD) a.midi (0x80, (UInt32) n, 0);
        renderLR (a, 188, &out, &us);                        // 2.0 s release + tail
        a.midi (0x90, 72, 90); renderLR (a, 47, &out, &us);  // a second, single note
        a.midi (0x80, 72, 0);  renderLR (a, 94, &out, &us);
        a.close();
        double pk = 0, e = 0; for (float v : out) { pk = std::max (pk, (double) std::fabs (v)); e += (double) v * v; }
        std::ofstream o (outDir + "/" + name + ".f32", std::ios::binary); o.write ((const char*) out.data(), (std::streamsize) (out.size() * 4));
        std::printf ("  %-26s peak %6.3f  rms %7.2f dBFS  cpu %5.1f%%  worst block %5.1f%%\n", name.c_str(), pk,
                     10 * std::log10 (e / std::max<size_t> (1, out.size()) + 1e-30), pct (us), worstPct (us));
    }
    return 0;
}

static int compare (const std::string& A, const std::string& B, const std::vector<std::string>& names)
{
    int differ = 0;
    for (const auto& n : names)
    {
        const std::string a = slurpBin (A + "/" + n + ".f32"), b = slurpBin (B + "/" + n + ".f32");
        if (a.empty() || b.empty()) { std::printf ("  %-26s MISSING (%zu / %zu bytes)\n", n.c_str(), a.size(), b.size()); ++differ; continue; }
        const size_t N = std::min (a.size(), b.size()) / 4; const float* x = (const float*) a.data(); const float* y = (const float*) b.data();
        size_t same = 0, first = (size_t) -1; double mx = 0, d = 0, r = 0;
        for (size_t i = 0; i < N; ++i)
        {
            if (std::memcmp (x + i, y + i, 4) == 0) ++same; else if (first == (size_t) -1) first = i;
            const double e = (double) x[i] - y[i]; mx = std::max (mx, std::fabs (e)); d += e * e; r += (double) x[i] * x[i];
        }
        const bool ident = same == N && a.size() == b.size();
        if (! ident) ++differ;
        std::printf ("  %-26s %s  %zu/%zu identical  max|d| %.3e  null %s\n", n.c_str(), ident ? "BIT-IDENTICAL" : "DIFFERS      ", same, N, mx,
                     ident ? "-inf" : (r > 0 ? (std::to_string (10 * std::log10 (d / r + 1e-300)) + " dB (first @" + std::to_string (first / 2) + ")").c_str() : "n/a"));
    }
    std::printf ("\n  %d of %zu differ\n", differ, names.size());
    return differ ? 1 : 0;
}

struct Cpu { double held, heldWorst, gated, gatedWorst; };
static Cpu measure (AU& a, int reps)
{
    std::vector<double> H, G; double hw = 0, gw = 0;
    for (int r = 0; r < reps; ++r)
    {
        std::vector<double> us;
        for (int n : CHORD) a.midi (0x90, (UInt32) n, 100);
        renderLR (a, 47, nullptr, nullptr);                  // attack settles
        renderLR (a, 188, nullptr, &us); H.push_back (pct (us)); hw = std::max (hw, worstPct (us));
        for (int n : CHORD) a.midi (0x80, (UInt32) n, 0);
        renderLR (a, 470, nullptr, nullptr);                 // let every tail finish
        us.clear();
        for (int s = 0; s < 6; ++s)                          // a chord a second, keys up at 0.6 s: the releases overlap
        {
            for (int n : CHORD) a.midi (0x90, (UInt32) n, 100);
            renderLR (a, 56, nullptr, &us);
            for (int n : CHORD) a.midi (0x80, (UInt32) n, 0);
            renderLR (a, 38, nullptr, &us);
        }
        G.push_back (pct (us)); gw = std::max (gw, worstPct (us));
        renderLR (a, 470, nullptr, nullptr);
    }
    return { med (H), hw, med (G), gw };
}

static int cpu (int reps, const std::vector<std::string>& files)
{
    std::printf ("  %-26s %8s %8s %8s %8s\n", "preset", "held%", "worst%", "gated%", "worst%");
    for (const auto& f : files)
    {
        const std::string xml = xmlOrTerrain (f), name = baseName (f);
        AU a; if (! a.open()) return 2;
        if (xml.empty() || ! loadInto (a, xml)) { std::printf ("  %-26s load failed\n", name.c_str()); a.close(); continue; }
        const Cpu c = measure (a, reps); a.close();
        std::printf ("  %-26s %8.2f %8.1f %8.2f %8.1f\n", name.c_str(), c.held, c.heldWorst, c.gated, c.gatedWorst);
        std::fflush (stdout);
    }
    return 0;
}

static int synth (int reps)
{
    AU v; if (! v.open()) return 2; std::string why; const std::string virgin = v.readXml (why);
    // idle, nothing played
    { std::vector<double> us; v.pump (1.0); renderLR (v, 470, nullptr, &us); std::printf ("  init, idle (no notes)          %6.2f%%\n", pct (us)); }
    v.close();
    const char* O = "ABCD";
    for (int k = 0; k <= 4; ++k)
    {
        std::string xml = virgin;
        for (int o = 0; o < 4; ++o) setParam (xml, std::string ("SYN_OSC_") + O[o] + "_ENABLE", o < k ? 1.0 : 0.0);
        AU a; if (! a.open() || ! loadInto (a, xml)) return 2;
        const Cpu c = measure (a, reps); a.close();
        std::printf ("  init, %d oscillator%s on          held %6.2f%% (worst %5.1f)   gated %6.2f%% (worst %5.1f)\n", k, k == 1 ? " " : "s", c.held, c.heldWorst, c.gated, c.gatedWorst);
        std::fflush (stdout);
    }
    return 0;
}


// spikes <reps> <file|init:N>  — chord every 1.0 s, keys up at 0.6 s; every block timed with its event context.
//   Run with TERRAIN_PROFILE=1 TERRAIN_PROFILE_US=<us> to get the plugin's own section split for slow blocks
//   (it prints to stderr; this prints "[evt]" markers there too so the two interleave).
static int spikes (int reps, const std::string& what)
{
    AU a; if (! a.open()) return 2;
    std::string why; std::string xml;
    if (what.rfind ("init:", 0) == 0)
    { xml = a.readXml (why); const int k = std::atoi (what.c_str() + 5); const char* O = "ABCD";
      for (int o = 0; o < 4; ++o) setParam (xml, std::string ("SYN_OSC_") + O[o] + "_ENABLE", o < k ? 1.0 : 0.0); }
    else xml = xmlOrTerrain (what);
    if (xml.empty() || ! loadInto (a, xml)) { std::printf ("load failed\n"); return 2; }
    struct B { double us; int blk; int sinceOn; int sinceOff; };
    std::vector<B> all; int blk = 0, lastOn = -1000, lastOff = -1000;
    for (int r = 0; r < reps; ++r)
        for (int ph = 0; ph < 94; ++ph, ++blk)
        {
            if (ph == 0)  { for (int n : CHORD) a.midi (0x90, (UInt32) n, 100); lastOn = blk; std::fprintf (stderr, "[evt] blk %d NOTE-ON\n", blk); }
            if (ph == 56) { for (int n : CHORD) a.midi (0x80, (UInt32) n, 0); lastOff = blk; std::fprintf (stderr, "[evt] blk %d NOTE-OFF\n", blk); }
            std::vector<double> us; renderLR (a, 1, nullptr, &us);
            all.push_back ({ us.empty() ? 0 : us[0], blk, blk - lastOn, blk - lastOff });
        }
    a.close();
    std::vector<double> v; for (auto& b : all) v.push_back (b.us);
    const double m = med (v);
    std::sort (all.begin(), all.end(), [] (const B& x, const B& y) { return x.us > y.us; });
    std::printf ("  %s: %zu blocks, median %.0f us (%.1f%%), mean %.1f%%\n", what.c_str(), v.size(), m, 100 * m / kBudgetUs, pct (v));
    for (size_t i = 0; i < std::min<size_t> (12, all.size()); ++i)
        std::printf ("    #%zu  %6.0f us (%5.1f%%)  blk %4d  since note-on %3d  since note-off %4d\n", i + 1, all[i].us, 100 * all[i].us / kBudgetUs, all[i].blk, all[i].sinceOn, all[i].sinceOff);
    int onHot = 0, offHot = 0, other = 0;
    for (auto& b : all) if (b.us > 3 * m) { if (b.sinceOn == 0) ++onHot; else if (b.sinceOff == 0) ++offHot; else ++other; }
    std::printf ("  blocks over 3x median: %d in the note-on block, %d in the note-off block, %d elsewhere\n", onHot, offHot, other);
    return 0;
}

// mem [a.terrain ...] — the instance's memory as macOS counts it (phys_footprint, what Activity Monitor's "Memory" shows):
//   the harness alone, one instance opened + initialised (ctor + prepareToPlay), after each preset load and a chord,
//   a second instance, and after both close.
#include <mach/task_info.h>
static double footprintMB() { task_vm_info_data_t v; mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info (mach_task_self(), TASK_VM_INFO, (task_info_t) &v, &n) != KERN_SUCCESS) return -1; return (double) v.phys_footprint / 1048576.0; }
static int mem (const std::vector<std::string>& files)
{
    const double m0 = footprintMB(); std::printf ("  harness alone                    %8.1f MB\n", m0);
    AU a; if (! a.open()) return 2; attachHost (a); a.pump (1.0);
    const double m1 = footprintMB(); std::printf ("  + one instance (init patch)      %8.1f MB   (instance %+.1f)\n", m1, m1 - m0);
    for (int n : CHORD) a.midi (0x90, (UInt32) n, 100); renderLR (a, 94, nullptr, nullptr); for (int n : CHORD) a.midi (0x80, (UInt32) n, 0); renderLR (a, 94, nullptr, nullptr);
    std::printf ("  + a chord on the init patch      %8.1f MB\n", footprintMB());
    for (const auto& f : files)
    {
        const std::string xml = xmlOrTerrain (f); if (xml.empty() || ! loadInto (a, xml)) continue;
        for (int n : CHORD) a.midi (0x90, (UInt32) n, 100); renderLR (a, 94, nullptr, nullptr); for (int n : CHORD) a.midi (0x80, (UInt32) n, 0); renderLR (a, 188, nullptr, nullptr);
        std::printf ("  after %-26s %8.1f MB\n", baseName (f).c_str(), footprintMB());
    }
    AU b; if (b.open()) { b.pump (1.0); std::printf ("  + a second instance              %8.1f MB\n", footprintMB()); b.close(); }
    a.close(); a.pump (0.5); std::printf ("  both closed                      %8.1f MB\n", footprintMB());
    return 0;
}

// live <seconds> <file|init:N> — THE DAW'S OWN THREAD. Terrain renders inside the default output device's IOProc (the
//   real CoreAudio I/O thread, its workgroup and its deadline, at real-time pace — what Ableton's meter measures), the
//   device is fed zeros (nothing reaches the speakers), and every callback is timed. A chord every ~1 s, keys up at 0.6 s.
#include <CoreAudio/CoreAudio.h>
struct LiveState { AU* t = nullptr; std::vector<double> us; std::vector<int> onBlk, offBlk; int cb = 0; int maxCb = 0; double st = 0; std::vector<float> l, r; };
static OSStatus liveRender (void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n, AudioBufferList* io)
{
    auto* L = (LiveState*) ref;
    for (UInt32 b = 0; b < io->mNumberBuffers; ++b) std::memset (io->mBuffers[b].mData, 0, io->mBuffers[b].mDataByteSize);
    if (L->cb >= L->maxCb || n > (UInt32) BLK) { ++L->cb; return noErr; }
    const int ph = L->cb % 94;
    if (ph == 0)  for (int k : CHORD) MusicDeviceMIDIEvent (L->t->au, 0x90, (UInt32) k, 100, 0);
    if (ph == 56) for (int k : CHORD) MusicDeviceMIDIEvent (L->t->au, 0x80, (UInt32) k, 0, 0);
    AudioBufferList* abl = (AudioBufferList*) alloca (sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
    abl->mBuffers[0] = { 1, (UInt32) (n * 4), L->l.data() }; abl->mBuffers[1] = { 1, (UInt32) (n * 4), L->r.data() };
    AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = L->st; g_hostSample = L->st;
    AudioUnitRenderActionFlags fl = 0;
    const auto t0 = std::chrono::steady_clock::now();
    AudioUnitRender (L->t->au, &fl, &ts, 0, n, abl);
    L->us[(size_t) L->cb] = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() * ((double) BLK / n);
    L->st += n; ++L->cb;
    return noErr;
}
static int live (double seconds, const std::string& what)
{
    AU a; if (! a.open()) return 2;
    std::string why, xml;
    if (what.rfind ("init:", 0) == 0)
    { xml = a.readXml (why); const int k = std::atoi (what.c_str() + 5); const char* O = "ABCD";
      for (int o = 0; o < 4; ++o) setParam (xml, std::string ("SYN_OSC_") + O[o] + "_ENABLE", o < k ? 1.0 : 0.0); }
    else xml = xmlOrTerrain (what);
    if (xml.empty() || ! loadInto (a, xml)) { std::printf ("load failed\n"); return 2; }
    AudioComponentDescription od {}; od.componentType = kAudioUnitType_Output; od.componentSubType = kAudioUnitSubType_DefaultOutput; od.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioUnit out = nullptr; AudioComponentInstanceNew (AudioComponentFindNext (nullptr, &od), &out);
    AudioDeviceID dev = 0; UInt32 sz = sizeof dev; AudioUnitGetProperty (out, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, &sz);
    Float64 devSr = 0; sz = sizeof devSr; AudioObjectPropertyAddress pa { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectGetPropertyData (dev, &pa, 0, nullptr, &sz, &devSr);
    UInt32 frames = BLK; AudioObjectPropertyAddress pb { kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectSetPropertyData (dev, &pb, 0, nullptr, sizeof frames, &frames);
    AudioStreamBasicDescription f {}; f.mSampleRate = SR; f.mFormatID = kAudioFormatLinearPCM; f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    AudioUnitSetProperty (out, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    LiveState L; L.t = &a; L.maxCb = (int) (seconds * SR / BLK) + 2; L.us.assign ((size_t) L.maxCb + 8, 0.0); L.l.resize (4096); L.r.resize (4096);
    AURenderCallbackStruct cbs { liveRender, &L };
    AudioUnitSetProperty (out, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cbs, sizeof cbs);
    AudioUnitInitialize (out);
    std::printf ("  device %.0f Hz, buffer %u frames (Terrain at %.0f Hz; %s)\n", devSr, frames, SR, devSr == SR ? "no conversion" : "AUHAL converts");
    AudioOutputUnitStart (out);
    while (L.cb < L.maxCb) a.pump (0.1);
    AudioOutputUnitStop (out); AudioUnitUninitialize (out); AudioComponentInstanceDispose (out);
    for (int k : CHORD) a.midi (0x80, (UInt32) k, 0);
    a.close();
    std::vector<double> v (L.us.begin() + 94, L.us.begin() + L.maxCb - 1);   // the first second is warm-up
    std::vector<double> s = v; std::sort (s.begin(), s.end());
    auto q = [&] (double p) { return s[(size_t) std::min<double> (s.size() - 1, p * s.size())]; };
    std::printf ("  %s: %zu callbacks · mean %.1f%% · median %.1f%% · p99 %.1f%% · p99.9 %.1f%% · worst %.1f%% of the buffer\n", baseName (what).c_str(), v.size(),
                 pct (v), 100 * q (0.5) / kBudgetUs, 100 * q (0.99) / kBudgetUs, 100 * q (0.999) / kBudgetUs, 100 * s.back() / kBudgetUs);
    int worst = 0; for (size_t i = 1; i < v.size(); ++i) if (v[i] > v[(size_t) worst]) worst = (int) i;
    const int blk = worst + 94; std::printf ("     worst at callback %d: %d after the last note-on, %d after the last note-off\n", blk, blk % 94, (blk % 94 + 94 - 56) % 94);
    return 0;
}

// CPB_PROF=<file> with `hold`: a DIY sampling profiler. A second thread interrupts the render thread every
// ~250 µs (thread_suspend + thread_get_state), records its exact PC, and writes the PCs and the Terrain image's
// load address at the end — `atos -i` against a dSYM then names every inlined frame (sample(1) cannot).
#include <mach-o/dyld.h>
#include <atomic>
#include <thread>
static uintptr_t terrainImageBase()
{
    for (uint32_t i = 0; i < _dyld_image_count(); ++i)
    { const char* n = _dyld_get_image_name (i); if (n && std::strstr (n, "Terrain.component/Contents/MacOS/Terrain")) return (uintptr_t) _dyld_get_image_header (i); }
    return 0;
}
struct PcSampler
{
    std::atomic<bool> stop { false }; std::thread th; std::vector<uint64_t> pcs; mach_port_t target;
    void start() { target = mach_thread_self(); pcs.reserve (400000);
        th = std::thread ([this] { while (! stop.load()) {
            if (thread_suspend (target) == KERN_SUCCESS) {
                arm_thread_state64_t st; mach_msg_type_number_t n = ARM_THREAD_STATE64_COUNT;
                if (thread_get_state (target, ARM_THREAD_STATE64, (thread_state_t) &st, &n) == KERN_SUCCESS) pcs.push_back (arm_thread_state64_get_pc (st));
                thread_resume (target); }
            std::this_thread::sleep_for (std::chrono::microseconds (250)); } }); }
    void finish (const char* path) { stop = true; th.join(); std::FILE* f = std::fopen (path, "w");
        std::fprintf (f, "base 0x%llx\n", (unsigned long long) terrainImageBase()); for (auto pc : pcs) std::fprintf (f, "0x%llx\n", (unsigned long long) pc); std::fclose (f);
        std::printf ("  profiler: %zu samples -> %s\n", pcs.size(), path); }
};

// hold <seconds> <file|init:N> — a 4-note chord held and rendered flat out for <seconds> (a steady target for `sample`).
static int hold (double seconds, const std::string& what)
{
    AU a; if (! a.open()) return 2; std::string why, xml;
    if (what.rfind ("init:", 0) == 0)
    { xml = a.readXml (why); const int k = std::atoi (what.c_str() + 5); const char* O = "ABCD";
      for (int o = 0; o < 4; ++o) setParam (xml, std::string ("SYN_OSC_") + O[o] + "_ENABLE", o < k ? 1.0 : 0.0); }
    else xml = xmlOrTerrain (what);
    if (xml.empty() || ! loadInto (a, xml)) { std::printf ("load failed\n"); return 2; }
    for (int n : CHORD) a.midi (0x90, (UInt32) n, 100);
    renderLR (a, 94, nullptr, nullptr);
    std::vector<double> us; const int nb = (int) (seconds * SR / BLK);
    PcSampler ps; const char* pf = std::getenv ("CPB_PROF"); if (pf) ps.start();
    for (int b = 0; b < nb; b += 94) renderLR (a, std::min (94, nb - b), nullptr, &us);
    if (pf) ps.finish (pf);
    std::printf ("  %s held %.0f s: %.2f%%\n", baseName (what).c_str(), seconds, pct (us)); a.close(); return 0;
}

// goldauto <outdir> <files...> — the golden performance PLUS a hand on the controls: after the chord starts, a set of
//   parameters is swept through the AU's own parameter door every block (smoothers moving, setters re-firing, cutoff
//   redesigns, level/pan glides), then parked, then snapped back — the paths a static render never exercises.
//   The same deterministic render twice is the contract; any change must stay bit-identical here too.
static const char* kAutoParams[] = { "Synth OSC A Level", "Synth OSC A Pan", "Synth Filter 1 Cutoff", "Synth Filter 1 Resonance",
                                     "Synth OSC A Warp Amount", "Synth OSC A WT Position", "Output Gain", "Synth OSC B Level",
                                     "Synth Amp Sustain", "Synth OSC A Fold", "Synth Noise Level", "Synth OSC A Unison Detune" };
static int goldauto (const std::string& outDir, const std::vector<std::string>& files)
{
    mkdir (outDir.c_str(), 0755);
    for (const auto& f : files)
    {
        const std::string xml = xmlOrTerrain (f), name = baseName (f);
        AU a; if (! a.open()) return 2;
        if (xml.empty() || ! loadInto (a, xml)) { a.close(); continue; }
        std::vector<std::pair<AudioUnitParameterID, AudioUnitParameterInfo>> ps;
        for (const char* n : kAutoParams) { auto it = a.byName.find (n); if (it == a.byName.end()) continue;
            AudioUnitParameterInfo pi {}; UInt32 sz = sizeof pi; AudioUnitGetProperty (a.au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, it->second, &pi, &sz); ps.push_back ({ it->second, pi }); }
        std::vector<AudioUnitParameterValue> orig; for (auto& p : ps) { AudioUnitParameterValue v = 0; AudioUnitGetParameter (a.au, p.first, kAudioUnitScope_Global, 0, &v); orig.push_back (v); }
        std::vector<float> out;
        for (int n : CHORD) a.midi (0x90, (UInt32) n, 100);
        for (int b = 0; b < 188; ++b)
        {
            for (size_t k = 0; k < ps.size(); ++k)
            {
                const auto& pi = ps[k].second; const double ph = (b < 120) ? std::sin (0.07 * b + 0.9 * (double) k) : (b < 150 ? 0.3 : -1.0);
                const double lo = pi.minValue, hi = pi.maxValue, mid = orig[k];
                const double v = (b < 150) ? std::clamp (mid + ph * 0.35 * (hi - lo), lo, hi) : mid;   // sweep, park, snap back
                AudioUnitSetParameter (a.au, ps[k].first, kAudioUnitScope_Global, 0, (AudioUnitParameterValue) v, 0);
            }
            renderLR (a, 1, &out, nullptr);
            // the message thread gets its ticks, as in a DAW. ⚠️ fb636: a 20 ms pump sometimes caught the processor
            // timer's tick and sometimes did not, so a pooled pair was built one block earlier or later from run to
            // run — the harness itself was not repeatable (Don't Go). 300 ms always holds several ticks.
            if (b % 47 == 46) a.pump (0.30);
        }
        for (int n : CHORD) a.midi (0x80, (UInt32) n, 0);
        renderLR (a, 141, &out, nullptr);
        a.close();
        std::ofstream o (outDir + "/" + name + ".f32", std::ios::binary); o.write ((const char*) out.data(), (std::streamsize) (out.size() * 4));
        std::printf ("  %-26s automated %zu params, %zu samples\n", name.c_str(), ps.size(), out.size() / 2);
    }
    return 0;
}

int main (int argc, char** argv)
{
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
    makeRealtime();
    const std::string mode = argc > 1 ? argv[1] : "";
    std::vector<std::string> rest; for (int i = 2; i < argc; ++i) rest.push_back (argv[i]);
    if (mode == "golden" && rest.size() >= 2) return golden (rest[0], std::vector<std::string> (rest.begin() + 1, rest.end()));
    if (mode == "compare" && rest.size() >= 3) return compare (rest[0], rest[1], std::vector<std::string> (rest.begin() + 2, rest.end()));
    if (mode == "cpu" && rest.size() >= 2) return cpu (std::atoi (rest[0].c_str()), std::vector<std::string> (rest.begin() + 1, rest.end()));
    if (mode == "spikes" && rest.size() >= 2) return spikes (std::atoi (rest[0].c_str()), rest[1]);
    if (mode == "dump" && rest.size() >= 1) { AU a; if (! a.open()) return 2; attachHost (a); a.pump (1.0); std::string why; const std::string x = a.readXml (why); a.close(); std::ofstream o (rest[0]); o << x; std::printf ("dumped %zu bytes\n", x.size()); return x.empty() ? 2 : 0; }
    if (mode == "mem") return mem (rest);
    if (mode == "goldauto" && rest.size() >= 2) return goldauto (rest[0], std::vector<std::string> (rest.begin() + 1, rest.end()));
    if (mode == "hold" && rest.size() >= 2) return hold (std::atof (rest[0].c_str()), rest[1]);
    if (mode == "live" && rest.size() >= 2) return live (std::atof (rest[0].c_str()), rest[1]);
    if (mode == "synth") return synth (rest.empty() ? 3 : std::atoi (rest[0].c_str()));
    std::printf ("usage: golden <outdir> <files..> | compare <dirA> <dirB> <names..> | cpu <reps> <files..> | synth [reps]\n"); return 2;
}
