// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb498 — THE WINDOWS PORT OF Tests/au_blk_cpu.cpp
//
//  The handoff (§3) says the block-size harness is "macOS only today — worth porting to Windows"
//  and calls it "the single most useful instrument built tonight". Without it nothing on Windows
//  can be measured before and after, so nothing on Windows can honestly be called fixed. This is
//  that port, and it is the first instrument in this project that runs where the problem is.
//
//  It differs from the AU original in ONE deliberate way: instead of talking to an AudioUnit it
//  hosts the REAL, INSTALLED .vst3 through JUCE's own VST3 host. That keeps it measuring the
//  shipping artifact (as auval did on the Mac) rather than a privately relinked copy, and it
//  keeps this target free of the plugin's static library — linking that would duplicate every
//  JUCE module symbol the plugin already carries.
//
//  Two modes:
//    (default)  CPU as a share of one core at 45/88/128/256/512-sample blocks, idle, no notes.
//               Block size changes ONLY how often the host calls, so a SLOPED row is fixed
//               per-call cost and a FLAT row is none. Prints the same verdict line as the Mac.
//    --mem      The memory RATCHET test (handoff §5.1/§5.2): working set across repeated
//               prepareToPlay/releaseResources cycles. If releaseResources() frees nothing, the
//               resident set never comes back down and the per-cycle delta IS the ratchet.
//
//  Set TERRAIN_CPU_PROBE=1 in the environment and the plugin's own beacon writes the phase split
//  (gather / voices / fx+master) to %TEMP%\terrain-cpu.txt while this runs — the Mac harness
//  never had that, because the beacon is #if JUCE_WINDOWS.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <cstdio>
#include <memory>
#include <cstring>
#include <xmmintrin.h>
#include <pmmintrin.h>
#include <tlhelp32.h>
#include <map>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>

static const double SR      = 48000.0;   // what this machine's device actually runs at
static const double SECONDS = 6.0;
static const double WARM    = 1.0;

static const char* kDefaultPath =
    "C:\\Program Files\\Common Files\\VST3\\Terrain Instrument.vst3";

// ── peak working set: the Windows answer to /usr/bin/time -l ──────────────────────────────────
static double peakWorkingSetMB()
{
    PROCESS_MEMORY_COUNTERS pmc {};
    if (! GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof pmc)) return 0.0;
    return (double) pmc.PeakWorkingSetSize / (1024.0 * 1024.0);
}
static double workingSetMB()
{
    PROCESS_MEMORY_COUNTERS pmc {};
    if (! GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof pmc)) return 0.0;
    return (double) pmc.WorkingSetSize / (1024.0 * 1024.0);
}

// ── one shared scan, so the 12 MB binary is not re-parsed five times ───────────────────────────
struct Host
{
    juce::AudioPluginFormatManager fm;
    juce::OwnedArray<juce::PluginDescription> found;
    juce::String path;

    bool init (const juce::String& p)
    {
        path = p;
        fm.addFormat (new juce::VST3PluginFormat());
        juce::VST3PluginFormat fmt;
        fmt.findAllTypesForFile (found, path);
        if (found.isEmpty()) { std::printf ("  !! no VST3 types found at %s\n", path.toRawUTF8()); return false; }
        return true;
    }

    std::unique_ptr<juce::AudioPluginInstance> make (int blk)
    {
        juce::String err;
        auto inst = fm.createPluginInstance (*found[0], SR, blk, err);
        if (inst == nullptr) std::printf ("  !! createPluginInstance failed: %s\n", err.toRawUTF8());
        return inst;
    }
};

