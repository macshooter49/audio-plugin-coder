// ══════════════════════════════════════════════════════════════════════════════════════════════
//  fb498 — WHERE THE 1.4 GB ACTUALLY IS
//
//  Tests/win_blk_cpu.cpp --mem measured, on this machine, that one Terrain instance costs
//  1,462 MB at CONSTRUCTION and only 232 MB more at prepareToPlay. That inverts the handoff's
//  memory plan: releaseResources() can only ever give back the 232 MB, so the eager-allocation
//  story has to be about what the voices cost simply by EXISTING.
//
//  tw::SynthVoice is `= default` constructed and header-only, so every one of those bytes is an
//  INLINE MEMBER, and sizeof() answers the question exactly and for free -- no profiler, no
//  guessing. This target links no plugin code (that would duplicate every JUCE symbol); it just
//  includes the header and prints the map.
#include "SynthVoice.h"
#include "PluginProcessor.h"
#include <cstdio>

// fb605 — THE PROCESSOR CLASS CARRIES THE PRODUCT NAME, AND THE PRODUCT WAS RENAMED THIS BUILD
// (Terrain Instrument -> Terrain). sizeof() has to NAME a type, so this harness is the one file in
// Tests/ that cannot be spelling-agnostic. It is parameterised instead of hardcoded, so whichever
// side of the class rename you compile against, one flag fixes it rather than an edit:
//     -DTERRAIN_PROC=TerrainAudioProcessor            (post-class-rename tree)
//     -DTERRAIN_PROC=TerrainInstrumentAudioProcessor  (pre-fb605 tree)
// The class name is PRESENTATION, not identity — PLUGIN_CODE / PLUGIN_MANUFACTURER_CODE and the
// VST3 class UID are what every saved session keys on, and Tests/identity_lock_gate.py pins those.
#define TP_STR2(x) #x
#define TP_STR(x)  TP_STR2(x)
#ifndef TERRAIN_PROC
 #define TERRAIN_PROC TerrainAudioProcessor
#endif

// PluginProcessor.h:1414 kSynthVoiceCount = 96. NOT kNumVoices (1408) = 32, which is the
// SAMPLER voice count per layer — mixing the two understates every synth-voice total by 3x.
static constexpr int kVoices = 96;
static constexpr int kUni    = tw::SynthVoice::kMaxUnison;
static constexpr int kOscs   = 4;    // A B C D

static double mb (double bytes) { return bytes / (1024.0 * 1024.0); }

static void row (const char* name, size_t each, int count)
{
    const double total = (double) each * (double) count;
    std::printf ("    %-28s %10zu B each  x %5d  = %9.1f MB   (x%d voices = %8.1f MB)\n",
                 name, each, count, mb (total), kVoices, mb (total * kVoices));
}

int main()
{
    std::printf ("\n  TERRAIN VOICE MEMORY MAP  (sizeof, per voice unless noted)\n\n");
    std::printf ("    sizeof(%s) = %zu B = %.1f MB   <- paid at construction\n\n",
                 TP_STR(TERRAIN_PROC),
                 sizeof (TERRAIN_PROC),
                 mb ((double) sizeof (TERRAIN_PROC)));
    std::printf ("    kNumVoices = %d, kMaxUnison = %d, oscs = %d\n\n", kVoices, kUni, kOscs);

    std::printf ("    sizeof(tw::SynthVoice)       %10zu B  = %8.1f MB\n",
                 sizeof (tw::SynthVoice), mb ((double) sizeof (tw::SynthVoice)));
    std::printf ("    x %d voices                                = %8.1f MB\n\n",
                 kVoices, mb ((double) sizeof (tw::SynthVoice) * kVoices));

    std::printf ("  PER-OSC ENGINE ARRAYS  (each held as std::array<T, kMaxUnison> x %d oscs)\n\n", kOscs);
    row ("tw::ModalEngine",     sizeof (tw::ModalEngine),     kUni * kOscs);
    row ("tw::HarmonicEngine",  sizeof (tw::HarmonicEngine),  kUni * kOscs);

    std::printf ("\n  OTHER ENGINE TYPES (count per voice varies -- shown x1 osc for scale)\n\n");
    row ("tw::SampleEngine",    sizeof (tw::SampleEngine),    kOscs);
    row ("tw::GranularEngine",  sizeof (tw::GranularEngine),  kOscs);
    row ("tw::GeodeEngine",     sizeof (tw::GeodeEngine),     kOscs);

    std::printf ("\n  NOTE: ModalEngine's SIX waveguide delay lines are std::vector, so they are NOT\n");
    std::printf ("        in sizeof -- they are heap, allocated by ModalEngine::prepare()\n");
    std::printf ("        (ModalEngine.h:107-112, kMaxDelay=%d floats each).\n", tw::modal::kMaxDelay);
    const double dlEach  = 6.0 * (double) tw::modal::kMaxDelay * sizeof (float);
    const double dlVoice = dlEach * kUni * kOscs;
    std::printf ("        heap per ModalEngine  = %8.1f KB\n", dlEach / 1024.0);
    std::printf ("        heap per voice        = %8.1f MB   (%d engines)\n", mb (dlVoice), kUni * kOscs);
    std::printf ("        heap x %d voices      = %8.1f MB\n\n", kVoices, mb (dlVoice * kVoices));
    return 0;
}
