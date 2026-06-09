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

    // Synth section — Phase 2A (wavetable)
    juce::WebSliderRelay synOscAWtPresetRelay { ParameterIDs::SYN_OSC_A_WT_PRESET };
    juce::WebSliderRelay synOscAWtFrameRelay  { ParameterIDs::SYN_OSC_A_WT_FRAME };

    // Synth section — Phase 2C (warp modes)
    juce::WebSliderRelay synOscAWarpModeRelay   { ParameterIDs::SYN_OSC_A_WARP_MODE };
    juce::WebSliderRelay synOscAWarpAmountRelay { ParameterIDs::SYN_OSC_A_WARP_AMOUNT };
    juce::WebSliderRelay synOscAPhaseModeRelay  { ParameterIDs::SYN_OSC_A_PHASE_MODE };
    juce::WebSliderRelay synOscAWaverRelay      { ParameterIDs::SYN_OSC_A_WAVER };
    juce::WebSliderRelay synOscAKeytrackRelay     { ParameterIDs::SYN_OSC_A_KEYTRACK };
    juce::WebSliderRelay synOscAKeytrackDestRelay { ParameterIDs::SYN_OSC_A_KEYTRACK_DEST };

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
    juce::WebSliderRelay synOscBPhaseModeRelay  { ParameterIDs::SYN_OSC_B_PHASE_MODE };
    juce::WebSliderRelay synOscBWaverRelay      { ParameterIDs::SYN_OSC_B_WAVER };
    juce::WebSliderRelay synOscBKeytrackRelay     { ParameterIDs::SYN_OSC_B_KEYTRACK };
    juce::WebSliderRelay synOscBKeytrackDestRelay { ParameterIDs::SYN_OSC_B_KEYTRACK_DEST };

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
    };

    CaptureDragStrip captureDragStrip { audioProcessor };

    // 3. PARAMETER ATTACHMENTS LAST (destroyed first)
    std::unique_ptr<juce::WebSliderParameterAttachment> grainSizeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> densityAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> sprayAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> pitchAttachment;
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

    // Synth section — Phase 2A (wavetable)
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWtPresetAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWtFrameAttachment;

    // Synth section — Phase 2C (warp)
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWarpModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWarpAmountAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAPhaseModeAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAWaverAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAKeytrackAttachment;
    std::unique_ptr<juce::WebSliderParameterAttachment> synOscAKeytrackDestAttachment;

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

    // Mod state lifecycle tick counter
    // RESTORE phase: push saved JSON to JS every tick until pageReady
    // SAVE phase: pull serialized state from JS every 5 ticks (only after pageReady)
    int modStateTickCount { 0 };
    bool pageReady { false };

    // Resource provider
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainInstrumentAudioProcessorEditor)
};