// ── the measurement: identical audio, different call rate ──────────────────────────────────────
static double measure (Host& host, int blk, bool* ok)
{
    *ok = false;
    auto inst = host.make (blk);
    if (inst == nullptr) return 0.0;

    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (SR, blk);
    inst->prepareToPlay (SR, blk);

    const int nch = juce::jmax (2, inst->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (nch, blk);
    juce::MidiBuffer midi;

    const int total = (int) (SR * SECONDS / blk);
    const int warm  = (int) (SR * WARM    / blk);
    double dspSec = 0.0;

    for (int b = 0; b < warm + total; ++b)
    {
        buf.clear();
        midi.clear();
        const auto t0 = std::chrono::steady_clock::now();
        inst->processBlock (buf, midi);
        const auto t1 = std::chrono::steady_clock::now();
        if (b >= warm) dspSec += std::chrono::duration<double> (t1 - t0).count();
    }

    inst->releaseResources();
    inst.reset();

    *ok = true;
    const double audioSec = (double) total * blk / SR;
    return 100.0 * dspSec / audioSec;
}

// ── the ratchet: does releaseResources() give anything back? ────────────────────────────────────
static int memoryMode (Host& host)
{
    std::printf ("\n  MEMORY RATCHET  (working set, MB — does releaseResources() free the rings?)\n\n");
    std::printf ("    baseline (host only, no plugin)      ws %8.1f   peak %8.1f\n", workingSetMB(), peakWorkingSetMB());

    auto inst = host.make (512);
    if (inst == nullptr) return 1;
    std::printf ("    after construction                   ws %8.1f   peak %8.1f\n", workingSetMB(), peakWorkingSetMB());

    juce::AudioBuffer<float> buf (juce::jmax (2, inst->getTotalNumOutputChannels()), 512);
    juce::MidiBuffer midi;

    double afterFirstPrepare = 0.0, lastAfterRelease = 0.0;
    for (int cycle = 1; cycle <= 4; ++cycle)
    {
        inst->prepareToPlay (SR, 512);
        // a little real audio, so anything allocated on first use is actually allocated
        for (int b = 0; b < 200; ++b) { buf.clear(); midi.clear(); inst->processBlock (buf, midi); }
        const double afterPrep = workingSetMB();
        if (cycle == 1) afterFirstPrepare = afterPrep;

        inst->releaseResources();
        const double afterRel = workingSetMB();
        lastAfterRelease = afterRel;

        std::printf ("    cycle %d: prepare -> ws %8.1f   release -> ws %8.1f   freed %7.1f\n",
                     cycle, afterPrep, afterRel, afterPrep - afterRel);
        std::fflush (stdout);
    }

    inst.reset();
    std::printf ("    after destruction                    ws %8.1f   peak %8.1f\n", workingSetMB(), peakWorkingSetMB());
    std::printf ("\n    first prepare reached %.1f MB; last release left %.1f MB resident.\n",
                 afterFirstPrepare, lastAfterRelease);
    std::printf ("    PEAK WORKING SET %.1f MB\n\n", peakWorkingSetMB());
    return 0;
}

// ── the MODAL regression gate ──────────────────────────────────────────────────────────────────
//  Making the waveguide delay lines lazy changes WHEN memory is allocated. It must not change
//  one sample of what MODAL sounds like. So: select Engine::MODAL on osc A, play a note, and
//  fingerprint the rendered audio. Run it before the change and after; the fingerprint must
//  match. This is the fb283 law (validate by measurement, not by eye) applied to a memory fix.
//  NOTE: a VST3-hosted instance hands back the HOST-side parameter objects, which are not
//  AudioProcessorParameterWithID — the APVTS paramID is not visible across the wrapper. Match on
//  the human-readable name that createParameterLayout registered instead
//  (PluginProcessor.cpp:1975 "Synth OSC A Engine").
static juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& inst, const juce::String& name)
{
    for (auto* p : inst.getParameters())
        if (p->getName (128).trim() == name) return p;
    return nullptr;
}

