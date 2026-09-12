// ══════════════════════════════════════════════════════════════════════════════════════════════
//  morph_pages_cert.cpp — fb636 F4. THE MORPH HALF OF M2: A SPECTRAL-MORPH BUFFER NOTHING CAN REACH
//  IS GIVEN BACK, AND NOTHING READS ONE AFTER IT HAS GONE. (macOS only: tag-240 and the footprint
//  are Mach reads.)
//
//    python3 Tests/extract_morph_life.py Source/PluginProcessor.h Source/PluginProcessor.cpp /tmp/ml.h
//    clang++ -O2 -std=c++17 -pthread -I Tests/shim -I Source -DMORPH_LIFE_HEADER='"/tmp/ml.h"' \
//            Tests/morph_pages_cert.cpp -o /tmp/morph_pages_cert -framework Accelerate
//    /tmp/morph_pages_cert [stress seconds, default 6]      (bash Tests/morph_pages_gates.sh runs every control)
//
//  Once a morph had run, both of an osc's MorphSlot buffers (7.63 MiB each, page-backed since F1) stayed
//  resident for the life of the instance, Spectral off or not: Razor Blade and Watchmen / Watchmen V2
//  left up to 61 MiB behind in Max's browse. F4 stamps the buffer a publish replaces (publishMorph) and
//  the 60 Hz timer frees it behind M2's fence (freeRetiredMorphs).
//
//  What runs is SHIPPING CODE, sliced verbatim by extract_morph_life.py: struct MorphSlot and struct
//  ImportSlot, wavetableForOsc (the audio read), rebuildMorphIfNeeded (the None branch, the skip gate,
//  the retire cooldown, the claim, the build, the publish), publishMorph, freeRetiredMorphs and
//  importGraceOver. The host supplies only what those touch: five raw parameters, the effective-value
//  atomics, a one-table bank, oscSourceSpec (a factory spec) and the hold before a free
//  (kImportFreeHoldMs — 500 ms in the plugin; 40 ms in phase M, 0 in phase L). The timer is the MAIN
//  thread, as in the plugin: a rebuild keeps ~400 KB of specs on its stack.
//    M1  a morph switched on builds ONE page-backed buffer: a Memory Tag 240 region of its own size
//    M2  a rebuild (the amount moved) retires the other buffer; it stays resident THROUGH the hold,
//        then is freed while the morph stays on and keeps sounding (tag-240 back to one buffer, the
//        footprint down by ~7.6 MB) — the retired buffer after a rebuild
//    M3  switching the morph off gives BOTH back, although the None branch runs every tick (the F4
//        skeptic's trap: a stamp per tick would push the hold out for ever)
//    M4  a rebuild INTO a freed buffer is the same table, float for float: its hash equals the same
//        spec built before the free and the same spec built over resident pages
//    M5  the free never writes rebuild state (retireCooldown, buildIdx, ready[], the built* keys, live,
//        the buffers' epochs): when the next rebuild runs, and into which buffer, is what it was
//    L1  THE AUDIO READ (wavetableForOsc between processBlock's audioSeq_ +1 / +1) never has a morph
//        buffer freed, zeroed or rebuilt under it while the timer rebuilds, switches off and frees
//        with NO hold — only the grace fence and audioReadingIdx stand between a free and the block
//    L2  the stress really EXPOSED it: many reads held a buffer retired WHILE they read, and many
//        frees were of a buffer retired INSIDE a block (an odd stamp: the fence had to wait)
//  A SIGSEGV / SIGBUS is reported as the L1 failure it is: a page unmapped under the audio read.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#include "SpectralMorph.h"   // Wavetable.h + the pure spectral transform (no juce:: symbol beyond the shim)

namespace juce   // the things the sliced lifetime code touches beyond the shim
{
    using uint32 = std::uint32_t;
    using uint64 = std::uint64_t;
    using String = std::string;   // rebuildMorphIfNeeded takes its five parameter IDs as const juce::String&
    struct Time
    {
        static uint32 getMillisecondCounter() noexcept
        {
            using namespace std::chrono;
            return (uint32) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
        }
        static double getMillisecondCounterHiRes() noexcept
        {
            using namespace std::chrono;
            return duration<double, std::milli> (steady_clock::now().time_since_epoch()).count();
        }
    };
}

#ifndef MORPH_LIFE_HEADER
 #error "pass -DMORPH_LIFE_HEADER='\"<path>\"' (written by Tests/extract_morph_life.py)"
