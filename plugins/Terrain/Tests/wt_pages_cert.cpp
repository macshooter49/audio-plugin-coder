// ══════════════════════════════════════════════════════════════════════════════════════════════
//  wt_pages_cert.cpp — fb636 F1. A FREED WAVETABLE REALLY LEAVES THE FOOTPRINT, AND NOTHING READS
//  ONE AFTER IT HAS LEFT. (macOS only: phys_footprint and the region tags are Mach reads.)
//
//    python3 Tests/extract_import_life.py Source/PluginProcessor.h Source/PluginProcessor.cpp /tmp/il.h
//    clang++ -O2 -std=c++17 -pthread -I Tests/shim -I Source -DIMPORT_LIFE_HEADER='"/tmp/il.h"' \
//            Tests/wt_pages_cert.cpp -o /tmp/wt_pages_cert -framework Accelerate
//    /tmp/wt_pages_cert [stress seconds, default 6]      (bash Tests/wt_pages_gates.sh runs every control)
//
//  Max's FL climbed to 4.2 GB while he browsed presets. M2 already freed the import tables a preset
//  leaves behind (61 MiB per 128 frames), but libmalloc kept every freed LARGE block dirty and COUNTED
//  (memgrowth harness: MALLOC_LARGE (empty) 200 / 311 MB at the end of pass 1 / 2). F1 gives the
//  tables their own pages — tw::PageBackedAllocator, mmap in and munmap out. The bars:
//    P1  a 128-frame table lives in a "Memory Tag 240" region of exactly its own size (the page door)
//    P2  releaseStorage() gives it back: the tag-240 bytes return to 0 and phys_footprint to within
//        2 MB of where it stood before the build (through the malloc door all 61 MiB stayed)
//    P3  60 build / release cycles over 64 / 128 / 256 frames never ratchet the footprint
//    P4  prints a hash of every sampled float of three builds (two PCM sizes and a factory spec);
//        the runner demands the SAME hash from the malloc mutant: the storage source moves no float
//    L1  THE SHIPPING IMPORT LIFETIME (ImportSlot, ImportRead, the claim / publish / free, sliced
//        verbatim by extract_import_life.py): an off-audio reader pinned by ImportRead never has its
//        buffer rebuilt, zeroed or unmapped under it — while a builder retires, claims and reallocates
//        page-backed tables as fast as it can and a timer runs the free every 250 µs
//    L2  the audio reader (the grace fence: audioSeq_ odd while a block runs) likewise
//    L3  the stress really EXPOSED both: each reader held, many times, a buffer that was RETIRED while
//        it read (without that, L1 and L2 would have tested nothing)
//  A SIGSEGV / SIGBUS is reported as the L1/L2 failure it is: a page unmapped under a reader.
//  The ONE value this host chooses is the hold before the timer frees (kImportFreeHoldMs, 500 ms in
//  the plugin, 1 ms here) so the free races the readers thousands of times; the fence and the pins
//  are the shipping bytes.
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
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#ifndef WT_PAGES_HEADER
 #define WT_PAGES_HEADER "Wavetable.h"   // the runner points this at the malloc mutant (never -I: the fb606 trap)
#endif
#include WT_PAGES_HEADER

namespace juce   // the two things the sliced lifetime code touches beyond the shim
{
    using uint32 = std::uint32_t;
    using uint64 = std::uint64_t;
    struct Time
    {
        static uint32 getMillisecondCounter() noexcept
        {
            using namespace std::chrono;
            return (uint32) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
        }
    };
}

#ifndef IMPORT_LIFE_HEADER
 #error "pass -DIMPORT_LIFE_HEADER='\"<path>\"' (written by Tests/extract_import_life.py)"
#endif
struct ImportLife
{
    #include IMPORT_LIFE_HEADER             // struct ImportSlot + struct ImportRead, as they ship
    ImportSlot importSlot_[4];
    std::atomic<juce::uint64> audioSeq_ { 0 };
    std::atomic<juce::uint32> wtBuildReq_[4] { {0}, {0}, {0}, {0} };
    static constexpr juce::uint32 kImportFreeHoldMs = 1;   // the plugin: 500 (see the header)
    bool importGraceOver (juce::uint64 retiredAt) const noexcept;
    tw::Wavetable& claimImportBufLocked (ImportSlot& slot);
    void publishImportLocked (ImportSlot& slot, const tw::Wavetable* to) noexcept;
    void buildImportLocked (int osc, const float* pcm, int numSamples, int frames);
    void dropImportTable (int osc);
    void freeRetiredImports();
};
#define IMPORT_LIFE_FUNCS 1
#include IMPORT_LIFE_HEADER                 // the six functions, TerrainAudioProcessor:: -> ImportLife::