static int modalMode (Host& host)
{
    const double sr = SR;
    const int blk = 512;

    auto inst = host.make (blk);
    if (inst == nullptr) return 1;

    auto* eng = findParam (*inst, "Synth OSC A Engine");
    if (eng == nullptr)
    {
        std::printf ("  !! \"Synth OSC A Engine\" not found. First 40 parameter names:\n");
        int n = 0;
        for (auto* p : inst->getParameters())
        { std::printf ("      [%3d] %s\n", n, p->getName (128).toRawUTF8()); if (++n >= 40) break; }
        return 1;
    }

    std::printf ("\n  MODAL AUDIO FINGERPRINT\n\n");
    std::printf ("    engine param: \"%s\"  steps=%d  default=%.4f\n",
                 eng->getName (64).toRawUTF8(), eng->getNumSteps(), eng->getValue());

    eng->setValueNotifyingHost (1.0f);        // 7 choices, MODAL = index 6 -> normalised 6/6 = 1.0
    std::printf ("    set to %.4f -> \"%s\"\n", eng->getValue(), eng->getCurrentValueAsText().toRawUTF8());

    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (sr, blk);
    inst->prepareToPlay (sr, blk);

    const int nch = juce::jmax (2, inst->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (nch, blk);
    juce::MidiBuffer midi;

    // settle
    for (int b = 0; b < 20; ++b) { buf.clear(); midi.clear(); inst->processBlock (buf, midi); }

    // note on, render, note off, render the tail
    double sumSq = 0.0, peak = 0.0, weighted = 0.0, magSum = 0.0;
    juce::uint64 fnv = 1469598103934665603ULL;
    int nonSilent = 0, total = 0;

    const int blocksOn = (int) (sr * 2.0 / blk), blocksOff = (int) (sr * 1.0 / blk);
    for (int b = 0; b < blocksOn + blocksOff; ++b)
    {
        buf.clear();
        midi.clear();
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        if (b == blocksOn) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        inst->processBlock (buf, midi);

        const float* L = buf.getReadPointer (0);
        for (int i = 0; i < blk; ++i)
        {
            const float v = L[i];
            sumSq += (double) v * v;
            if (std::abs (v) > peak) peak = std::abs (v);
            if (std::abs (v) > 1.0e-6f) ++nonSilent;
            ++total;
            // zero-crossing-rate proxy for brightness, and an exact byte fingerprint
            juce::uint32 bits;
            std::memcpy (&bits, &v, sizeof bits);
            fnv = (fnv ^ bits) * 1099511628211ULL;
        }
        // crude spectral centroid via first-difference energy vs total energy
        for (int i = 1; i < blk; ++i)
        {
            const double d = (double) L[i] - (double) L[i - 1];
            weighted += d * d;
            magSum   += (double) L[i] * L[i];
        }
    }

    inst->releaseResources();
    inst.reset();

    const double rms = std::sqrt (sumSq / juce::jmax (1, total));
    const double bright = (magSum > 1.0e-12) ? std::sqrt (weighted / magSum) : 0.0;
    std::printf ("\n    samples %d   non-silent %d (%.1f%%)\n", total, nonSilent, 100.0 * nonSilent / juce::jmax (1, total));
    std::printf ("    peak    %.6f\n    rms     %.6f\n    bright  %.6f   (HF proxy: rms of first difference / rms)\n",
                 peak, rms, bright);
    std::printf ("    fingerprint  %016llx\n", (unsigned long long) fnv);
    std::printf ("    peak working set %.1f MB\n\n", peakWorkingSetMB());

    if (nonSilent == 0) { std::printf ("    !! MODAL PRODUCED SILENCE — the gate itself is broken or the engine did not arm\n\n"); return 1; }
    return 0;
}

// ── THE LIVE-SWITCH GATE (fb498's actual new code path) ────────────────────────────────────────
//  --modal proves a patch that ALREADY has MODAL selected is armed by prepareToPlay. It does NOT
//  exercise the other half: a user switching an osc TO Modal while the plugin is running, which
//  is armed by the processor's 60 Hz timerCallback. A console harness has no running message
//  loop, so that timer never fires unless the loop is pumped — and if it never fires, MODAL is
//  silent forever. That would be a far worse bug than the memory it saves, so it gets its own gate.
static int modalLiveMode (Host& host)
{
    const int blk = 512;
    auto inst = host.make (blk);
    if (inst == nullptr) return 1;

    auto* eng = findParam (*inst, "Synth OSC A Engine");
    if (eng == nullptr) { std::printf ("  !! engine param not found\n"); return 1; }

    std::printf ("\n  MODAL LIVE-SWITCH GATE  (engine changed while running; the 60 Hz timer must arm it)\n\n");
    std::printf ("    starting engine: \"%s\"\n", eng->getCurrentValueAsText().toRawUTF8());

    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (SR, blk);
    inst->prepareToPlay (SR, blk);

    const int nch = juce::jmax (2, inst->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (nch, blk);
    juce::MidiBuffer midi;

    double lastPeak = 0.0;
    auto renderRms = [&] (int blocks, bool noteOnFirst) -> double
    {
        double sumSq = 0.0; int n = 0; lastPeak = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear(); midi.clear();
            if (b == 0 && noteOnFirst) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            inst->processBlock (buf, midi);
            const float* L = buf.getReadPointer (0);
            for (int i = 0; i < blk; ++i) { sumSq += (double) L[i] * L[i]; if (std::abs (L[i]) > lastPeak) lastPeak = std::abs (L[i]); ++n; }
        }
        return std::sqrt (sumSq / juce::jmax (1, n));
    };
    // Silence everything and let every tail decay, so the next measurement is only the new note.
    auto settle = [&] ()
    {
        midi.clear(); midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        buf.clear(); inst->processBlock (buf, midi);
        for (int b = 0; b < 200; ++b) { buf.clear(); midi.clear(); inst->processBlock (buf, midi); }
    };

    for (int b = 0; b < 20; ++b) { buf.clear(); midi.clear(); inst->processBlock (buf, midi); }
    const double rmsDefault = renderRms (120, true);
    std::printf ("    default engine (WT), note on   rms %.6f  peak %.6f\n", rmsDefault, lastPeak);

    // Switch the engine live. Then SETTLE FULLY before measuring, so what follows is only the new
    // note and not the previous engine's ringing tail — the first draft of this gate measured that
    // tail and reported 0.218 for an engine that had not been armed at all.
    settle();
    eng->setValueNotifyingHost (1.0f);
    std::printf ("    switched live to               \"%s\"\n", eng->getCurrentValueAsText().toRawUTF8());
    settle();

    const double rmsNoPump = renderRms (120, true);
    const double peakNoPump = lastPeak;
    std::printf ("    MODAL, timer NOT yet ticked    rms %.6f  peak %.6f   <- the bounded silent window\n",
                 rmsNoPump, peakNoPump);

    settle();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);   // let the 60 Hz timer tick
    std::printf ("    pumped the message loop 150 ms (>= 9 timer ticks at 60 Hz)\n");

    const double rmsAfterPump = renderRms (120, true);
    const double peakAfterPump = lastPeak;
    std::printf ("    MODAL, after the timer armed   rms %.6f  peak %.6f\n", rmsAfterPump, peakAfterPump);
    std::printf ("    peak working set %.1f MB\n\n", peakWorkingSetMB());

    inst->releaseResources();
    inst.reset();

    // The proof is the CONTRAST: unarmed must be effectively silent, armed must clearly sound.
    // A single "it made some noise" reading proves nothing, because a tail or another osc can
    // supply it.
    if (! (rmsAfterPump > 1.0e-4))
    {
        std::printf ("    !! FAIL — MODAL is SILENT after a live switch. The lazy arm never fired.\n\n");
        return 1;
    }
    if (! (rmsAfterPump > rmsNoPump * 4.0))
    {
        std::printf ("    !! FAIL — arming made no real difference (%.6f vs %.6f). Either it was already\n"
                     "       armed, or what is sounding is not MODAL.\n\n", rmsAfterPump, rmsNoPump);
        return 1;
    }
    std::printf ("    PASS — unarmed MODAL is silent, the 60 Hz timer arms it, and then it sounds.\n\n");
    return 0;
}

