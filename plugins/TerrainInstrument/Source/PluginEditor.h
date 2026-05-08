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
    void loadSampleAsync   (const juce::File& file);
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
    // Parametric EQ attachments — bind APVTS params to the eqRelays above.
    std::array<std::unique_ptr<juce::WebSliderParameterAttachment>, NUM_EQ_RELAYS> eqAttachments;

    // Mod state lifecycle tick counter
    // RESTORE phase: push saved JSON to JS every tick until pageReady
    // SAVE phase: pull serialized state from JS every 5 ticks (only after pageReady)
    int modStateTickCount { 0 };
    bool pageReady { false };

    // Resource provider
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainInstrumentAudioProcessorEditor)
};
