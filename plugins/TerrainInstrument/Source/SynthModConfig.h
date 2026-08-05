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
    EnvPitch,                  // fb178 — the fifth legacy envelope (pitch) as a matrix source
    EnvD1, EnvD2, EnvD3, EnvD4, EnvD5, EnvD6, EnvD7, EnvD8, EnvD9, EnvD10, EnvD11, EnvD12, EnvD13, EnvD14, EnvD15, EnvD16, EnvD17, EnvD18, EnvD19, EnvD20, EnvD21, EnvD22, EnvD23, EnvD24, EnvD25, EnvD26, EnvD27,   // fb178 — dynamic envelopes 6..32 (Row 3)
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
    // ── fb76 — SPECTRAL + BLUR now routable (Max's override of the fb75 exclusion). Spectral morph
    //    rebuilds on the MESSAGE thread (60 Hz timer + retire-cooldown ⇒ ~20 Hz max churn, ZERO
    //    audio-thread cost); Blur re-renders the bounded Gaussian band per voice — the SAME
    //    pre-existing path a frame-LFO with static blur already exercises. ──
    SpectralA, SpectralB, SpectralC, SpectralD,
    BlurA, BlurB, BlurC, BlurD,
    // ── fb77 — the WHOLE osc back panel (Max): tuning Oct/Semi/Cent (summed in SEMITONES and folded
    //    into the voice's cents lane — continuous pitch, no stepped octave switching), Warp2 amount,
    //    and the Unison pill's continuous params (Detune/Blend/Width; Voices stays a discrete pick). ──
    OctA, OctB, OctC, OctD,
    SemiA, SemiB, SemiC, SemiD,
    CentA, CentB, CentC, CentD,
    Warp2A, Warp2B, Warp2C, Warp2D,
    UniDetA, UniDetB, UniDetC, UniDetD,
    UniBlendA, UniBlendB, UniBlendC, UniBlendD,
    UniWidthA, UniWidthB, UniWidthC, UniWidthD,
    // ── fb78 — NOTHING OFF THE TABLE (Max): FM page-1 ratios/depths, granular Key ("if a madman
    //    wants to modulate the key of a granular, let them") + Dir, filter back-panel Vel/Track +
    //    Spread, Sub Octave/Shape (stepped), Unison Voices (stepped). Stepped dests round + clamp. ──
    FmRatio1A, FmRatio1B, FmRatio1C, FmRatio1D,
    FmDepth1A, FmDepth1B, FmDepth1C, FmDepth1D,
    FmRatio2A, FmRatio2B, FmRatio2C, FmRatio2D,
    FmDepth2A, FmDepth2B, FmDepth2C, FmDepth2D,
    GrainKeyA, GrainKeyB, GrainKeyC, GrainKeyD,
    GrainDirA, GrainDirB, GrainDirC, GrainDirD,
    FVel1, FVel2,
    FTrack1, FTrack2,
    FSpread1, FSpread2,
    SubRangeA, SubRangeB, SubRangeC, SubRangeD,
    SubFormA, SubFormB, SubFormC, SubFormD,
    UniVoicesA, UniVoicesB, UniVoicesC, UniVoicesD,
    // ── fb79 — per-osc FILTER SENDS (the F1/F2 pills went per-oscillator; default 0 = dry) ──
    OscF1SendA, OscF1SendB, OscF1SendC, OscF1SendD,
    OscF2SendA, OscF2SendB, OscF2SendC, OscF2SendD,
    // ── fb145 — EVERY CARD KNOB + SLIDER (append-only; block-rate, applied via flowKnob
    //    at the processor read sites; steppers/toggles/chain excluded by design) ──
    FlowArpSwing, FlowArpRoll, FlowArpTimbre, FlowArpGlide,
    FlowArpPRange, FlowArpPCurve, FlowArpPQuant, FlowArpPSlide,
    FlowArpGLen, FlowArpGCurve, FlowArpGRand, FlowArpGSlide,
    FlowArpVRange, FlowArpVCurve, FlowArpVRand, FlowArpVFloor,
    FlowArpORange, FlowArpOBias, FlowArpORand, FlowArpOSpread,
    FlowArpRCount, FlowArpRDecay, FlowArpRCurve, FlowArpRAmt,
    FlowArpCAmt, FlowArpCBias, FlowArpCSeed, FlowArpCDrift,
    FlowArpWDepth, FlowArpWCurve, FlowArpWSlide, FlowArpWRand,
    FlowArpMix,
    FlowChopScan, FlowChopWander, FlowChopSpread, FlowChopSpeed,
    FlowChopCrush, FlowChopDetune, FlowChopWow, FlowChopSmooth,
    FlowChopGrit, FlowChopTrim, FlowChopOSpread, FlowChopOBias,
    FlowChopOLock, FlowChopOSeed, FlowChopPRange, FlowChopPSteps,
    FlowChopPGlide, FlowChopPQuant, FlowChopRvOdds, FlowChopRvRun,
    FlowChopRvSpread, FlowChopRvSnap, FlowChopTLen, FlowChopTCurve,
    FlowChopTRand, FlowChopTGate, FlowChopRCount, FlowChopRDecay,
    FlowChopRCurve, FlowChopROdds, FlowChopDAmt, FlowChopDSize,
    FlowChopDSpray, FlowChopDTone, FlowChopMix,
    FlowGliDecay, FlowGliDrop, FlowGliBurst, FlowGliPing,
    FlowGliBend, FlowGliRepSize, FlowGliRepSpeed, FlowGliRepFade,
    FlowGliRepVary, FlowGliRevLen, FlowGliRevFade, FlowGliRevSprd,
    FlowGliRevSnap, FlowGliTapeCurve, FlowGliTapeTime, FlowGliTapeDepth,
    FlowGliTapeSpin, FlowGliGateRate, FlowGliGateShape, FlowGliGateNudge,
    FlowGliGateAmt, FlowGliPitShift, FlowGliPitWalk, FlowGliPitGlide,
    FlowGliPitJump, FlowGliCrshBits, FlowGliCrshRate, FlowGliCrshTone,
    FlowGliCrshAmt, FlowGliFrzSize, FlowGliFrzSpray, FlowGliFrzShine,
    FlowGliFrzMelt, FlowGliSctSize, FlowGliSctAmt, FlowGliSctVary,
    FlowGliSctWidth, FlowGliMix,
    FlowRbnVary, FlowRbnDrift, FlowRbnWobble, FlowRbnLvl,
    FlowRbnPan, FlowRbnAfter, FlowRbnGlide, FlowRbnOverlap,
    FlowRbnFade,
    // fb193 — S4b: envelope params as dests. EnvPBase + (env−1)*6 + {Dly,Atk,Hld,Dec,Sus,Rel},
    // envs 1..32 (5 legacy + 27 dyn). APPEND-ONLY (the law).
    EnvPBase,
    LfoRateBase = EnvPBase + 192,   // fb196 — env/LFO on an LFO's RATE (10 dests, Hz mode)
    LfoPhaseBase = LfoRateBase + 10,   // fb245 — env/LFO on an LFO's PHASE (10 dests, read-phase shift 0..1)
    NumDests = LfoPhaseBase + 10
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
    { ModDomain::Linear01,  1.0f },  // SpectralA: spectral morph amount — osc A (message-thread rebuild)
    { ModDomain::Linear01,  1.0f },  // SpectralB
    { ModDomain::Linear01,  1.0f },  // SpectralC
    { ModDomain::Linear01,  1.0f },  // SpectralD
    { ModDomain::Linear01,  1.0f },  // BlurA: wavetable frame blur — osc A
    { ModDomain::Linear01,  1.0f },  // BlurB
    { ModDomain::Linear01,  1.0f },  // BlurC
    { ModDomain::Linear01,  1.0f },  // BlurD
    { ModDomain::Semitone, 48.0f },  // OctA: ±4 OCTAVES at full depth — osc A. fb233 (Max): the sum is SNAPPED to whole octaves at the fold (PluginProcessor tuneModCents) — octave mod JUMPS, never wobbles
    { ModDomain::Semitone, 48.0f },  // OctB
    { ModDomain::Semitone, 48.0f },  // OctC
    { ModDomain::Semitone, 48.0f },  // OctD
    { ModDomain::Semitone, 12.0f },  // SemiA: ±12 st at full depth (the knob's own span)
    { ModDomain::Semitone, 12.0f },  // SemiB
    { ModDomain::Semitone, 12.0f },  // SemiC
    { ModDomain::Semitone, 12.0f },  // SemiD
    { ModDomain::Semitone,  1.0f },  // CentA: ±1 st (±100 cents) at full depth — vibrato lane
    { ModDomain::Semitone,  1.0f },  // CentB
    { ModDomain::Semitone,  1.0f },  // CentC
    { ModDomain::Semitone,  1.0f },  // CentD
    { ModDomain::Linear01,  1.0f },  // Warp2A: warp 2 amount — osc A
    { ModDomain::Linear01,  1.0f },  // Warp2B
    { ModDomain::Linear01,  1.0f },  // Warp2C
    { ModDomain::Linear01,  1.0f },  // Warp2D
    { ModDomain::Linear01,  1.0f },  // UniDetA: unison detune — osc A
    { ModDomain::Linear01,  1.0f },  // UniDetB
    { ModDomain::Linear01,  1.0f },  // UniDetC
    { ModDomain::Linear01,  1.0f },  // UniDetD
    { ModDomain::Linear01,  1.0f },  // UniBlendA: unison blend — osc A
    { ModDomain::Linear01,  1.0f },  // UniBlendB
    { ModDomain::Linear01,  1.0f },  // UniBlendC
    { ModDomain::Linear01,  1.0f },  // UniBlendD
    { ModDomain::Linear01,  1.0f },  // UniWidthA: unison width — osc A
    { ModDomain::Linear01,  1.0f },  // UniWidthB
    { ModDomain::Linear01,  1.0f },  // UniWidthC
    { ModDomain::Linear01,  1.0f },  // UniWidthD
    { ModDomain::Linear01,  4.0f },  // FmRatio1A: ±4 ratio at full depth (param 0.25..16) — osc A
    { ModDomain::Linear01,  4.0f },  // FmRatio1B
    { ModDomain::Linear01,  4.0f },  // FmRatio1C
    { ModDomain::Linear01,  4.0f },  // FmRatio1D
    { ModDomain::Linear01,  1.0f },  // FmDepth1A
    { ModDomain::Linear01,  1.0f },  // FmDepth1B
    { ModDomain::Linear01,  1.0f },  // FmDepth1C
    { ModDomain::Linear01,  1.0f },  // FmDepth1D
    { ModDomain::Linear01,  4.0f },  // FmRatio2A
    { ModDomain::Linear01,  4.0f },  // FmRatio2B
    { ModDomain::Linear01,  4.0f },  // FmRatio2C
    { ModDomain::Linear01,  4.0f },  // FmRatio2D
    { ModDomain::Linear01,  1.0f },  // FmDepth2A
    { ModDomain::Linear01,  1.0f },  // FmDepth2B
    { ModDomain::Linear01,  1.0f },  // FmDepth2C
    { ModDomain::Linear01,  1.0f },  // FmDepth2D
    { ModDomain::Linear01,  6.0f },  // GrainKeyA: ±6 key steps at full depth (stepped 0..6) — osc A
    { ModDomain::Linear01,  6.0f },  // GrainKeyB
    { ModDomain::Linear01,  6.0f },  // GrainKeyC
    { ModDomain::Linear01,  6.0f },  // GrainKeyD
    { ModDomain::Bipolar,   1.0f },  // GrainDirA: grain direction bias (−1 rev · 0 rand · +1 fwd)
    { ModDomain::Bipolar,   1.0f },  // GrainDirB
    { ModDomain::Bipolar,   1.0f },  // GrainDirC
    { ModDomain::Bipolar,   1.0f },  // GrainDirD
    { ModDomain::Linear01,  1.0f },  // FVel1: filter 1 velocity→cutoff amount
    { ModDomain::Linear01,  1.0f },  // FVel2
    { ModDomain::Linear01,  1.0f },  // FTrack1: filter 1 keytrack amount
    { ModDomain::Linear01,  1.0f },  // FTrack2
    { ModDomain::Linear01,  1.0f },  // FSpread1: filter 1 stereo spread
    { ModDomain::Linear01,  1.0f },  // FSpread2
    { ModDomain::Linear01,  8.0f },  // SubRangeA: ±8 octave steps at full depth (stepped 0..8) — osc A
    { ModDomain::Linear01,  8.0f },  // SubRangeB
    { ModDomain::Linear01,  8.0f },  // SubRangeC
    { ModDomain::Linear01,  8.0f },  // SubRangeD
    { ModDomain::Linear01,  3.0f },  // SubFormA: ±3 shape steps at full depth (stepped 0..3) — osc A
    { ModDomain::Linear01,  3.0f },  // SubFormB
    { ModDomain::Linear01,  3.0f },  // SubFormC
    { ModDomain::Linear01,  3.0f },  // SubFormD
    { ModDomain::Linear01, 15.0f },  // UniVoicesA: ±15 voices at full depth (stepped 1..16) — osc A
    { ModDomain::Linear01, 15.0f },  // UniVoicesB
    { ModDomain::Linear01, 15.0f },  // UniVoicesC
    { ModDomain::Linear01, 15.0f },  // UniVoicesD
    { ModDomain::Linear01,  1.0f },  // OscF1SendA: osc A → Filter 1 send
    { ModDomain::Linear01,  1.0f },  // OscF1SendB
    { ModDomain::Linear01,  1.0f },  // OscF1SendC
    { ModDomain::Linear01,  1.0f },  // OscF1SendD
    { ModDomain::Linear01,  1.0f },  // OscF2SendA: osc A → Filter 2 send
    { ModDomain::Linear01,  1.0f },  // OscF2SendB
    { ModDomain::Linear01,  1.0f },  // OscF2SendC
    { ModDomain::Linear01,  1.0f },  // OscF2SendD
    // ── fb145 — card knob/slider dests (all normalized 0..1 params; Linear01 full-span) ──
    { ModDomain::Linear01,  1.0f },  // FlowArpSwing
    { ModDomain::Linear01,  1.0f },  // FlowArpRoll
    { ModDomain::Linear01,  1.0f },  // FlowArpTimbre
    { ModDomain::Linear01,  1.0f },  // FlowArpGlide
    { ModDomain::Linear01,  1.0f },  // FlowArpPRange
    { ModDomain::Linear01,  1.0f },  // FlowArpPCurve
    { ModDomain::Linear01,  1.0f },  // FlowArpPQuant
    { ModDomain::Linear01,  1.0f },  // FlowArpPSlide
    { ModDomain::Linear01,  1.0f },  // FlowArpGLen
    { ModDomain::Linear01,  1.0f },  // FlowArpGCurve
    { ModDomain::Linear01,  1.0f },  // FlowArpGRand
    { ModDomain::Linear01,  1.0f },  // FlowArpGSlide
    { ModDomain::Linear01,  1.0f },  // FlowArpVRange
    { ModDomain::Linear01,  1.0f },  // FlowArpVCurve
    { ModDomain::Linear01,  1.0f },  // FlowArpVRand
    { ModDomain::Linear01,  1.0f },  // FlowArpVFloor
    { ModDomain::Linear01,  1.0f },  // FlowArpORange
    { ModDomain::Linear01,  1.0f },  // FlowArpOBias
    { ModDomain::Linear01,  1.0f },  // FlowArpORand
    { ModDomain::Linear01,  1.0f },  // FlowArpOSpread
    { ModDomain::Linear01,  1.0f },  // FlowArpRCount
    { ModDomain::Linear01,  1.0f },  // FlowArpRDecay
    { ModDomain::Linear01,  1.0f },  // FlowArpRCurve
    { ModDomain::Linear01,  1.0f },  // FlowArpRAmt
    { ModDomain::Linear01,  1.0f },  // FlowArpCAmt
    { ModDomain::Linear01,  1.0f },  // FlowArpCBias
    { ModDomain::Linear01,  1.0f },  // FlowArpCSeed
    { ModDomain::Linear01,  1.0f },  // FlowArpCDrift
    { ModDomain::Linear01,  1.0f },  // FlowArpWDepth
    { ModDomain::Linear01,  1.0f },  // FlowArpWCurve
    { ModDomain::Linear01,  1.0f },  // FlowArpWSlide
    { ModDomain::Linear01,  1.0f },  // FlowArpWRand
    { ModDomain::Linear01,  1.0f },  // FlowArpMix
    { ModDomain::Linear01,  1.0f },  // FlowChopScan
    { ModDomain::Linear01,  1.0f },  // FlowChopWander
    { ModDomain::Linear01,  1.0f },  // FlowChopSpread
    { ModDomain::Linear01,  1.0f },  // FlowChopSpeed
    { ModDomain::Linear01,  1.0f },  // FlowChopCrush
    { ModDomain::Linear01,  1.0f },  // FlowChopDetune
    { ModDomain::Linear01,  1.0f },  // FlowChopWow
    { ModDomain::Linear01,  1.0f },  // FlowChopSmooth
    { ModDomain::Linear01,  1.0f },  // FlowChopGrit
    { ModDomain::Linear01,  1.0f },  // FlowChopTrim
    { ModDomain::Linear01,  1.0f },  // FlowChopOSpread
    { ModDomain::Linear01,  1.0f },  // FlowChopOBias
    { ModDomain::Linear01,  1.0f },  // FlowChopOLock
    { ModDomain::Linear01,  1.0f },  // FlowChopOSeed
    { ModDomain::Linear01,  1.0f },  // FlowChopPRange
    { ModDomain::Linear01,  1.0f },  // FlowChopPSteps
    { ModDomain::Linear01,  1.0f },  // FlowChopPGlide
    { ModDomain::Linear01,  1.0f },  // FlowChopPQuant
    { ModDomain::Linear01,  1.0f },  // FlowChopRvOdds
    { ModDomain::Linear01,  1.0f },  // FlowChopRvRun
    { ModDomain::Linear01,  1.0f },  // FlowChopRvSpread
    { ModDomain::Linear01,  1.0f },  // FlowChopRvSnap
    { ModDomain::Linear01,  1.0f },  // FlowChopTLen
    { ModDomain::Linear01,  1.0f },  // FlowChopTCurve
    { ModDomain::Linear01,  1.0f },  // FlowChopTRand
    { ModDomain::Linear01,  1.0f },  // FlowChopTGate
    { ModDomain::Linear01,  1.0f },  // FlowChopRCount
    { ModDomain::Linear01,  1.0f },  // FlowChopRDecay
    { ModDomain::Linear01,  1.0f },  // FlowChopRCurve
    { ModDomain::Linear01,  1.0f },  // FlowChopROdds
    { ModDomain::Linear01,  1.0f },  // FlowChopDAmt
    { ModDomain::Linear01,  1.0f },  // FlowChopDSize
    { ModDomain::Linear01,  1.0f },  // FlowChopDSpray
    { ModDomain::Linear01,  1.0f },  // FlowChopDTone
    { ModDomain::Linear01,  1.0f },  // FlowChopMix
    { ModDomain::Linear01,  1.0f },  // FlowGliDecay
    { ModDomain::Linear01,  1.0f },  // FlowGliDrop
    { ModDomain::Linear01,  1.0f },  // FlowGliBurst
    { ModDomain::Linear01,  1.0f },  // FlowGliPing
    { ModDomain::Linear01,  1.0f },  // FlowGliBend
    { ModDomain::Linear01,  1.0f },  // FlowGliRepSize
    { ModDomain::Linear01,  1.0f },  // FlowGliRepSpeed
    { ModDomain::Linear01,  1.0f },  // FlowGliRepFade
    { ModDomain::Linear01,  1.0f },  // FlowGliRepVary
    { ModDomain::Linear01,  1.0f },  // FlowGliRevLen
    { ModDomain::Linear01,  1.0f },  // FlowGliRevFade
    { ModDomain::Linear01,  1.0f },  // FlowGliRevSprd
    { ModDomain::Linear01,  1.0f },  // FlowGliRevSnap
    { ModDomain::Linear01,  1.0f },  // FlowGliTapeCurve
    { ModDomain::Linear01,  1.0f },  // FlowGliTapeTime
    { ModDomain::Linear01,  1.0f },  // FlowGliTapeDepth
    { ModDomain::Linear01,  1.0f },  // FlowGliTapeSpin
    { ModDomain::Linear01,  1.0f },  // FlowGliGateRate
    { ModDomain::Linear01,  1.0f },  // FlowGliGateShape
    { ModDomain::Linear01,  1.0f },  // FlowGliGateNudge
    { ModDomain::Linear01,  1.0f },  // FlowGliGateAmt
    { ModDomain::Linear01,  1.0f },  // FlowGliPitShift
    { ModDomain::Linear01,  1.0f },  // FlowGliPitWalk
    { ModDomain::Linear01,  1.0f },  // FlowGliPitGlide
    { ModDomain::Linear01,  1.0f },  // FlowGliPitJump
    { ModDomain::Linear01,  1.0f },  // FlowGliCrshBits
    { ModDomain::Linear01,  1.0f },  // FlowGliCrshRate
    { ModDomain::Linear01,  1.0f },  // FlowGliCrshTone
    { ModDomain::Linear01,  1.0f },  // FlowGliCrshAmt
    { ModDomain::Linear01,  1.0f },  // FlowGliFrzSize
    { ModDomain::Linear01,  1.0f },  // FlowGliFrzSpray
    { ModDomain::Linear01,  1.0f },  // FlowGliFrzShine
    { ModDomain::Linear01,  1.0f },  // FlowGliFrzMelt
    { ModDomain::Linear01,  1.0f },  // FlowGliSctSize
    { ModDomain::Linear01,  1.0f },  // FlowGliSctAmt
    { ModDomain::Linear01,  1.0f },  // FlowGliSctVary
    { ModDomain::Linear01,  1.0f },  // FlowGliSctWidth
    { ModDomain::Linear01,  1.0f },  // FlowGliMix
    { ModDomain::Linear01,  1.0f },  // FlowRbnVary
    { ModDomain::Linear01,  1.0f },  // FlowRbnDrift
    { ModDomain::Linear01,  1.0f },  // FlowRbnWobble
    { ModDomain::Linear01,  1.0f },  // FlowRbnLvl
    { ModDomain::Linear01,  1.0f },  // FlowRbnPan
    { ModDomain::Linear01,  1.0f },  // FlowRbnAfter
    { ModDomain::Linear01,  1.0f },  // FlowRbnGlide
    { ModDomain::Linear01,  1.0f },  // FlowRbnOverlap
    { ModDomain::Linear01,  1.0f },  // FlowRbnFade
    { ModDomain::Linear01,  1.0f },  // EnvP: Env1 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env1 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env1 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env1 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env1 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env1 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env2 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env2 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env2 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env2 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env2 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env2 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env3 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env3 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env3 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env3 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env3 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env3 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env4 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env4 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env4 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env4 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env4 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env4 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env5 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env5 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env5 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env5 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env5 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env5 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env6 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env6 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env6 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env6 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env6 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env6 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env7 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env7 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env7 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env7 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env7 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env7 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env8 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env8 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env8 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env8 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env8 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env8 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env9 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env9 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env9 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env9 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env9 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env9 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env10 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env10 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env10 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env10 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env10 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env10 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env11 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env11 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env11 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env11 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env11 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env11 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env12 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env12 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env12 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env12 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env12 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env12 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env13 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env13 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env13 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env13 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env13 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env13 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env14 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env14 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env14 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env14 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env14 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env14 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env15 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env15 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env15 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env15 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env15 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env15 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env16 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env16 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env16 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env16 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env16 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env16 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env17 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env17 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env17 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env17 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env17 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env17 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env18 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env18 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env18 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env18 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env18 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env18 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env19 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env19 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env19 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env19 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env19 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env19 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env20 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env20 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env20 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env20 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env20 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env20 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env21 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env21 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env21 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env21 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env21 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env21 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env22 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env22 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env22 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env22 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env22 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env22 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env23 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env23 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env23 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env23 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env23 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env23 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env24 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env24 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env24 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env24 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env24 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env24 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env25 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env25 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env25 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env25 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env25 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env25 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env26 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env26 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env26 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env26 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env26 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env26 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env27 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env27 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env27 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env27 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env27 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env27 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env28 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env28 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env28 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env28 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env28 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env28 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env29 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env29 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env29 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env29 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env29 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env29 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env30 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env30 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env30 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env30 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env30 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env30 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env31 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env31 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env31 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env31 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env31 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env31 Rel
    { ModDomain::Linear01,  1.0f },  // EnvP: Env32 Dly
    { ModDomain::Linear01,  1.0f },  // EnvP: Env32 Atk
    { ModDomain::Linear01,  1.0f },  // EnvP: Env32 Hld
    { ModDomain::Linear01,  1.0f },  // EnvP: Env32 Dec
    { ModDomain::Linear01,  1.0f },  // EnvP: Env32 Sus
    { ModDomain::Linear01,  1.0f },  // EnvP: Env32 Rel
    { ModDomain::Linear01,  1.0f },  // LfoRate1
    { ModDomain::Linear01,  1.0f },  // LfoRate2
    { ModDomain::Linear01,  1.0f },  // LfoRate3
    { ModDomain::Linear01,  1.0f },  // LfoRate4
    { ModDomain::Linear01,  1.0f },  // LfoRate5
    { ModDomain::Linear01,  1.0f },  // LfoRate6
    { ModDomain::Linear01,  1.0f },  // LfoRate7
    { ModDomain::Linear01,  1.0f },  // LfoRate8
    { ModDomain::Linear01,  1.0f },  // LfoRate9
    { ModDomain::Linear01,  1.0f },  // LfoRate10
    { ModDomain::Linear01,  1.0f },  // LfoPhase1  (fb245 — read-phase shift, 0..1 wraps)
    { ModDomain::Linear01,  1.0f },  // LfoPhase2
    { ModDomain::Linear01,  1.0f },  // LfoPhase3
    { ModDomain::Linear01,  1.0f },  // LfoPhase4
    { ModDomain::Linear01,  1.0f },  // LfoPhase5
    { ModDomain::Linear01,  1.0f },  // LfoPhase6
    { ModDomain::Linear01,  1.0f },  // LfoPhase7
    { ModDomain::Linear01,  1.0f },  // LfoPhase8
    { ModDomain::Linear01,  1.0f },  // LfoPhase9
    { ModDomain::Linear01,  1.0f },  // LfoPhase10
};