#endif
struct MorphLife
{
    #include MORPH_LIFE_HEADER             // struct ImportSlot + struct MorphSlot + wavetableForOsc, as they ship
    ImportSlot importSlot_[4];
    MorphSlot  morphA_, morphB_, morphC_, morphD_;
    std::atomic<juce::uint64> audioSeq_ { 0 };
    static inline juce::uint32 kImportFreeHoldMs = 500;   // the plugin's value; a variable HERE: phase M 40 ms, phase L 0
    std::atomic<float> spectralEffAmt_[4] { { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f } };   // -1 = the raw parameter
    std::atomic<float> specLoEff_[4]      { { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f } };
    std::atomic<float> specHiEff_[4]      { { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f } };
    std::atomic<float>         lastBakeMs_ { 0.0f };
    std::atomic<std::uint32_t> bakeCount_  { 0 };
    const tw::Wavetable* bankLastGood_[4] { nullptr, nullptr, nullptr, nullptr };
    struct Bank   // one small malloc'd table (128 KiB: below the page door, never in the tag-240 numbers)
    {
        tw::Wavetable t { 16 };
        const tw::Wavetable* getTableIfBuilt (int) const noexcept { return &t; }
        const tw::Wavetable* getTable (int) const noexcept        { return &t; }
    } wavetableBank;
    struct Params  // P preset · M spectral type · A amount · L lo · H hi
    {
        std::atomic<float> v[5] { { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f }, { 1.0f } };
        std::atomic<float>* getRawParameterValue (const juce::String& id) noexcept { return &v[std::string ("PMALH").find (id[0])]; }
    } apvts;
    tw::WavetableSpec srcSpec_ = tw::Wavetable::makeProphetSawSpec();
    const tw::WavetableSpec* oscSourceSpec (int, int, tw::WavetableSpec&) { return &srcSpec_; }

    bool importGraceOver (juce::uint64 retiredAt) const noexcept;
    void publishMorph (MorphSlot& slot, const tw::Wavetable* to) noexcept;
    void freeRetiredMorphs();
    void rebuildMorphIfNeeded (MorphSlot& slot, int oscIdx, const juce::String& presetId, const juce::String& modeId,
                               const juce::String& amtId, const juce::String& loId, const juce::String& hiId);
};
#define MORPH_LIFE_FUNCS 1
#include MORPH_LIFE_HEADER                 // the four functions, TerrainAudioProcessor:: -> MorphLife::

static int g_pass = 0, g_fail = 0;
static void bar (bool ok, const char* id, const std::string& detail)
{
    (ok ? g_pass : g_fail)++;
    std::printf ("  %s  %-3s %s\n", ok ? "PASS" : "FAIL", id, detail.c_str());
    std::fflush (stdout);
}
static std::string fmt (const char* f, ...)
{
    char b[640]; va_list a; va_start (a, f); std::vsnprintf (b, sizeof b, f, a); va_end (a); return b;
}

static double footprintMB()
{
    task_vm_info_data_t v {}; mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    task_info (mach_task_self(), TASK_VM_INFO, (task_info_t) &v, &n);
    return (double) v.phys_footprint / 1048576.0;
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

static unsigned long long tableHash (const tw::Wavetable& w)
{
    unsigned long long h = 1469598103934665603ull;
    const int nf = w.getNumFrames();
    for (int mip : { 0, 12, 30, 60 })
        for (int f = 0; f < nf; ++f)
            for (int s = 0; s < 2048; ++s)
            {
                const float v = w.lookup (mip, nf > 1 ? (float) f / (float) (nf - 1) : 0.0f, (float) s / 2048.0f);
                std::uint32_t b; std::memcpy (&b, &v, 4);
                for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 0xffu; h *= 1099511628211ull; }
            }
    return h;
}

static void onFault (int)
{
    const char m[] = "  FAIL  L1  the audio read touched an unmapped page (SIGSEGV/SIGBUS): a morph buffer was freed under the block\n";
    (void) ::write (1, m, sizeof m - 1);
    _exit (1);
}

// one "block" of reads of a table the caller holds: `windows` windows of 64 lookups across its frames
// and phases. Returns false if a window came back exactly zero — assign() zero-filling it under the read.
static bool readTable (const tw::Wavetable* wt, int windows, unsigned salt)
{
    bool ok = true;
    for (int w = 0; w < windows; ++w)
    {
        float acc = 0.0f;
        for (int i = 0; i < 64; ++i)
            acc += std::fabs (wt->lookup (0, (float) ((w + i + salt) % 17) / 16.0f, (float) (i * 32 + (int) salt % 32) / 2048.0f));
        if (acc == 0.0f) ok = false;
    }
    return ok;
}