// ── HOLD: render idle at ONE block size long enough for the plugin's own phase probe to speak ──
//  The sweep spends 6 s per size, but the CPU beacon only writes every 5 s, so the split it
//  reports is smeared across sizes. This holds one size for as long as asked and then prints the
//  plugin's own gather / voices / fx+master breakdown — the number that says WHERE the Windows
//  cost is, rather than merely that there is one.
//
//  --ftz additionally sets FTZ+DAZ in this thread's MXCSR before rendering. processBlock opens
//  with juce::ScopedNoDenormals (PluginProcessor.cpp:7227) so this should change NOTHING; if it
//  changes something, denormals are escaping that scope and that is the answer on its own.
static int holdMode (Host& host, int blk, double seconds, bool ftz)
{
    auto inst = host.make (blk);
    if (inst == nullptr) return 1;

    if (ftz)
    {
        _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
    }

    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (SR, blk);
    inst->prepareToPlay (SR, blk);

    const int nch = juce::jmax (2, inst->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (nch, blk);
    juce::MidiBuffer midi;

    const int total = (int) (SR * seconds / blk);
    const int warm  = (int) (SR * 1.0 / blk);
    double dspSec = 0.0;
    for (int b = 0; b < warm + total; ++b)
    {
        buf.clear(); midi.clear();
        const auto t0 = std::chrono::steady_clock::now();
        inst->processBlock (buf, midi);
        const auto t1 = std::chrono::steady_clock::now();
        if (b >= warm) dspSec += std::chrono::duration<double> (t1 - t0).count();
    }
    const double audioSec = (double) total * blk / SR;
    std::printf ("\n  HOLD  blk %d  for %.0f s  %s\n", blk, seconds, ftz ? "[FTZ+DAZ FORCED ON]" : "[default FP mode]");
    std::printf ("    harness-measured DSP: %.2f%% of one core\n", 100.0 * dspSec / audioSec);

    inst->releaseResources();
    inst.reset();

    auto probe = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("terrain-cpu.txt");
    if (probe.existsAsFile())
        std::printf ("    plugin's own probe:   %s\n", probe.loadFileAsString().trim().toRawUTF8());
    else
        std::printf ("    (no probe file — is TERRAIN_CPU_PROBE set?)\n");
    std::printf ("\n");
    return 0;
}

// ── ATTRIBUTION: which per-sample work is running when NOTHING is playing? ────────────────────
//  Solving the two block sizes (94 calls/s -> 6.31%, 1067 calls/s -> 10.02%) says ~5.95% of a
//  core is per-SAMPLE work that runs with no notes, no editor and a default patch. That is the
//  dominant idle cost at any sane buffer, and the gather is not it. This mode finds the owner:
//  measure the baseline, then flip ONE candidate parameter at a time and measure the delta.
//  A parameter whose change drops CPU is running work it does not need to run.
static double measureWith (juce::AudioPluginInstance& inst, int blk, double seconds)
{
    juce::AudioBuffer<float> buf (juce::jmax (2, inst.getTotalNumOutputChannels()), blk);
    juce::MidiBuffer midi;
    const int total = (int) (SR * seconds / blk);
    const int warm  = (int) (SR * 0.5 / blk);
    double dspSec = 0.0;
    for (int b = 0; b < warm + total; ++b)
    {
        buf.clear(); midi.clear();
        const auto t0 = std::chrono::steady_clock::now();
        inst.processBlock (buf, midi);
        const auto t1 = std::chrono::steady_clock::now();
        if (b >= warm) dspSec += std::chrono::duration<double> (t1 - t0).count();
    }
    return 100.0 * dspSec / ((double) total * blk / SR);
}

static int paramsMode (Host& host, const juce::String& filter)
{
    auto inst = host.make (512);
    if (inst == nullptr) return 1;
    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (SR, 512);
    inst->prepareToPlay (SR, 512);

    int n = 0;
    for (auto* p : inst->getParameters())
    {
        const juce::String nm = p->getName (128);
        if (filter.isEmpty() || nm.containsIgnoreCase (filter))
            std::printf ("    [%4d] %-46s = %8.4f  \"%s\"\n", n, nm.toRawUTF8(), p->getValue(),
                         p->getCurrentValueAsText().toRawUTF8());
        ++n;
    }
    std::printf ("\n    %d parameters total\n\n", n);
    return 0;
}

// ══════════════════════════════════════════════════════════════════════════════════════════════
//  --editor : WHY DOES OPENING THE WINDOW COST +20?
//
//  Every other mode here renders headless and offline, which cannot see the thing Max actually
//  reports: window closed = butter, window open = spikes and dropped frames. So this mode builds
//  the real situation and then attributes the cost PER THREAD:
//
//    · the REAL editor, in a real window, created through the plugin's own createEditorIfNeeded
//    · a REAL-TIME-PACED audio thread (one block every blk/SR seconds, like a DAW's callback) —
//      NOT the free-running offline loop, because free-running starves the UI and would make the
//      editor look cheap by stealing the core from it
//    · the message loop pumping on the main thread, exactly as a DAW pumps it
//
//  Then GetThreadTimes over every thread in the process says where the CPU went: audio thread,
//  message thread, or one of the plugin's own workers. WebView2 lives in SEPARATE processes, so
//  the caller diffs those (see the PowerShell wrapper) — they are not in this table.
struct ThreadCpuSnap { std::map<DWORD, double> byTid; };

static void snapshotThreadCpu (ThreadCpuSnap& out)
{
    out.byTid.clear();
    const DWORD me = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te {};
    te.dwSize = sizeof (te);
    if (Thread32First (snap, &te))
    {
        do
        {
            if (te.th32OwnerProcessID != me) continue;
            HANDLE h = OpenThread (THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (h == nullptr) continue;
            FILETIME c {}, e {}, k {}, u {};
            if (GetThreadTimes (h, &c, &e, &k, &u))
            {
                ULARGE_INTEGER K {}, U {};
                K.LowPart = k.dwLowDateTime; K.HighPart = k.dwHighDateTime;
                U.LowPart = u.dwLowDateTime; U.HighPart = u.dwHighDateTime;
                out.byTid[te.th32ThreadID] = (double) (K.QuadPart + U.QuadPart) / 1.0e7;   // 100ns units
            }
            CloseHandle (h);
        } while (Thread32Next (snap, &te));
    }
    CloseHandle (snap);
}

static int editorMode (Host& host, int blk, double seconds, bool openEditor, bool playNotes)
{
    auto inst = host.make (blk);
    if (inst == nullptr) return 1;

    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (SR, blk);
    inst->prepareToPlay (SR, blk);

    const DWORD msgTid = GetCurrentThreadId();
    std::atomic<DWORD> audioTid { 0 };
    std::atomic<bool>  stop { false };
    std::atomic<double> dspSec { 0.0 };
    std::atomic<long long> blocksDone { 0 };
    std::atomic<long long> lateBlocks { 0 };

    // ── the audio thread: paced to real time, like a host callback ────────────────────────────
    std::thread audio ([&]
    {
        audioTid.store (GetCurrentThreadId());
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        const int nch = juce::jmax (2, inst->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (nch, blk);
        juce::MidiBuffer midi;
        const auto period = std::chrono::duration<double> ((double) blk / SR);
        auto next = std::chrono::steady_clock::now();
        double acc = 0.0;
        int lastStep = -1;   // fb506 -- note sequencer state
        while (! stop.load (std::memory_order_relaxed))
        {
            next += std::chrono::duration_cast<std::chrono::steady_clock::duration> (period);
            buf.clear(); midi.clear();
            // fb506 -- REAL PLAYING: an arpeggiated chord, notes on/off every ~300 ms, so the
            // scope, meters, LFO phase and analyzer all carry CHANGING data. This is the state
            // Max actually lives in; every earlier editor measurement was idle/static-data.
            if (playNotes)
            {
                const long long blkIdx = blocksDone.load (std::memory_order_relaxed);
                const double tSec = (double) blkIdx * (double) blk / SR;
                const int step = (int) (tSec / 0.3);
                if (step != lastStep)
                {
                    static const int seq[4] = { 48, 60, 64, 67 };
                    if (lastStep >= 0) midi.addEvent (juce::MidiMessage::noteOff (1, seq[lastStep & 3]), 0);
                    midi.addEvent (juce::MidiMessage::noteOn (1, seq[step & 3], (juce::uint8) 100), 0);
                    lastStep = step;
                }
            }
            const auto t0 = std::chrono::steady_clock::now();
            inst->processBlock (buf, midi);
            const auto t1 = std::chrono::steady_clock::now();
            acc += std::chrono::duration<double> (t1 - t0).count();
            dspSec.store (acc, std::memory_order_relaxed);
            blocksDone.fetch_add (1, std::memory_order_relaxed);
            if (t1 > next) lateBlocks.fetch_add (1, std::memory_order_relaxed);   // missed its slot
            std::this_thread::sleep_until (next);
        }
    });

    // ── the editor, on the message thread ─────────────────────────────────────────────────────
    std::unique_ptr<juce::DocumentWindow> win;
    juce::AudioProcessorEditor* ed = nullptr;
    if (openEditor)
    {
        ed = inst->createEditorIfNeeded();
        if (ed == nullptr) { std::printf ("  !! the plugin returned no editor\n"); }
        else
        {
            win.reset (new juce::DocumentWindow ("Terrain harness", juce::Colours::black,
                                                 juce::DocumentWindow::allButtons));
            win->setUsingNativeTitleBar (true);
            win->setContentNonOwned (ed, true);
            win->centreWithSize (ed->getWidth(), ed->getHeight());
            win->setVisible (true);
            // Chromium THROTTLES requestAnimationFrame for a window it considers hidden or
            // occluded, and JUCE's own push is gated by emitEventIfBrowserIsVisible. A harness
            // window that is merely "visible" but behind something measures an idle page and
            // reports a page cost near zero — which is how a first attempt here read 0.07% for
            // a UI that visibly animates. Force it genuinely frontmost so the measurement is of
            // the page a user is actually looking at.
            win->setAlwaysOnTop (true);
            win->toFront (true);
        }
        // let WebView2 boot and the page settle before the measurement window opens
        juce::MessageManager::getInstance()->runDispatchLoopUntil (4000);
    }

    ThreadCpuSnap before, after;
    const double wall0 = (double) juce::Time::getMillisecondCounterHiRes();
    const double dsp0  = dspSec.load();
    const long long blk0 = blocksDone.load(), late0 = lateBlocks.load();
    snapshotThreadCpu (before);

    juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (seconds * 1000.0));

    snapshotThreadCpu (after);
    const double wallSec = ((double) juce::Time::getMillisecondCounterHiRes() - wall0) / 1000.0;
    const double dspDelta = dspSec.load() - dsp0;
    const long long blkDelta = blocksDone.load() - blk0, lateDelta = lateBlocks.load() - late0;

    stop.store (true);
    audio.join();

    if (win != nullptr) { win->setVisible (false); win->clearContentComponent(); win.reset(); }
    if (ed != nullptr) inst->editorBeingDeleted (ed);
    inst->releaseResources();
    inst.reset();

    // ── the report ────────────────────────────────────────────────────────────────────────────
    std::printf ("\n  EDITOR %s   blk %d   %.1f s wall\n\n", openEditor ? "OPEN" : "CLOSED", blk, wallSec);
    std::printf ("    audio thread DSP        %6.2f%% of one core   (%lld blocks, %lld late)\n",
                 100.0 * dspDelta / wallSec, blkDelta, lateDelta);

    struct Row { DWORD tid; double sec; };
    std::vector<Row> rows;
    double total = 0.0;
    for (const auto& kv : after.byTid)
    {
        const auto it = before.byTid.find (kv.first);
        const double d = kv.second - (it == before.byTid.end() ? 0.0 : it->second);
        if (d > 0.0005) { rows.push_back ({ kv.first, d }); total += d; }
    }
    std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.sec > b.sec; });

    std::printf ("\n    PER-THREAD CPU in this process (%% of one core)\n");
    for (const auto& r : rows)
    {
        const char* tag = (r.tid == msgTid) ? "  <- MESSAGE thread (UI + frame push)"
                        : (r.tid == audioTid.load()) ? "  <- AUDIO thread"
                        : "";
        std::printf ("      tid %-6lu  %6.2f%%%s\n", (unsigned long) r.tid, 100.0 * r.sec / wallSec, tag);
    }
    std::printf ("      %-11s %6.2f%%   <- whole process (WebView2 lives in OTHER processes)\n",
                 "TOTAL", 100.0 * total / wallSec);
    std::printf ("\n");
    return 0;
}