// ── Tempo-sync divisions. beatsPerCycle = quarter-notes spanned by one LFO cycle. ──
//   syncedHz = (BPM/60) / beatsPerCycle.  Straight 1/4 -> 1 beat -> 2 Hz @ 120.
//   Triplet = straight beats * 2/3 (faster). Dotted = straight beats * 1.5 (slower).
struct SyncDiv { const char* name; float beatsPerCycle; };
static constexpr SyncDiv kSyncDivisions[] = {
    { "8 bar",  32.0f }, { "4 bar", 16.0f }, { "2 bar", 8.0f }, { "1 bar", 4.0f },
    { "1/2",     2.0f }, { "1/4",    1.0f }, { "1/8",   0.5f }, { "1/16",  0.25f }, { "1/32", 0.125f },
    { "1/4.",    1.5f }, { "1/8.",   0.75f},                                   // dotted
    { "1/4T",    1.0f * 2.0f/3.0f }, { "1/8T", 0.5f * 2.0f/3.0f }, { "1/16T", 0.25f * 2.0f/3.0f }, // triplet
    // fb219 — APPENDED (append-only law: saved div indices stay valid; the UI presents them
    // in ladder order via a display map): the slow giants + the audio-rate multiples.
    { "32 bar", 128.0f }, { "16 bar", 64.0f },
    { "1/64",  0.0625f }, { "1/128", 0.03125f }, { "1/256", 0.015625f }
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

static constexpr int MAX_ASSIGNMENTS = 128;   // fb178 — envelopes joined the matrix (was 32)

// The whole modulation table. Published to the audio thread as an immutable copy.
struct ModConfig
{
    LFOSettings lfos[NUM_LFOS];
    Assignment  assignments[MAX_ASSIGNMENTS];
    int         numAssignments = 0;
    float       driftLanes[8] = { 0 };   // FLOW·DRIFT block-rate lane values (sources Drift1..Drift8)
};

// fb178 — envelope sources (Row 3 S2). Blob src encoding: 100 + (envNum-1), Env 1..32.
static constexpr int kEnvSrcBase = 100;
inline ModSource envSourceFor (int envNum) noexcept   // envNum 1..32
{
    switch (envNum)
    {
        case 1: return ModSource::EnvAmp;   case 2: return ModSource::EnvFilter;
        case 3: return ModSource::EnvPitch; case 4: return ModSource::EnvMod1;
        case 5: return ModSource::EnvMod2;
        default: return (ModSource) ((int) ModSource::EnvD1 + (envNum - 6));
    }
}
inline bool isEnvModSource (int sI) noexcept
{
    return sI == (int) ModSource::EnvAmp  || sI == (int) ModSource::EnvFilter
        || sI == (int) ModSource::EnvMod1 || sI == (int) ModSource::EnvMod2
        || sI == (int) ModSource::EnvPitch
        || (sI >= (int) ModSource::EnvD1 && sI <= (int) ModSource::EnvD1 + 26);
}

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
