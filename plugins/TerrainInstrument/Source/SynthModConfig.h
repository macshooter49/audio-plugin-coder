#pragma once
// =============================================================================
//  SynthModConfig.h  —  Terrain Instrument · synth modulation data model (Batch 1)
//  Waves Crate
//
//  Header-only, NO JUCE. Shared by SynthVoice (audio) and the unit test. Defines:
//    - ModSource / ModDest enums (the matrix axes)
//    - SyncDivisions table + syncedHz()  (tempo -> Hz, the locked math)
//    - Assignment (one route) + ModConfig (the whole table, 32 slots)
//    - per-destination domain + full-scale span (how a normalized mod maps to the
//      parameter's real units — semitones for cutoff in Batch 1)
//
//  Batch 1 wires ONE route end to end: L1 -> Cut1 (filter-1 cutoff, semitone domain).
//  Every other source/dest exists in the enum so Batch 2/3 only add behaviour,
//  never reshuffle indices (saved projects stay valid — same discipline as the
//  FX ModulationEngine's ParamIndex).
// =============================================================================

#include "SynthLFO.h"
#include <cmath>
#include <algorithm>

namespace wc
{

// ── Sources (what modulates). LFOs bipolar [-1,1]; envelopes unipolar [0,1]. ──
enum class ModSource : int
{
    L1 = 0, L2, L3, L4, L5,    // per-voice LFOs 1..5
    L6, L7, L8, L9, L10,       // mod matrix — LFOs 6..10 (contiguous so L1+idx works for all 10)
    EnvAmp, EnvFilter,         // the two "always-on" envelopes
    EnvMod1, EnvMod2,          // the spare envelopes
    Velocity, Note,            // (Batch 2+)
    NumSources
};
static constexpr int NUM_LFOS = 10;

// ── Destinations (what gets modulated). Each maps to a SynthVoice effective member. ──
enum class ModDest : int
{
    Cut1 = 0, Cut2,            // filter cutoffs (semitone domain) — Cut1 wired in Batch 1
    Frame, Warp, Fold,         // OSC A wavetable frame / warp / fold (linear 0..1)
    Pitch,                     // semitones
    Level, Pan,                // linear
    FlowTime,                  // FLOW · TIME (linear)
    // ── Mod-matrix stage (append-only; never reshuffle — saved projects stay valid) ──
    FrameB, WarpB, FoldB,      // OSC B wavetable frame / warp / fold (linear 0..1)
    LfoAmt1, LfoAmt2, LfoAmt3, LfoAmt4, LfoAmt5,        // LFO→LFO: scales the target LFO's output
    LfoAmt6, LfoAmt7, LfoAmt8, LfoAmt9, LfoAmt10,       // (LFOs 6..10) — LfoAmt{n} stays at dest 11+n
    FlowGate, FlowVary, FlowTraj, FlowMorph,            // FLOW knobs 2..5 (FlowTime already above) — append-only
    NumDests
};

// How a normalized modulation sum is converted to the parameter's real units.
enum class ModDomain : int { Semitone = 0, Linear01, Bipolar };

struct DestInfo
{
    ModDomain domain;
    float     fullScale;   // value at depth=1, source=+1 (units depend on domain)
};

// Per-destination domain + full-scale span. Tunable; indices match ModDest.
static constexpr DestInfo kDestInfo[(int) ModDest::NumDests] = {
    { ModDomain::Semitone, 48.0f },  // Cut1: +/-48 ST (4 oct) at full depth — musical, never 0 Hz
    { ModDomain::Semitone, 48.0f },  // Cut2
    { ModDomain::Linear01,  1.0f },  // Frame
    { ModDomain::Linear01,  1.0f },  // Warp
    { ModDomain::Linear01,  1.0f },  // Fold
    { ModDomain::Semitone, 24.0f },  // Pitch: +/-24 ST (2 oct) at full depth
    { ModDomain::Linear01,  1.0f },  // Level
    { ModDomain::Bipolar,   1.0f },  // Pan
    { ModDomain::Linear01,  1.0f },  // FlowTime
    { ModDomain::Linear01,  1.0f },  // FrameB
    { ModDomain::Linear01,  1.0f },  // WarpB
    { ModDomain::Linear01,  1.0f },  // FoldB
    { ModDomain::Linear01,  1.0f },  // LfoAmt1
    { ModDomain::Linear01,  1.0f },  // LfoAmt2
    { ModDomain::Linear01,  1.0f },  // LfoAmt3
    { ModDomain::Linear01,  1.0f },  // LfoAmt4
    { ModDomain::Linear01,  1.0f },  // LfoAmt5
    { ModDomain::Linear01,  1.0f },  // LfoAmt6
    { ModDomain::Linear01,  1.0f },  // LfoAmt7
    { ModDomain::Linear01,  1.0f },  // LfoAmt8
    { ModDomain::Linear01,  1.0f },  // LfoAmt9
    { ModDomain::Linear01,  1.0f },  // LfoAmt10
    { ModDomain::Linear01,  1.0f },  // FlowGate
    { ModDomain::Linear01,  1.0f },  // FlowVary
    { ModDomain::Linear01,  1.0f },  // FlowTraj
    { ModDomain::Linear01,  1.0f },  // FlowMorph
};

// ── Tempo-sync divisions. beatsPerCycle = quarter-notes spanned by one LFO cycle. ──
//   syncedHz = (BPM/60) / beatsPerCycle.  Straight 1/4 -> 1 beat -> 2 Hz @ 120.
//   Triplet = straight beats * 2/3 (faster). Dotted = straight beats * 1.5 (slower).
struct SyncDiv { const char* name; float beatsPerCycle; };
static constexpr SyncDiv kSyncDivisions[] = {
    { "8 bar",  32.0f }, { "4 bar", 16.0f }, { "2 bar", 8.0f }, { "1 bar", 4.0f },
    { "1/2",     2.0f }, { "1/4",    1.0f }, { "1/8",   0.5f }, { "1/16",  0.25f }, { "1/32", 0.125f },
    { "1/4.",    1.5f }, { "1/8.",   0.75f},                                   // dotted
    { "1/4T",    1.0f * 2.0f/3.0f }, { "1/8T", 0.5f * 2.0f/3.0f }, { "1/16T", 0.25f * 2.0f/3.0f } // triplet
};
static constexpr int kNumSyncDivisions = (int) (sizeof (kSyncDivisions) / sizeof (SyncDiv));

inline float syncedHz (int syncIdx, float bpm) noexcept
{
    if (bpm <= 0.0f) bpm = 120.0f;
    if (syncIdx < 0) syncIdx = 0;
    if (syncIdx >= kNumSyncDivisions) syncIdx = kNumSyncDivisions - 1;
    return (bpm / 60.0f) / kSyncDivisions[syncIdx].beatsPerCycle;
}

// ── One route. depth is BIPOLAR [-1,+1]; negative inverts the source. ──
struct Assignment
{
    ModSource source  = ModSource::L1;
    ModDest   dest    = ModDest::Cut1;
    float     depth   = 0.0f;           // -1..+1  (UI shows as -100..+100%)
    ModSource auxSource = ModSource::L1; // (Batch 3) scales depth; ignored in Batch 1
    bool      useAux  = false;
    bool      enabled = false;
};

static constexpr int MAX_ASSIGNMENTS = 32;

// The whole modulation table. Published to the audio thread as an immutable copy.
struct ModConfig
{
    LFOSettings lfos[NUM_LFOS];
    Assignment  assignments[MAX_ASSIGNMENTS];
    int         numAssignments = 0;
};

// ---------------------------------------------------------------------------
//  Accumulation helpers — the "base + sum(source*depth)" model, clamp ONCE.
//  These are pure functions so the unit test and the voice share identical math.
// ---------------------------------------------------------------------------

// Convert one route's contribution into the destination's natural units.
//   sourceValue : the source's current output (LFO bipolar, env unipolar)
//   depth       : route depth -1..+1
//   returns     : units = semitones (Semitone), or normalized delta (Linear01/Bipolar)
inline float routeContribution (const DestInfo& info, float sourceValue, float depth) noexcept
{
    return sourceValue * depth * info.fullScale;
}

// Apply an accumulated SEMITONE modulation to a base frequency (cutoff/pitch).
//   freqOut = baseHz * 2^(semitones/12).  Symmetric in octaves; never reaches 0 Hz.
inline float applySemitones (float baseHz, float semitones) noexcept
{
    return baseHz * std::exp2 (semitones / 12.0f);
}

// Clamp helper (clamp ONCE, after summing all routes).
inline float clampRange (float v, float lo, float hi) noexcept
{
    return std::max (lo, std::min (hi, v));
}

} // namespace wc
