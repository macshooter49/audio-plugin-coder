// ══════════════════════════════════════════════════════════════════════════════════════════════
//  morph_free_au.cpp — fb636 F4 ON THE REAL PLUGIN: A SPECTRAL MORPH LEFT ALONE OR SWITCHED OFF GIVES
//  ITS BUFFERS BACK, AND 32-SAMPLE BLOCKS ON THEIR OWN THREAD NEVER TOUCH ONE AFTER IT HAS GONE.
//
//    c++ -std=c++17 -O2 -I Tests Tests/morph_free_au.cpp -framework AudioToolbox -framework CoreFoundation \
//        -o /tmp/mfau && TERRAIN_AU_BUNDLE=<Terrain.component> /tmp/mfau [seconds, default 20]
//
//  morph_pages_cert.cpp runs the sliced lifetime; this runs the SHIPPING BINARY the way a DAW does: the
//  render on its own thread (32-sample blocks, flat out, a chord held throughout) while the main thread is
//  the message thread — it pumps the run loop, so the processor's own timer rebuilds and frees, and it
//  automates Osc A's Spectral amount through the host (AudioUnitSetParameter) in cycles of
//      MODULATED 0.8 s  a new amount every 40 ms: ping-pong rebuilds, both buffers in play
//      STATIC    1.2 s  one amount: the buffer the last rebuild retired sits out M2's 500 ms hold, then goes
//      OFF       1.2 s  amount 0 — the None branch: the live buffer is retired and goes too
//  and reads "Memory Tag 240" (the page-backed tables since F1; nothing else in the plugin carries the tag)
//  at the end of each phase. THE BLOB IS SYNTHETIC, IN MEMORY ONLY: the virgin state with Osc A's Spectral
//  type 1, amount 0.5, no cut. No preset is ever read or written.
//
//  BARS
//   [0] the harness reaches the morph: Osc A's Spectral amount is a host parameter, and switching the morph
//       on grows tag-240 by at least one 7.63 MiB buffer
//   [1] ALIVE: the render thread ran its 32-sample blocks for the whole run — 0 render errors, 0 non-finite
//       samples, the chord audible — with no crash (a page unmapped under a block is a SIGSEGV here)
//   [2] THE FREE, ON THE SHIPPING BINARY: in EVERY cycle tag-240 at the end of STATIC is one buffer below the
//       end of MODULATED, and at the end of OFF two buffers below it — and the next MODULATED phase builds
//       them back (so every cycle rebuilt INTO freed buffers while the blocks ran)
//  CONTROL: TERRAIN_AU_BUNDLE=<a build before F4> — [0] and [1] green, [2] RED (the buffers never leave).
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include "au_state_blob.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <thread>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

static int g_pass = 0, g_fail = 0;
static void bar (bool ok, const char* id, const char* fmt, ...)
{
    (ok ? g_pass : g_fail)++;
    char b[700]; va_list a; va_start (a, fmt); std::vsnprintf (b, sizeof b, fmt, a); va_end (a);
    std::printf ("  %s  %-3s %s\n", ok ? "PASS" : "FAIL", id, b);
    std::fflush (stdout);
}
// what vmmap -summary shows as "Memory Tag 240": the bytes of every region carrying that tag
static double tagMB (unsigned tag = 240)
{
    mach_vm_address_t addr = 0; natural_t depth = 0; double total = 0.0;
    for (;;)
    {
        mach_vm_size_t size = 0;
        vm_region_submap_info_data_64_t info {}; mach_msg_type_number_t cnt = VM_REGION_SUBMAP_INFO_COUNT_64;
        if (mach_vm_region_recurse (mach_task_self(), &addr, &size, &depth, (vm_region_recurse_info_t) &info, &cnt) != KERN_SUCCESS) break;
        if (info.is_submap) { ++depth; continue; }
        if (info.user_tag == tag) total += (double) size;
        addr += size;
    }
    return total / 1048576.0;
}
static void onFault (int)
{
    const char m[] = "  FAIL  [1] the render thread touched an unmapped page (SIGSEGV/SIGBUS): a morph buffer was freed under a block\n";
    (void) ::write (1, m, sizeof m - 1);
    _exit (1);
}

