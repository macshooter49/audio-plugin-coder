#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <map>
#include "PluginProcessor.h"
#include "ParameterIDs.hpp"
#include "BlendEngine.h"

class TerrainInstrumentAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                     public juce::FileDragAndDropTarget,
                                     private juce::Timer
{
public:
    TerrainInstrumentAudioProcessorEditor (TerrainInstrumentAudioProcessor&);
    ~TerrainInstrumentAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // fb83 — called by the PROCESSOR when a popped card window closes or docks
    // (the processor owns those windows; this editor just mirrors the state into
    // its WebView so the tile can reopen the card in-plugin).
    void notifyCardWindowGone (const juce::String& id, bool redock);

    // FileDragAndDropTarget — sample / patch / pack drag-drop
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter          (const juce::StringArray& files, int x, int y) override;
    void fileDragExit           (const juce::StringArray& files) override;

private:
    int dstVizPushCtr_ = 0;   // fb354 — rate-limits the distortion-curve push to ~15 Hz
    void timerCallback() override;
    // fb214 — OSC-scope dead-feed guard: oscScopeSeq advances ONLY when the audio thread
    // publishes a frame; if it stalls (host stopped calling processBlock) the latched
    // oscScopeActive would freeze the display — we count stale ticks and fall to inactive.
    int lastOscScopeSeq_   = -1;
    int oscScopeStaleTicks_ = 0;

    // Sample / patch loading helpers (Task 11; loadPatch + importTerrainPack
    // are stubs implemented in Tasks 18 / 22 of the v0a plan)
    void loadSampleAsync      (const juce::File& file);
    void loadSampleFromMemory (juce::MemoryBlock data, const juce::String& filename);   // front sampler — sandbox-safe (no temp file)
    // Task 13: targets a specific layer index (0..3) instead of editingLayer.
    // Used by the editor constructor to fan out V2 preset reloads across all
    // 4 layer slots without temporarily changing editingLayer on the audio thread.
    void loadSampleIntoLayer  (const juce::File& file, int layerIdx);
    void loadOscSampleAsync   (int oscIdx, const juce::File& file);   // PEROSC
    void loadOscSampleFromMemory (int oscIdx, juce::MemoryBlock data, const juce::String& filename);   // PEROSC — sandbox-safe (no temp file)
    void loadNoiseSampleFromMemory (juce::MemoryBlock data, const juce::String& filename);   // NOISE IMPORT (P5) — sandbox-safe looping-noise sample
    void loadPatch         (const juce::File& patchFile);    // Task 18 stub
    void importTerrainPack (const juce::File& packFile);     // Task 22 stub

    juce::String currentSampleSourcePath;
    bool synthPageActive_ = false;   // PEROSC-DRAGGUARD — true while the synth page is visible (message-thread only)

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
    juce::WebSliderRelay synOscAEnableRelay   { ParameterIDs::SYN_OSC_A_ENABLE };
    juce::WebSliderRelay synOscAMuteRelay     { ParameterIDs::SYN_OSC_A_MUTE };
    juce::WebSliderRelay synOscASoloRelay     { ParameterIDs::SYN_OSC_A_SOLO };
    juce::WebSliderRelay synOscAPanRelay      { ParameterIDs::SYN_OSC_A_PAN };
    juce::WebSliderRelay synFilter1CutRelay   { ParameterIDs::SYN_FILTER1_CUT };
    juce::WebSliderRelay synFilter1ResRelay   { ParameterIDs::SYN_FILTER1_RES };
    juce::WebSliderRelay synFilter1KeytrackRelay { ParameterIDs::SYN_FILTER1_KEYTRACK };
    juce::WebSliderRelay synFilter2KeytrackRelay { ParameterIDs::SYN_FILTER2_KEYTRACK };
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
    juce::WebSliderRelay synFilter1VelRelay   { ParameterIDs::SYN_FILTER1_VEL };
    juce::WebSliderRelay synFilter2VelRelay   { ParameterIDs::SYN_FILTER2_VEL };
    juce::WebSliderRelay synFilter1PdrvRelay  { ParameterIDs::SYN_FILTER1_PDRV };
    juce::WebSliderRelay synFilter2PdrvRelay  { ParameterIDs::SYN_FILTER2_PDRV };
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
    juce::WebSliderRelay synOscBEnableRelay   { ParameterIDs::SYN_OSC_B_ENABLE };
    juce::WebSliderRelay synOscBMuteRelay       { ParameterIDs::SYN_OSC_B_MUTE };
    juce::WebSliderRelay synOscBSoloRelay       { ParameterIDs::SYN_OSC_B_SOLO };
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
    juce::WebSliderRelay synOscASpectralLoRelay   { ParameterIDs::SYN_OSC_A_SPECTRAL_LO };   // fb467 — partial window
    juce::WebSliderRelay synOscASpectralHiRelay   { ParameterIDs::SYN_OSC_A_SPECTRAL_HI };
    juce::WebSliderRelay synOscAFoldShapeRelay    { ParameterIDs::SYN_OSC_A_FOLD_SHAPE };
    juce::WebSliderRelay synOscAFoldAmtRelay      { ParameterIDs::SYN_OSC_A_FOLD_AMT };
    juce::WebSliderRelay synOscAFrameSpreadRelay  { ParameterIDs::SYN_OSC_A_FRAME_SPREAD };
    juce::WebSliderRelay synOscAInterpModeRelay   { ParameterIDs::SYN_OSC_A_INTERP_MODE };
    // Phase 11a — OSC B wavetable rework relays (6 new)
    juce::WebSliderRelay synOscBSpectralTypeRelay { ParameterIDs::SYN_OSC_B_SPECTRAL_TYPE };
    juce::WebSliderRelay synOscBSpectralAmtRelay  { ParameterIDs::SYN_OSC_B_SPECTRAL_AMT };
    juce::WebSliderRelay synOscBSpectralLoRelay   { ParameterIDs::SYN_OSC_B_SPECTRAL_LO };   // fb467 — partial window
    juce::WebSliderRelay synOscBSpectralHiRelay   { ParameterIDs::SYN_OSC_B_SPECTRAL_HI };
    juce::WebSliderRelay synOscBFoldShapeRelay    { ParameterIDs::SYN_OSC_B_FOLD_SHAPE };
    juce::WebSliderRelay synOscBFoldAmtRelay      { ParameterIDs::SYN_OSC_B_FOLD_AMT };
    juce::WebSliderRelay synOscBFrameSpreadRelay  { ParameterIDs::SYN_OSC_B_FRAME_SPREAD };
    juce::WebSliderRelay synOscBInterpModeRelay   { ParameterIDs::SYN_OSC_B_INTERP_MODE };
    // ── OSC C + D relays (4-osc) ──
    juce::WebSliderRelay synOscCEngineRelay     { ParameterIDs::SYN_OSC_C_ENGINE };
    juce::WebSliderRelay synOscCOctRelay        { ParameterIDs::SYN_OSC_C_OCT };
    juce::WebSliderRelay synOscCSemiRelay       { ParameterIDs::SYN_OSC_C_SEMI };
    juce::WebSliderRelay synOscCCentRelay       { ParameterIDs::SYN_OSC_C_CENT };
    juce::WebSliderRelay synOscCLevelRelay      { ParameterIDs::SYN_OSC_C_LEVEL };
    juce::WebSliderRelay synOscCEnableRelay   { ParameterIDs::SYN_OSC_C_ENABLE };
    juce::WebSliderRelay synOscCMuteRelay       { ParameterIDs::SYN_OSC_C_MUTE };
    juce::WebSliderRelay synOscCSoloRelay       { ParameterIDs::SYN_OSC_C_SOLO };
    juce::WebSliderRelay synOscCPanRelay        { ParameterIDs::SYN_OSC_C_PAN };
    juce::WebSliderRelay synOscCWtPresetRelay   { ParameterIDs::SYN_OSC_C_WT_PRESET };
    juce::WebSliderRelay synOscCWtFrameRelay    { ParameterIDs::SYN_OSC_C_WT_FRAME };
    juce::WebSliderRelay synOscCWarpModeRelay   { ParameterIDs::SYN_OSC_C_WARP_MODE };
    juce::WebSliderRelay synOscCWarpAmountRelay { ParameterIDs::SYN_OSC_C_WARP_AMOUNT };
    juce::WebSliderRelay synOscCWarp2ModeRelay  { ParameterIDs::SYN_OSC_C_WARP2_MODE };
    juce::WebSliderRelay synOscCWarp2AmtRelay   { ParameterIDs::SYN_OSC_C_WARP2_AMT };
    juce::WebSliderRelay synOscCPhaseModeRelay  { ParameterIDs::SYN_OSC_C_PHASE_MODE };
    juce::WebSliderRelay synOscCWaverRelay      { ParameterIDs::SYN_OSC_C_WAVER };
    juce::WebSliderRelay synOscCKeytrackRelay     { ParameterIDs::SYN_OSC_C_KEYTRACK };
    juce::WebSliderRelay synOscCKeytrackDestRelay { ParameterIDs::SYN_OSC_C_KEYTRACK_DEST };
    juce::WebSliderRelay synOscCRouteSrcRelay     { ParameterIDs::SYN_OSC_C_ROUTE_SRC };
    juce::WebSliderRelay synOscCRouteDestRelay    { ParameterIDs::SYN_OSC_C_ROUTE_DEST };
    juce::WebSliderRelay synOscCRouteAmtRelay     { ParameterIDs::SYN_OSC_C_ROUTE_AMT };
    juce::WebSliderRelay synOscCUnisonRelay       { ParameterIDs::SYN_OSC_C_UNISON };
    juce::WebSliderRelay synOscCUdetuneRelay      { ParameterIDs::SYN_OSC_C_UDETUNE };
    juce::WebSliderRelay synOscCUblendRelay       { ParameterIDs::SYN_OSC_C_UBLEND };
    juce::WebSliderRelay synOscCUwidthRelay       { ParameterIDs::SYN_OSC_C_UWIDTH };
    juce::WebSliderRelay synOscCSpectralTypeRelay { ParameterIDs::SYN_OSC_C_SPECTRAL_TYPE };
    juce::WebSliderRelay synOscCSpectralAmtRelay  { ParameterIDs::SYN_OSC_C_SPECTRAL_AMT };
    juce::WebSliderRelay synOscCSpectralLoRelay   { ParameterIDs::SYN_OSC_C_SPECTRAL_LO };   // fb467 — partial window
    juce::WebSliderRelay synOscCSpectralHiRelay   { ParameterIDs::SYN_OSC_C_SPECTRAL_HI };
    juce::WebSliderRelay synOscCFoldShapeRelay    { ParameterIDs::SYN_OSC_C_FOLD_SHAPE };
    juce::WebSliderRelay synOscCFoldAmtRelay      { ParameterIDs::SYN_OSC_C_FOLD_AMT };
    juce::WebSliderRelay synOscCFrameSpreadRelay  { ParameterIDs::SYN_OSC_C_FRAME_SPREAD };
    juce::WebSliderRelay synOscCInterpModeRelay   { ParameterIDs::SYN_OSC_C_INTERP_MODE };
    juce::WebSliderRelay synOscDEngineRelay     { ParameterIDs::SYN_OSC_D_ENGINE };
    juce::WebSliderRelay synOscDOctRelay        { ParameterIDs::SYN_OSC_D_OCT };
    juce::WebSliderRelay synOscDSemiRelay       { ParameterIDs::SYN_OSC_D_SEMI };
    juce::WebSliderRelay synOscDCentRelay       { ParameterIDs::SYN_OSC_D_CENT };
    juce::WebSliderRelay synOscDLevelRelay      { ParameterIDs::SYN_OSC_D_LEVEL };
    juce::WebSliderRelay synOscDEnableRelay   { ParameterIDs::SYN_OSC_D_ENABLE };
    juce::WebSliderRelay synOscDMuteRelay       { ParameterIDs::SYN_OSC_D_MUTE };
    juce::WebSliderRelay synOscDSoloRelay       { ParameterIDs::SYN_OSC_D_SOLO };
    juce::WebSliderRelay synOscDPanRelay        { ParameterIDs::SYN_OSC_D_PAN };
    juce::WebSliderRelay synOscDWtPresetRelay   { ParameterIDs::SYN_OSC_D_WT_PRESET };
    juce::WebSliderRelay synOscDWtFrameRelay    { ParameterIDs::SYN_OSC_D_WT_FRAME };
    juce::WebSliderRelay synOscDWarpModeRelay   { ParameterIDs::SYN_OSC_D_WARP_MODE };
    juce::WebSliderRelay synOscDWarpAmountRelay { ParameterIDs::SYN_OSC_D_WARP_AMOUNT };
    juce::WebSliderRelay synOscDWarp2ModeRelay  { ParameterIDs::SYN_OSC_D_WARP2_MODE };
    juce::WebSliderRelay synOscDWarp2AmtRelay   { ParameterIDs::SYN_OSC_D_WARP2_AMT };
    juce::WebSliderRelay synOscDPhaseModeRelay  { ParameterIDs::SYN_OSC_D_PHASE_MODE };
    juce::WebSliderRelay synOscDWaverRelay      { ParameterIDs::SYN_OSC_D_WAVER };
    juce::WebSliderRelay synOscDKeytrackRelay     { ParameterIDs::SYN_OSC_D_KEYTRACK };
    juce::WebSliderRelay synOscDKeytrackDestRelay { ParameterIDs::SYN_OSC_D_KEYTRACK_DEST };
    juce::WebSliderRelay synOscDRouteSrcRelay     { ParameterIDs::SYN_OSC_D_ROUTE_SRC };
    juce::WebSliderRelay synOscDRouteDestRelay    { ParameterIDs::SYN_OSC_D_ROUTE_DEST };
    juce::WebSliderRelay synOscDRouteAmtRelay     { ParameterIDs::SYN_OSC_D_ROUTE_AMT };
    juce::WebSliderRelay synOscDUnisonRelay       { ParameterIDs::SYN_OSC_D_UNISON };
    juce::WebSliderRelay synOscDUdetuneRelay      { ParameterIDs::SYN_OSC_D_UDETUNE };
    juce::WebSliderRelay synOscDUblendRelay       { ParameterIDs::SYN_OSC_D_UBLEND };
    juce::WebSliderRelay synOscDUwidthRelay       { ParameterIDs::SYN_OSC_D_UWIDTH };
    // ════ SAMPLE-ENGINE-RELAYS (Opus) ════
    juce::WebSliderRelay synOscASampleScanRelay { ParameterIDs::SYN_OSC_A_SAMPLE_SCAN };
    juce::WebSliderRelay synOscASampleStretchRelay { ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH };
    juce::WebSliderRelay synOscASampleFormantRelay { ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT };
    juce::WebSliderRelay synOscASampleSprayRelay { ParameterIDs::SYN_OSC_A_SAMPLE_SPRAY };
    juce::WebSliderRelay synOscASampleXfadeRelay { ParameterIDs::SYN_OSC_A_SAMPLE_XFADE };
    juce::WebSliderRelay synOscASampleAirRelay { ParameterIDs::SYN_OSC_A_SAMPLE_AIR };
    juce::WebSliderRelay synOscASampleWarpRelay { ParameterIDs::SYN_OSC_A_SAMPLE_WARP };
    juce::WebSliderRelay synOscASampleWarpModeRelay { ParameterIDs::SYN_OSC_A_SAMPLE_WARPMODE };
    juce::WebSliderRelay synOscASampleStartRelay { ParameterIDs::SYN_OSC_A_SAMPLE_START };
    juce::WebSliderRelay synOscASampleEndRelay { ParameterIDs::SYN_OSC_A_SAMPLE_END };
    juce::WebSliderRelay synOscASampleLoopStartRelay { ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_START };
    juce::WebSliderRelay synOscASampleLoopEndRelay { ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_END };
    juce::WebSliderRelay synOscASampleLoopModeRelay { ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE };
    juce::WebSliderRelay synOscASampleStretchModeRelay { ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH_MODE };
    juce::WebSliderRelay synOscASampleFormantModeRelay { ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT_MODE };
    juce::WebSliderRelay synOscASampleSnapRelay { ParameterIDs::SYN_OSC_A_SAMPLE_SNAP };
    juce::WebSliderRelay synOscASampleFadeInRelay { ParameterIDs::SYN_OSC_A_SAMPLE_FADE_IN };
    juce::WebSliderRelay synOscASampleFadeOutRelay { ParameterIDs::SYN_OSC_A_SAMPLE_FADE_OUT };
    juce::WebSliderRelay synOscBSampleScanRelay { ParameterIDs::SYN_OSC_B_SAMPLE_SCAN };
    juce::WebSliderRelay synOscBSampleStretchRelay { ParameterIDs::SYN_OSC_B_SAMPLE_STRETCH };
    juce::WebSliderRelay synOscBSampleFormantRelay { ParameterIDs::SYN_OSC_B_SAMPLE_FORMANT };
    juce::WebSliderRelay synOscBSampleSprayRelay { ParameterIDs::SYN_OSC_B_SAMPLE_SPRAY };
    juce::WebSliderRelay synOscBSampleXfadeRelay { ParameterIDs::SYN_OSC_B_SAMPLE_XFADE };
    juce::WebSliderRelay synOscBSampleAirRelay { ParameterIDs::SYN_OSC_B_SAMPLE_AIR };
    juce::WebSliderRelay synOscBSampleWarpRelay { ParameterIDs::SYN_OSC_B_SAMPLE_WARP };
    juce::WebSliderRelay synOscBSampleWarpModeRelay { ParameterIDs::SYN_OSC_B_SAMPLE_WARPMODE };
    juce::WebSliderRelay synOscBSampleStartRelay { ParameterIDs::SYN_OSC_B_SAMPLE_START };
    juce::WebSliderRelay synOscBSampleEndRelay { ParameterIDs::SYN_OSC_B_SAMPLE_END };
    juce::WebSliderRelay synOscBSampleLoopStartRelay { ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_START };
    juce::WebSliderRelay synOscBSampleLoopEndRelay { ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_END };
    juce::WebSliderRelay synOscBSampleLoopModeRelay { ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_MODE };
    juce::WebSliderRelay synOscBSampleStretchModeRelay { ParameterIDs::SYN_OSC_B_SAMPLE_STRETCH_MODE };
    juce::WebSliderRelay synOscBSampleFormantModeRelay { ParameterIDs::SYN_OSC_B_SAMPLE_FORMANT_MODE };
    juce::WebSliderRelay synOscBSampleSnapRelay { ParameterIDs::SYN_OSC_B_SAMPLE_SNAP };
    juce::WebSliderRelay synOscBSampleFadeInRelay { ParameterIDs::SYN_OSC_B_SAMPLE_FADE_IN };
    juce::WebSliderRelay synOscBSampleFadeOutRelay { ParameterIDs::SYN_OSC_B_SAMPLE_FADE_OUT };
    juce::WebSliderRelay synOscCSampleScanRelay { ParameterIDs::SYN_OSC_C_SAMPLE_SCAN };
    juce::WebSliderRelay synOscCSampleStretchRelay { ParameterIDs::SYN_OSC_C_SAMPLE_STRETCH };
    juce::WebSliderRelay synOscCSampleFormantRelay { ParameterIDs::SYN_OSC_C_SAMPLE_FORMANT };
    juce::WebSliderRelay synOscCSampleSprayRelay { ParameterIDs::SYN_OSC_C_SAMPLE_SPRAY };
    juce::WebSliderRelay synOscCSampleXfadeRelay { ParameterIDs::SYN_OSC_C_SAMPLE_XFADE };
    juce::WebSliderRelay synOscCSampleAirRelay { ParameterIDs::SYN_OSC_C_SAMPLE_AIR };
    juce::WebSliderRelay synOscCSampleWarpRelay { ParameterIDs::SYN_OSC_C_SAMPLE_WARP };
    juce::WebSliderRelay synOscCSampleWarpModeRelay { ParameterIDs::SYN_OSC_C_SAMPLE_WARPMODE };
    juce::WebSliderRelay synOscCSampleStartRelay { ParameterIDs::SYN_OSC_C_SAMPLE_START };
    juce::WebSliderRelay synOscCSampleEndRelay { ParameterIDs::SYN_OSC_C_SAMPLE_END };
    juce::WebSliderRelay synOscCSampleLoopStartRelay { ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_START };
    juce::WebSliderRelay synOscCSampleLoopEndRelay { ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_END };
    juce::WebSliderRelay synOscCSampleLoopModeRelay { ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_MODE };
    juce::WebSliderRelay synOscCSampleStretchModeRelay { ParameterIDs::SYN_OSC_C_SAMPLE_STRETCH_MODE };
    juce::WebSliderRelay synOscCSampleFormantModeRelay { ParameterIDs::SYN_OSC_C_SAMPLE_FORMANT_MODE };
    juce::WebSliderRelay synOscCSampleSnapRelay { ParameterIDs::SYN_OSC_C_SAMPLE_SNAP };
    juce::WebSliderRelay synOscCSampleFadeInRelay { ParameterIDs::SYN_OSC_C_SAMPLE_FADE_IN };
    juce::WebSliderRelay synOscCSampleFadeOutRelay { ParameterIDs::SYN_OSC_C_SAMPLE_FADE_OUT };
    juce::WebSliderRelay synOscDSampleScanRelay { ParameterIDs::SYN_OSC_D_SAMPLE_SCAN };
    juce::WebSliderRelay synOscDSampleStretchRelay { ParameterIDs::SYN_OSC_D_SAMPLE_STRETCH };
    juce::WebSliderRelay synOscDSampleFormantRelay { ParameterIDs::SYN_OSC_D_SAMPLE_FORMANT };
    juce::WebSliderRelay synOscDSampleSprayRelay { ParameterIDs::SYN_OSC_D_SAMPLE_SPRAY };
    juce::WebSliderRelay synOscDSampleXfadeRelay { ParameterIDs::SYN_OSC_D_SAMPLE_XFADE };
    juce::WebSliderRelay synOscDSampleAirRelay { ParameterIDs::SYN_OSC_D_SAMPLE_AIR };
    juce::WebSliderRelay synOscDSampleWarpRelay { ParameterIDs::SYN_OSC_D_SAMPLE_WARP };
    juce::WebSliderRelay synOscDSampleWarpModeRelay { ParameterIDs::SYN_OSC_D_SAMPLE_WARPMODE };
    juce::WebSliderRelay synOscDSampleStartRelay { ParameterIDs::SYN_OSC_D_SAMPLE_START };
    juce::WebSliderRelay synOscDSampleEndRelay { ParameterIDs::SYN_OSC_D_SAMPLE_END };
    juce::WebSliderRelay synOscDSampleLoopStartRelay { ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_START };
    juce::WebSliderRelay synOscDSampleLoopEndRelay { ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_END };
    juce::WebSliderRelay synOscDSampleLoopModeRelay { ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_MODE };
    juce::WebSliderRelay synOscDSampleStretchModeRelay { ParameterIDs::SYN_OSC_D_SAMPLE_STRETCH_MODE };
    juce::WebSliderRelay synOscDSampleFormantModeRelay { ParameterIDs::SYN_OSC_D_SAMPLE_FORMANT_MODE };
    juce::WebSliderRelay synOscDSampleSnapRelay { ParameterIDs::SYN_OSC_D_SAMPLE_SNAP };
    juce::WebSliderRelay synOscDSampleFadeInRelay { ParameterIDs::SYN_OSC_D_SAMPLE_FADE_IN };
    juce::WebSliderRelay synOscDSampleFadeOutRelay { ParameterIDs::SYN_OSC_D_SAMPLE_FADE_OUT };

    // ── GRAIN-ENGINE-RELAYS — per-OSC granular params (6 × 4 osc) ──
    juce::WebSliderRelay synOscAGrainScanRelay    { ParameterIDs::SYN_OSC_A_GRAIN_SCAN };
    juce::WebSliderRelay synOscAGrainDensityRelay { ParameterIDs::SYN_OSC_A_GRAIN_DENSITY };
    juce::WebSliderRelay synOscAGrainSizeRelay    { ParameterIDs::SYN_OSC_A_GRAIN_SIZE };
    juce::WebSliderRelay synOscAGrainSprayRelay   { ParameterIDs::SYN_OSC_A_GRAIN_SPRAY };
    juce::WebSliderRelay synOscAGrainShapeRelay   { ParameterIDs::SYN_OSC_A_GRAIN_SHAPE };
    juce::WebSliderRelay synOscAGrainKeyRelay     { ParameterIDs::SYN_OSC_A_GRAIN_KEY };
    juce::WebSliderRelay synOscBGrainScanRelay    { ParameterIDs::SYN_OSC_B_GRAIN_SCAN };
    juce::WebSliderRelay synOscBGrainDensityRelay { ParameterIDs::SYN_OSC_B_GRAIN_DENSITY };
    juce::WebSliderRelay synOscBGrainSizeRelay    { ParameterIDs::SYN_OSC_B_GRAIN_SIZE };
    juce::WebSliderRelay synOscBGrainSprayRelay   { ParameterIDs::SYN_OSC_B_GRAIN_SPRAY };
    juce::WebSliderRelay synOscBGrainShapeRelay   { ParameterIDs::SYN_OSC_B_GRAIN_SHAPE };
    juce::WebSliderRelay synOscBGrainKeyRelay     { ParameterIDs::SYN_OSC_B_GRAIN_KEY };
    juce::WebSliderRelay synOscCGrainScanRelay    { ParameterIDs::SYN_OSC_C_GRAIN_SCAN };
    juce::WebSliderRelay synOscCGrainDensityRelay { ParameterIDs::SYN_OSC_C_GRAIN_DENSITY };
    juce::WebSliderRelay synOscCGrainSizeRelay    { ParameterIDs::SYN_OSC_C_GRAIN_SIZE };
    juce::WebSliderRelay synOscCGrainSprayRelay   { ParameterIDs::SYN_OSC_C_GRAIN_SPRAY };
    juce::WebSliderRelay synOscCGrainShapeRelay   { ParameterIDs::SYN_OSC_C_GRAIN_SHAPE };
    juce::WebSliderRelay synOscCGrainKeyRelay     { ParameterIDs::SYN_OSC_C_GRAIN_KEY };
    juce::WebSliderRelay synOscDGrainScanRelay    { ParameterIDs::SYN_OSC_D_GRAIN_SCAN };
    juce::WebSliderRelay synOscDGrainDensityRelay { ParameterIDs::SYN_OSC_D_GRAIN_DENSITY };
    juce::WebSliderRelay synOscDGrainSizeRelay    { ParameterIDs::SYN_OSC_D_GRAIN_SIZE };
    juce::WebSliderRelay synOscDGrainSprayRelay   { ParameterIDs::SYN_OSC_D_GRAIN_SPRAY };
    juce::WebSliderRelay synOscDGrainShapeRelay   { ParameterIDs::SYN_OSC_D_GRAIN_SHAPE };
    juce::WebSliderRelay synOscDGrainKeyRelay     { ParameterIDs::SYN_OSC_D_GRAIN_KEY };
    // ════ FADE CURVE relays ════
    juce::WebSliderRelay synOscAFadeInCurveRelay { ParameterIDs::SYN_OSC_A_SAMPLE_FADEIN_CURVE };
    juce::WebSliderRelay synOscAFadeOutCurveRelay { ParameterIDs::SYN_OSC_A_SAMPLE_FADEOUT_CURVE };
    juce::WebSliderRelay synOscBFadeInCurveRelay { ParameterIDs::SYN_OSC_B_SAMPLE_FADEIN_CURVE };
    juce::WebSliderRelay synOscBFadeOutCurveRelay { ParameterIDs::SYN_OSC_B_SAMPLE_FADEOUT_CURVE };
    juce::WebSliderRelay synOscCFadeInCurveRelay { ParameterIDs::SYN_OSC_C_SAMPLE_FADEIN_CURVE };
    juce::WebSliderRelay synOscCFadeOutCurveRelay { ParameterIDs::SYN_OSC_C_SAMPLE_FADEOUT_CURVE };
    juce::WebSliderRelay synOscDFadeInCurveRelay { ParameterIDs::SYN_OSC_D_SAMPLE_FADEIN_CURVE };
    juce::WebSliderRelay synOscDFadeOutCurveRelay { ParameterIDs::SYN_OSC_D_SAMPLE_FADEOUT_CURVE };
    // ════ BLEND relays (per-osc one-shot blend knobs) ════
    juce::WebSliderRelay synOscABlendMorphRelay            { ParameterIDs::SYN_OSC_A_BLEND_MORPH };
    juce::WebSliderRelay synOscABlendAttackRelay           { ParameterIDs::SYN_OSC_A_BLEND_ATTACK };
    juce::WebSliderRelay synOscABlendBodyRelay             { ParameterIDs::SYN_OSC_A_BLEND_BODY };
    juce::WebSliderRelay synOscABlendBreathRelay           { ParameterIDs::SYN_OSC_A_BLEND_BREATH };
    juce::WebSliderRelay synOscABlendSculptRelay           { ParameterIDs::SYN_OSC_A_BLEND_SCULPT };
    juce::WebSliderRelay synOscABlendDiceRelay             { ParameterIDs::SYN_OSC_A_BLEND_DICE };
    juce::WebSliderRelay synOscBBlendMorphRelay            { ParameterIDs::SYN_OSC_B_BLEND_MORPH };
    juce::WebSliderRelay synOscBBlendAttackRelay           { ParameterIDs::SYN_OSC_B_BLEND_ATTACK };
    juce::WebSliderRelay synOscBBlendBodyRelay             { ParameterIDs::SYN_OSC_B_BLEND_BODY };
    juce::WebSliderRelay synOscBBlendBreathRelay           { ParameterIDs::SYN_OSC_B_BLEND_BREATH };
    juce::WebSliderRelay synOscBBlendSculptRelay           { ParameterIDs::SYN_OSC_B_BLEND_SCULPT };
    juce::WebSliderRelay synOscBBlendDiceRelay             { ParameterIDs::SYN_OSC_B_BLEND_DICE };
    juce::WebSliderRelay synOscCBlendMorphRelay            { ParameterIDs::SYN_OSC_C_BLEND_MORPH };
    juce::WebSliderRelay synOscCBlendAttackRelay           { ParameterIDs::SYN_OSC_C_BLEND_ATTACK };
    juce::WebSliderRelay synOscCBlendBodyRelay             { ParameterIDs::SYN_OSC_C_BLEND_BODY };
    juce::WebSliderRelay synOscCBlendBreathRelay           { ParameterIDs::SYN_OSC_C_BLEND_BREATH };
    juce::WebSliderRelay synOscCBlendSculptRelay           { ParameterIDs::SYN_OSC_C_BLEND_SCULPT };
    juce::WebSliderRelay synOscCBlendDiceRelay             { ParameterIDs::SYN_OSC_C_BLEND_DICE };
    juce::WebSliderRelay synOscDBlendMorphRelay            { ParameterIDs::SYN_OSC_D_BLEND_MORPH };
    juce::WebSliderRelay synOscDBlendAttackRelay           { ParameterIDs::SYN_OSC_D_BLEND_ATTACK };
    juce::WebSliderRelay synOscDBlendBodyRelay             { ParameterIDs::SYN_OSC_D_BLEND_BODY };
    juce::WebSliderRelay synOscDBlendBreathRelay           { ParameterIDs::SYN_OSC_D_BLEND_BREATH };
    juce::WebSliderRelay synOscDBlendSculptRelay           { ParameterIDs::SYN_OSC_D_BLEND_SCULPT };
    juce::WebSliderRelay synOscDBlendDiceRelay             { ParameterIDs::SYN_OSC_D_BLEND_DICE };
    // ════ FM-ENGINE relays — wavetable-carrier FM (6 × 4 osc) ════
    juce::WebSliderRelay synOscAFmAlgoRelay   { ParameterIDs::SYN_OSC_A_FM_ALGO };
    juce::WebSliderRelay synOscAFmRatio1Relay { ParameterIDs::SYN_OSC_A_FM_RATIO1 };
    juce::WebSliderRelay synOscAFmDepth1Relay { ParameterIDs::SYN_OSC_A_FM_DEPTH1 };
    juce::WebSliderRelay synOscAFmRatio2Relay { ParameterIDs::SYN_OSC_A_FM_RATIO2 };
    juce::WebSliderRelay synOscAFmDepth2Relay { ParameterIDs::SYN_OSC_A_FM_DEPTH2 };
    juce::WebSliderRelay synOscAFmFbRelay     { ParameterIDs::SYN_OSC_A_FM_FB };
    juce::WebSliderRelay synOscBFmAlgoRelay   { ParameterIDs::SYN_OSC_B_FM_ALGO };
    juce::WebSliderRelay synOscBFmRatio1Relay { ParameterIDs::SYN_OSC_B_FM_RATIO1 };
    juce::WebSliderRelay synOscBFmDepth1Relay { ParameterIDs::SYN_OSC_B_FM_DEPTH1 };
    juce::WebSliderRelay synOscBFmRatio2Relay { ParameterIDs::SYN_OSC_B_FM_RATIO2 };
    juce::WebSliderRelay synOscBFmDepth2Relay { ParameterIDs::SYN_OSC_B_FM_DEPTH2 };
    juce::WebSliderRelay synOscBFmFbRelay     { ParameterIDs::SYN_OSC_B_FM_FB };
    juce::WebSliderRelay synOscCFmAlgoRelay   { ParameterIDs::SYN_OSC_C_FM_ALGO };
    juce::WebSliderRelay synOscCFmRatio1Relay { ParameterIDs::SYN_OSC_C_FM_RATIO1 };
    juce::WebSliderRelay synOscCFmDepth1Relay { ParameterIDs::SYN_OSC_C_FM_DEPTH1 };
    juce::WebSliderRelay synOscCFmRatio2Relay { ParameterIDs::SYN_OSC_C_FM_RATIO2 };
    juce::WebSliderRelay synOscCFmDepth2Relay { ParameterIDs::SYN_OSC_C_FM_DEPTH2 };
    juce::WebSliderRelay synOscCFmFbRelay     { ParameterIDs::SYN_OSC_C_FM_FB };
    juce::WebSliderRelay synOscDFmAlgoRelay   { ParameterIDs::SYN_OSC_D_FM_ALGO };
    juce::WebSliderRelay synOscDFmRatio1Relay { ParameterIDs::SYN_OSC_D_FM_RATIO1 };
    juce::WebSliderRelay synOscDFmDepth1Relay { ParameterIDs::SYN_OSC_D_FM_DEPTH1 };
    juce::WebSliderRelay synOscDFmRatio2Relay { ParameterIDs::SYN_OSC_D_FM_RATIO2 };
    juce::WebSliderRelay synOscDFmDepth2Relay { ParameterIDs::SYN_OSC_D_FM_DEPTH2 };
    juce::WebSliderRelay synOscDFmFbRelay     { ParameterIDs::SYN_OSC_D_FM_FB };
    // ════ FM WEATHERING relays — page-2 suite (6 × 4 osc) ════
    juce::WebSliderRelay synOscAFmStrikeRelay { ParameterIDs::SYN_OSC_A_FM_STRIKE };
    juce::WebSliderRelay synOscAFmAgeRelay    { ParameterIDs::SYN_OSC_A_FM_AGE };
    juce::WebSliderRelay synOscAFmRustRelay   { ParameterIDs::SYN_OSC_A_FM_RUST };
    juce::WebSliderRelay synOscAFmGaleRelay   { ParameterIDs::SYN_OSC_A_FM_GALE };
    juce::WebSliderRelay synOscAFmBendRelay   { ParameterIDs::SYN_OSC_A_FM_BEND };
    juce::WebSliderRelay synOscAFmStormRelay  { ParameterIDs::SYN_OSC_A_FM_STORM };
    juce::WebSliderRelay synOscBFmStrikeRelay { ParameterIDs::SYN_OSC_B_FM_STRIKE };
    juce::WebSliderRelay synOscBFmAgeRelay    { ParameterIDs::SYN_OSC_B_FM_AGE };
    juce::WebSliderRelay synOscBFmRustRelay   { ParameterIDs::SYN_OSC_B_FM_RUST };
    juce::WebSliderRelay synOscBFmGaleRelay   { ParameterIDs::SYN_OSC_B_FM_GALE };
    juce::WebSliderRelay synOscBFmBendRelay   { ParameterIDs::SYN_OSC_B_FM_BEND };
    juce::WebSliderRelay synOscBFmStormRelay  { ParameterIDs::SYN_OSC_B_FM_STORM };
    juce::WebSliderRelay synOscCFmStrikeRelay { ParameterIDs::SYN_OSC_C_FM_STRIKE };
    juce::WebSliderRelay synOscCFmAgeRelay    { ParameterIDs::SYN_OSC_C_FM_AGE };
    juce::WebSliderRelay synOscCFmRustRelay   { ParameterIDs::SYN_OSC_C_FM_RUST };
    juce::WebSliderRelay synOscCFmGaleRelay   { ParameterIDs::SYN_OSC_C_FM_GALE };
    juce::WebSliderRelay synOscCFmBendRelay   { ParameterIDs::SYN_OSC_C_FM_BEND };
    juce::WebSliderRelay synOscCFmStormRelay  { ParameterIDs::SYN_OSC_C_FM_STORM };
    juce::WebSliderRelay synOscDFmStrikeRelay { ParameterIDs::SYN_OSC_D_FM_STRIKE };
    juce::WebSliderRelay synOscDFmAgeRelay    { ParameterIDs::SYN_OSC_D_FM_AGE };
    juce::WebSliderRelay synOscDFmRustRelay   { ParameterIDs::SYN_OSC_D_FM_RUST };
    juce::WebSliderRelay synOscDFmGaleRelay   { ParameterIDs::SYN_OSC_D_FM_GALE };
    juce::WebSliderRelay synOscDFmBendRelay   { ParameterIDs::SYN_OSC_D_FM_BEND };
    juce::WebSliderRelay synOscDFmStormRelay  { ParameterIDs::SYN_OSC_D_FM_STORM };
    // ════ HARM-ENGINE relays — harmonic additive oscillator (14 × 4 osc) ════
    juce::WebSliderRelay synOscAHarmModeRelay  { ParameterIDs::SYN_OSC_A_HARM_MODE };
    juce::WebSliderRelay synOscAHarmSculptRelay { ParameterIDs::SYN_OSC_A_HARM_SCULPT };
    juce::WebSliderRelay synOscAHarmHueRelay   { ParameterIDs::SYN_OSC_A_HARM_HUE };
    juce::WebSliderRelay synOscAHarmCountRelay { ParameterIDs::SYN_OSC_A_HARM_COUNT };
    juce::WebSliderRelay synOscAHarmLeanRelay  { ParameterIDs::SYN_OSC_A_HARM_LEAN };
    juce::WebSliderRelay synOscAHarmFanRelay   { ParameterIDs::SYN_OSC_A_HARM_FAN };
    juce::WebSliderRelay synOscAHarmGritRelay  { ParameterIDs::SYN_OSC_A_HARM_GRIT };
    juce::WebSliderRelay synOscAHarmBraidRelay { ParameterIDs::SYN_OSC_A_HARM_BRAID };
    juce::WebSliderRelay synOscAHarmCarveRelay { ParameterIDs::SYN_OSC_A_HARM_CARVE };
    juce::WebSliderRelay synOscAHarmChurnRelay { ParameterIDs::SYN_OSC_A_HARM_CHURN };
    juce::WebSliderRelay synOscAHarmRootRelay  { ParameterIDs::SYN_OSC_A_HARM_ROOT };
    juce::WebSliderRelay synOscAHarmShineRelay { ParameterIDs::SYN_OSC_A_HARM_SHINE };
    juce::WebSliderRelay synOscAHarmWiltRelay  { ParameterIDs::SYN_OSC_A_HARM_WILT };
    juce::WebSliderRelay synOscAHarmFizzRelay  { ParameterIDs::SYN_OSC_A_HARM_FIZZ };
    juce::WebSliderRelay synOscBHarmModeRelay  { ParameterIDs::SYN_OSC_B_HARM_MODE };
    juce::WebSliderRelay synOscBHarmSculptRelay { ParameterIDs::SYN_OSC_B_HARM_SCULPT };
    juce::WebSliderRelay synOscBHarmHueRelay   { ParameterIDs::SYN_OSC_B_HARM_HUE };
    juce::WebSliderRelay synOscBHarmCountRelay { ParameterIDs::SYN_OSC_B_HARM_COUNT };
    juce::WebSliderRelay synOscBHarmLeanRelay  { ParameterIDs::SYN_OSC_B_HARM_LEAN };
    juce::WebSliderRelay synOscBHarmFanRelay   { ParameterIDs::SYN_OSC_B_HARM_FAN };
    juce::WebSliderRelay synOscBHarmGritRelay  { ParameterIDs::SYN_OSC_B_HARM_GRIT };
    juce::WebSliderRelay synOscBHarmBraidRelay { ParameterIDs::SYN_OSC_B_HARM_BRAID };
    juce::WebSliderRelay synOscBHarmCarveRelay { ParameterIDs::SYN_OSC_B_HARM_CARVE };
    juce::WebSliderRelay synOscBHarmChurnRelay { ParameterIDs::SYN_OSC_B_HARM_CHURN };
    juce::WebSliderRelay synOscBHarmRootRelay  { ParameterIDs::SYN_OSC_B_HARM_ROOT };
    juce::WebSliderRelay synOscBHarmShineRelay { ParameterIDs::SYN_OSC_B_HARM_SHINE };
    juce::WebSliderRelay synOscBHarmWiltRelay  { ParameterIDs::SYN_OSC_B_HARM_WILT };
    juce::WebSliderRelay synOscBHarmFizzRelay  { ParameterIDs::SYN_OSC_B_HARM_FIZZ };
    juce::WebSliderRelay synOscCHarmModeRelay  { ParameterIDs::SYN_OSC_C_HARM_MODE };
    juce::WebSliderRelay synOscCHarmSculptRelay { ParameterIDs::SYN_OSC_C_HARM_SCULPT };
    juce::WebSliderRelay synOscCHarmHueRelay   { ParameterIDs::SYN_OSC_C_HARM_HUE };
    juce::WebSliderRelay synOscCHarmCountRelay { ParameterIDs::SYN_OSC_C_HARM_COUNT };
    juce::WebSliderRelay synOscCHarmLeanRelay  { ParameterIDs::SYN_OSC_C_HARM_LEAN };
    juce::WebSliderRelay synOscCHarmFanRelay   { ParameterIDs::SYN_OSC_C_HARM_FAN };
    juce::WebSliderRelay synOscCHarmGritRelay  { ParameterIDs::SYN_OSC_C_HARM_GRIT };
    juce::WebSliderRelay synOscCHarmBraidRelay { ParameterIDs::SYN_OSC_C_HARM_BRAID };
    juce::WebSliderRelay synOscCHarmCarveRelay { ParameterIDs::SYN_OSC_C_HARM_CARVE };
    juce::WebSliderRelay synOscCHarmChurnRelay { ParameterIDs::SYN_OSC_C_HARM_CHURN };
    juce::WebSliderRelay synOscCHarmRootRelay  { ParameterIDs::SYN_OSC_C_HARM_ROOT };
    juce::WebSliderRelay synOscCHarmShineRelay { ParameterIDs::SYN_OSC_C_HARM_SHINE };
    juce::WebSliderRelay synOscCHarmWiltRelay  { ParameterIDs::SYN_OSC_C_HARM_WILT };
    juce::WebSliderRelay synOscCHarmFizzRelay  { ParameterIDs::SYN_OSC_C_HARM_FIZZ };
    juce::WebSliderRelay synOscDHarmModeRelay  { ParameterIDs::SYN_OSC_D_HARM_MODE };
    juce::WebSliderRelay synOscDHarmSculptRelay { ParameterIDs::SYN_OSC_D_HARM_SCULPT };
    juce::WebSliderRelay synOscDHarmHueRelay   { ParameterIDs::SYN_OSC_D_HARM_HUE };
    juce::WebSliderRelay synOscDHarmCountRelay { ParameterIDs::SYN_OSC_D_HARM_COUNT };
    juce::WebSliderRelay synOscDHarmLeanRelay  { ParameterIDs::SYN_OSC_D_HARM_LEAN };
    juce::WebSliderRelay synOscDHarmFanRelay   { ParameterIDs::SYN_OSC_D_HARM_FAN };
    juce::WebSliderRelay synOscDHarmGritRelay  { ParameterIDs::SYN_OSC_D_HARM_GRIT };
    juce::WebSliderRelay synOscDHarmBraidRelay { ParameterIDs::SYN_OSC_D_HARM_BRAID };
    juce::WebSliderRelay synOscDHarmCarveRelay { ParameterIDs::SYN_OSC_D_HARM_CARVE };
    juce::WebSliderRelay synOscDHarmChurnRelay { ParameterIDs::SYN_OSC_D_HARM_CHURN };
    juce::WebSliderRelay synOscDHarmRootRelay  { ParameterIDs::SYN_OSC_D_HARM_ROOT };
    juce::WebSliderRelay synOscDHarmShineRelay { ParameterIDs::SYN_OSC_D_HARM_SHINE };
    juce::WebSliderRelay synOscDHarmWiltRelay  { ParameterIDs::SYN_OSC_D_HARM_WILT };
    juce::WebSliderRelay synOscDHarmFizzRelay  { ParameterIDs::SYN_OSC_D_HARM_FIZZ };

    // ════ MODAL-ENGINE relays — physical modeling oscillator (13 × 4 osc) ════
    juce::WebSliderRelay synOscAModalFamilyRelay { ParameterIDs::SYN_OSC_A_MODAL_FAMILY };
    juce::WebSliderRelay synOscAModalFormRelay { ParameterIDs::SYN_OSC_A_MODAL_FORM };
    juce::WebSliderRelay synOscAModalSourceRelay { ParameterIDs::SYN_OSC_A_MODAL_SOURCE };
    juce::WebSliderRelay synOscAModalHardRelay { ParameterIDs::SYN_OSC_A_MODAL_HARD };
    juce::WebSliderRelay synOscAModalPosRelay { ParameterIDs::SYN_OSC_A_MODAL_POS };
    juce::WebSliderRelay synOscAModalDecayRelay { ParameterIDs::SYN_OSC_A_MODAL_DECAY };
    juce::WebSliderRelay synOscAModalMaterialRelay { ParameterIDs::SYN_OSC_A_MODAL_MATERIAL };
    juce::WebSliderRelay synOscAModalBreathRelay { ParameterIDs::SYN_OSC_A_MODAL_BREATH };
    juce::WebSliderRelay synOscAModalStretchRelay { ParameterIDs::SYN_OSC_A_MODAL_STRETCH };
    juce::WebSliderRelay synOscAModalBloomRelay { ParameterIDs::SYN_OSC_A_MODAL_BLOOM };
    juce::WebSliderRelay synOscAModalHaloRelay { ParameterIDs::SYN_OSC_A_MODAL_HALO };
    juce::WebSliderRelay synOscAModalAgeRelay { ParameterIDs::SYN_OSC_A_MODAL_AGE };
    juce::WebSliderRelay synOscAModalBodyRelay { ParameterIDs::SYN_OSC_A_MODAL_BODY };
    juce::WebSliderRelay synOscBModalFamilyRelay { ParameterIDs::SYN_OSC_B_MODAL_FAMILY };
    juce::WebSliderRelay synOscBModalFormRelay { ParameterIDs::SYN_OSC_B_MODAL_FORM };
    juce::WebSliderRelay synOscBModalSourceRelay { ParameterIDs::SYN_OSC_B_MODAL_SOURCE };
    juce::WebSliderRelay synOscBModalHardRelay { ParameterIDs::SYN_OSC_B_MODAL_HARD };
    juce::WebSliderRelay synOscBModalPosRelay { ParameterIDs::SYN_OSC_B_MODAL_POS };
    juce::WebSliderRelay synOscBModalDecayRelay { ParameterIDs::SYN_OSC_B_MODAL_DECAY };
    juce::WebSliderRelay synOscBModalMaterialRelay { ParameterIDs::SYN_OSC_B_MODAL_MATERIAL };
    juce::WebSliderRelay synOscBModalBreathRelay { ParameterIDs::SYN_OSC_B_MODAL_BREATH };
    juce::WebSliderRelay synOscBModalStretchRelay { ParameterIDs::SYN_OSC_B_MODAL_STRETCH };
    juce::WebSliderRelay synOscBModalBloomRelay { ParameterIDs::SYN_OSC_B_MODAL_BLOOM };
    juce::WebSliderRelay synOscBModalHaloRelay { ParameterIDs::SYN_OSC_B_MODAL_HALO };
    juce::WebSliderRelay synOscBModalAgeRelay { ParameterIDs::SYN_OSC_B_MODAL_AGE };
    juce::WebSliderRelay synOscBModalBodyRelay { ParameterIDs::SYN_OSC_B_MODAL_BODY };
    juce::WebSliderRelay synOscCModalFamilyRelay { ParameterIDs::SYN_OSC_C_MODAL_FAMILY };
    juce::WebSliderRelay synOscCModalFormRelay { ParameterIDs::SYN_OSC_C_MODAL_FORM };
    juce::WebSliderRelay synOscCModalSourceRelay { ParameterIDs::SYN_OSC_C_MODAL_SOURCE };
    juce::WebSliderRelay synOscCModalHardRelay { ParameterIDs::SYN_OSC_C_MODAL_HARD };
    juce::WebSliderRelay synOscCModalPosRelay { ParameterIDs::SYN_OSC_C_MODAL_POS };
    juce::WebSliderRelay synOscCModalDecayRelay { ParameterIDs::SYN_OSC_C_MODAL_DECAY };
    juce::WebSliderRelay synOscCModalMaterialRelay { ParameterIDs::SYN_OSC_C_MODAL_MATERIAL };
    juce::WebSliderRelay synOscCModalBreathRelay { ParameterIDs::SYN_OSC_C_MODAL_BREATH };
    juce::WebSliderRelay synOscCModalStretchRelay { ParameterIDs::SYN_OSC_C_MODAL_STRETCH };
    juce::WebSliderRelay synOscCModalBloomRelay { ParameterIDs::SYN_OSC_C_MODAL_BLOOM };
    juce::WebSliderRelay synOscCModalHaloRelay { ParameterIDs::SYN_OSC_C_MODAL_HALO };
    juce::WebSliderRelay synOscCModalAgeRelay { ParameterIDs::SYN_OSC_C_MODAL_AGE };
    juce::WebSliderRelay synOscCModalBodyRelay { ParameterIDs::SYN_OSC_C_MODAL_BODY };
    juce::WebSliderRelay synOscDModalFamilyRelay { ParameterIDs::SYN_OSC_D_MODAL_FAMILY };
    juce::WebSliderRelay synOscDModalFormRelay { ParameterIDs::SYN_OSC_D_MODAL_FORM };
    juce::WebSliderRelay synOscDModalSourceRelay { ParameterIDs::SYN_OSC_D_MODAL_SOURCE };
    juce::WebSliderRelay synOscDModalHardRelay { ParameterIDs::SYN_OSC_D_MODAL_HARD };
    juce::WebSliderRelay synOscDModalPosRelay { ParameterIDs::SYN_OSC_D_MODAL_POS };
    juce::WebSliderRelay synOscDModalDecayRelay { ParameterIDs::SYN_OSC_D_MODAL_DECAY };
    juce::WebSliderRelay synOscDModalMaterialRelay { ParameterIDs::SYN_OSC_D_MODAL_MATERIAL };
    juce::WebSliderRelay synOscDModalBreathRelay { ParameterIDs::SYN_OSC_D_MODAL_BREATH };
    juce::WebSliderRelay synOscDModalStretchRelay { ParameterIDs::SYN_OSC_D_MODAL_STRETCH };
    juce::WebSliderRelay synOscDModalBloomRelay { ParameterIDs::SYN_OSC_D_MODAL_BLOOM };
    juce::WebSliderRelay synOscDModalHaloRelay { ParameterIDs::SYN_OSC_D_MODAL_HALO };
    juce::WebSliderRelay synOscDModalAgeRelay { ParameterIDs::SYN_OSC_D_MODAL_AGE };
    juce::WebSliderRelay synOscDModalBodyRelay { ParameterIDs::SYN_OSC_D_MODAL_BODY };

    // ════ UNIVERSAL OSC BOXES — COARSE + SUB relays (5 × 4 osc, 2026-07-09) ════
    juce::WebSliderRelay synOscACoarseRelay { ParameterIDs::SYN_OSC_A_COARSE };
    juce::WebSliderRelay synOscBCoarseRelay { ParameterIDs::SYN_OSC_B_COARSE };
    juce::WebSliderRelay synOscCCoarseRelay { ParameterIDs::SYN_OSC_C_COARSE };
    juce::WebSliderRelay synOscDCoarseRelay { ParameterIDs::SYN_OSC_D_COARSE };
    juce::WebSliderRelay synOscASubRangeRelay { ParameterIDs::SYN_OSC_A_SUB_RANGE };
    juce::WebSliderRelay synOscBSubRangeRelay { ParameterIDs::SYN_OSC_B_SUB_RANGE };
    juce::WebSliderRelay synOscCSubRangeRelay { ParameterIDs::SYN_OSC_C_SUB_RANGE };
    juce::WebSliderRelay synOscDSubRangeRelay { ParameterIDs::SYN_OSC_D_SUB_RANGE };
    juce::WebSliderRelay synOscASubFormRelay { ParameterIDs::SYN_OSC_A_SUB_FORM };
    juce::WebSliderRelay synOscBSubFormRelay { ParameterIDs::SYN_OSC_B_SUB_FORM };
    juce::WebSliderRelay synOscCSubFormRelay { ParameterIDs::SYN_OSC_C_SUB_FORM };
    juce::WebSliderRelay synOscDSubFormRelay { ParameterIDs::SYN_OSC_D_SUB_FORM };
    juce::WebSliderRelay synOscASubWeightRelay { ParameterIDs::SYN_OSC_A_SUB_WEIGHT };
    juce::WebSliderRelay synOscBSubWeightRelay { ParameterIDs::SYN_OSC_B_SUB_WEIGHT };
    juce::WebSliderRelay synOscCSubWeightRelay { ParameterIDs::SYN_OSC_C_SUB_WEIGHT };
    juce::WebSliderRelay synOscDSubWeightRelay { ParameterIDs::SYN_OSC_D_SUB_WEIGHT };
    juce::WebSliderRelay synOscASubHeatRelay { ParameterIDs::SYN_OSC_A_SUB_HEAT };
    juce::WebSliderRelay synOscBSubHeatRelay { ParameterIDs::SYN_OSC_B_SUB_HEAT };
    juce::WebSliderRelay synOscCSubHeatRelay { ParameterIDs::SYN_OSC_C_SUB_HEAT };
    juce::WebSliderRelay synOscDSubHeatRelay { ParameterIDs::SYN_OSC_D_SUB_HEAT };
    // ════ GEODE-ENGINE relays (resynthesis oscillator) ════
    juce::WebSliderRelay synOscAGeodePositionRelay { ParameterIDs::SYN_OSC_A_GEODE_POSITION };
    juce::WebSliderRelay synOscAGeodeFossilRelay { ParameterIDs::SYN_OSC_A_GEODE_FOSSIL };
    juce::WebSliderRelay synOscAGeodeCreepRelay { ParameterIDs::SYN_OSC_A_GEODE_CREEP };
    juce::WebSliderRelay synOscAGeodeSiltRelay { ParameterIDs::SYN_OSC_A_GEODE_SILT };
    juce::WebSliderRelay synOscAGeodeFormantRelay { ParameterIDs::SYN_OSC_A_GEODE_FORMANT };
    juce::WebSliderRelay synOscAGeodeCutRelay { ParameterIDs::SYN_OSC_A_GEODE_CUT };
    juce::WebSliderRelay synOscAGeodeSieveRelay { ParameterIDs::SYN_OSC_A_GEODE_SIEVE };
    juce::WebSliderRelay synOscAGeodeDistillRelay { ParameterIDs::SYN_OSC_A_GEODE_DISTILL };
    juce::WebSliderRelay synOscAGeodeHazeRelay { ParameterIDs::SYN_OSC_A_GEODE_HAZE };
    juce::WebSliderRelay synOscAGeodeFractureRelay { ParameterIDs::SYN_OSC_A_GEODE_FRACTURE };
    juce::WebSliderRelay synOscAGeodeTiltRelay { ParameterIDs::SYN_OSC_A_GEODE_TILT };
    juce::WebSliderRelay synOscAGeodeQualityRelay { ParameterIDs::SYN_OSC_A_GEODE_QUALITY };
    juce::WebSliderRelay synOscAGeodeFkeepRelay { ParameterIDs::SYN_OSC_A_GEODE_FKEEP };
    juce::WebSliderRelay synOscAGeodeLoopRelay { ParameterIDs::SYN_OSC_A_GEODE_LOOP };
    juce::WebSliderRelay synOscBGeodePositionRelay { ParameterIDs::SYN_OSC_B_GEODE_POSITION };
    juce::WebSliderRelay synOscBGeodeFossilRelay { ParameterIDs::SYN_OSC_B_GEODE_FOSSIL };
    juce::WebSliderRelay synOscBGeodeCreepRelay { ParameterIDs::SYN_OSC_B_GEODE_CREEP };
    juce::WebSliderRelay synOscBGeodeSiltRelay { ParameterIDs::SYN_OSC_B_GEODE_SILT };
    juce::WebSliderRelay synOscBGeodeFormantRelay { ParameterIDs::SYN_OSC_B_GEODE_FORMANT };
    juce::WebSliderRelay synOscBGeodeCutRelay { ParameterIDs::SYN_OSC_B_GEODE_CUT };
    juce::WebSliderRelay synOscBGeodeSieveRelay { ParameterIDs::SYN_OSC_B_GEODE_SIEVE };
    juce::WebSliderRelay synOscBGeodeDistillRelay { ParameterIDs::SYN_OSC_B_GEODE_DISTILL };
    juce::WebSliderRelay synOscBGeodeHazeRelay { ParameterIDs::SYN_OSC_B_GEODE_HAZE };
    juce::WebSliderRelay synOscBGeodeFractureRelay { ParameterIDs::SYN_OSC_B_GEODE_FRACTURE };
    juce::WebSliderRelay synOscBGeodeTiltRelay { ParameterIDs::SYN_OSC_B_GEODE_TILT };
    juce::WebSliderRelay synOscBGeodeQualityRelay { ParameterIDs::SYN_OSC_B_GEODE_QUALITY };
    juce::WebSliderRelay synOscBGeodeFkeepRelay { ParameterIDs::SYN_OSC_B_GEODE_FKEEP };
    juce::WebSliderRelay synOscBGeodeLoopRelay { ParameterIDs::SYN_OSC_B_GEODE_LOOP };
    juce::WebSliderRelay synOscCGeodePositionRelay { ParameterIDs::SYN_OSC_C_GEODE_POSITION };
    juce::WebSliderRelay synOscCGeodeFossilRelay { ParameterIDs::SYN_OSC_C_GEODE_FOSSIL };
    juce::WebSliderRelay synOscCGeodeCreepRelay { ParameterIDs::SYN_OSC_C_GEODE_CREEP };
    juce::WebSliderRelay synOscCGeodeSiltRelay { ParameterIDs::SYN_OSC_C_GEODE_SILT };
    juce::WebSliderRelay synOscCGeodeFormantRelay { ParameterIDs::SYN_OSC_C_GEODE_FORMANT };
    juce::WebSliderRelay synOscCGeodeCutRelay { ParameterIDs::SYN_OSC_C_GEODE_CUT };
    juce::WebSliderRelay synOscCGeodeSieveRelay { ParameterIDs::SYN_OSC_C_GEODE_SIEVE };
    juce::WebSliderRelay synOscCGeodeDistillRelay { ParameterIDs::SYN_OSC_C_GEODE_DISTILL };
    juce::WebSliderRelay synOscCGeodeHazeRelay { ParameterIDs::SYN_OSC_C_GEODE_HAZE };
    juce::WebSliderRelay synOscCGeodeFractureRelay { ParameterIDs::SYN_OSC_C_GEODE_FRACTURE };
    juce::WebSliderRelay synOscCGeodeTiltRelay { ParameterIDs::SYN_OSC_C_GEODE_TILT };
    juce::WebSliderRelay synOscCGeodeQualityRelay { ParameterIDs::SYN_OSC_C_GEODE_QUALITY };
    juce::WebSliderRelay synOscCGeodeFkeepRelay { ParameterIDs::SYN_OSC_C_GEODE_FKEEP };
    juce::WebSliderRelay synOscCGeodeLoopRelay { ParameterIDs::SYN_OSC_C_GEODE_LOOP };
    juce::WebSliderRelay synOscDGeodePositionRelay { ParameterIDs::SYN_OSC_D_GEODE_POSITION };
    juce::WebSliderRelay synOscDGeodeFossilRelay { ParameterIDs::SYN_OSC_D_GEODE_FOSSIL };
    juce::WebSliderRelay synOscDGeodeCreepRelay { ParameterIDs::SYN_OSC_D_GEODE_CREEP };
    juce::WebSliderRelay synOscDGeodeSiltRelay { ParameterIDs::SYN_OSC_D_GEODE_SILT };
    juce::WebSliderRelay synOscDGeodeFormantRelay { ParameterIDs::SYN_OSC_D_GEODE_FORMANT };
    juce::WebSliderRelay synOscDGeodeCutRelay { ParameterIDs::SYN_OSC_D_GEODE_CUT };
    juce::WebSliderRelay synOscDGeodeSieveRelay { ParameterIDs::SYN_OSC_D_GEODE_SIEVE };
    juce::WebSliderRelay synOscDGeodeDistillRelay { ParameterIDs::SYN_OSC_D_GEODE_DISTILL };
    juce::WebSliderRelay synOscDGeodeHazeRelay { ParameterIDs::SYN_OSC_D_GEODE_HAZE };
    juce::WebSliderRelay synOscDGeodeFractureRelay { ParameterIDs::SYN_OSC_D_GEODE_FRACTURE };
    juce::WebSliderRelay synOscDGeodeTiltRelay { ParameterIDs::SYN_OSC_D_GEODE_TILT };
    juce::WebSliderRelay synOscDGeodeQualityRelay { ParameterIDs::SYN_OSC_D_GEODE_QUALITY };
    juce::WebSliderRelay synOscDGeodeFkeepRelay { ParameterIDs::SYN_OSC_D_GEODE_FKEEP };
    juce::WebSliderRelay synOscDGeodeLoopRelay { ParameterIDs::SYN_OSC_D_GEODE_LOOP };
    juce::WebSliderRelay synOscAGeodeBedrockRelay { ParameterIDs::SYN_OSC_A_GEODE_BEDROCK };
    juce::WebSliderRelay synOscBGeodeBedrockRelay { ParameterIDs::SYN_OSC_B_GEODE_BEDROCK };
    juce::WebSliderRelay synOscCGeodeBedrockRelay { ParameterIDs::SYN_OSC_C_GEODE_BEDROCK };
    juce::WebSliderRelay synOscDGeodeBedrockRelay { ParameterIDs::SYN_OSC_D_GEODE_BEDROCK };
    // Resynth Shape-Target + Cut-Mode (choice; driven by the right-click menu)
    juce::WebSliderRelay synOscAGeodeShapeTargetRelay { ParameterIDs::SYN_OSC_A_GEODE_SHAPE_TARGET };
    juce::WebSliderRelay synOscBGeodeShapeTargetRelay { ParameterIDs::SYN_OSC_B_GEODE_SHAPE_TARGET };
    juce::WebSliderRelay synOscCGeodeShapeTargetRelay { ParameterIDs::SYN_OSC_C_GEODE_SHAPE_TARGET };
    juce::WebSliderRelay synOscDGeodeShapeTargetRelay { ParameterIDs::SYN_OSC_D_GEODE_SHAPE_TARGET };
    juce::WebSliderRelay synOscAGeodeCutModeRelay { ParameterIDs::SYN_OSC_A_GEODE_CUT_MODE };
    juce::WebSliderRelay synOscBGeodeCutModeRelay { ParameterIDs::SYN_OSC_B_GEODE_CUT_MODE };
    juce::WebSliderRelay synOscCGeodeCutModeRelay { ParameterIDs::SYN_OSC_C_GEODE_CUT_MODE };
    juce::WebSliderRelay synOscDGeodeCutModeRelay { ParameterIDs::SYN_OSC_D_GEODE_CUT_MODE };
    juce::WebSliderRelay synOscAGeodeDriveModeRelay { ParameterIDs::SYN_OSC_A_GEODE_DRIVE_MODE };
    juce::WebSliderRelay synOscBGeodeDriveModeRelay { ParameterIDs::SYN_OSC_B_GEODE_DRIVE_MODE };
    juce::WebSliderRelay synOscCGeodeDriveModeRelay { ParameterIDs::SYN_OSC_C_GEODE_DRIVE_MODE };
    juce::WebSliderRelay synOscDGeodeDriveModeRelay { ParameterIDs::SYN_OSC_D_GEODE_DRIVE_MODE };
    juce::WebSliderRelay synOscAGeodeSieveModeRelay { ParameterIDs::SYN_OSC_A_GEODE_SIEVE_MODE };
    juce::WebSliderRelay synOscBGeodeSieveModeRelay { ParameterIDs::SYN_OSC_B_GEODE_SIEVE_MODE };
    juce::WebSliderRelay synOscCGeodeSieveModeRelay { ParameterIDs::SYN_OSC_C_GEODE_SIEVE_MODE };
    juce::WebSliderRelay synOscDGeodeSieveModeRelay { ParameterIDs::SYN_OSC_D_GEODE_SIEVE_MODE };
    // ── GRAIN-EXPANDED-RELAYS — the 8 reassignable functions (8 × 4 osc) ──
    juce::WebSliderRelay synOscAGrainPositionRelay { ParameterIDs::SYN_OSC_A_GRAIN_POSITION };
    juce::WebSliderRelay synOscAGrainPitchRelay    { ParameterIDs::SYN_OSC_A_GRAIN_PITCH };
    juce::WebSliderRelay synOscAGrainPsprayRelay   { ParameterIDs::SYN_OSC_A_GRAIN_PSPRAY };
    juce::WebSliderRelay synOscAGrainWidthRelay    { ParameterIDs::SYN_OSC_A_GRAIN_WIDTH };
    juce::WebSliderRelay synOscAGrainDirRelay      { ParameterIDs::SYN_OSC_A_GRAIN_DIR };
    juce::WebSliderRelay synOscAGrainSkewRelay     { ParameterIDs::SYN_OSC_A_GRAIN_SKEW };
    juce::WebSliderRelay synOscBGrainPositionRelay { ParameterIDs::SYN_OSC_B_GRAIN_POSITION };
    juce::WebSliderRelay synOscBGrainPitchRelay    { ParameterIDs::SYN_OSC_B_GRAIN_PITCH };
    juce::WebSliderRelay synOscBGrainPsprayRelay   { ParameterIDs::SYN_OSC_B_GRAIN_PSPRAY };
    juce::WebSliderRelay synOscBGrainWidthRelay    { ParameterIDs::SYN_OSC_B_GRAIN_WIDTH };
    juce::WebSliderRelay synOscBGrainDirRelay      { ParameterIDs::SYN_OSC_B_GRAIN_DIR };
    juce::WebSliderRelay synOscBGrainSkewRelay     { ParameterIDs::SYN_OSC_B_GRAIN_SKEW };
    juce::WebSliderRelay synOscCGrainPositionRelay { ParameterIDs::SYN_OSC_C_GRAIN_POSITION };
    juce::WebSliderRelay synOscCGrainPitchRelay    { ParameterIDs::SYN_OSC_C_GRAIN_PITCH };
    juce::WebSliderRelay synOscCGrainPsprayRelay   { ParameterIDs::SYN_OSC_C_GRAIN_PSPRAY };
    juce::WebSliderRelay synOscCGrainWidthRelay    { ParameterIDs::SYN_OSC_C_GRAIN_WIDTH };
    juce::WebSliderRelay synOscCGrainDirRelay      { ParameterIDs::SYN_OSC_C_GRAIN_DIR };
    juce::WebSliderRelay synOscCGrainSkewRelay     { ParameterIDs::SYN_OSC_C_GRAIN_SKEW };
    juce::WebSliderRelay synOscDGrainPositionRelay { ParameterIDs::SYN_OSC_D_GRAIN_POSITION };
    juce::WebSliderRelay synOscDGrainPitchRelay    { ParameterIDs::SYN_OSC_D_GRAIN_PITCH };
    juce::WebSliderRelay synOscDGrainPsprayRelay   { ParameterIDs::SYN_OSC_D_GRAIN_PSPRAY };
    juce::WebSliderRelay synOscDGrainWidthRelay    { ParameterIDs::SYN_OSC_D_GRAIN_WIDTH };
    juce::WebSliderRelay synOscDGrainDirRelay      { ParameterIDs::SYN_OSC_D_GRAIN_DIR };
    juce::WebSliderRelay synOscDGrainSkewRelay     { ParameterIDs::SYN_OSC_D_GRAIN_SKEW };
    juce::WebSliderRelay synOscDSpectralTypeRelay { ParameterIDs::SYN_OSC_D_SPECTRAL_TYPE };
    juce::WebSliderRelay synOscDSpectralAmtRelay  { ParameterIDs::SYN_OSC_D_SPECTRAL_AMT };
    juce::WebSliderRelay synOscDSpectralLoRelay   { ParameterIDs::SYN_OSC_D_SPECTRAL_LO };   // fb467 — partial window
    juce::WebSliderRelay synOscDSpectralHiRelay   { ParameterIDs::SYN_OSC_D_SPECTRAL_HI };
    juce::WebSliderRelay synOscDFoldShapeRelay    { ParameterIDs::SYN_OSC_D_FOLD_SHAPE };
    juce::WebSliderRelay synOscDFoldAmtRelay      { ParameterIDs::SYN_OSC_D_FOLD_AMT };
    juce::WebSliderRelay synOscDFrameSpreadRelay  { ParameterIDs::SYN_OSC_D_FRAME_SPREAD };
    juce::WebSliderRelay synOscDInterpModeRelay   { ParameterIDs::SYN_OSC_D_INTERP_MODE };

    // Synth section — Phase 8a (Voice settings + flagship features)
    juce::WebSliderRelay synVoicesRelay  { ParameterIDs::SYN_VOICES };
    juce::WebSliderRelay synUnisonRelay  { ParameterIDs::SYN_UNISON };
    juce::WebSliderRelay synSpreadRelay  { ParameterIDs::SYN_SPREAD };
    juce::WebSliderRelay synErosionRelay { ParameterIDs::SYN_EROSION };
    juce::WebSliderRelay synHorizonRelay { ParameterIDs::SYN_HORIZON };
    juce::WebSliderRelay synPortaRelay        { ParameterIDs::SYN_PORTA };
    juce::WebSliderRelay synGlideCurveRelay   { ParameterIDs::SYN_GLIDE_CURVE };
    juce::WebSliderRelay synVelDepthRelay     { ParameterIDs::SYN_VEL_DEPTH };   // fb260
    juce::WebSliderRelay synGlideAlwaysRelay  { ParameterIDs::SYN_GLIDE_ALWAYS };
    juce::WebSliderRelay synGlideScaledRelay  { ParameterIDs::SYN_GLIDE_SCALED };
    juce::WebSliderRelay synMonoRelay         { ParameterIDs::SYN_MONO };
    juce::WebSliderRelay synLegatoRelay       { ParameterIDs::SYN_LEGATO };
    // ── FLOW (performance engine) relays ──
    juce::WebSliderRelay flowModeRelay      { ParameterIDs::FLOW_MODE };
    juce::WebSliderRelay flowArpLatchRelay  { ParameterIDs::FLOW_ARP_LATCH };
    juce::WebSliderRelay flowArpRateRelay { ParameterIDs::FLOW_ARP_RATE }; juce::WebSliderRelay flowArpGateRelay { ParameterIDs::FLOW_ARP_GATE }; juce::WebSliderRelay flowArpVaryRelay { ParameterIDs::FLOW_ARP_VARY }; juce::WebSliderRelay flowArpTrajRelay { ParameterIDs::FLOW_ARP_TRAJ }; juce::WebSliderRelay flowArpMorphRelay { ParameterIDs::FLOW_ARP_MORPH };
    juce::WebSliderRelay flowSeqRateRelay { ParameterIDs::FLOW_SEQ_RATE }; juce::WebSliderRelay flowSeqGateRelay { ParameterIDs::FLOW_SEQ_GATE }; juce::WebSliderRelay flowSeqVaryRelay { ParameterIDs::FLOW_SEQ_VARY }; juce::WebSliderRelay flowSeqTrajRelay { ParameterIDs::FLOW_SEQ_TRAJ }; juce::WebSliderRelay flowSeqMorphRelay { ParameterIDs::FLOW_SEQ_MORPH };
    juce::WebSliderRelay flowChopBlendRelay { ParameterIDs::FLOW_CHOP_BLEND };   // CHOP dry/wet (glass menu)
    juce::WebSliderRelay flowGliBlendRelay  { ParameterIDs::FLOW_GLI_BLEND };    // GLITCH dry/wet (glass menu)
    juce::WebSliderRelay flowArpBlendRelay  { ParameterIDs::FLOW_ARP_BLEND };    // ARP vs dry held-chord (glass menu)
    juce::WebSliderRelay flowGliRateRelay { ParameterIDs::FLOW_GLI_RATE }; juce::WebSliderRelay flowGliGateRelay { ParameterIDs::FLOW_GLI_GATE }; juce::WebSliderRelay flowGliVaryRelay { ParameterIDs::FLOW_GLI_VARY }; juce::WebSliderRelay flowGliTrajRelay { ParameterIDs::FLOW_GLI_TRAJ }; juce::WebSliderRelay flowGliMorphRelay { ParameterIDs::FLOW_GLI_MORPH };
    juce::WebSliderRelay flowDrfRateRelay { ParameterIDs::FLOW_DRF_RATE }; juce::WebSliderRelay flowDrfGateRelay { ParameterIDs::FLOW_DRF_GATE }; juce::WebSliderRelay flowDrfVaryRelay { ParameterIDs::FLOW_DRF_VARY }; juce::WebSliderRelay flowDrfTrajRelay { ParameterIDs::FLOW_DRF_TRAJ }; juce::WebSliderRelay flowDrfMorphRelay { ParameterIDs::FLOW_DRF_MORPH };
    // ── ANNULUS resonator relays (Material = choice via slider relay, like FLOW_MODE) ──
    juce::WebSliderRelay resoStructureRelay  { ParameterIDs::SYN_RESO_STRUCTURE };
    juce::WebSliderRelay resoBrightnessRelay { ParameterIDs::SYN_RESO_BRIGHTNESS };
    juce::WebSliderRelay resoDampingRelay    { ParameterIDs::SYN_RESO_DAMPING };
    juce::WebSliderRelay resoPositionRelay   { ParameterIDs::SYN_RESO_POSITION };
    juce::WebSliderRelay resoMixRelay        { ParameterIDs::SYN_RESO_MIX };
    juce::WebSliderRelay resoKeyTrackRelay   { ParameterIDs::SYN_RESO_KEYTRACK };
    juce::WebSliderRelay resoMaterialRelay   { ParameterIDs::SYN_RESO_MATERIAL };

    // 2. WEBVIEW SECOND (destroyed middle)
    // fb135 — HOST-KEY BRIDGE: while a web inline editor is armed (editArm native), JUCE
    // holds keyboard focus and keyPressed pipes every keystroke into the page (__tiHostKey).
    bool keyPressed (const juce::KeyPress& key) override;
    std::atomic<bool> tiEditArmed_ { false };

    std::unique_ptr<juce::WebBrowserComponent> webView;

    // fb342 — EQ/spectrum push gate: last-pushed analyzer frame counts. MEMBERS, not statics
    // in timerCallback (a static there is shared across instances — the fb339 pluginval trap).
    uint32_t eqPushSeqPre_ = 0xffffffff, eqPushSeqPost_ = 0xffffffff;
    // fb342 review — the floating .filt-ext analyzer overlay outlives page switches (position:fixed,
    // closed only by its own X), so it must keep the spectrum feed alive on ANY page. JS reports
    // open/close via setFltExtOpen; this ORs into the push gate.
    std::atomic<bool> fltExtOpen_ { false };
    // fb343 — last-relayed curve-blob version (the lfoLiveSeen_ pattern). Starts at the counter's
    // initial value so boot does NOT relay (crvXWin's boot pull already covers cold state).
    int dstPtRelaySeen_ = 0;

    // 2a. POPPED-OUT FLOW EXTENSION CARDS (fb82/fb83) — ⧉ on a card header detaches
    // it into a borderless always-on-top native window holding its OWN WebView, booted
    // with ?card=<id> so the page renders just that card (no relays — the popped card
    // talks through the setSynParam/getSynParam bypass). fb83: the windows are OWNED
    // BY THE PROCESSOR so they survive FL auto-closing an unfocused editor; this
    // editor only CREATES them (it knows the card's rect + serves the page snapshot),
    // and their style flags include windowIgnoresKeyPresses so clicking a card never
    // steals key focus from the host's plugin window in the first place.
    // fb86 — grab/mouseScreen present = DRAG-ACROSS-THE-EDGE handoff: the window is
    // born with its grab point pinned under the cursor, and the main view's SAME drag
    // keeps steering it via dragPoppedCardWindow until mouseup.
    void popOutCardWindow  (const juce::String& id, juce::Rectangle<int> viewportRect,
                            std::optional<juce::Point<int>> grabOffset  = {},
                            std::optional<juce::Point<int>> mouseScreen = {});
    void dragPoppedCardWindow (const juce::String& id, juce::Point<int> mouseScreen);

    // fb95/fb102/fb103 — resizable editor: magnification rides the drag, pageZoom
    // settles crisp (+ DPR push so canvases re-raster); junk host sizes self-heal
    // to the intended size until the user really drags
    double uiZoom_ = 1.0, restZoom_ = 1.0;
    juce::int64 lfoLiveSeen_ = 0;   // fb236 — last relayed live-stroke seq
    int    zoomPushLeft_ = 0, zoomTick2_ = 0, settleTicks_ = 0, healTicks_ = 0;
    int    zoomVerifyTicks_ = 0;    // fb175 — countdown to the post-settle layout verify
    double pageVW_ = -1.0;          // fb175 — page-reported innerWidth (eval callback, message thread)
    int    intendedW_ = 820;
    bool   userSized_ = false;

    // 2b. NATIVE CAPTURE DRAG STRIP (below WebView — receives real mouse events)
    static constexpr int CAPTURE_STRIP_HEIGHT = 16;   // fb272 — thinner strip (Max: "very thin"); whole window a bit shorter. Label font now scales with strip height so text never clips at small window sizes.

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
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAEnableAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAMuteAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASoloAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAPanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1CutAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1ResAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1KeytrackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2KeytrackAttachment;
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
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1VelAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2VelAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter1PdrvAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synFilter2PdrvAttachment;
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
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBEnableAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBMuteAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSoloAttachment;
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
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASpectralLoAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASpectralHiAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFoldShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFoldAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFrameSpreadAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAInterpModeAttachment;
    // Phase 11a — OSC B attachments
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSpectralTypeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSpectralAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSpectralLoAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSpectralHiAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBFoldShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBFoldAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBFrameSpreadAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBInterpModeAttachment;
    // ── OSC C + D attachments (4-osc) ──
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCEngineAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCOctAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSemiAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCCentAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCLevelAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCEnableAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCMuteAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSoloAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCPanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCWtPresetAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCWtFrameAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCWarpAmountAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCWarp2ModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCWarp2AmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCPhaseModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCWaverAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCKeytrackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCKeytrackDestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCRouteSrcAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCRouteDestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCRouteAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCUnisonAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCUdetuneAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCUblendAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCUwidthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSpectralTypeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSpectralAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSpectralLoAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSpectralHiAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCFoldShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCFoldAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCFrameSpreadAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCInterpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDEngineAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDOctAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSemiAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDCentAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDLevelAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDEnableAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDMuteAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSoloAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDPanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDWtPresetAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDWtFrameAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDWarpAmountAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDWarp2ModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDWarp2AmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDPhaseModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDWaverAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDKeytrackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDKeytrackDestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDRouteSrcAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDRouteDestAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDRouteAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDUnisonAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDUdetuneAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDUblendAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDUwidthAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSpectralTypeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSpectralAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSpectralLoAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSpectralHiAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDFoldShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDFoldAmtAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDFrameSpreadAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDInterpModeAttachment;
    // ════ SAMPLE-ENGINE-ATTACHMENTS (Opus) ════
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleScanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleStretchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleFormantAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleSprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleXfadeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleAirAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleWarpAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleStartAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleEndAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleLoopStartAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleLoopEndAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleLoopModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleStretchModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleFormantModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleSnapAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleFadeInAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASampleFadeOutAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleScanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleStretchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleFormantAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleSprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleXfadeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleAirAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleWarpAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleStartAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleEndAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleLoopStartAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleLoopEndAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleLoopModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleStretchModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleFormantModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleSnapAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleFadeInAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBSampleFadeOutAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleScanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleStretchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleFormantAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleSprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleXfadeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleAirAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleWarpAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleStartAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleEndAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleLoopStartAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleLoopEndAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleLoopModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleStretchModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleFormantModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleSnapAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleFadeInAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCSampleFadeOutAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleScanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleStretchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleFormantAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleSprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleXfadeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleAirAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleWarpAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleStartAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleEndAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleLoopStartAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleLoopEndAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleLoopModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleStretchModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleFormantModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleSnapAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleFadeInAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDSampleFadeOutAttachment;

    // ── GRAIN-ENGINE-ATTACHMENTS — per-OSC granular params (6 × 4 osc) ──
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGrainScanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGrainDensityAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGrainSizeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGrainSprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGrainShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGrainKeyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBGrainScanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBGrainDensityAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBGrainSizeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBGrainSprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBGrainShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBGrainKeyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCGrainScanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCGrainDensityAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCGrainSizeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCGrainSprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCGrainShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCGrainKeyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDGrainScanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDGrainDensityAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDGrainSizeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDGrainSprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDGrainShapeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDGrainKeyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFadeInCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFadeOutCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBFadeInCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBFadeOutCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCFadeInCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCFadeOutCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDFadeInCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDFadeOutCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscABlendMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscABlendAttackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscABlendBodyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscABlendBreathAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscABlendSculptAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscABlendDiceAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBBlendMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBBlendAttackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBBlendBodyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBBlendBreathAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBBlendSculptAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBBlendDiceAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCBlendMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCBlendAttackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCBlendBodyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCBlendBreathAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCBlendSculptAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCBlendDiceAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDBlendMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDBlendAttackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDBlendBodyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDBlendBreathAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDBlendSculptAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDBlendDiceAttachment;
    // ── FM-ENGINE attachments (6 × 4 osc) ──
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmAlgoAttachment,   synOscBFmAlgoAttachment,   synOscCFmAlgoAttachment,   synOscDFmAlgoAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmRatio1Attachment, synOscBFmRatio1Attachment, synOscCFmRatio1Attachment, synOscDFmRatio1Attachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmDepth1Attachment, synOscBFmDepth1Attachment, synOscCFmDepth1Attachment, synOscDFmDepth1Attachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmRatio2Attachment, synOscBFmRatio2Attachment, synOscCFmRatio2Attachment, synOscDFmRatio2Attachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmDepth2Attachment, synOscBFmDepth2Attachment, synOscCFmDepth2Attachment, synOscDFmDepth2Attachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmFbAttachment,     synOscBFmFbAttachment,     synOscCFmFbAttachment,     synOscDFmFbAttachment;
    // ── FM WEATHERING attachments (6 × 4 osc) ──
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmStrikeAttachment, synOscBFmStrikeAttachment, synOscCFmStrikeAttachment, synOscDFmStrikeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmAgeAttachment,    synOscBFmAgeAttachment,    synOscCFmAgeAttachment,    synOscDFmAgeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmRustAttachment,   synOscBFmRustAttachment,   synOscCFmRustAttachment,   synOscDFmRustAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmGaleAttachment,   synOscBFmGaleAttachment,   synOscCFmGaleAttachment,   synOscDFmGaleAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmBendAttachment,   synOscBFmBendAttachment,   synOscCFmBendAttachment,   synOscDFmBendAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAFmStormAttachment,  synOscBFmStormAttachment,  synOscCFmStormAttachment,  synOscDFmStormAttachment;
    // ── HARM-ENGINE attachments (14 × 4 osc) ──
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmModeAttachment, synOscBHarmModeAttachment, synOscCHarmModeAttachment, synOscDHarmModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmSculptAttachment, synOscBHarmSculptAttachment, synOscCHarmSculptAttachment, synOscDHarmSculptAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmHueAttachment, synOscBHarmHueAttachment, synOscCHarmHueAttachment, synOscDHarmHueAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmCountAttachment, synOscBHarmCountAttachment, synOscCHarmCountAttachment, synOscDHarmCountAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmLeanAttachment, synOscBHarmLeanAttachment, synOscCHarmLeanAttachment, synOscDHarmLeanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmFanAttachment, synOscBHarmFanAttachment, synOscCHarmFanAttachment, synOscDHarmFanAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmGritAttachment, synOscBHarmGritAttachment, synOscCHarmGritAttachment, synOscDHarmGritAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmBraidAttachment, synOscBHarmBraidAttachment, synOscCHarmBraidAttachment, synOscDHarmBraidAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmCarveAttachment, synOscBHarmCarveAttachment, synOscCHarmCarveAttachment, synOscDHarmCarveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmChurnAttachment, synOscBHarmChurnAttachment, synOscCHarmChurnAttachment, synOscDHarmChurnAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmRootAttachment, synOscBHarmRootAttachment, synOscCHarmRootAttachment, synOscDHarmRootAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmShineAttachment, synOscBHarmShineAttachment, synOscCHarmShineAttachment, synOscDHarmShineAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmWiltAttachment, synOscBHarmWiltAttachment, synOscCHarmWiltAttachment, synOscDHarmWiltAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAHarmFizzAttachment, synOscBHarmFizzAttachment, synOscCHarmFizzAttachment, synOscDHarmFizzAttachment;

    // ── MODAL-ENGINE attachments (13 × 4 osc) ──
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalFamilyAttachment, synOscBModalFamilyAttachment, synOscCModalFamilyAttachment, synOscDModalFamilyAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalFormAttachment, synOscBModalFormAttachment, synOscCModalFormAttachment, synOscDModalFormAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalSourceAttachment, synOscBModalSourceAttachment, synOscCModalSourceAttachment, synOscDModalSourceAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalHardAttachment, synOscBModalHardAttachment, synOscCModalHardAttachment, synOscDModalHardAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalPosAttachment, synOscBModalPosAttachment, synOscCModalPosAttachment, synOscDModalPosAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalDecayAttachment, synOscBModalDecayAttachment, synOscCModalDecayAttachment, synOscDModalDecayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalMaterialAttachment, synOscBModalMaterialAttachment, synOscCModalMaterialAttachment, synOscDModalMaterialAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalBreathAttachment, synOscBModalBreathAttachment, synOscCModalBreathAttachment, synOscDModalBreathAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalStretchAttachment, synOscBModalStretchAttachment, synOscCModalStretchAttachment, synOscDModalStretchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalBloomAttachment, synOscBModalBloomAttachment, synOscCModalBloomAttachment, synOscDModalBloomAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalHaloAttachment, synOscBModalHaloAttachment, synOscCModalHaloAttachment, synOscDModalHaloAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalAgeAttachment, synOscBModalAgeAttachment, synOscCModalAgeAttachment, synOscDModalAgeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAModalBodyAttachment, synOscBModalBodyAttachment, synOscCModalBodyAttachment, synOscDModalBodyAttachment;
    // UNIVERSAL OSC BOXES — COARSE + SUB attachments
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscACoarseAttachment, synOscBCoarseAttachment, synOscCCoarseAttachment, synOscDCoarseAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASubRangeAttachment, synOscBSubRangeAttachment, synOscCSubRangeAttachment, synOscDSubRangeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASubFormAttachment, synOscBSubFormAttachment, synOscCSubFormAttachment, synOscDSubFormAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASubWeightAttachment, synOscBSubWeightAttachment, synOscCSubWeightAttachment, synOscDSubWeightAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscASubHeatAttachment, synOscBSubHeatAttachment, synOscCSubHeatAttachment, synOscDSubHeatAttachment;
    // ── HARM-ENGINE display banks — message-thread instances that feed the white
    //    partial-bar viz (rebuilt from the processor's gathered knobs each tick) ──
    tw::HarmonicEngine harmDispEng_[4];
    bool harmDispInit_ = false;
    // ════ GEODE-ENGINE attachments ════
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodePositionAttachment, synOscBGeodePositionAttachment, synOscCGeodePositionAttachment, synOscDGeodePositionAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeFossilAttachment, synOscBGeodeFossilAttachment, synOscCGeodeFossilAttachment, synOscDGeodeFossilAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeCreepAttachment, synOscBGeodeCreepAttachment, synOscCGeodeCreepAttachment, synOscDGeodeCreepAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeSiltAttachment, synOscBGeodeSiltAttachment, synOscCGeodeSiltAttachment, synOscDGeodeSiltAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeFormantAttachment, synOscBGeodeFormantAttachment, synOscCGeodeFormantAttachment, synOscDGeodeFormantAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeCutAttachment, synOscBGeodeCutAttachment, synOscCGeodeCutAttachment, synOscDGeodeCutAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeSieveAttachment, synOscBGeodeSieveAttachment, synOscCGeodeSieveAttachment, synOscDGeodeSieveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeDistillAttachment, synOscBGeodeDistillAttachment, synOscCGeodeDistillAttachment, synOscDGeodeDistillAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeHazeAttachment, synOscBGeodeHazeAttachment, synOscCGeodeHazeAttachment, synOscDGeodeHazeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeFractureAttachment, synOscBGeodeFractureAttachment, synOscCGeodeFractureAttachment, synOscDGeodeFractureAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeTiltAttachment, synOscBGeodeTiltAttachment, synOscCGeodeTiltAttachment, synOscDGeodeTiltAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeQualityAttachment, synOscBGeodeQualityAttachment, synOscCGeodeQualityAttachment, synOscDGeodeQualityAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeFkeepAttachment, synOscBGeodeFkeepAttachment, synOscCGeodeFkeepAttachment, synOscDGeodeFkeepAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeLoopAttachment, synOscBGeodeLoopAttachment, synOscCGeodeLoopAttachment, synOscDGeodeLoopAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeBedrockAttachment, synOscBGeodeBedrockAttachment, synOscCGeodeBedrockAttachment, synOscDGeodeBedrockAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeShapeTargetAttachment, synOscBGeodeShapeTargetAttachment, synOscCGeodeShapeTargetAttachment, synOscDGeodeShapeTargetAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeCutModeAttachment, synOscBGeodeCutModeAttachment, synOscCGeodeCutModeAttachment, synOscDGeodeCutModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeDriveModeAttachment, synOscBGeodeDriveModeAttachment, synOscCGeodeDriveModeAttachment, synOscDGeodeDriveModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGeodeSieveModeAttachment, synOscBGeodeSieveModeAttachment, synOscCGeodeSieveModeAttachment, synOscDGeodeSieveModeAttachment;
    // ── GRAIN-EXPANDED-ATTACHMENTS (8 × 4 osc) ──
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGrainPositionAttachment, synOscAGrainPitchAttachment, synOscAGrainPsprayAttachment, synOscAGrainWidthAttachment, synOscAGrainDirAttachment, synOscAGrainSkewAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscBGrainPositionAttachment, synOscBGrainPitchAttachment, synOscBGrainPsprayAttachment, synOscBGrainWidthAttachment, synOscBGrainDirAttachment, synOscBGrainSkewAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscCGrainPositionAttachment, synOscCGrainPitchAttachment, synOscCGrainPsprayAttachment, synOscCGrainWidthAttachment, synOscCGrainDirAttachment, synOscCGrainSkewAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscDGrainPositionAttachment, synOscDGrainPitchAttachment, synOscDGrainPsprayAttachment, synOscDGrainWidthAttachment, synOscDGrainDirAttachment, synOscDGrainSkewAttachment;

    // Synth section — Phase 8a (Voice settings + flagship features)
    std::unique_ptr<juce::WebSliderParameterAttachment> synVoicesAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synUnisonAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synSpreadAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synErosionAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synHorizonAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synPortaAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synGlideCurveAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synVelDepthAttachment;   // fb260
    std::unique_ptr<juce::WebSliderParameterAttachment> synGlideAlwaysAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synGlideScaledAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synMonoAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synLegatoAttachment;
    // ── FLOW attachments ──
    std::unique_ptr<juce::WebSliderParameterAttachment> flowModeAttachment, flowArpLatchAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowArpRateAttachment, flowArpGateAttachment, flowArpVaryAttachment, flowArpTrajAttachment, flowArpMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowSeqRateAttachment, flowSeqGateAttachment, flowSeqVaryAttachment, flowSeqTrajAttachment, flowSeqMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowChopBlendAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowGliBlendAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowArpBlendAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowGliRateAttachment, flowGliGateAttachment, flowGliVaryAttachment, flowGliTrajAttachment, flowGliMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> flowDrfRateAttachment, flowDrfGateAttachment, flowDrfVaryAttachment, flowDrfTrajAttachment, flowDrfMorphAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> resoStructureAttachment, resoBrightnessAttachment, resoDampingAttachment, resoPositionAttachment, resoMixAttachment, resoKeyTrackAttachment, resoMaterialAttachment;

    // Mod state lifecycle tick counter
    // RESTORE phase: push saved JSON to JS every tick until pageReady
    // SAVE phase: pull serialized state from JS every 5 ticks (only after pageReady)
    int modStateTickCount { 0 };
    bool pageReady { false };
    // fb480 -- set on the FIRST real page load (JUCE pageFinishedLoading). Until then the 60Hz
    // timer must not evaluateJavascript: on Windows/WebView2 pre-load evals queue unboundedly in
    // JUCE while the browser initialises (and this is the exact window of JUCE bug 64917).
    bool pageLoaded_ { false };
    juce::String bootSettingsJson_;   // fb148 — InstrumentSettings.json read ONCE at open (the pre-ready tick was hitting disk ~60x/sec)

    // CHANNEL WATCHDOG (wd9) — the WKWebView eval channel can die silently (evals
    // no-op, callbacks stop; scopes freeze/flatline until the editor is reopened).
    // A tiny heartbeat eval with a completion callback = proof of life; >3s of
    // silence while the 60Hz timer demonstrably runs ⇒ reload the page ourselves
    // (the manual close/reopen fix, automated). See timerCallback.
    double lastEvalOkMs_      = 0;
    double lastRecoveryMs_    = 0;
    int    channelRecoveries_ = 0;

    // GEODE SPECTRUM WATERFALL — editor-side "display engines" (one per osc). Each timer tick,
    // for every osc on Engine::SPEC, we feed the live analysed store + current sculpt params and
    // run computeDisplayEnvelope (interpolate+sculpt, NO audio) → push window.__geodeSpectrum to
    // the WebView. All on the message thread: zero audio-thread cost, moves with the knobs even
    // when no note sounds (the "every curve must MOVE" rule). No new APVTS params (viz-only).
    tw::GeodeEngine geodeDispEng_[4];
    bool   geodeDispPrepared_ = false;
    double geodeDispSr_       = 0.0;
    // SPECTRAL-IMAGE STRIP (rs6) — per-osc bake state for window.__geodeImage: ghost (raw analyzed
    // data) is re-baked when the STORE changes (or on the 5s heartbeat = self-heal for a reloaded
    // page); bright (current-knob survivors) also re-bakes on content-param changes, throttled.
    const tw::GeodeFrameStore* geodeImgStore_[4] = { nullptr, nullptr, nullptr, nullptr };
    tw::GeodeParams geodeImgParams_[4];
    int  geodeImgGen_[4]       = { 0, 0, 0, 0 };
    int  geodeImgCooldown_[4]  = { 0, 0, 0, 0 };   // ticks until the next bright re-bake
    int  geodeImgHeartbeat_[4] = { 0, 75, 150, 225 };   // ticks to the next unconditional full push — STAGGERED so 4 oscs never bake the same tick (cleanup sweep)

    // Resource provider
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    // ═══ BLEND — one-shot morph baker (offline; audio thread only plays the published result) ═══
    // A live blend holds the source pair in RAM + an analyzed BlendEngine; knob moves re-render
    // (debounced, ~ms) and re-publish through loadOscSampleAsync — waveform + persistence free.
    struct OscBlend
    {
        std::shared_ptr<juce::AudioBuffer<float>> srcA, srcB;
        double rateA = 48000.0, rateB = 48000.0;
        std::shared_ptr<tw::BlendEngine> engine;
        bool live = false;                     // message-thread flag: pair loaded + analyzed
        std::atomic<bool> baking { false };    // one bake in flight per osc
        std::atomic<bool> dirty  { false };    // params moved while baking → chain another
        int outSlot = 0;                       // alternating output file slot (0/1)
        float lastVals[6] = { -1.f, -1.f, -1.f, -1.f, -1.f, -1.f };   // param poll cache
        juce::int64 lastMoveMs = 0;            // debounce timestamp
    };
    OscBlend oscBlends_[4];
    bool blendRestoreTried_ = false;           // lazy one-shot state-restore (first timer tick)
    void startBlend (int oscIdx, std::shared_ptr<juce::AudioBuffer<float>> srcB, double rateB, const juce::String& name);   // drop → "Blend" (memory)
    void queueBlendBake (int oscIdx);                           // analyze-if-needed + render on the pool
    void publishBlendBuffer (int oscIdx, std::shared_ptr<juce::AudioBuffer<float>> buf, double rate);   // publish a baked buffer (memory, NO disk) + UI push
    void pollBlendKnobs();                                      // timerCallback: debounce re-bakes
    void restoreBlendsFromState();                              // reload persisted source pairs
    void resetBlend (int oscIdx, bool pushUi);                  // end a live blend (Replace / undo / delete)
    std::array<std::vector<std::pair<std::shared_ptr<juce::AudioBuffer<float>>, double>>, 4> blendHistory_;   // UNDO — pre-blend buffer snapshots (memory), ≤100 deep
    tw::BlendParams currentBlendParams (int oscIdx) const;
    juce::File blendCacheDir() const;
    static std::shared_ptr<juce::AudioBuffer<float>> readAudioFile (const juce::File& f, double& rateOut);
    static std::shared_ptr<juce::AudioBuffer<float>> readAudioFromMemory (const void* data, size_t size, double& rateOut);   // sandbox-safe (no File)
    static bool writeWav24 (const juce::File& f, const juce::AudioBuffer<float>& b, double rate);
    // Declared LAST → destroyed FIRST: the pool must drain its jobs (which touch members
    // above) before anything else in this editor dies.
    juce::ThreadPool blendPool_ { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainInstrumentAudioProcessorEditor)
};