// __ fb516: THE KEEP-ALIVE ACCEPTANCE HARNESS ______________________________________________
// open -> settle -> close -> 8 s HIDDEN phase -> reopen. Readiness signal: the fb504 exp hook
// (terrain-ui-exp.js in TEMP) -- its result file appears on the first editor tick after
// pageReady, uniformly for both opens (the keep-alive implementation must re-arm the hook on
// reattach). The PS driver samples msedgewebview2 CPU between the HIDDEN-PHASE markers.
// Today (no cache) REOPEN reads as a full cold boot; the bar is REOPEN < 600 ms, hidden CPU ~0.
static int reopenMode (Host& host, int blk)
{
    auto inst = host.make (blk);
    if (inst == nullptr) return 1;
    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (SR, blk);
    inst->prepareToPlay (SR, blk);

    const auto expFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("terrain-ui-exp.js");
    const auto resFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("terrain-ui-exp-result.txt");

    juce::AudioProcessorEditor* ed = nullptr;
    std::unique_ptr<juce::DocumentWindow> win;

    auto openEd = [&] (const char* tag, double* msOut) -> bool
    {
        resFile.deleteFile();
        expFile.replaceWithText (juce::String ("\'") + tag + "\'");
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        ed = inst->createEditorIfNeeded();
        if (ed == nullptr) { std::printf ("  !! no editor\n"); return false; }
        win = std::make_unique<juce::DocumentWindow> ("Terrain reopen harness", juce::Colours::black,
                                                      juce::DocumentWindow::allButtons);
        win->setUsingNativeTitleBar (true);
        win->setContentNonOwned (ed, true);
        win->centreWithSize (ed->getWidth(), ed->getHeight());
        win->setVisible (true);
        win->setAlwaysOnTop (true);
        win->toFront (true);
        while (! resFile.existsAsFile())
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            if (juce::Time::getMillisecondCounterHiRes() - t0 > 30000.0)
            { std::printf ("  !! %s: page never became ready (30 s)\n", tag); return false; }
        }
        *msOut = juce::Time::getMillisecondCounterHiRes() - t0;
        return true;
    };
    auto closeEd = [&]
    {
        // fb516d ORDER LAW (matches FL): HIDE the window first -- the shell's showing-watcher
        // parks the core synchronously inside this call, while the window's HWND is still alive
        // (the park log proved every later moment is too late: IsWindow(old)=0 by dtor time).
        // Then detach + delete the editor, then destroy the window.
        if (win != nullptr) { win->setVisible (false); }
        if (win != nullptr) { win->clearContentComponent(); }
        if (ed != nullptr)  { inst->editorBeingDeleted (ed); delete ed; ed = nullptr; }
        win.reset();
    };

    double open1 = 0.0, reopen = 0.0;
    if (! openEd ("r1", &open1)) return 1;
    std::printf ("  OPEN1   %8.0f ms   (ws %6.0f MB)\n", open1, workingSetMB());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (800);   // settle; never close mid-boot
    closeEd();

    std::printf ("  HIDDEN-PHASE-START  (ws %6.0f MB after close)\n", workingSetMB());
    std::fflush (stdout);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (8000);
    std::printf ("  HIDDEN-PHASE-END    (ws %6.0f MB)\n", workingSetMB());
    std::fflush (stdout);

    if (! openEd ("r2", &reopen)) return 1;
    std::printf ("  REOPEN  %8.0f ms   (ws %6.0f MB)\n", reopen, workingSetMB());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    closeEd();

    const char* cache = std::getenv ("TERRAIN_UI_CACHE");
    std::printf ("  CACHE   %s\n", cache != nullptr ? cache : "(default)");
    std::printf ("  verdict: %s\n\n", reopen < 600.0 ? "REOPEN IS INSTANT-CLASS (< 600 ms)"
                                                           : "reopen is a cold boot");
    inst->releaseResources();
    inst.reset();
    return 0;
}

