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
    Drift1, Drift2, Drift3, Drift4, Drift5, Drift6, Drift7, Drift8,  // FLOW·DRIFT lanes (block-rate bipolar) — append-only (replaced dead SeqMod)
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
    FrameC, WarpC, FoldC,      // OSC C wavetable frame / warp / fold (linear 0..1) — Phase 4B
    FrameD, WarpD, FoldD,      // OSC D wavetable frame / warp / fold (linear 0..1) — Phase 4B
    ChopRate, ChopGate, ChopVary, ChopTraj, ChopMorph,   // FLOW · CHOP macros (mode 2) — append-only
    ResoStructure, ResoBrightness, ResoDamping, ResoPosition, ResoMix,  // ANNULUS resonator macros — append-only
    CoarseA, CoarseB, CoarseC, CoarseD,                  // per-osc COARSE pitch lane (semitones) — append-only
    SubWeightA, SubWeightB, SubWeightC, SubWeightD,      // per-osc SUB level (linear 0..1)
    SubHeatA, SubHeatB, SubHeatC, SubHeatD,              // per-osc SUB drive (linear 0..1)
    // ── fb75 — UNIVERSAL LFO MOD batch (append-only; block-rate, applied at the processor
    //    param-push site — NOT in SynthVoice). Everything from Res1 down modulates the value
    //    the processor pushes each block: filters 1/2 (res/env/drive/post-drive/mix), per-osc
    //    level+pan, the noise module, the 16 blend-slot depths, and every engine macro knob
    //    (family-major, 4 oscs per family: dest = FamilyA + oscIdx). EXCLUDED on purpose (CPU):
    //    FRAME_SPREAD/BLUR + SPECTRAL_AMT (table re-renders) and the blend-bake knobs (offline). ──
    Res1, Res2, FEnv1, FEnv2,
    FDrv1, FDrv2, FPDrv1, FPDrv2,
    FMix1, FMix2, LevelA, LevelB,
    LevelC, LevelD, PanA, PanB,
    PanC, PanD, NoiseLevel, NoiseScan,
    NoisePan, NoiseWidth, BlendDepthA1, BlendDepthA2,
    BlendDepthA3, BlendDepthA4, BlendDepthB1, BlendDepthB2,
    BlendDepthB3, BlendDepthB4, BlendDepthC1, BlendDepthC2,
    BlendDepthC3, BlendDepthC4, BlendDepthD1, BlendDepthD2,
    BlendDepthD3, BlendDepthD4, SampScanA, SampScanB,
    SampScanC, SampScanD, SampStretchA, SampStretchB,
    SampStretchC, SampStretchD, SampFormantA, SampFormantB,
    SampFormantC, SampFormantD, SampAirA, SampAirB,
    SampAirC, SampAirD, SampSprayA, SampSprayB,
    SampSprayC, SampSprayD, SampXfadeA, SampXfadeB,
    SampXfadeC, SampXfadeD, GrainPosA, GrainPosB,
    GrainPosC, GrainPosD, GrainDensityA, GrainDensityB,
    GrainDensityC, GrainDensityD, GrainSizeA, GrainSizeB,
    GrainSizeC, GrainSizeD, GrainPitchA, GrainPitchB,
    GrainPitchC, GrainPitchD, GrainSprayA, GrainSprayB,
    GrainSprayC, GrainSprayD, GrainPSprayA, GrainPSprayB,
    GrainPSprayC, GrainPSprayD, GrainShapeA, GrainShapeB,
    GrainShapeC, GrainShapeD, GrainSkewA, GrainSkewB,
    GrainSkewC, GrainSkewD, GrainWidthA, GrainWidthB,
    GrainWidthC, GrainWidthD, GrainScanA, GrainScanB,
    GrainScanC, GrainScanD, GeoQualityA, GeoQualityB,
    GeoQualityC, GeoQualityD, GeoFormantA, GeoFormantB,
    GeoFormantC, GeoFormantD, GeoTiltA, GeoTiltB,
    GeoTiltC, GeoTiltD, GeoCrushA, GeoCrushB,
    GeoCrushC, GeoCrushD, GeoStartA, GeoStartB,
    GeoStartC, GeoStartD, GeoMeltA, GeoMeltB,
    GeoMeltC, GeoMeltD, GeoScanA, GeoScanB,
    GeoScanC, GeoScanD, GeoCutA, GeoCutB,
    GeoCutC, GeoCutD, GeoShapeA, GeoShapeB,
    GeoShapeC, GeoShapeD, GeoStretchA, GeoStretchB,
    GeoStretchC, GeoStretchD, GeoDriveA, GeoDriveB,
    GeoDriveC, GeoDriveD, GeoSieveA, GeoSieveB,
    GeoSieveC, GeoSieveD, FmStrikeA, FmStrikeB,
    FmStrikeC, FmStrikeD, FmAgeA, FmAgeB,
    FmAgeC, FmAgeD, FmRustA, FmRustB,
    FmRustC, FmRustD, FmGaleA, FmGaleB,
    FmGaleC, FmGaleD, FmBendA, FmBendB,
    FmBendC, FmBendD, FmStormA, FmStormB,
    FmStormC, FmStormD, FmFbA, FmFbB,
    FmFbC, FmFbD, HarmHueA, HarmHueB,
    HarmHueC, HarmHueD, HarmCountA, HarmCountB,
    HarmCountC, HarmCountD, HarmLeanA, HarmLeanB,
    HarmLeanC, HarmLeanD, HarmFanA, HarmFanB,
    HarmFanC, HarmFanD, HarmGritA, HarmGritB,
    HarmGritC, HarmGritD, HarmBraidA, HarmBraidB,
    HarmBraidC, HarmBraidD, HarmCarveA, HarmCarveB,
    HarmCarveC, HarmCarveD, HarmChurnA, HarmChurnB,
    HarmChurnC, HarmChurnD, HarmRootA, HarmRootB,
    HarmRootC, HarmRootD, HarmShineA, HarmShineB,
    HarmShineC, HarmShineD, HarmWiltA, HarmWiltB,
    HarmWiltC, HarmWiltD, HarmFizzA, HarmFizzB,
    HarmFizzC, HarmFizzD,
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
    { ModDomain::Linear01,  1.0f },  // FrameC
    { ModDomain::Linear01,  1.0f },  // WarpC
    { ModDomain::Linear01,  1.0f },  // FoldC
    { ModDomain::Linear01,  1.0f },  // FrameD
    { ModDomain::Linear01,  1.0f },  // WarpD
    { ModDomain::Linear01,  1.0f },  // FoldD
    { ModDomain::Linear01,  1.0f },  // ChopRate
    { ModDomain::Linear01,  1.0f },  // ChopGate
    { ModDomain::Linear01,  1.0f },  // ChopVary
    { ModDomain::Linear01,  1.0f },  // ChopTraj
    { ModDomain::Linear01,  1.0f },  // ChopMorph
    // BUGFIX (2026-07-09): the 5 ANNULUS entries were MISSING — value-init gave them
    // fullScale 0, so Reso mod routes contributed exactly nothing. Now they modulate.
    { ModDomain::Linear01,  1.0f },  // ResoStructure
    { ModDomain::Linear01,  1.0f },  // ResoBrightness
    { ModDomain::Linear01,  1.0f },  // ResoDamping
    { ModDomain::Linear01,  1.0f },  // ResoPosition
    { ModDomain::Linear01,  1.0f },  // ResoMix
    { ModDomain::Semitone, 24.0f },  // CoarseA: ±24 st at full depth (matches Pitch)
    { ModDomain::Semitone, 24.0f },  // CoarseB
    { ModDomain::Semitone, 24.0f },  // CoarseC
    { ModDomain::Semitone, 24.0f },  // CoarseD
    { ModDomain::Linear01,  1.0f },  // SubWeightA
    { ModDomain::Linear01,  1.0f },  // SubWeightB
    { ModDomain::Linear01,  1.0f },  // SubWeightC
    { ModDomain::Linear01,  1.0f },  // SubWeightD
    { ModDomain::Linear01,  1.0f },  // SubHeatA
    { ModDomain::Linear01,  1.0f },  // SubHeatB
    { ModDomain::Linear01,  1.0f },  // SubHeatC
    { ModDomain::Linear01,  1.0f },  // SubHeatD
    // ── fb75 batch (indices continue append-only; block-rate processor-side dests) ──
    { ModDomain::Linear01,  1.0f },  // Res1: filter 1 resonance
    { ModDomain::Linear01,  1.0f },  // Res2: filter 2 resonance
    { ModDomain::Linear01,  1.0f },  // FEnv1: filter 1 env amount (bipolar param, clamped at site)
    { ModDomain::Linear01,  1.0f },  // FEnv2: filter 2 env amount
    { ModDomain::Linear01,  1.0f },  // FDrv1: filter 1 front drive
    { ModDomain::Linear01,  1.0f },  // FDrv2: filter 2 front drive
    { ModDomain::Linear01,  1.0f },  // FPDrv1: filter 1 back post-drive
    { ModDomain::Linear01,  1.0f },  // FPDrv2: filter 2 back post-drive
    { ModDomain::Linear01,  1.0f },  // FMix1: filter 1 wet/dry mix (the F1 pill)
    { ModDomain::Linear01,  1.0f },  // FMix2: filter 2 wet/dry mix (the F2 pill)
    { ModDomain::Linear01,  1.0f },  // LevelA: osc A level
    { ModDomain::Linear01,  1.0f },  // LevelB: osc B level
    { ModDomain::Linear01,  1.0f },  // LevelC: osc C level
    { ModDomain::Linear01,  1.0f },  // LevelD: osc D level
    { ModDomain::Bipolar,  1.0f },  // PanA: osc A pan
    { ModDomain::Bipolar,  1.0f },  // PanB: osc B pan
    { ModDomain::Bipolar,  1.0f },  // PanC: osc C pan
    { ModDomain::Bipolar,  1.0f },  // PanD: osc D pan
    { ModDomain::Linear01,  1.0f },  // NoiseLevel: noise level
    { ModDomain::Linear01,  1.0f },  // NoiseScan: noise scan rate
    { ModDomain::Linear01,  1.0f },  // NoisePan: noise pan (0..1, 0.5 = C)
    { ModDomain::Linear01,  1.0f },  // NoiseWidth: noise stereo width (0..2)
    { ModDomain::Linear01,  1.0f },  // BlendDepthA1: osc A blend slot 1 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthA2: osc A blend slot 2 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthA3: osc A blend slot 3 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthA4: osc A blend slot 4 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthB1: osc B blend slot 1 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthB2: osc B blend slot 2 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthB3: osc B blend slot 3 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthB4: osc B blend slot 4 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthC1: osc C blend slot 1 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthC2: osc C blend slot 2 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthC3: osc C blend slot 3 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthC4: osc C blend slot 4 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthD1: osc D blend slot 1 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthD2: osc D blend slot 2 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthD3: osc D blend slot 3 depth
    { ModDomain::Linear01,  1.0f },  // BlendDepthD4: osc D blend slot 4 depth
    { ModDomain::Bipolar,  1.0f },  // SampScanA: sample scan rate (±1) — osc A
    { ModDomain::Bipolar,  1.0f },  // SampScanB: sample scan rate (±1) — osc B
    { ModDomain::Bipolar,  1.0f },  // SampScanC: sample scan rate (±1) — osc C
    { ModDomain::Bipolar,  1.0f },  // SampScanD: sample scan rate (±1) — osc D
    { ModDomain::Linear01,  1.0f },  // SampStretchA: sample stretch — osc A
    { ModDomain::Linear01,  1.0f },  // SampStretchB: sample stretch — osc B
    { ModDomain::Linear01,  1.0f },  // SampStretchC: sample stretch — osc C
    { ModDomain::Linear01,  1.0f },  // SampStretchD: sample stretch — osc D
    { ModDomain::Linear01,  1.0f },  // SampFormantA: sample formant — osc A
    { ModDomain::Linear01,  1.0f },  // SampFormantB: sample formant — osc B
    { ModDomain::Linear01,  1.0f },  // SampFormantC: sample formant — osc C
    { ModDomain::Linear01,  1.0f },  // SampFormantD: sample formant — osc D
    { ModDomain::Linear01,  1.0f },  // SampAirA: sample air — osc A
    { ModDomain::Linear01,  1.0f },  // SampAirB: sample air — osc B
    { ModDomain::Linear01,  1.0f },  // SampAirC: sample air — osc C
    { ModDomain::Linear01,  1.0f },  // SampAirD: sample air — osc D
    { ModDomain::Linear01,  1.0f },  // SampSprayA: sample spray — osc A
    { ModDomain::Linear01,  1.0f },  // SampSprayB: sample spray — osc B
    { ModDomain::Linear01,  1.0f },  // SampSprayC: sample spray — osc C
    { ModDomain::Linear01,  1.0f },  // SampSprayD: sample spray — osc D
    { ModDomain::Linear01,  1.0f },  // SampXfadeA: sample loop xfade — osc A
    { ModDomain::Linear01,  1.0f },  // SampXfadeB: sample loop xfade — osc B
    { ModDomain::Linear01,  1.0f },  // SampXfadeC: sample loop xfade — osc C
    { ModDomain::Linear01,  1.0f },  // SampXfadeD: sample loop xfade — osc D
    { ModDomain::Linear01,  1.0f },  // GrainPosA: grain position (start anchor) — osc A
    { ModDomain::Linear01,  1.0f },  // GrainPosB: grain position (start anchor) — osc B
    { ModDomain::Linear01,  1.0f },  // GrainPosC: grain position (start anchor) — osc C
    { ModDomain::Linear01,  1.0f },  // GrainPosD: grain position (start anchor) — osc D
    { ModDomain::Linear01,  1.0f },  // GrainDensityA: grain density — osc A
    { ModDomain::Linear01,  1.0f },  // GrainDensityB: grain density — osc B
    { ModDomain::Linear01,  1.0f },  // GrainDensityC: grain density — osc C
    { ModDomain::Linear01,  1.0f },  // GrainDensityD: grain density — osc D
    { ModDomain::Linear01,  1.0f },  // GrainSizeA: grain size — osc A
    { ModDomain::Linear01,  1.0f },  // GrainSizeB: grain size — osc B
    { ModDomain::Linear01,  1.0f },  // GrainSizeC: grain size — osc C
    { ModDomain::Linear01,  1.0f },  // GrainSizeD: grain size — osc D
    { ModDomain::Semitone,  24.0f },  // GrainPitchA: grain pitch (semitones) — osc A
    { ModDomain::Semitone,  24.0f },  // GrainPitchB: grain pitch (semitones) — osc B
    { ModDomain::Semitone,  24.0f },  // GrainPitchC: grain pitch (semitones) — osc C
    { ModDomain::Semitone,  24.0f },  // GrainPitchD: grain pitch (semitones) — osc D
    { ModDomain::Linear01,  1.0f },  // GrainSprayA: grain spray — osc A
    { ModDomain::Linear01,  1.0f },  // GrainSprayB: grain spray — osc B
    { ModDomain::Linear01,  1.0f },  // GrainSprayC: grain spray — osc C
    { ModDomain::Linear01,  1.0f },  // GrainSprayD: grain spray — osc D
    { ModDomain::Linear01,  1.0f },  // GrainPSprayA: grain pitch spray — osc A
    { ModDomain::Linear01,  1.0f },  // GrainPSprayB: grain pitch spray — osc B
    { ModDomain::Linear01,  1.0f },  // GrainPSprayC: grain pitch spray — osc C
    { ModDomain::Linear01,  1.0f },  // GrainPSprayD: grain pitch spray — osc D
    { ModDomain::Linear01,  1.0f },  // GrainShapeA: grain window shape — osc A
    { ModDomain::Linear01,  1.0f },  // GrainShapeB: grain window shape — osc B
    { ModDomain::Linear01,  1.0f },  // GrainShapeC: grain window shape — osc C
    { ModDomain::Linear01,  1.0f },  // GrainShapeD: grain window shape — osc D
    { ModDomain::Bipolar,  1.0f },  // GrainSkewA: grain window skew (±1) — osc A
    { ModDomain::Bipolar,  1.0f },  // GrainSkewB: grain window skew (±1) — osc B
    { ModDomain::Bipolar,  1.0f },  // GrainSkewC: grain window skew (±1) — osc C
    { ModDomain::Bipolar,  1.0f },  // GrainSkewD: grain window skew (±1) — osc D
    { ModDomain::Linear01,  1.0f },  // GrainWidthA: grain stereo width — osc A
    { ModDomain::Linear01,  1.0f },  // GrainWidthB: grain stereo width — osc B
    { ModDomain::Linear01,  1.0f },  // GrainWidthC: grain stereo width — osc C
    { ModDomain::Linear01,  1.0f },  // GrainWidthD: grain stereo width — osc D
    { ModDomain::Bipolar,  1.0f },  // GrainScanA: grain scan rate (±1) — osc A
    { ModDomain::Bipolar,  1.0f },  // GrainScanB: grain scan rate (±1) — osc B
    { ModDomain::Bipolar,  1.0f },  // GrainScanC: grain scan rate (±1) — osc C
    { ModDomain::Bipolar,  1.0f },  // GrainScanD: grain scan rate (±1) — osc D
    { ModDomain::Linear01,  1.0f },  // GeoQualityA: resynth quality — osc A
    { ModDomain::Linear01,  1.0f },  // GeoQualityB: resynth quality — osc B
    { ModDomain::Linear01,  1.0f },  // GeoQualityC: resynth quality — osc C
    { ModDomain::Linear01,  1.0f },  // GeoQualityD: resynth quality — osc D
    { ModDomain::Linear01,  1.0f },  // GeoFormantA: resynth formant — osc A
    { ModDomain::Linear01,  1.0f },  // GeoFormantB: resynth formant — osc B
    { ModDomain::Linear01,  1.0f },  // GeoFormantC: resynth formant — osc C
    { ModDomain::Linear01,  1.0f },  // GeoFormantD: resynth formant — osc D
    { ModDomain::Linear01,  1.0f },  // GeoTiltA: resynth tilt — osc A
    { ModDomain::Linear01,  1.0f },  // GeoTiltB: resynth tilt — osc B
    { ModDomain::Linear01,  1.0f },  // GeoTiltC: resynth tilt — osc C
    { ModDomain::Linear01,  1.0f },  // GeoTiltD: resynth tilt — osc D
    { ModDomain::Linear01,  1.0f },  // GeoCrushA: resynth crush (SILT) — osc A
    { ModDomain::Linear01,  1.0f },  // GeoCrushB: resynth crush (SILT) — osc B
    { ModDomain::Linear01,  1.0f },  // GeoCrushC: resynth crush (SILT) — osc C
    { ModDomain::Linear01,  1.0f },  // GeoCrushD: resynth crush (SILT) — osc D
    { ModDomain::Linear01,  1.0f },  // GeoStartA: resynth start (POSITION) — osc A
    { ModDomain::Linear01,  1.0f },  // GeoStartB: resynth start (POSITION) — osc B
    { ModDomain::Linear01,  1.0f },  // GeoStartC: resynth start (POSITION) — osc C
    { ModDomain::Linear01,  1.0f },  // GeoStartD: resynth start (POSITION) — osc D
    { ModDomain::Linear01,  1.0f },  // GeoMeltA: resynth melt (FRACTURE) — osc A
    { ModDomain::Linear01,  1.0f },  // GeoMeltB: resynth melt (FRACTURE) — osc B
    { ModDomain::Linear01,  1.0f },  // GeoMeltC: resynth melt (FRACTURE) — osc C
    { ModDomain::Linear01,  1.0f },  // GeoMeltD: resynth melt (FRACTURE) — osc D
    { ModDomain::Linear01,  1.0f },  // GeoScanA: resynth scan (CREEP) — osc A
    { ModDomain::Linear01,  1.0f },  // GeoScanB: resynth scan (CREEP) — osc B
    { ModDomain::Linear01,  1.0f },  // GeoScanC: resynth scan (CREEP) — osc C
    { ModDomain::Linear01,  1.0f },  // GeoScanD: resynth scan (CREEP) — osc D
    { ModDomain::Linear01,  1.0f },  // GeoCutA: resynth cut — osc A
    { ModDomain::Linear01,  1.0f },  // GeoCutB: resynth cut — osc B
    { ModDomain::Linear01,  1.0f },  // GeoCutC: resynth cut — osc C
    { ModDomain::Linear01,  1.0f },  // GeoCutD: resynth cut — osc D
    { ModDomain::Linear01,  1.0f },  // GeoShapeA: resynth shape (DISTILL) — osc A
    { ModDomain::Linear01,  1.0f },  // GeoShapeB: resynth shape (DISTILL) — osc B
    { ModDomain::Linear01,  1.0f },  // GeoShapeC: resynth shape (DISTILL) — osc C
    { ModDomain::Linear01,  1.0f },  // GeoShapeD: resynth shape (DISTILL) — osc D
    { ModDomain::Linear01,  1.0f },  // GeoStretchA: resynth stretch (FOSSIL) — osc A
    { ModDomain::Linear01,  1.0f },  // GeoStretchB: resynth stretch (FOSSIL) — osc B
    { ModDomain::Linear01,  1.0f },  // GeoStretchC: resynth stretch (FOSSIL) — osc C
    { ModDomain::Linear01,  1.0f },  // GeoStretchD: resynth stretch (FOSSIL) — osc D
    { ModDomain::Linear01,  1.0f },  // GeoDriveA: resynth drive (HAZE) — osc A
    { ModDomain::Linear01,  1.0f },  // GeoDriveB: resynth drive (HAZE) — osc B
    { ModDomain::Linear01,  1.0f },  // GeoDriveC: resynth drive (HAZE) — osc C
    { ModDomain::Linear01,  1.0f },  // GeoDriveD: resynth drive (HAZE) — osc D
    { ModDomain::Linear01,  1.0f },  // GeoSieveA: resynth sieve — osc A
    { ModDomain::Linear01,  1.0f },  // GeoSieveB: resynth sieve — osc B
    { ModDomain::Linear01,  1.0f },  // GeoSieveC: resynth sieve — osc C
    { ModDomain::Linear01,  1.0f },  // GeoSieveD: resynth sieve — osc D
    { ModDomain::Linear01,  1.0f },  // FmStrikeA: FM strike — osc A
    { ModDomain::Linear01,  1.0f },  // FmStrikeB: FM strike — osc B
    { ModDomain::Linear01,  1.0f },  // FmStrikeC: FM strike — osc C
    { ModDomain::Linear01,  1.0f },  // FmStrikeD: FM strike — osc D
    { ModDomain::Linear01,  1.0f },  // FmAgeA: FM age — osc A
    { ModDomain::Linear01,  1.0f },  // FmAgeB: FM age — osc B
    { ModDomain::Linear01,  1.0f },  // FmAgeC: FM age — osc C
    { ModDomain::Linear01,  1.0f },  // FmAgeD: FM age — osc D
    { ModDomain::Linear01,  1.0f },  // FmRustA: FM rust — osc A
    { ModDomain::Linear01,  1.0f },  // FmRustB: FM rust — osc B
    { ModDomain::Linear01,  1.0f },  // FmRustC: FM rust — osc C
    { ModDomain::Linear01,  1.0f },  // FmRustD: FM rust — osc D
    { ModDomain::Linear01,  1.0f },  // FmGaleA: FM gale (QUAKE) — osc A
    { ModDomain::Linear01,  1.0f },  // FmGaleB: FM gale (QUAKE) — osc B
    { ModDomain::Linear01,  1.0f },  // FmGaleC: FM gale (QUAKE) — osc C
    { ModDomain::Linear01,  1.0f },  // FmGaleD: FM gale (QUAKE) — osc D
    { ModDomain::Linear01,  1.0f },  // FmBendA: FM bend (SCORCH) — osc A
    { ModDomain::Linear01,  1.0f },  // FmBendB: FM bend (SCORCH) — osc B
    { ModDomain::Linear01,  1.0f },  // FmBendC: FM bend (SCORCH) — osc C
    { ModDomain::Linear01,  1.0f },  // FmBendD: FM bend (SCORCH) — osc D
    { ModDomain::Linear01,  1.0f },  // FmStormA: FM storm — osc A
    { ModDomain::Linear01,  1.0f },  // FmStormB: FM storm — osc B
    { ModDomain::Linear01,  1.0f },  // FmStormC: FM storm — osc C
    { ModDomain::Linear01,  1.0f },  // FmStormD: FM storm — osc D
    { ModDomain::Linear01,  1.0f },  // FmFbA: FM feedback — osc A
    { ModDomain::Linear01,  1.0f },  // FmFbB: FM feedback — osc B
    { ModDomain::Linear01,  1.0f },  // FmFbC: FM feedback — osc C
    { ModDomain::Linear01,  1.0f },  // FmFbD: FM feedback — osc D
    { ModDomain::Linear01,  1.0f },  // HarmHueA: harmonic hue — osc A
    { ModDomain::Linear01,  1.0f },  // HarmHueB: harmonic hue — osc B
    { ModDomain::Linear01,  1.0f },  // HarmHueC: harmonic hue — osc C
    { ModDomain::Linear01,  1.0f },  // HarmHueD: harmonic hue — osc D
    { ModDomain::Linear01,  1.0f },  // HarmCountA: harmonic count — osc A
    { ModDomain::Linear01,  1.0f },  // HarmCountB: harmonic count — osc B
    { ModDomain::Linear01,  1.0f },  // HarmCountC: harmonic count — osc C
    { ModDomain::Linear01,  1.0f },  // HarmCountD: harmonic count — osc D
    { ModDomain::Linear01,  1.0f },  // HarmLeanA: harmonic lean — osc A
    { ModDomain::Linear01,  1.0f },  // HarmLeanB: harmonic lean — osc B
    { ModDomain::Linear01,  1.0f },  // HarmLeanC: harmonic lean — osc C
    { ModDomain::Linear01,  1.0f },  // HarmLeanD: harmonic lean — osc D
    { ModDomain::Linear01,  1.0f },  // HarmFanA: harmonic fan — osc A
    { ModDomain::Linear01,  1.0f },  // HarmFanB: harmonic fan — osc B
    { ModDomain::Linear01,  1.0f },  // HarmFanC: harmonic fan — osc C
    { ModDomain::Linear01,  1.0f },  // HarmFanD: harmonic fan — osc D
    { ModDomain::Linear01,  1.0f },  // HarmGritA: harmonic grit — osc A
    { ModDomain::Linear01,  1.0f },  // HarmGritB: harmonic grit — osc B
    { ModDomain::Linear01,  1.0f },  // HarmGritC: harmonic grit — osc C
    { ModDomain::Linear01,  1.0f },  // HarmGritD: harmonic grit — osc D
    { ModDomain::Linear01,  1.0f },  // HarmBraidA: harmonic braid — osc A
    { ModDomain::Linear01,  1.0f },  // HarmBraidB: harmonic braid — osc B
    { ModDomain::Linear01,  1.0f },  // HarmBraidC: harmonic braid — osc C
    { ModDomain::Linear01,  1.0f },  // HarmBraidD: harmonic braid — osc D
    { ModDomain::Linear01,  1.0f },  // HarmCarveA: harmonic carve — osc A
    { ModDomain::Linear01,  1.0f },  // HarmCarveB: harmonic carve — osc B
    { ModDomain::Linear01,  1.0f },  // HarmCarveC: harmonic carve — osc C
    { ModDomain::Linear01,  1.0f },  // HarmCarveD: harmonic carve — osc D
    { ModDomain::Linear01,  1.0f },  // HarmChurnA: harmonic churn — osc A
    { ModDomain::Linear01,  1.0f },  // HarmChurnB: harmonic churn — osc B
    { ModDomain::Linear01,  1.0f },  // HarmChurnC: harmonic churn — osc C
    { ModDomain::Linear01,  1.0f },  // HarmChurnD: harmonic churn — osc D
    { ModDomain::Linear01,  1.0f },  // HarmRootA: harmonic root — osc A
    { ModDomain::Linear01,  1.0f },  // HarmRootB: harmonic root — osc B
    { ModDomain::Linear01,  1.0f },  // HarmRootC: harmonic root — osc C
    { ModDomain::Linear01,  1.0f },  // HarmRootD: harmonic root — osc D
    { ModDomain::Linear01,  1.0f },  // HarmShineA: harmonic shine — osc A
    { ModDomain::Linear01,  1.0f },  // HarmShineB: harmonic shine — osc B
    { ModDomain::Linear01,  1.0f },  // HarmShineC: harmonic shine — osc C
    { ModDomain::Linear01,  1.0f },  // HarmShineD: harmonic shine — osc D
    { ModDomain::Linear01,  1.0f },  // HarmWiltA: harmonic wilt — osc A
    { ModDomain::Linear01,  1.0f },  // HarmWiltB: harmonic wilt — osc B
    { ModDomain::Linear01,  1.0f },  // HarmWiltC: harmonic wilt — osc C
    { ModDomain::Linear01,  1.0f },  // HarmWiltD: harmonic wilt — osc D
    { ModDomain::Linear01,  1.0f },  // HarmFizzA: harmonic fizz (forge) — osc A
    { ModDomain::Linear01,  1.0f },  // HarmFizzB: harmonic fizz (forge) — osc B
    { ModDomain::Linear01,  1.0f },  // HarmFizzC: harmonic fizz (forge) — osc C
    { ModDomain::Linear01,  1.0f },  // HarmFizzD: harmonic fizz (forge) — osc D
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
    float       driftLanes[8] = { 0 };   // FLOW·DRIFT block-rate lane values (sources Drift1..Drift8)
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
