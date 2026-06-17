#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include "PluginProcessor.h"
#include "ParameterIDs.hpp"

class TerrainInstrumentAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                     public juce::FileDragAndDropTarget,
                                     private juce::Timer
{
public:
    TerrainInstrumentAudioProcessorEditor (TerrainInstrumentAudioProcessor&);
    ~TerrainInstrumentAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // FileDragAndDropTarget — sample / patch / pack drag-drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter          (const juce::StringArray& files, int x, int y) override;
    void fileDragExit           (const juce::StringArray& files) override;

private:
    void timerCallback() override;

    // Sample / patch loading helpers (Task 11; loadPatch + importTerrainPack
    // are stubs implemented in Tasks 18 / 22 of the v0a plan)
    void loadSampleAsync      (const juce::File& file);
    // Task 13: targets a specific layer index (0..3) instead of editingLayer.
    // Used by the editor constructor to fan out V2 preset reloads across all
    // 4 layer slots without temporarily changing editingLayer on the audio thread.
    void loadSampleIntoLayer  (const juce::File& file, int layerIdx);
    void loadPatch         (const juce::File& patchFile);    // Task 18 stub
    void importTerrainPack (const juce::File& packFile);     // Task 22 stub

    juce::String currentSampleSourcePath;

    TerrainInstrumentAudioProcessor& audioProcessor;

    // ═══════════════════════════════════════════════════════════════
    // CRITICAL: Member Declaration Order (Prevents DAW Crashes)
    // C++ destroys members in REVERSE order of declaration.
    // CORRECT ORDER: Relays → WebView → Attachments
    // ═══════════════════════════════════════════════════════════════

    // 1. PARAMETER RELAYS FIRST (destroyed last)
    juce::WebSliderRelay grainSizeRelay  { ParameterIDs::GRAIN_SIZE };
    juce::WebSliderRelay densityRelay    { ParameterIDs::DENSITY };
    juce::WebSliderRelay sprayRelay      { ParameterIDs::SPRAY };
    juce::WebSliderRelay pitchRelay      { ParameterIDs::PITCH };
    juce::WebSliderRelay wanderRelay   { ParameterIDs::WANDER };
    juce::WebSliderRelay freezeRelay     { ParameterIDs::FREEZE };
    juce::WebSliderRelay grainFilterRelay { ParameterIDs::GRAIN_FILTER };
    juce::WebSliderRelay mixRelay        { ParameterIDs::MIX };
    juce::WebSliderRelay wowFlutterRelay { ParameterIDs::WOW_FLUTTER };
    juce::WebSliderRelay saturationRelay { ParameterIDs::SATURATION };
    juce::WebSliderRelay hissRelay       { ParameterIDs::HISS };
    juce::WebSliderRelay wireWowRelay        { ParameterIDs::WIRE_WOW };
    juce::WebSliderRelay wireSaturationRelay { ParameterIDs::WIRE_SATURATION };
    juce::WebSliderRelay wireHissRelay       { ParameterIDs::WIRE_HISS };
    juce::WebSliderRelay studioSculptRelay { ParameterIDs::STUDIO_SCULPT };
    juce::WebSliderRelay studioWeaveRelay  { ParameterIDs::STUDIO_WEAVE };
    juce::WebSliderRelay studioTiltRelay   { ParameterIDs::STUDIO_TILT };
    juce::WebSliderRelay outputGainRelay { ParameterIDs::OUTPUT_GAIN };
    juce::WebSliderRelay masterMixRelay  { ParameterIDs::MASTER_MIX };
    juce::WebSliderRelay loopLengthRelay   { ParameterIDs::LOOP_LENGTH };
    juce::WebSliderRelay loopFeedbackRelay { ParameterIDs::LOOP_FEEDBACK };
    juce::WebSliderRelay loopDegradeRelay  { ParameterIDs::LOOP_DEGRADE };
    juce::WebSliderRelay loopSpeedRelay    { ParameterIDs::LOOP_SPEED };
    juce::WebSliderRelay spaceSizeRelay    { ParameterIDs::SPACE_SIZE };
    juce::WebSliderRelay spaceDecayRelay   { ParameterIDs::SPACE_DECAY };
    juce::WebSliderRelay spaceToneRelay    { ParameterIDs::SPACE_TONE };
    juce::WebSliderRelay spaceMixRelay     { ParameterIDs::SPACE_MIX };
    juce::WebSliderRelay dlyTimeRelay      { ParameterIDs::DLY_TIME };
    juce::WebSliderRelay dlyFeedbackRelay  { ParameterIDs::DLY_FEEDBACK };
    juce::WebSliderRelay dlyToneRelay      { ParameterIDs::DLY_TONE };
    juce::WebSliderRelay dlyCharacterRelay { ParameterIDs::DLY_CHARACTER };
    juce::WebSliderRelay dlyModRelay       { ParameterIDs::DLY_MOD };
    juce::WebSliderRelay dlyModRateRelay   { ParameterIDs::DLY_MOD_RATE };
    juce::WebSliderRelay dlyMixRelay       { ParameterIDs::DLY_MIX };
    juce::WebSliderRelay dlyDuckRelay      { ParameterIDs::DLY_DUCK };
    juce::WebSliderRelay dlyFreezeRelay    { ParameterIDs::DLY_FREEZE };
    // Choice-type delay relays. Without these, JS setNormalisedValue() writes
    // to a phantom SliderState — APVTS never updates and the audio thread
    // reads the default forever (PITCH stuck at OFF, WIDTH stuck at MONO, etc.).
    juce::WebSliderRelay dlySyncRelay      { ParameterIDs::DLY_SYNC };
    juce::WebSliderRelay dlySyncDivRelay   { ParameterIDs::DLY_SYNC_DIV };
    juce::WebSliderRelay dlyModWaveRelay   { ParameterIDs::DLY_MOD_WAVE };
    juce::WebSliderRelay dlyPitchRelay     { ParameterIDs::DLY_PITCH };
    juce::WebSliderRelay dlyWidthRelay     { ParameterIDs::DLY_WIDTH };
    juce::WebSliderRelay chorusAmountRelay    { ParameterIDs::CHORUS_AMOUNT };
    juce::WebSliderRelay chorusWidthRelay     { ParameterIDs::CHORUS_WIDTH };
    juce::WebSliderRelay chorusCharacterRelay { ParameterIDs::CHORUS_CHARACTER };
    juce::WebSliderRelay chopFadeRelay        { ParameterIDs::CHOP_FADE_MS };
    // Parametric EQ — 37 relays bridging WebView ↔ APVTS for EQ_MASTER_BYPASS,
    // HP/LP (freq/slope/bypass), 7 bands × (freq/gain/Q/bypass), and the two
    // UI-only filter-mode flags (EQ_B1_HP_MODE / EQ_B7_LP_MODE).
    static constexpr int NUM_EQ_RELAYS = 37;
    std::array<juce::WebSliderRelay, NUM_EQ_RELAYS> eqRelays {{
        juce::WebSliderRelay { ParameterIDs::EQ_MASTER_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_HP_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_HP_SLOPE }, juce::WebSliderRelay { ParameterIDs::EQ_HP_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_LP_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_LP_SLOPE }, juce::WebSliderRelay { ParameterIDs::EQ_LP_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_B1_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_B1_GAIN }, juce::WebSliderRelay { ParameterIDs::EQ_B1_Q }, juce::WebSliderRelay { ParameterIDs::EQ_B1_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_B2_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_B2_GAIN }, juce::WebSliderRelay { ParameterIDs::EQ_B2_Q }, juce::WebSliderRelay { ParameterIDs::EQ_B2_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_B3_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_B3_GAIN }, juce::WebSliderRelay { ParameterIDs::EQ_B3_Q }, juce::WebSliderRelay { ParameterIDs::EQ_B3_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_B4_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_B4_GAIN }, juce::WebSliderRelay { ParameterIDs::EQ_B4_Q }, juce::WebSliderRelay { ParameterIDs::EQ_B4_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_B5_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_B5_GAIN }, juce::WebSliderRelay { ParameterIDs::EQ_B5_Q }, juce::WebSliderRelay { ParameterIDs::EQ_B5_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_B6_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_B6_GAIN }, juce::WebSliderRelay { ParameterIDs::EQ_B6_Q }, juce::WebSliderRelay { ParameterIDs::EQ_B6_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_B7_FREQ }, juce::WebSliderRelay { ParameterIDs::EQ_B7_GAIN }, juce::WebSliderRelay { ParameterIDs::EQ_B7_Q }, juce::WebSliderRelay { ParameterIDs::EQ_B7_BYPASS },
        juce::WebSliderRelay { ParameterIDs::EQ_B1_HP_MODE }, juce::WebSliderRelay { ParameterIDs::EQ_B7_LP_MODE },
    }};

    // Synth section — Phase 1 (MPV)
    juce::WebSliderRelay synOscAEngineRelay   { ParameterIDs::SYN_OSC_A_ENGINE };
    juce::WebSliderRelay synOscAOctRelay      { ParameterIDs::SYN_OSC_A_OCT };
    juce::WebSliderRelay synOscASemiRelay     { ParameterIDs::SYN_OSC_A_SEMI };
    juce::WebSliderRelay synOscACentRelay     { ParameterIDs::SYN_OSC_A_CENT };
    juce::WebSliderRelay synOscALevelRelay    { ParameterIDs::SYN_OSC_A_LEVEL };
    juce::WebSliderRelay synOscAPanRelay      { ParameterIDs::SYN_OSC_A_PAN };
    juce::WebSliderRelay synFilter1CutRelay   { ParameterIDs::SYN_FILTER1_CUT };
    juce::WebSliderRelay synFilter1ResRelay   { ParameterIDs::SYN_FILTER1_RES };
    juce::WebSliderRelay lfo1RateRelay         { ParameterIDs::LFO1_RATE };   // Batch 1
    juce::WebSliderRelay lfo1DepthRelay        { ParameterIDs::LFO1_DEPTH };  // Batch 1
    juce::WebSliderRelay lfo1ShapeRelay        { ParameterIDs::LFO1_SHAPE };  // Mod redesign
    juce::WebSliderRelay lfo1SyncRelay         { ParameterIDs::LFO1_SYNC };   // Mod redesign
    juce::WebSliderRelay lfo1DivRelay          { ParameterIDs::LFO1_DIV };    // Mod redesign
    // Mod redesign Stage 2 — LFOs 2..5
    juce::WebSliderRelay lfo2RateRelay { ParameterIDs::LFO2_RATE };  juce::WebSliderRelay lfo2DepthRelay { ParameterIDs::LFO2_DEPTH };
    juce::WebSliderRelay lfo2ShapeRelay{ ParameterIDs::LFO2_SHAPE }; juce::WebSliderRelay lfo2SyncRelay  { ParameterIDs::LFO2_SYNC };  juce::WebSliderRelay lfo2DivRelay { ParameterIDs::LFO2_DIV };
    juce::WebSliderRelay lfo3RateRelay { ParameterIDs::LFO3_RATE };  juce::WebSliderRelay lfo3DepthRelay { ParameterIDs::LFO3_DEPTH };
    juce::WebSliderRelay lfo3ShapeRelay{ ParameterIDs::LFO3_SHAPE }; juce::WebSliderRelay lfo3SyncRelay  { ParameterIDs::LFO3_SYNC };  juce::WebSliderRelay lfo3DivRelay { ParameterIDs::LFO3_DIV };
    juce::WebSliderRelay lfo4RateRelay { ParameterIDs::LFO4_RATE };  juce::WebSliderRelay lfo4DepthRelay { ParameterIDs::LFO4_DEPTH };
    juce::WebSliderRelay lfo4ShapeRelay{ ParameterIDs::LFO4_SHAPE }; juce::WebSliderRelay lfo4SyncRelay  { ParameterIDs::LFO4_SYNC };  juce::WebSliderRelay lfo4DivRelay { ParameterIDs::LFO4_DIV };
    juce::WebSliderRelay lfo5RateRelay { ParameterIDs::LFO5_RATE };  juce::WebSliderRelay lfo5DepthRelay { ParameterIDs::LFO5_DEPTH };
    juce::WebSliderRelay lfo5ShapeRelay{ ParameterIDs::LFO5_SHAPE }; juce::WebSliderRelay lfo5SyncRelay  { ParameterIDs::LFO5_SYNC };  juce::WebSliderRelay lfo5DivRelay { ParameterIDs::LFO5_DIV };
    juce::WebSliderRelay lfo1PhaseRelay{ ParameterIDs::LFO1_PHASE }; juce::WebSliderRelay lfo2PhaseRelay{ ParameterIDs::LFO2_PHASE }; juce::WebSliderRelay lfo3PhaseRelay{ ParameterIDs::LFO3_PHASE };
    juce::WebSliderRelay lfo4PhaseRelay{ ParameterIDs::LFO4_PHASE }; juce::WebSliderRelay lfo5PhaseRelay{ ParameterIDs::LFO5_PHASE };
    // Mod matrix — LFOs 6..10
    juce::WebSliderRelay lfo6RateRelay { ParameterIDs::LFO6_RATE };  juce::WebSliderRelay lfo6DepthRelay { ParameterIDs::LFO6_DEPTH };
    juce::WebSliderRelay lfo6ShapeRelay{ ParameterIDs::LFO6_SHAPE }; juce::WebSliderRelay lfo6SyncRelay  { ParameterIDs::LFO6_SYNC };  juce::WebSliderRelay lfo6DivRelay { ParameterIDs::LFO6_DIV };  juce::WebSliderRelay lfo6PhaseRelay{ ParameterIDs::LFO6_PHASE };
    juce::WebSliderRelay lfo7RateRelay { ParameterIDs::LFO7_RATE };  juce::WebSliderRelay lfo7DepthRelay { ParameterIDs::LFO7_DEPTH };
    juce::WebSliderRelay lfo7ShapeRelay{ ParameterIDs::LFO7_SHAPE }; juce::WebSliderRelay lfo7SyncRelay  { ParameterIDs::LFO7_SYNC };  juce::WebSliderRelay lfo7DivRelay { ParameterIDs::LFO7_DIV };  juce::WebSliderRelay lfo7PhaseRelay{ ParameterIDs::LFO7_PHASE };
    juce::WebSliderRelay lfo8RateRelay { ParameterIDs::LFO8_RATE };  juce::WebSliderRelay lfo8DepthRelay { ParameterIDs::LFO8_DEPTH };
    juce::WebSliderRelay lfo8ShapeRelay{ ParameterIDs::LFO8_SHAPE }; juce::WebSliderRelay lfo8SyncRelay  { ParameterIDs::LFO8_SYNC };  juce::WebSliderRelay lfo8DivRelay { ParameterIDs::LFO8_DIV };  juce::WebSliderRelay lfo8PhaseRelay{ ParameterIDs::LFO8_PHASE };
    juce::WebSliderRelay lfo9RateRelay { ParameterIDs::LFO9_RATE };  juce::WebSliderRelay lfo9DepthRelay { ParameterIDs::LFO9_DEPTH };
    juce::WebSliderRelay lfo9ShapeRelay{ ParameterIDs::LFO9_SHAPE }; juce::WebSliderRelay lfo9SyncRelay  { ParameterIDs::LFO9_SYNC };  juce::WebSliderRelay lfo9DivRelay { ParameterIDs::LFO9_DIV };  juce::WebSliderRelay lfo9PhaseRelay{ ParameterIDs::LFO9_PHASE };
    juce::WebSliderRelay lfo10RateRelay{ ParameterIDs::LFO10_RATE }; juce::WebSliderRelay lfo10DepthRelay{ ParameterIDs::LFO10_DEPTH };
    juce::WebSliderRelay lfo10ShapeRelay{ParameterIDs::LFO10_SHAPE };juce::WebSliderRelay lfo10SyncRelay { ParameterIDs::LFO10_SYNC }; juce::WebSliderRelay lfo10DivRelay{ ParameterIDs::LFO10_DIV }; juce::WebSliderRelay lfo10PhaseRelay{ ParameterIDs::LFO10_PHASE };
    // Batch 1 Filter — without these relays the JUCE WebView backend doesn't
    // know about the new JS slider state, so setNormalisedValue from the
    // dropdown / DRV+ENV knobs never reaches APVTS — and the C++ filter type
    // never changes. That was the "everything sounds the same" bug from
    // Batch 1 Part 2 first build.
    juce::WebSliderRelay synFilter1TypeRelay  { ParameterIDs::SYN_FILTER1_TYPE };
    juce::WebSliderRelay synFilter1DrvRelay   { ParameterIDs::SYN_FILTER1_DRV };
    juce::WebSliderRelay synFilter1EnvRelay   { ParameterIDs::SYN_FILTER1_ENV };
    juce::WebSliderRelay synFilterSlotRelay   { ParameterIDs::SYN_FILTER_SLOT };
    juce::WebSliderRelay synFilter2TypeRelay  { ParameterIDs::SYN_FILTER2_TYPE };
    juce::WebSliderRelay synFilter2CutRelay   { ParameterIDs::SYN_FILTER2_CUT };
    juce::WebSliderRelay synFilter2ResRelay   { ParameterIDs::SYN_FILTER2_RES };
    juce::WebSliderRelay synFilter2DrvRelay   { ParameterIDs::SYN_FILTER2_DRV };
    juce::WebSliderRelay synFilter2EnvRelay   { ParameterIDs::SYN_FILTER2_ENV };
    juce::WebSliderRelay synFilter1MixRelay   { ParameterIDs::SYN_FILTER1_MIX };
    juce::WebSliderRelay synFilter2MixRelay   { ParameterIDs::SYN_FILTER2_MIX };
    juce::WebSliderRelay synFilterRoutingRelay{ ParameterIDs::SYN_FILTER_ROUTING };
    juce::WebSliderRelay synEnvFltARelay      { ParameterIDs::SYN_ENV_FLT_A };
    juce::WebSliderRelay synEnvFltDRelay      { ParameterIDs::SYN_ENV_FLT_D };
    juce::WebSliderRelay synEnvFltSRelay      { ParameterIDs::SYN_ENV_FLT_S };
    juce::WebSliderRelay synEnvFltRRelay      { ParameterIDs::SYN_ENV_FLT_R };
    juce::WebSliderRelay synEnvAmpARelay      { ParameterIDs::SYN_ENV_AMP_A };
    juce::WebSliderRelay synEnvAmpDRelay      { ParameterIDs::SYN_ENV_AMP_D };
    juce::WebSliderRelay synEnvAmpSRelay      { ParameterIDs::SYN_ENV_AMP_S };
    juce::WebSliderRelay synEnvAmpRRelay      { ParameterIDs::SYN_ENV_AMP_R };
    // ── Envelope DAHDSR extension relays (Batch 2/3) ──
    juce::WebSliderRelay synEnvAmpDlyRelay { ParameterIDs::SYN_ENV_AMP_DLY };
    juce::WebSliderRelay synEnvAmpHRelay { ParameterIDs::SYN_ENV_AMP_H };
    juce::WebSliderRelay synEnvAmpCaRelay { ParameterIDs::SYN_ENV_AMP_CA };
    juce::WebSliderRelay synEnvAmpCdRelay { ParameterIDs::SYN_ENV_AMP_CD };
    juce::WebSliderRelay synEnvAmpCrRelay { ParameterIDs::SYN_ENV_AMP_CR };
    juce::WebSliderRelay synEnvAmpLoopRelay { ParameterIDs::SYN_ENV_AMP_LOOP };
    juce::WebSliderRelay synEnvFltDlyRelay { ParameterIDs::SYN_ENV_FLT_DLY };
    juce::WebSliderRelay synEnvFltHRelay { ParameterIDs::SYN_ENV_FLT_H };
    juce::WebSliderRelay synEnvFltCaRelay { ParameterIDs::SYN_ENV_FLT_CA };
    juce::WebSliderRelay synEnvFltCdRelay { ParameterIDs::SYN_ENV_FLT_CD };
    juce::WebSliderRelay synEnvFltCrRelay { ParameterIDs::SYN_ENV_FLT_CR };
    juce::WebSliderRelay synEnvFltLoopRelay { ParameterIDs::SYN_ENV_FLT_LOOP };
    juce::WebSliderRelay synEnvPitDlyRelay { ParameterIDs::SYN_ENV_PIT_DLY };
    juce::WebSliderRelay synEnvPitARelay { ParameterIDs::SYN_ENV_PIT_A };
    juce::WebSliderRelay synEnvPitHRelay { ParameterIDs::SYN_ENV_PIT_H };
    juce::WebSliderRelay synEnvPitDRelay { ParameterIDs::SYN_ENV_PIT_D };
    juce::WebSliderRelay synEnvPitSRelay { ParameterIDs::SYN_ENV_PIT_S };
    juce::WebSliderRelay synEnvPitRRelay { ParameterIDs::SYN_ENV_PIT_R };
    juce::WebSliderRelay synEnvPitCaRelay { ParameterIDs::SYN_ENV_PIT_CA };
    juce::WebSliderRelay synEnvPitCdRelay { ParameterIDs::SYN_ENV_PIT_CD };
    juce::WebSliderRelay synEnvPitCrRelay { ParameterIDs::SYN_ENV_PIT_CR };
    juce::WebSliderRelay synEnvPitDepthRelay  { ParameterIDs::SYN_ENV_PIT_DEPTH };
    // Per-envelope ROUTING (envs 2–5): DEST (choice) + DEPTH (float). WebSliderRelay
    // bridges choice params too (same as synFilter*TypeRelay) so the hub's getSliderState
    // calls resolve and the Route-to dropdown + Depth slider actually write the params.
    juce::WebSliderRelay synEnv2DestRelay  { ParameterIDs::SYN_ENV2_DEST };
    juce::WebSliderRelay synEnv2DepthRelay { ParameterIDs::SYN_ENV2_DEPTH };
    juce::WebSliderRelay synEnv3DestRelay  { ParameterIDs::SYN_ENV3_DEST };
    juce::WebSliderRelay synEnv3DepthRelay { ParameterIDs::SYN_ENV3_DEPTH };
    juce::WebSliderRelay synEnv4DestRelay  { ParameterIDs::SYN_ENV4_DEST };
    juce::WebSliderRelay synEnv4DepthRelay { ParameterIDs::SYN_ENV4_DEPTH };
    juce::WebSliderRelay synEnv5DestRelay  { ParameterIDs::SYN_ENV5_DEST };
    juce::WebSliderRelay synEnv5DepthRelay { ParameterIDs::SYN_ENV5_DEPTH };
    juce::WebSliderRelay synEnvPitLoopRelay { ParameterIDs::SYN_ENV_PIT_LOOP };
    juce::WebSliderRelay synEnvM1DlyRelay { ParameterIDs::SYN_ENV_M1_DLY };
    juce::WebSliderRelay synEnvM1ARelay { ParameterIDs::SYN_ENV_M1_A };
    juce::WebSliderRelay synEnvM1HRelay { ParameterIDs::SYN_ENV_M1_H };
    juce::WebSliderRelay synEnvM1DRelay { ParameterIDs::SYN_ENV_M1_D };
    juce::WebSliderRelay synEnvM1SRelay { ParameterIDs::SYN_ENV_M1_S };
    juce::WebSliderRelay synEnvM1RRelay { ParameterIDs::SYN_ENV_M1_R };
    juce::WebSliderRelay synEnvM1CaRelay { ParameterIDs::SYN_ENV_M1_CA };
    juce::WebSliderRelay synEnvM1CdRelay { ParameterIDs::SYN_ENV_M1_CD };
    juce::WebSliderRelay synEnvM1CrRelay { ParameterIDs::SYN_ENV_M1_CR };
    juce::WebSliderRelay synEnvM1LoopRelay { ParameterIDs::SYN_ENV_M1_LOOP };
    juce::WebSliderRelay synEnvM2DlyRelay { ParameterIDs::SYN_ENV_M2_DLY };
    juce::WebSliderRelay synEnvM2ARelay { ParameterIDs::SYN_ENV_M2_A };
    juce::WebSliderRelay synEnvM2HRelay { ParameterIDs::SYN_ENV_M2_H };
    juce::WebSliderRelay synEnvM2DRelay { ParameterIDs::SYN_ENV_M2_D };
    juce::WebSliderRelay synEnvM2SRelay { ParameterIDs::SYN_ENV_M2_S };
    juce::WebSliderRelay synEnvM2RRelay { ParameterIDs::SYN_ENV_M2_R };
    juce::WebSliderRelay synEnvM2CaRelay { ParameterIDs::SYN_ENV_M2_CA };
    juce::WebSliderRelay synEnvM2CdRelay { ParameterIDs::SYN_ENV_M2_CD };
    juce::WebSliderRelay synEnvM2CrRelay { ParameterIDs::SYN_ENV_M2_CR };
    juce::WebSliderRelay synEnvM2LoopRelay { ParameterIDs::SYN_ENV_M2_LOOP };

    // Synth section — Phase 2A (wavetable)
    juce::WebSliderRelay synOscAWtPresetRelay { ParameterIDs::SYN_OSC_A_WT_PRESET };
    juce::WebSliderRelay synOscAWtFrameRelay  { ParameterIDs::SYN_OSC_A_WT_FRAME };

    // Synth section — Phase 2C (warp modes)
    juce::WebSliderRelay synOscAWarpModeRelay   { ParameterIDs::SYN_OSC_A_WARP_MODE };
    juce::WebSliderRelay synOscAWarpAmountRelay { ParameterIDs::SYN_OSC_A_WARP_AMOUNT };
    juce::WebSliderRelay synOscAWarp2ModeRelay  { ParameterIDs::SYN_OSC_A_WARP2_MODE };
    juce::WebSliderRelay synOscAWarp2AmtRelay   { ParameterIDs::SYN_OSC_A_WARP2_AMT };
    juce::WebSliderRelay synOscAPhaseModeRelay  { ParameterIDs::SYN_OSC_A_PHASE_MODE };
    juce::WebSliderRelay synOscAWaverRelay      { ParameterIDs::SYN_OSC_A_WAVER };
    juce::WebSliderRelay synOscAKeytrackRelay     { ParameterIDs::SYN_OSC_A_KEYTRACK };
    juce::WebSliderRelay synOscAKeytrackDestRelay { ParameterIDs::SYN_OSC_A_KEYTRACK_DEST };
    juce::WebSliderRelay synOscARouteSrcRelay     { ParameterIDs::SYN_OSC_A_ROUTE_SRC };
    juce::WebSliderRelay synOscARouteDestRelay    { ParameterIDs::SYN_OSC_A_ROUTE_DEST };
    juce::WebSliderRelay synOscARouteAmtRelay     { ParameterIDs::SYN_OSC_A_ROUTE_AMT };
    juce::WebSliderRelay synOscAUnisonRelay       { ParameterIDs::SYN_OSC_A_UNISON };
    juce::WebSliderRelay synOscAUdetuneRelay      { ParameterIDs::SYN_OSC_A_UDETUNE };
    juce::WebSliderRelay synOscAUblendRelay       { ParameterIDs::SYN_OSC_A_UBLEND };
    juce::WebSliderRelay synOscAUwidthRelay       { ParameterIDs::SYN_OSC_A_UWIDTH };

    // Synth section — Phase 9 (OSC B relays)
    juce::WebSliderRelay synOscBEngineRelay     { ParameterIDs::SYN_OSC_B_ENGINE };
    juce::WebSliderRelay synOscBOctRelay        { ParameterIDs::SYN_OSC_B_OCT };
    juce::WebSliderRelay synOscBSemiRelay       { ParameterIDs::SYN_OSC_B_SEMI };
    juce::WebSliderRelay synOscBCentRelay       { ParameterIDs::SYN_OSC_B_CENT };
    juce::WebSliderRelay synOscBLevelRelay      { ParameterIDs::SYN_OSC_B_LEVEL };
    juce::WebSliderRelay synOscBPanRelay        { ParameterIDs::SYN_OSC_B_PAN };
    juce::WebSliderRelay synOscBWtPresetRelay   { ParameterIDs::SYN_OSC_B_WT_PRESET };
    juce::WebSliderRelay synOscBWtFrameRelay    { ParameterIDs::SYN_OSC_B_WT_FRAME };
    juce::WebSliderRelay synOscBWarpModeRelay   { ParameterIDs::SYN_OSC_B_WARP_MODE };
    juce::WebSliderRelay synOscBWarpAmountRelay { ParameterIDs::SYN_OSC_B_WARP_AMOUNT };
    juce::WebSliderRelay synOscBWarp2ModeRelay  { ParameterIDs::SYN_OSC_B_WARP2_MODE };
    juce::WebSliderRelay synOscBWarp2AmtRelay   { ParameterIDs::SYN_OSC_B_WARP2_AMT };
    juce::WebSliderRelay synOscBPhaseModeRelay  { ParameterIDs::SYN_OSC_B_PHASE_MODE };
    juce::WebSliderRelay synOscBWaverRelay      { ParameterIDs::SYN_OSC_B_WAVER };
    juce::WebSliderRelay synOscBKeytrackRelay     { ParameterIDs::SYN_OSC_B_KEYTRACK };
    juce::WebSliderRelay synOscBKeytrackDestRelay { ParameterIDs::SYN_OSC_B_KEYTRACK_DEST };
    juce::WebSliderRelay synOscBRouteSrcRelay     { ParameterIDs::SYN_OSC_B_ROUTE_SRC };
    juce::WebSliderRelay synOscBRouteDestRelay    { ParameterIDs::SYN_OSC_B_ROUTE_DEST };
    juce::WebSliderRelay synOscBRouteAmtRelay     { ParameterIDs::SYN_OSC_B_ROUTE_AMT };
    juce::WebSliderRelay synOscBUnisonRelay       { ParameterIDs::SYN_OSC_B_UNISON };
    juce::WebSliderRelay synOscBUdetuneRelay      { ParameterIDs::SYN_OSC_B_UDETUNE };
    juce::WebSliderRelay synOscBUblendRelay       { ParameterIDs::SYN_OSC_B_UBLEND };
    juce::WebSliderRelay synOscBUwidthRelay       { ParameterIDs::SYN_OSC_B_UWIDTH };

    // Phase 11a — OSC A wavetable rework relays (6 new)
    juce::WebSliderRelay synOscASpectralTypeRelay { ParameterIDs::SYN_OSC_A_SPECTRAL_TYPE };
    juce::WebSliderRelay synOscASpectralAmtRelay  { ParameterIDs::SYN_OSC_A_SPECTRAL_AMT };
    juce::WebSliderRelay synOscAFoldShapeRelay    { ParameterIDs::SYN_OSC_A_FOLD_SHAPE };
    juce::WebSliderRelay synOscAFoldAmtRelay      { ParameterIDs::SYN_OSC_A_FOLD_AMT };
    juce::WebSliderRelay synOscAFrameSpreadRelay  { ParameterIDs::SYN_OSC_A_FRAME_SPREAD };
    juce::WebSliderRelay synOscAInterpModeRelay   { ParameterIDs::SYN_OSC_A_INTERP_MODE };
    // Phase 11a — OSC B wavetable rework relays (6 new)
    juce::WebSliderRelay synOscBSpectralTypeRelay { ParameterIDs::SYN_OSC_B_SPECTRAL_TYPE };
    juce::WebSliderRelay synOscBSpectralAmtRelay  { ParameterIDs::SYN_OSC_B_SPECTRAL_AMT };
    juce::WebSliderRelay synOscBFoldShapeRelay    { ParameterIDs::SYN_OSC_B_FOLD_SHAPE };
    juce::WebSliderRelay synOscBFoldAmtRelay      { ParameterIDs::SYN_OSC_B_FOLD_AMT };
    juce::WebSliderRelay synOscBFrameSpreadRelay  { ParameterIDs::SYN_OSC_B_FRAME_SPREAD };
    juce::WebSliderRelay synOscBInterpModeRelay   { ParameterIDs::SYN_OSC_B_INTERP_MODE };

    // Synth section — Phase 8a (Voice settings + flagship features)
    juce::WebSliderRelay synVoicesRelay  { ParameterIDs::SYN_VOICES };
    juce::WebSliderRelay synUnisonRelay  { ParameterIDs::SYN_UNISON };
    juce::WebSliderRelay synSpreadRelay  { ParameterIDs::SYN_SPREAD };
    juce::WebSliderRelay synErosionRelay { ParameterIDs::SYN_EROSION };
    juce::WebSliderRelay synHorizonRelay { ParameterIDs::SYN_HORIZON };
    juce::WebSliderRelay synPortaRelay        { ParameterIDs::SYN_PORTA };
    juce::WebSliderRelay synGlideCurveRelay   { ParameterIDs::SYN_GLIDE_CURVE };
    juce::WebSliderRelay synGlideAlwaysRelay  { ParameterIDs::SYN_GLIDE_ALWAYS };
    juce::WebSliderRelay synGlideScaledRelay  { ParameterIDs::SYN_GLIDE_SCALED };
    juce::WebSliderRelay synMonoRelay         { ParameterIDs::SYN_MONO };
    juce::WebSliderRelay synLegatoRelay       { ParameterIDs::SYN_LEGATO };
    // ── FLOW (performance engine) relays ──
    juce::WebSliderRelay flowModeRelay      { ParameterIDs::FLOW_MODE };
    juce::WebSliderRelay flowArpLatchRelay  { ParameterIDs::FLOW_ARP_LATCH };
    juce::WebSliderRelay flowArpRateRelay { ParameterIDs::FLOW_ARP_RATE }; juce::WebSliderRelay flowArpGateRelay { ParameterIDs::FLOW_ARP_GATE }; juce::WebSliderRelay flowArpVaryRelay { ParameterIDs::FLOW_ARP_VARY }; juce::WebSliderRelay flowArpTrajRelay { ParameterIDs::FLOW_ARP_TRAJ }; juce::WebSliderRelay flowArpMorphRelay { ParameterIDs::FLOW_ARP_MORPH };
    juce::WebSliderRelay flowSeqRateRelay { ParameterIDs::FLOW_SEQ_RATE }; juce::WebSliderRelay flowSeqGateRelay { ParameterIDs::FLOW_SEQ_GATE }; juce::WebSliderRelay flowSeqVaryRelay { ParameterIDs::FLOW_SEQ_VARY }; juce::WebSliderRelay flowSeqTrajRelay { ParameterIDs::FLOW_SEQ_TRAJ }; juce::WebSliderRelay flowSeqMorphRelay { ParameterIDs::FLOW_SEQ_MORPH };
    juce::WebSliderRelay flowGliRateRelay { ParameterIDs::FLOW_GLI_RATE }; juce::WebSliderRelay flowGliGateRelay { ParameterIDs::FLOW_GLI_GATE }; juce::WebSliderRelay flowGliVaryRelay { ParameterIDs::FLOW_GLI_VARY }; juce::WebSliderRelay flowGliTrajRelay { ParameterIDs::FLOW_GLI_TRAJ }; juce::WebSliderRelay flowGliMorphRelay { ParameterIDs::FLOW_GLI_MORPH };
    juce::WebSliderRelay flowDrfRateRelay { ParameterIDs::FLOW_DRF_RATE }; juce::WebSliderRelay flowDrfGateRelay { ParameterIDs::FLOW_DRF_GATE }; juce::WebSliderRelay flowDrfVaryRelay { ParameterIDs::FLOW_DRF_VARY }; juce::WebSliderRelay flowDrfTrajRelay { ParameterIDs::FLOW_DRF_TRAJ }; juce::WebSliderRelay flowDrfMorphRelay { ParameterIDs::FLOW_DRF_MORPH };

    // 2. WEBVIEW SECOND (destroyed middle)
    std::unique_ptr<juce::WebBrowserComponent> webView;

    // 2b. NATIVE CAPTURE DRAG STRIP (below WebView — receives real mouse events)
    static constexpr int CAPTURE_STRIP_HEIGHT = 26;

    class CaptureDragStrip : public juce::Component
    {
    public:
        CaptureDragStrip (TerrainInstrumentAudioProcessor& p) : processor (p) {}

        void updateState (int exportState, float availSeconds)
        {
            if (state != exportState || std::abs (avail - availSeconds) > 0.5f)
            {
                state = exportState;
                avail = availSeconds;
                repaint();
            }
        }

        int getState() const { return state; }

        void setDarkMode (bool dark)
        {
            if (isDarkMode != dark) { isDarkMode = dark; repaint(); }
        }

        // When the synth view is active, the panel forces its own dark background
        // regardless of theme — so the strip matches it (seamless, no light/dark seam
        // under the VOICING toggles).
        void setSynthViewActive (bool active)
        {
            if (synthViewActive != active) { synthViewActive = active; repaint(); }
        }

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;

    private:
        TerrainInstrumentAudioProcessor& processor;
        int state = 0;
        float avail = 0.f;
        bool mouseWasDown = false;
        bool isDragging = false;
        bool isDarkMode = false;
        bool synthViewActive = false;
    };

    CaptureDragStrip captureDragStrip { audioProcessor };

    // 3. PARAMETER ATTACHMENTS LAST (destroyed first)
    std::unique_ptr<juce::WebSliderParameterAttachment> grainSizeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> densityAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> sprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> pitchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo1RateAttachment;    // Batch 1
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo1DepthAttachment;   // Batch 1
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo1ShapeAttachment;   // Mod redesign
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo1SyncAttachment;    // Mod redesign
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo1DivAttachment;     // Mod redesign
    // Mod redesign Stage 2 — LFOs 2..5 attachments
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo2RateAttachment, lfo2DepthAttachment, lfo2ShapeAttachment, lfo2SyncAttachment, lfo2DivAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo3RateAttachment, lfo3DepthAttachment, lfo3ShapeAttachment, lfo3SyncAttachment, lfo3DivAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo4RateAttachment, lfo4DepthAttachment, lfo4ShapeAttachment, lfo4SyncAttachment, lfo4DivAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo5RateAttachment, lfo5DepthAttachment, lfo5ShapeAttachment, lfo5SyncAttachment, lfo5DivAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo1PhaseAttachment, lfo2PhaseAttachment, lfo3PhaseAttachment, lfo4PhaseAttachment, lfo5PhaseAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo6RateAttachment, lfo6DepthAttachment, lfo6ShapeAttachment, lfo6SyncAttachment, lfo6DivAttachment, lfo6PhaseAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo7RateAttachment, lfo7DepthAttachment, lfo7ShapeAttachment, lfo7SyncAttachment, lfo7DivAttachment, lfo7PhaseAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo8RateAttachment, lfo8DepthAttachment, lfo8ShapeAttachment, lfo8SyncAttachment, lfo8DivAttachment, lfo8PhaseAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo9RateAttachment, lfo9DepthAttachment, lfo9ShapeAttachment, lfo9SyncAttachment, lfo9DivAttachment, lfo9PhaseAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> lfo10RateAttachment, lfo10DepthAttachment, lfo10ShapeAttachment, lfo10SyncAttachment, lfo10DivAttachment, lfo10PhaseAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> wanderAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> freezeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> grainFilterAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> mixAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> wowFlutterAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> saturationAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> hissAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> wireWowAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> wireSaturationAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> wireHissAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> studioSculptAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> studioWeaveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> studioTiltAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> outputGainAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> masterMixAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> loopLengthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> loopFeedbackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> loopDegradeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> loopSpeedAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> spaceSizeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> spaceDecayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> spaceToneAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> spaceMixAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyTimeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyFeedbackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyToneAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyCharacterAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyModAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyModRateAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyMixAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyDuckAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyFreezeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlySyncAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlySyncDivAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyModWaveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyPitchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> dlyWidthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> chorusAmountAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> chorusWidthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> chorusCharacterAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> chopFadeAttachment;
    // Parametric EQ attachments — bind APVTS params to the eqRelays above.
    std::array<std::unique_ptr<juce::WebSliderParameterAttachment>, NUM_EQ_RELAYS> eqAttachments;

    // Synth section — Phase 1 (MPV)
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAEngineAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAOctAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASemiAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscACentAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscALevelAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAPanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1CutAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1ResAttachment;
    // Batch 1 Filter — attachments for the new params (TYPE/DRV/ENV + slot 2 reserved + FLT env)
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1TypeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1DrvAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1EnvAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilterSlotAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2TypeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2CutAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2ResAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2DrvAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2EnvAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1MixAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2MixAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilterRoutingAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltAAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltDAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltSAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltRAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpAAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpDAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpSAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpRAttachment;
    // ── Envelope DAHDSR extension attachments (Batch 2/3) ──
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpDlyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpHAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpCaAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpCdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpCrAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvAmpLoopAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltDlyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltHAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltCaAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltCdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltCrAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvFltLoopAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitDlyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitAAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitHAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitDAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitSAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitRAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitCaAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitCdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitCrAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitDepthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnv2DestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnv2DepthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnv3DestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnv3DepthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnv4DestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnv4DepthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnv5DestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnv5DepthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvPitLoopAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1DlyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1AAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1HAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1DAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1SAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1RAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1CaAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1CdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1CrAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM1LoopAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2DlyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2AAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2HAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2DAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2SAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2RAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2CaAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2CdAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2CrAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synEnvM2LoopAttachment;

    // Synth section — Phase 2A (wavetable)
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWtPresetAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWtFrameAttachment;

    // Synth section — Phase 2C (warp)
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWarp2ModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWarp2AmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBWarp2ModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBWarp2AmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWarpAmountAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAPhaseModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWaverAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAKeytrackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAKeytrackDestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscARouteSrcAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscARouteDestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscARouteAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAUnisonAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAUdetuneAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAUblendAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAUwidthAttachment;

    // Synth section — Phase 9 (OSC B attachments)
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBEngineAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBOctAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSemiAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBCentAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBLevelAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBPanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBWtPresetAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBWtFrameAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBWarpAmountAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBPhaseModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBWaverAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBKeytrackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBKeytrackDestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBRouteSrcAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBRouteDestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBRouteAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBUnisonAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBUdetuneAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBUblendAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBUwidthAttachment;

    // Phase 11a — OSC A attachments
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASpectralTypeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASpectralAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFoldShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFoldAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFrameSpreadAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAInterpModeAttachment;
    // Phase 11a — OSC B attachments
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSpectralTypeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSpectralAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBFoldShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBFoldAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBFrameSpreadAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBInterpModeAttachment;

    // Synth section — Phase 8a (Voice settings + flagship features)
    std::unique_ptr<juce::WebSliderParameterAttachment> synVoicesAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synUnisonAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synSpreadAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synErosionAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synHorizonAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synPortaAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synGlideCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synGlideAlwaysAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synGlideScaledAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synMonoAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synLegatoAttachment;
    // ── FLOW attachments ──
    std::unique_ptr<juce::WebSliderParameterAttachment> flowModeAttachment, flowArpLatchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowArpRateAttachment, flowArpGateAttachment, flowArpVaryAttachment, flowArpTrajAttachment, flowArpMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowSeqRateAttachment, flowSeqGateAttachment, flowSeqVaryAttachment, flowSeqTrajAttachment, flowSeqMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowGliRateAttachment, flowGliGateAttachment, flowGliVaryAttachment, flowGliTrajAttachment, flowGliMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowDrfRateAttachment, flowDrfGateAttachment, flowDrfVaryAttachment, flowDrfTrajAttachment, flowDrfMorphAttachment;

    // Mod state lifecycle tick counter
    // RESTORE phase: push saved JSON to JS every tick until pageReady
    // SAVE phase: pull serialized state from JS every 5 ticks (only after pageReady)
    int modStateTickCount { 0 };
    bool pageReady { false };

    // Resource provider
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainInstrumentAudioProcessorEditor)
};