// __ fb519: TWO-INSTANCE keep-alive acceptance _____________________________________________
// Both instances open (cold), both close (park), then each reopens -- with the default cap
// BOTH must be instant-class; with TERRAIN_UI_CACHE=1 the older park is evicted and its
// reopen must be a cold boot (eviction still works). The driver sets the env per run.
static int reopen2Mode (Host& host, int blk)
{
    auto instA = host.make (blk);
    auto instB = host.make (blk);
    if (instA == nullptr || instB == nullptr) return 1;
    for (auto* inst : { instA.get(), instB.get() })
    {
        inst->enableAllBuses();
        inst->setRateAndBufferSizeDetails (SR, blk);
        inst->prepareToPlay (SR, blk);
    }

    const auto expFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("terrain-ui-exp.js");
    const auto resFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("terrain-ui-exp-result.txt");

    struct Slot { juce::AudioProcessorEditor* ed = nullptr; std::unique_ptr<juce::DocumentWindow> win; };
    Slot slots[2];

    auto openEd = [&] (int i, juce::AudioPluginInstance& inst, const char* tag, double* msOut) -> bool
    {
        resFile.deleteFile();
        expFile.replaceWithText (juce::String ("\'") + tag + "\'");
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        slots[i].ed = inst.createEditorIfNeeded();
        if (slots[i].ed == nullptr) { std::printf ("  !! no editor (%s)\n", tag); return false; }
        slots[i].win = std::make_unique<juce::DocumentWindow> ("Terrain reopen2", juce::Colours::black,
                                                               juce::DocumentWindow::allButtons);
        slots[i].win->setUsingNativeTitleBar (true);
        slots[i].win->setContentNonOwned (slots[i].ed, true);
        slots[i].win->centreWithSize (slots[i].ed->getWidth(), slots[i].ed->getHeight());
        slots[i].win->setVisible (true);
        slots[i].win->setAlwaysOnTop (true);
        slots[i].win->toFront (true);
        while (! resFile.existsAsFile())
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            if (juce::Time::getMillisecondCounterHiRes() - t0 > 40000.0)
            { std::printf ("  !! %s: page never became ready (40 s)\n", tag); return false; }
        }
        *msOut = juce::Time::getMillisecondCounterHiRes() - t0;
        return true;
    };
    auto closeEd = [&] (int i, juce::AudioPluginInstance& inst)
    {
        if (slots[i].win != nullptr) { slots[i].win->setVisible (false); slots[i].win->clearContentComponent(); }
        if (slots[i].ed != nullptr)  { inst.editorBeingDeleted (slots[i].ed); delete slots[i].ed; slots[i].ed = nullptr; }
        slots[i].win.reset();
    };

    double a1=0, b1=0, a2=0, b2=0;
    if (! openEd (0, *instA, "A1", &a1)) return 1;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
    closeEd (0, *instA);
    if (! openEd (1, *instB, "B1", &b1)) return 1;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
    closeEd (1, *instB);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);

    if (! openEd (0, *instA, "A2", &a2)) return 1;   // the OLDER park -- the fb519 acceptance case
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    closeEd (0, *instA);
    if (! openEd (1, *instB, "B2", &b2)) return 1;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    closeEd (1, *instB);

    const char* cache = std::getenv ("TERRAIN_UI_CACHE");
    std::printf ("  OPEN  A1 %6.0f ms   B1 %6.0f ms   (cold)\n", a1, b1);
    std::printf ("  REOPEN A2 %6.0f ms   B2 %6.0f ms   (cache=%s)\n", a2, b2, cache != nullptr ? cache : "default");
    const bool bothInstant = a2 < 600.0 && b2 < 600.0;
    const bool oldestCold  = a2 > 900.0 && b2 < 600.0;
    std::printf ("  verdict: %s\n\n", bothInstant ? "BOTH INSTANT -- every instance keeps its park"
                                          : oldestCold ? "OLDEST EVICTED (cap respected), newest instant"
                                                       : "UNEXPECTED -- inspect");
    for (auto* inst : { instA.get(), instB.get() }) inst->releaseResources();
    instB.reset(); instA.reset();
    return 0;
}