int main (int argc, char** argv)
{
    const double secs = argc > 1 ? std::atof (argv[1]) : 20.0;
    std::signal (SIGSEGV, onFault);
    std::signal (SIGBUS,  onFault);
    std::printf ("══ morph_free_au — fb636 F4 on the real plugin (%s) ══\n", std::getenv ("TERRAIN_AU_BUNDLE") ? std::getenv ("TERRAIN_AU_BUNDLE") : "installed AU");

    AU a; if (! a.open()) return 2;
    std::string why, xml = a.readXml (why);
    const bool edited = ! xml.empty() && setParam (xml, "SYN_OSC_A_SPECTRAL_TYPE", 1.0) && setParam (xml, "SYN_OSC_A_SPECTRAL_AMT", 0.0)
                        && setParam (xml, "SYN_OSC_A_SPECTRAL_LO", 0.0) && setParam (xml, "SYN_OSC_A_SPECTRAL_HI", 1.0);
    if (! edited || ! a.writeXml (xml)) { std::printf ("  !! could not build the blob (%s)\n", why.c_str()); a.close(); return 2; }
    a.pump (1.0);
    AudioUnitParameterID amtId = 0; std::string amtName; AudioUnitParameterInfo amtInfo {};
    for (const auto& kv : a.byName)
        if (kv.first.find ("OSC A") != std::string::npos && kv.first.find ("Spectral") != std::string::npos
            && (kv.first.find ("Amount") != std::string::npos || kv.first.find ("Amt") != std::string::npos))
        { amtId = kv.second; amtName = kv.first; }
    if (amtName.empty()) { std::printf ("  !! no host parameter for Osc A's Spectral amount\n"); a.close(); return 2; }
    UInt32 isz = sizeof amtInfo;
    AudioUnitGetProperty (a.au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, amtId, &amtInfo, &isz);
    auto setAmt = [&] (double frac) { AudioUnitSetParameter (a.au, amtId, kAudioUnitScope_Global, 0,
                                          (AudioUnitParameterValue) (amtInfo.minValue + frac * (amtInfo.maxValue - amtInfo.minValue)), 0); };

    // the render thread: 32-sample blocks, flat out, a chord held for the whole run
    for (int n : { 48, 55, 60, 64 }) a.midi (0x90, (UInt32) n, 100);
    std::atomic<bool> stop { false };
    std::atomic<long> blocks { 0 }, errs { 0 }, nonFinite { 0 };
    std::atomic<double> energy { 0.0 };
    std::thread render ([&]
    {
        constexpr int N = 32;
        std::vector<float> l ((size_t) N), r ((size_t) N);
        AudioBufferList* abl = (AudioBufferList*) calloc (1, sizeof (AudioBufferList) + sizeof (AudioBuffer)); abl->mNumberBuffers = 2;
        AudioTimeStamp ts {}; ts.mFlags = kAudioTimeStampSampleTimeValid; ts.mSampleTime = a.clock_;
        double e = 0.0; long n = 0;
        while (! stop.load (std::memory_order_relaxed))
        {
            abl->mBuffers[0] = { 1, (UInt32) (N * 4), l.data() }; abl->mBuffers[1] = { 1, (UInt32) (N * 4), r.data() };
            AudioUnitRenderActionFlags fl = 0;
            if (AudioUnitRender (a.au, &fl, &ts, 0, N, abl) != noErr) errs.fetch_add (1);
            ts.mSampleTime += N;
            for (int i = 0; i < N; ++i)
            {
                if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) nonFinite.fetch_add (1);
                else e += 0.5 * ((double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i]);
            }
            ++n; blocks.store (n, std::memory_order_relaxed);
        }
        energy.store (e / std::max (1.0, (double) n * N));
        free (abl);
    });

    const double t0 = tagMB();
    setAmt (0.5); a.pump (1.0);
    const double tOn = tagMB() - t0;
    bar (tOn > 7.5, "[0]", "'%s' (%.2f..%.2f) reaches the morph: switching it on grew tag-240 by %+.2f MiB (a buffer is 7.63 MiB)",
         amtName.c_str(), amtInfo.minValue, amtInfo.maxValue, tOn);

    unsigned rs = 2463534242u;
    auto rnd = [&] { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (double) (rs % 100000u) / 100000.0; };
    int cycles = 0, good = 0; double worstStatic = 1e9, worstOff = 1e9, worstBack = 1e9;
    double prevOff = -1.0;
    const auto tEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds ((long) (secs * 1000.0));
    while (std::chrono::steady_clock::now() < tEnd)
    {
        for (int k = 0; k < 20; ++k) { setAmt (0.2 + 0.7 * rnd()); a.pump (0.04); }   // MODULATED 0.8 s
        const double tMod = tagMB();
        setAmt (0.3 + 0.6 * rnd()); a.pump (1.2);                                      // STATIC 1.2 s
        const double tStatic = tagMB();
        setAmt (0.0); a.pump (1.2);                                                    // OFF 1.2 s
        const double tOff = tagMB();
        const double dStatic = tMod - tStatic, dOff = tMod - tOff, dBack = prevOff < 0.0 ? 15.25 : tMod - prevOff;
        const bool ok = std::fabs (dStatic - 7.625) < 0.2 && std::fabs (dOff - 15.25) < 0.2 && std::fabs (dBack - 15.25) < 0.2;
        ++cycles; if (ok) ++good;
        worstStatic = std::min (worstStatic, dStatic); worstOff = std::min (worstOff, dOff); worstBack = std::min (worstBack, dBack);
        if (! ok) std::printf ("      cycle %d: modulated->static %+.2f, ->off %+.2f, previous off->modulated %+.2f MiB\n", cycles, -dStatic, -dOff, dBack);
        prevOff = tOff;
    }
    stop.store (true);
    render.join();
    for (int n : { 48, 55, 60, 64 }) a.midi (0x80, (UInt32) n, 0);
    const double rmsDb = 10.0 * std::log10 (energy.load() + 1e-30);
    const double audioS = (double) blocks.load() * 32.0 / SR;
    bar (errs.load() == 0 && nonFinite.load() == 0 && blocks.load() > (long) (secs * SR / 32.0 * 0.5) && rmsDb > -40.0,
         "[1]", "%ld blocks of 32 (%.1f s of audio in %.1f s), render errors %ld, non-finite samples %ld, rms %.1f dBFS",
         blocks.load(), audioS, secs, errs.load(), nonFinite.load(), rmsDb);
    bar (cycles >= 3 && good == cycles,
         "[2]", "%d of %d cycles gave the buffers back and built them again: worst modulated->static -%.2f MiB (one buffer 7.63), "
                "modulated->off -%.2f MiB (two), off->next modulated +%.2f MiB",
         good, cycles, worstStatic, worstOff, worstBack);
    a.close();
    std::printf ("  %d pass / %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