// everything a rebuild's timing and target depend on (M5: the free must write none of it)
struct RebuildState
{
    int cooldown, buildIdx, builtPreset, builtMode, builtEpochImp, e0, e1;
    float builtAmount, builtLo, builtHi;
    const void* builtImp; const void* live; bool r0, r1;
    bool operator== (const RebuildState& o) const noexcept
    {
        return cooldown == o.cooldown && buildIdx == o.buildIdx && builtPreset == o.builtPreset && builtMode == o.builtMode
            && builtEpochImp == o.builtEpochImp && e0 == o.e0 && e1 == o.e1 && builtAmount == o.builtAmount
            && builtLo == o.builtLo && builtHi == o.builtHi && builtImp == o.builtImp && live == o.live && r0 == o.r0 && r1 == o.r1;
    }
};
static RebuildState snap (const MorphLife::MorphSlot& s)
{
    return { s.retireCooldown, s.buildIdx, s.builtPreset, s.builtMode, s.builtImportEpoch, s.buf[0].buildEpoch(), s.buf[1].buildEpoch(),
             s.builtAmount, s.builtLo, s.builtHi, s.builtImportPtr, s.live.load(), s.ready[0].load(), s.ready[1].load() };
}

int main (int argc, char** argv)
{
    const double secs = argc > 1 ? std::atof (argv[1]) : 6.0;
    std::signal (SIGSEGV, onFault);
    std::signal (SIGBUS,  onFault);
    std::printf ("══ morph_pages_cert — fb636 F4 (morph buffers given back; the morph lifetime under stress) ══\n");

    static MorphLife L;
    auto& S = L.morphA_;
    long freeCalls = 0, freeStateDiffs = 0, frees = 0, oddFrees = 0;
    // timerCallback's order: the rebuild, then the free — with M5's snapshot around the free
    auto tick = [&]
    {
        L.rebuildMorphIfNeeded (S, 0, "P", "M", "A", "L", "H");
        const RebuildState a = snap (S);
        const bool p[2] = { S.pending[0], S.pending[1] };
        const juce::uint64 at[2] = { S.retiredAt[0], S.retiredAt[1] };
        L.freeRetiredMorphs();
        ++freeCalls;
        if (! (snap (S) == a)) ++freeStateDiffs;
        for (int i = 0; i < 2; ++i)
            if (p[i] && ! S.pending[i]) { ++frees; if (at[i] & 1) ++oddFrees; }
    };
    auto runMs = [&] (double ms)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::microseconds ((long) (ms * 1000.0));
        while (std::chrono::steady_clock::now() < end) { tick(); std::this_thread::sleep_for (std::chrono::milliseconds (2)); }
    };
    auto tickUntil = [&] (const tw::Wavetable* want, int maxTicks)
    {
        for (int k = 0; k < maxTicks && S.live.load() != want; ++k) tick();
        return S.live.load() == want;
    };

    // ── M: one osc, the timer alone (no block runs: audioSeq_ stays even) ──────────────────────
    const double holdMs = 40.0;
    MorphLife::kImportFreeHoldMs = (juce::uint32) holdMs;
    const auto mode1 = (tw::SpectralMode) 1;
    unsigned long long hRef = 0;
    {
        tw::Wavetable ref;   // the same spec built over RESIDENT pages (an in-place rebuild of a 0.8 table)
        ref.buildFromSpec (tw::SpectralMorph::apply (L.srcSpec_, mode1, 0.8f, 0.0f, 1.0f));
        ref.buildFromSpec (tw::SpectralMorph::apply (L.srcSpec_, mode1, 0.5f, 0.0f, 1.0f));
        hRef = tableHash (ref);
        ref.releaseStorage();
    }
    const double t0 = tagMB();
    L.apvts.v[1] = 1.0f; L.apvts.v[2] = 0.5f;
    tick();
    const bool on = S.live.load() == &S.buf[0];
    const double bufMB = on ? (double) S.buf[0].getNumMipLevels() * S.buf[0].getNumFrames() * S.buf[0].getFrameSize() * 4.0 / 1048576.0 : 7.625;
    const double tOn = tagMB() - t0;
    bar (on && std::fabs (tOn - bufMB) < 0.1,
         "M1", fmt ("a morph switched on builds one page-backed buffer: tag-240 %+.2f MiB (a buffer is %.2f MiB)", tOn, bufMB));
    const unsigned long long hA = on ? tableHash (*S.live.load()) : 0;

    L.apvts.v[2] = 0.8f;
    const bool rebuilt = tickUntil (&S.buf[1], 12);
    const double tBoth = tagMB() - t0, fpBoth = footprintMB();
    const bool heldThrough = S.pending[0];
    runMs (holdMs * 3.0 + 20.0);   // the morph stays ON and static: the skip gate returns every tick
    const double tOne = tagMB() - t0, fpOne = footprintMB();
    const bool plays = S.live.load() == &S.buf[1] && readTable (S.live.load(), 16, 0);
    bar (rebuilt && heldThrough && std::fabs (tBoth - 2.0 * bufMB) < 0.1 && ! S.pending[0]
             && std::fabs (tOne - bufMB) < 0.1 && fpBoth - fpOne > 0.8 * bufMB && plays,
         "M2", fmt ("a rebuild retires the other buffer: tag-240 %+.2f MiB just after it (held through the hold: %s) -> %+.2f MiB "
                    "%.0f ms later, footprint %.1f MB lower, the morph still sounding (%s); retireCooldown sits at %d on the "
                    "static morph, which is why it cannot be a free condition",
                    tBoth, heldThrough ? "yes" : "NO", tOne, holdMs * 3.0 + 20.0, fpBoth - fpOne, plays ? "yes" : "NO", S.retireCooldown));

    L.apvts.v[2] = 0.5f;
    const bool wasFreed = ! S.pending[0] && std::fabs ((tagMB() - t0) - bufMB) < 0.1;
    const bool back = tickUntil (&S.buf[0], 12);
    const unsigned long long hB = back ? tableHash (*S.live.load()) : 1;
    bar (back && wasFreed && hB == hA && hB == hRef,
         "M4", fmt ("the rebuild INTO the freed buffer (freed first: %s) hashes %016llx; before the free %016llx; over resident pages %016llx",
                    wasFreed ? "yes" : "NO", hB, hA, hRef));

    L.apvts.v[2] = 0.0f;           // the None branch, every tick from here
    runMs (holdMs * 3.0 + 20.0);
    const double tOff = tagMB() - t0;
    bar (S.live.load() == nullptr && ! S.pending[0] && ! S.pending[1] && tOff < 0.01,
         "M3", fmt ("switched off (the None branch ran every tick for %.0f ms): pending %d/%d, tag-240 %+.2f MiB (both buffers given back)",
                    holdMs * 3.0 + 20.0, (int) S.pending[0], (int) S.pending[1], tOff));
    bar (freeStateDiffs == 0 && freeCalls > 20 && frees >= 3,
         "M5", fmt ("%ld free passes, %ld frees: rebuild state changed across a free %ld times", freeCalls, frees, freeStateDiffs));

    // ── L: the audio read against the timer, NO hold ───────────────────────────────────────────
    MorphLife::kImportFreeHoldMs = 0;
    frees = oddFrees = 0;
    std::atomic<bool> stop { false };
    std::atomic<long> auReads { 0 }, auExposed { 0 }, auBad { 0 };
    std::thread audio ([&]   // processBlock's shape: +1 in, the shipping wavetableForOsc, a block of reads, +1 out
    {
        unsigned salt = 0;
        while (! stop.load())
        {
            L.audioSeq_.fetch_add (1, std::memory_order_seq_cst);
            const tw::Wavetable* wt = L.wavetableForOsc (0, S, 0);
            if (wt != &L.wavetableBank.t)
            {
                const int e0 = wt->buildEpoch();
                bool ok = readTable (wt, 160, salt++);
                if (wt->buildEpoch() != e0) ok = false;
                if (S.live.load (std::memory_order_relaxed) != wt) auExposed.fetch_add (1);
                auReads.fetch_add (1);
                if (! ok) { auBad.fetch_add (1); stop.store (true); }
            }
            L.audioSeq_.fetch_add (1, std::memory_order_seq_cst);
        }
    });
    unsigned r = 2463534242u;
    auto rnd = [&] { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (double) (r % 100000u) / 100000.0; };
    const unsigned buildsL0 = L.bakeCount_.load();
    int phase = 0; float amount = 0.6f;
    auto segEnd = std::chrono::steady_clock::now();
    const auto tEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds ((long) (secs * 1000.0));
    while (! stop.load() && std::chrono::steady_clock::now() < tEnd)
    {
        if (std::chrono::steady_clock::now() >= segEnd)   // modulated · static · off, 30-150 ms each
        {
            phase = (int) (rnd() * 3.0);
            segEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds (30 + (int) (rnd() * 120.0));
            amount = 0.25f + 0.7f * (float) rnd();
        }
        L.apvts.v[2] = phase == 0 ? 0.2f + 0.8f * (float) rnd() : phase == 1 ? amount : 0.0f;
        tick();
        std::this_thread::sleep_for (std::chrono::milliseconds (3));
    }
    stop.store (true);
    audio.join();
    const long buildsL = (long) (L.bakeCount_.load() - buildsL0);

    bar (auBad.load() == 0 && auReads.load() > 1000,
         "L1", fmt ("audio-block reads of a morph buffer: %ld, freed/zeroed/rebuilt inside the block: %ld (%ld rebuilds, %ld frees)",
                    auReads.load(), auBad.load(), buildsL, frees));
    bar (auExposed.load() > 20 && oddFrees > 20 && frees > 50,
         "L2", fmt ("reads that held a buffer RETIRED while they read: %ld; frees of a buffer retired INSIDE a block: %ld of %ld",
                    auExposed.load(), oddFrees, frees));

    std::printf ("  %d pass / %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