int main (int argc, char** argv)
{
    // --editor creates a real window and hands the exit path to JUCE's message loop, which does
    // not flush a redirected stdout on the way out: the whole run printed NOTHING under
    // PowerShell while still exiting 0. Unbuffered from the first byte, so a mode that dies
    // mid-way still shows what it managed to say.
    setvbuf (stdout, nullptr, _IONBF, 0);

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String path = kDefaultPath;
    bool wantMem = false;
    bool wantModal = false;
    bool wantModalLive = false;
    int  holdBlk = 0; double holdSec = 25.0; bool ftz = false;
    int  edBlk = 0; bool edOpen = true;
    bool playEd = false;
    bool wantReopen = false;   // fb516 -- keep-alive acceptance harness
    bool wantReopen2 = false;  // fb519 -- TWO instances, both must reopen instant
    bool wantParams = false; juce::String paramFilter;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--mem") wantMem = true;
        else if (a == "--modal") wantModal = true;
        else if (a == "--modal-live") wantModalLive = true;
        else if (a == "--ftz") ftz = true;
        else if (a.startsWith ("--editor=")) edBlk = a.substring (9).getIntValue();
        else if (a == "--noeditor") { edOpen = false; if (edBlk == 0) edBlk = 512; }
        else if (a == "--play") playEd = true;
        else if (a == "--reopen") wantReopen = true;   // fb516
        else if (a == "--reopen2") wantReopen2 = true;  // fb519
        else if (a.startsWith ("--params")) { wantParams = true; if (a.startsWith ("--params=")) paramFilter = a.substring (9); }
        else if (a.startsWith ("--hold=")) holdBlk = a.substring (7).getIntValue();
        else if (a.startsWith ("--secs=")) holdSec = a.substring (7).getDoubleValue();
        else if (a.isNotEmpty() && ! a.startsWith ("--")) path = a;
    }

    std::printf ("\n  Terrain — Windows block-size / memory harness\n  plugin: %s\n", path.toRawUTF8());

    Host host;
    if (! host.init (path)) return 1;

    if (wantMem) return memoryMode (host);
    if (wantModal) return modalMode (host);
    if (wantModalLive) return modalLiveMode (host);
    if (holdBlk > 0) return holdMode (host, holdBlk, holdSec, ftz);
    if (wantReopen2) return reopen2Mode (host, edBlk > 0 ? edBlk : 512); // fb519
    if (wantReopen) return reopenMode (host, edBlk > 0 ? edBlk : 512);   // fb516
    if (edBlk > 0) return editorMode (host, edBlk, holdSec, edOpen, playEd);
    if (wantParams) return paramsMode (host, paramFilter);

    std::printf ("\n  IDLE DSP cost vs HOST BLOCK SIZE (no notes, no editor, %.0f s per point, %.0f Hz)\n\n",
                 SECONDS, SR);
    const int sizes[5] = { 45, 88, 128, 256, 512 };
    double v[5] = { 0 };
    for (int i = 0; i < 5; ++i)
    {
        bool ok = false;
        v[i] = measure (host, sizes[i], &ok);
        std::printf ("    blk %4d  (%6.0f calls/s)   %6.2f%% of one core%s\n",
                     sizes[i], SR / sizes[i], v[i], ok ? "" : "   <- FAILED");
        std::fflush (stdout);
    }

    std::printf ("\n    peak working set %.0f MB\n", peakWorkingSetMB());

    if (v[4] > 0.0)
    {
        const double ratio = v[0] / v[4];
        std::printf ("\n    cost(45) / cost(512) = %.2fx\n", ratio);
        std::printf ("    %s\n\n", ratio < 1.6 ? "FLAT ENOUGH — the gather no longer rides the host's call rate."
                                               : "STILL SLOPED — fixed per-call cost still dominates.");
        return ratio < 1.6 ? 0 : 1;
    }
    return 1;
}