static int g_pass = 0, g_fail = 0;
static void bar (bool ok, const char* id, const std::string& detail)
{
    (ok ? g_pass : g_fail)++;
    std::printf ("  %s  %-3s %s\n", ok ? "PASS" : "FAIL", id, detail.c_str());
    std::fflush (stdout);
}
static std::string fmt (const char* f, ...)
{
    char b[512]; va_list a; va_start (a, f); std::vsnprintf (b, sizeof b, f, a); va_end (a); return b;
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

static std::vector<float> makePcm (int n, unsigned seed)
{
    std::vector<float> v ((size_t) n);
    unsigned s = seed;
    for (int i = 0; i < n; ++i)
    {
        s = s * 1664525u + 1013904223u;
        const double t = (double) i;
        v[(size_t) i] = (float) (0.6 * std::sin (0.0123 * t) + 0.3 * std::sin (0.2311 * t + 0.001 * t * t / n)
                                 + 0.1 * ((double) (s >> 8) / 16777216.0 - 0.5));
    }
    return v;
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
    const char m[] = "  FAIL  L1/L2 a reader touched an unmapped page (SIGSEGV/SIGBUS): a table was freed or reallocated under it\n";
    (void) ::write (1, m, sizeof m - 1);
    _exit (1);
}

// one "read" of a table the caller holds: `windows` windows of 64 lookups across its frames and
// phases. Returns false if a window came back exactly zero — assign() zero-filling it under the read.
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

int main (int argc, char** argv)
{
    const double secs = argc > 1 ? std::atof (argv[1]) : 6.0;
    std::signal (SIGSEGV, onFault);
    std::signal (SIGBUS,  onFault);
    std::printf ("══ wt_pages_cert — fb636 F1 (tables in their own pages; the import lifetime under stress) ══\n");

    // ── P: the page door ─────────────────────────────────────────────────────────────────────
    const std::vector<float> pcm = makePcm (1 << 18, 7u);
    { tw::Wavetable warm; warm.buildFromPcm (pcm.data(), (int) pcm.size(), 1); warm.releaseStorage(); }   // one-time FFT setup out of the numbers
    const double tableMB = 61.0 * 128 * 2048 * 4 / 1048576.0;
    const double fp0 = footprintMB(), t0 = tagMB();
    {
        tw::Wavetable w;
        w.buildFromPcm (pcm.data(), (int) pcm.size(), 128);
        const double fp1 = footprintMB(), t1 = tagMB();
        bar (std::fabs ((t1 - t0) - tableMB) < 0.1,
             "P1", fmt ("a 128-frame table is a Memory Tag 240 region of its own size: tag-240 %+.2f MiB (table %.2f MiB)", t1 - t0, tableMB));
        w.releaseStorage();
        const double fp2 = footprintMB(), t2 = tagMB();
        bar (fp1 - fp0 > 0.9 * tableMB && t2 - t0 < 0.01 && fp2 - fp0 < 2.0,
             "P2", fmt ("releaseStorage gives it back: footprint %+.1f MB built -> %+.1f MB released (tag-240 %+.2f MiB)", fp1 - fp0, fp2 - fp0, t2 - t0));
    }
    {
        double worst = -1e9;
        const double base = footprintMB();
        for (int c = 0; c < 60; ++c)
        {
            tw::Wavetable w;
            w.buildFromPcm (pcm.data(), (int) pcm.size(), c % 3 == 0 ? 64 : c % 3 == 1 ? 128 : 256);
            w.releaseStorage();
            worst = std::max (worst, footprintMB() - base);
        }
        bar (worst < 3.0 && tagMB() - t0 < 0.01,
             "P3", fmt ("60 build/release cycles over 64/128/256 frames: worst footprint after a release %+.1f MB", worst));
    }
    {
        tw::Wavetable a, b, c;
        a.buildFromPcm (pcm.data(), (int) pcm.size(), 128);
        b.buildFromPcm (pcm.data(), (int) pcm.size(), 256);
        c.buildFromSpec (tw::Wavetable::makeProphetSawSpec());
        std::printf ("  P4  hash pcm128 %016llx pcm256 %016llx spec %016llx\n", tableHash (a), tableHash (b), tableHash (c));
    }

    // ── L: the shipping lifetime, four threads ────────────────────────────────────────────────
    static ImportLife L;
    const std::vector<float> src = makePcm (16384, 11u);
    std::atomic<bool> stop { false };
    std::atomic<long> builds { 0 }, rdReads { 0 }, rdExposed { 0 }, rdBad { 0 }, auReads { 0 }, auExposed { 0 }, auBad { 0 };

    std::thread builder ([&]
    {
        unsigned k = 0, r = 12345u;
        while (! stop.load())
        {
            const int frames = 1 + (int) (k % 6);   // 0.48-2.9 MiB: every table takes the page door, and each step up reallocates
            {
                auto& slot = L.importSlot_[0];
                const std::lock_guard<std::mutex> g (slot.mx);                  // rebuildImport's own shape
                L.wtBuildReq_[0].fetch_add (1, std::memory_order_acq_rel);
                L.buildImportLocked (0, src.data(), (int) src.size(), frames);
            }
            if (k % 7 == 6) L.dropImportTable (0);
            ++k; builds.fetch_add (1);
            r = r * 1103515245u + 12345u;
            const int nap = (int) ((r >> 16) % 5);                             // 0-4 ms: sometimes claim at once, sometimes let the timer in
            if (nap > 1) std::this_thread::sleep_for (std::chrono::milliseconds (nap - 1));
        }
    });
    std::thread timer ([&]
    {
        while (! stop.load()) { L.freeRetiredImports(); std::this_thread::sleep_for (std::chrono::microseconds (250)); }
    });
    std::thread reader ([&]   // the display bake / WT->LFO / Table source / toSpec shape: pin, read for a few ms
    {
        unsigned salt = 0;
        while (! stop.load())
        {
            const ImportLife::ImportRead pin (L.importSlot_[0]);
            if (pin.wt == nullptr) { std::this_thread::yield(); continue; }
            const int e0 = pin.wt->buildEpoch();
            bool ok = true;
            for (int rep = 0; rep < 24; ++rep) ok = readTable (pin.wt, 16, salt++) && ok;
            if (pin.wt->buildEpoch() != e0) ok = false;
            if (L.importSlot_[0].live.load (std::memory_order_relaxed) != pin.wt) rdExposed.fetch_add (1);
            rdReads.fetch_add (1);
            if (! ok) { rdBad.fetch_add (1); stop.store (true); }
        }
    });
    std::thread audio ([&]   // processBlock's shape: +1 in, wavetableForOsc's seq_cst load, read, +1 out
    {
        unsigned salt = 0;
        while (! stop.load())
        {
            L.audioSeq_.fetch_add (1, std::memory_order_seq_cst);
            const tw::Wavetable* wt = L.importSlot_[0].live.load (std::memory_order_seq_cst);
            if (wt != nullptr)
            {
                const int e0 = wt->buildEpoch();
                bool ok = readTable (wt, 48, salt++);
                if (wt->buildEpoch() != e0) ok = false;
                if (L.importSlot_[0].live.load (std::memory_order_relaxed) != wt) auExposed.fetch_add (1);
                auReads.fetch_add (1);
                if (! ok) { auBad.fetch_add (1); stop.store (true); }
            }
            L.audioSeq_.fetch_add (1, std::memory_order_seq_cst);
        }
    });
    const auto tEnd = std::chrono::steady_clock::now() + std::chrono::milliseconds ((long) (secs * 1000.0));
    while (! stop.load() && std::chrono::steady_clock::now() < tEnd) std::this_thread::sleep_for (std::chrono::milliseconds (20));
    stop.store (true);
    builder.join(); timer.join(); reader.join(); audio.join();

    bar (rdBad.load() == 0 && rdReads.load() > 100,
         "L1", fmt ("pinned off-audio reads: %ld, rebuilt/zeroed under the pin: %ld (builds %ld)", rdReads.load(), rdBad.load(), builds.load()));
    bar (auBad.load() == 0 && auReads.load() > 1000,
         "L2", fmt ("audio-block reads: %ld, rebuilt/zeroed inside the block: %ld", auReads.load(), auBad.load()));
    bar (rdExposed.load() > 20 && auExposed.load() > 20,
         "L3", fmt ("reads that held a buffer RETIRED while they read: off-audio %ld, audio %ld", rdExposed.load(), auExposed.load()));

    std::printf ("  %d pass / %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
