#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
TerrainInstrumentAudioProcessorEditor::TerrainInstrumentAudioProcessorEditor (TerrainInstrumentAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Create WebBrowserComponent with all relay options
    webView = std::make_unique<juce::WebBrowserComponent>(
        juce::WebBrowserComponent::Options()
            .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options(
                juce::WebBrowserComponent::Options::WinWebView2{}
                    .withUserDataFolder(juce::File::getSpecialLocation(
                        juce::File::SpecialLocationType::tempDirectory)))
            .withNativeIntegrationEnabled()
            .withOptionsFrom(grainSizeRelay)
            .withOptionsFrom(densityRelay)
            .withOptionsFrom(sprayRelay)
            .withOptionsFrom(pitchRelay)
            .withOptionsFrom(lfo1RateRelay)
            .withOptionsFrom(lfo1DepthRelay)
            .withOptionsFrom(lfo1ShapeRelay)
            .withOptionsFrom(lfo1SyncRelay)
            .withOptionsFrom(lfo1DivRelay)
            .withOptionsFrom(lfo2RateRelay).withOptionsFrom(lfo2DepthRelay).withOptionsFrom(lfo2ShapeRelay).withOptionsFrom(lfo2SyncRelay).withOptionsFrom(lfo2DivRelay)
            .withOptionsFrom(lfo3RateRelay).withOptionsFrom(lfo3DepthRelay).withOptionsFrom(lfo3ShapeRelay).withOptionsFrom(lfo3SyncRelay).withOptionsFrom(lfo3DivRelay)
            .withOptionsFrom(lfo4RateRelay).withOptionsFrom(lfo4DepthRelay).withOptionsFrom(lfo4ShapeRelay).withOptionsFrom(lfo4SyncRelay).withOptionsFrom(lfo4DivRelay)
            .withOptionsFrom(lfo5RateRelay).withOptionsFrom(lfo5DepthRelay).withOptionsFrom(lfo5ShapeRelay).withOptionsFrom(lfo5SyncRelay).withOptionsFrom(lfo5DivRelay)
            .withOptionsFrom(lfo1PhaseRelay).withOptionsFrom(lfo2PhaseRelay).withOptionsFrom(lfo3PhaseRelay).withOptionsFrom(lfo4PhaseRelay).withOptionsFrom(lfo5PhaseRelay)
            .withOptionsFrom(lfo6RateRelay).withOptionsFrom(lfo6DepthRelay).withOptionsFrom(lfo6ShapeRelay).withOptionsFrom(lfo6SyncRelay).withOptionsFrom(lfo6DivRelay).withOptionsFrom(lfo6PhaseRelay)
            .withOptionsFrom(lfo7RateRelay).withOptionsFrom(lfo7DepthRelay).withOptionsFrom(lfo7ShapeRelay).withOptionsFrom(lfo7SyncRelay).withOptionsFrom(lfo7DivRelay).withOptionsFrom(lfo7PhaseRelay)
            .withOptionsFrom(lfo8RateRelay).withOptionsFrom(lfo8DepthRelay).withOptionsFrom(lfo8ShapeRelay).withOptionsFrom(lfo8SyncRelay).withOptionsFrom(lfo8DivRelay).withOptionsFrom(lfo8PhaseRelay)
            .withOptionsFrom(lfo9RateRelay).withOptionsFrom(lfo9DepthRelay).withOptionsFrom(lfo9ShapeRelay).withOptionsFrom(lfo9SyncRelay).withOptionsFrom(lfo9DivRelay).withOptionsFrom(lfo9PhaseRelay)
            .withOptionsFrom(lfo10RateRelay).withOptionsFrom(lfo10DepthRelay).withOptionsFrom(lfo10ShapeRelay).withOptionsFrom(lfo10SyncRelay).withOptionsFrom(lfo10DivRelay).withOptionsFrom(lfo10PhaseRelay)
            .withOptionsFrom(wanderRelay)
            .withOptionsFrom(freezeRelay)
            .withOptionsFrom(grainFilterRelay)
            .withOptionsFrom(mixRelay)
            .withOptionsFrom(wowFlutterRelay)
            .withOptionsFrom(saturationRelay)
            .withOptionsFrom(hissRelay)
            .withOptionsFrom(wireWowRelay)
            .withOptionsFrom(wireSaturationRelay)
            .withOptionsFrom(wireHissRelay)
            .withOptionsFrom(studioSculptRelay)
            .withOptionsFrom(studioWeaveRelay)
            .withOptionsFrom(studioTiltRelay)
            .withOptionsFrom(outputGainRelay)
            .withOptionsFrom(masterMixRelay)
            .withOptionsFrom(loopLengthRelay)
            .withOptionsFrom(loopFeedbackRelay)
            .withOptionsFrom(loopDegradeRelay)
            .withOptionsFrom(loopSpeedRelay)
            .withOptionsFrom(spaceSizeRelay)
            .withOptionsFrom(spaceDecayRelay)
            .withOptionsFrom(spaceToneRelay)
            .withOptionsFrom(spaceMixRelay)
            .withOptionsFrom(dlyTimeRelay)
            .withOptionsFrom(dlyFeedbackRelay)
            .withOptionsFrom(dlyToneRelay)
            .withOptionsFrom(dlyCharacterRelay)
            .withOptionsFrom(dlyModRelay)
            .withOptionsFrom(dlyModRateRelay)
            .withOptionsFrom(dlyMixRelay)
            .withOptionsFrom(dlyDuckRelay)
            .withOptionsFrom(dlyFreezeRelay)
            .withOptionsFrom(dlySyncRelay)
            .withOptionsFrom(dlySyncDivRelay)
            .withOptionsFrom(dlyModWaveRelay)
            .withOptionsFrom(dlyPitchRelay)
            .withOptionsFrom(dlyWidthRelay)
            .withOptionsFrom(chorusAmountRelay)
            .withOptionsFrom(chorusWidthRelay)
            .withOptionsFrom(chorusCharacterRelay)
            .withOptionsFrom(chopFadeRelay)
            .withOptionsFrom(eqRelays[0])  .withOptionsFrom(eqRelays[1])  .withOptionsFrom(eqRelays[2])  .withOptionsFrom(eqRelays[3])  .withOptionsFrom(eqRelays[4])
            .withOptionsFrom(eqRelays[5])  .withOptionsFrom(eqRelays[6])  .withOptionsFrom(eqRelays[7])  .withOptionsFrom(eqRelays[8])  .withOptionsFrom(eqRelays[9])
            .withOptionsFrom(eqRelays[10]) .withOptionsFrom(eqRelays[11]) .withOptionsFrom(eqRelays[12]) .withOptionsFrom(eqRelays[13]) .withOptionsFrom(eqRelays[14])
            .withOptionsFrom(eqRelays[15]) .withOptionsFrom(eqRelays[16]) .withOptionsFrom(eqRelays[17]) .withOptionsFrom(eqRelays[18]) .withOptionsFrom(eqRelays[19])
            .withOptionsFrom(eqRelays[20]) .withOptionsFrom(eqRelays[21]) .withOptionsFrom(eqRelays[22]) .withOptionsFrom(eqRelays[23]) .withOptionsFrom(eqRelays[24])
            .withOptionsFrom(eqRelays[25]) .withOptionsFrom(eqRelays[26]) .withOptionsFrom(eqRelays[27]) .withOptionsFrom(eqRelays[28]) .withOptionsFrom(eqRelays[29])
            .withOptionsFrom(eqRelays[30]) .withOptionsFrom(eqRelays[31]) .withOptionsFrom(eqRelays[32]) .withOptionsFrom(eqRelays[33]) .withOptionsFrom(eqRelays[34])
            .withOptionsFrom(synOscAEngineRelay)
            .withOptionsFrom(synOscAOctRelay)
            .withOptionsFrom(synOscASemiRelay)
            .withOptionsFrom(synOscACentRelay)
            .withOptionsFrom(synOscALevelRelay)
            .withOptionsFrom(synOscAPanRelay)
            .withOptionsFrom(synFilter1CutRelay)
            .withOptionsFrom(synFilter1ResRelay)
            // Batch 1 Filter — TYPE, DRV, ENV, slot 2 reserved, FLT envelope
            .withOptionsFrom(synFilter1TypeRelay)
            .withOptionsFrom(synFilter1DrvRelay)
            .withOptionsFrom(synFilter1EnvRelay)
            .withOptionsFrom(synFilterSlotRelay)
            .withOptionsFrom(synFilter2TypeRelay)
            .withOptionsFrom(synFilter2CutRelay)
            .withOptionsFrom(synFilter2ResRelay)
            .withOptionsFrom(synFilter2DrvRelay)
            .withOptionsFrom(synFilter2EnvRelay)
            .withOptionsFrom(synFilter1MixRelay)
            .withOptionsFrom(synFilter2MixRelay)
            .withOptionsFrom(synFilterRoutingRelay)
            .withOptionsFrom(synEnvFltARelay)
            .withOptionsFrom(synEnvFltDRelay)
            .withOptionsFrom(synEnvFltSRelay)
            .withOptionsFrom(synEnvFltRRelay)
            .withOptionsFrom(synEnvAmpARelay)
            .withOptionsFrom(synEnvAmpDRelay)
            .withOptionsFrom(synEnvAmpSRelay)
            .withOptionsFrom(synEnvAmpRRelay)
            .withOptionsFrom(synEnvAmpDlyRelay)
            .withOptionsFrom(synEnvAmpHRelay)
            .withOptionsFrom(synEnvAmpCaRelay)
            .withOptionsFrom(synEnvAmpCdRelay)
            .withOptionsFrom(synEnvAmpCrRelay)
            .withOptionsFrom(synEnvAmpLoopRelay)
            .withOptionsFrom(synEnvFltDlyRelay)
            .withOptionsFrom(synEnvFltHRelay)
            .withOptionsFrom(synEnvFltCaRelay)
            .withOptionsFrom(synEnvFltCdRelay)
            .withOptionsFrom(synEnvFltCrRelay)
            .withOptionsFrom(synEnvFltLoopRelay)
            .withOptionsFrom(synEnvPitDlyRelay)
            .withOptionsFrom(synEnvPitARelay)
            .withOptionsFrom(synEnvPitHRelay)
            .withOptionsFrom(synEnvPitDRelay)
            .withOptionsFrom(synEnvPitSRelay)
            .withOptionsFrom(synEnvPitRRelay)
            .withOptionsFrom(synEnvPitCaRelay)
            .withOptionsFrom(synEnvPitCdRelay)
            .withOptionsFrom(synEnvPitCrRelay)
            .withOptionsFrom(synEnvPitDepthRelay)
            .withOptionsFrom(synEnv2DestRelay)
            .withOptionsFrom(synEnv2DepthRelay)
            .withOptionsFrom(synEnv3DestRelay)
            .withOptionsFrom(synEnv3DepthRelay)
            .withOptionsFrom(synEnv4DestRelay)
            .withOptionsFrom(synEnv4DepthRelay)
            .withOptionsFrom(synEnv5DestRelay)
            .withOptionsFrom(synEnv5DepthRelay)
            .withOptionsFrom(synEnvPitLoopRelay)
            .withOptionsFrom(synEnvM1DlyRelay)
            .withOptionsFrom(synEnvM1ARelay)
            .withOptionsFrom(synEnvM1HRelay)
            .withOptionsFrom(synEnvM1DRelay)
            .withOptionsFrom(synEnvM1SRelay)
            .withOptionsFrom(synEnvM1RRelay)
            .withOptionsFrom(synEnvM1CaRelay)
            .withOptionsFrom(synEnvM1CdRelay)
            .withOptionsFrom(synEnvM1CrRelay)
            .withOptionsFrom(synEnvM1LoopRelay)
            .withOptionsFrom(synEnvM2DlyRelay)
            .withOptionsFrom(synEnvM2ARelay)
            .withOptionsFrom(synEnvM2HRelay)
            .withOptionsFrom(synEnvM2DRelay)
            .withOptionsFrom(synEnvM2SRelay)
            .withOptionsFrom(synEnvM2RRelay)
            .withOptionsFrom(synEnvM2CaRelay)
            .withOptionsFrom(synEnvM2CdRelay)
            .withOptionsFrom(synEnvM2CrRelay)
            .withOptionsFrom(synEnvM2LoopRelay)
            .withOptionsFrom(synOscAWtPresetRelay)
            .withOptionsFrom(synOscAWtFrameRelay)
            .withOptionsFrom(synOscAWarpModeRelay)
            .withOptionsFrom(synOscAWarpAmountRelay)
            .withOptionsFrom(synOscAWarp2ModeRelay)
            .withOptionsFrom(synOscAWarp2AmtRelay)
            .withOptionsFrom(synOscAPhaseModeRelay)
            .withOptionsFrom(synOscAWaverRelay)
            .withOptionsFrom(synOscAKeytrackRelay)
            .withOptionsFrom(synOscAKeytrackDestRelay)
            .withOptionsFrom(synOscARouteSrcRelay)
            .withOptionsFrom(synOscARouteDestRelay)
            .withOptionsFrom(synOscARouteAmtRelay)
            .withOptionsFrom(synOscAUnisonRelay)
            .withOptionsFrom(synOscAUdetuneRelay)
            .withOptionsFrom(synOscAUblendRelay)
            .withOptionsFrom(synOscAUwidthRelay)
            .withOptionsFrom(synOscBEngineRelay)
            .withOptionsFrom(synOscBOctRelay)
            .withOptionsFrom(synOscBSemiRelay)
            .withOptionsFrom(synOscBCentRelay)
            .withOptionsFrom(synOscBLevelRelay)
            .withOptionsFrom(synOscBPanRelay)
            .withOptionsFrom(synOscBWtPresetRelay)
            .withOptionsFrom(synOscBWtFrameRelay)
            .withOptionsFrom(synOscBWarpModeRelay)
            .withOptionsFrom(synOscBWarpAmountRelay)
            .withOptionsFrom(synOscBWarp2ModeRelay)
            .withOptionsFrom(synOscBWarp2AmtRelay)
            .withOptionsFrom(synOscBPhaseModeRelay)
            .withOptionsFrom(synOscBWaverRelay)
            .withOptionsFrom(synOscBKeytrackRelay)
            .withOptionsFrom(synOscBKeytrackDestRelay)
            .withOptionsFrom(synOscBRouteSrcRelay)
            .withOptionsFrom(synOscBRouteDestRelay)
            .withOptionsFrom(synOscBRouteAmtRelay)
            .withOptionsFrom(synOscBUnisonRelay)
            .withOptionsFrom(synOscBUdetuneRelay)
            .withOptionsFrom(synOscBUblendRelay)
            .withOptionsFrom(synOscBUwidthRelay)
            .withOptionsFrom(synVoicesRelay)
            .withOptionsFrom(synUnisonRelay)
            .withOptionsFrom(synSpreadRelay)
            .withOptionsFrom(synErosionRelay)
            .withOptionsFrom(synHorizonRelay)
            .withOptionsFrom(synPortaRelay)
            .withOptionsFrom(synGlideCurveRelay)
            .withOptionsFrom(synGlideAlwaysRelay)
            .withOptionsFrom(synGlideScaledRelay)
            .withOptionsFrom(synMonoRelay)
            .withOptionsFrom(synLegatoRelay)
            .withOptionsFrom(synOscASpectralTypeRelay)
            .withOptionsFrom(synOscASpectralAmtRelay)
            .withOptionsFrom(synOscAFoldShapeRelay)
            .withOptionsFrom(synOscAFoldAmtRelay)
            .withOptionsFrom(synOscAFrameSpreadRelay)
            .withOptionsFrom(synOscAInterpModeRelay)
            .withOptionsFrom(synOscBSpectralTypeRelay)
            .withOptionsFrom(synOscBSpectralAmtRelay)
            .withOptionsFrom(synOscBFoldShapeRelay)
            .withOptionsFrom(synOscBFoldAmtRelay)
            .withOptionsFrom(synOscBFrameSpreadRelay)
            .withOptionsFrom(synOscBInterpModeRelay)
            // ── OSC C + D options (4-osc) ──
            .withOptionsFrom(synOscCEngineRelay)
            .withOptionsFrom(synOscCOctRelay)
            .withOptionsFrom(synOscCSemiRelay)
            .withOptionsFrom(synOscCCentRelay)
            .withOptionsFrom(synOscCLevelRelay)
            .withOptionsFrom(synOscCPanRelay)
            .withOptionsFrom(synOscCWtPresetRelay)
            .withOptionsFrom(synOscCWtFrameRelay)
            .withOptionsFrom(synOscCWarpModeRelay)
            .withOptionsFrom(synOscCWarpAmountRelay)
            .withOptionsFrom(synOscCWarp2ModeRelay)
            .withOptionsFrom(synOscCWarp2AmtRelay)
            .withOptionsFrom(synOscCPhaseModeRelay)
            .withOptionsFrom(synOscCWaverRelay)
            .withOptionsFrom(synOscCKeytrackRelay)
            .withOptionsFrom(synOscCKeytrackDestRelay)
            .withOptionsFrom(synOscCRouteSrcRelay)
            .withOptionsFrom(synOscCRouteDestRelay)
            .withOptionsFrom(synOscCRouteAmtRelay)
            .withOptionsFrom(synOscCUnisonRelay)
            .withOptionsFrom(synOscCUdetuneRelay)
            .withOptionsFrom(synOscCUblendRelay)
            .withOptionsFrom(synOscCUwidthRelay)
            .withOptionsFrom(synOscCSpectralTypeRelay)
            .withOptionsFrom(synOscCSpectralAmtRelay)
            .withOptionsFrom(synOscCFoldShapeRelay)
            .withOptionsFrom(synOscCFoldAmtRelay)
            .withOptionsFrom(synOscCFrameSpreadRelay)
            .withOptionsFrom(synOscCInterpModeRelay)
            .withOptionsFrom(synOscDEngineRelay)
            .withOptionsFrom(synOscDOctRelay)
            .withOptionsFrom(synOscDSemiRelay)
            .withOptionsFrom(synOscDCentRelay)
            .withOptionsFrom(synOscDLevelRelay)
            .withOptionsFrom(synOscDPanRelay)
            .withOptionsFrom(synOscDWtPresetRelay)
            .withOptionsFrom(synOscDWtFrameRelay)
            .withOptionsFrom(synOscDWarpModeRelay)
            .withOptionsFrom(synOscDWarpAmountRelay)
            .withOptionsFrom(synOscDWarp2ModeRelay)
            .withOptionsFrom(synOscDWarp2AmtRelay)
            .withOptionsFrom(synOscDPhaseModeRelay)
            .withOptionsFrom(synOscDWaverRelay)
            .withOptionsFrom(synOscDKeytrackRelay)
            .withOptionsFrom(synOscDKeytrackDestRelay)
            .withOptionsFrom(synOscDRouteSrcRelay)
            .withOptionsFrom(synOscDRouteDestRelay)
            .withOptionsFrom(synOscDRouteAmtRelay)
            .withOptionsFrom(synOscDUnisonRelay)
            .withOptionsFrom(synOscDUdetuneRelay)
            .withOptionsFrom(synOscDUblendRelay)
            .withOptionsFrom(synOscDUwidthRelay)
            // ════ SAMPLE-ENGINE-WITHOPTIONS (Opus) ════
            .withOptionsFrom(synOscASampleScanRelay)
            .withOptionsFrom(synOscASampleStretchRelay)
            .withOptionsFrom(synOscASampleFormantRelay)
            .withOptionsFrom(synOscASampleSprayRelay)
            .withOptionsFrom(synOscASampleXfadeRelay)
            .withOptionsFrom(synOscASampleStartRelay)
            .withOptionsFrom(synOscASampleEndRelay)
            .withOptionsFrom(synOscASampleLoopStartRelay)
            .withOptionsFrom(synOscASampleLoopEndRelay)
            .withOptionsFrom(synOscASampleLoopModeRelay)
            .withOptionsFrom(synOscASampleSnapRelay)
            .withOptionsFrom(synOscASampleFadeInRelay)
            .withOptionsFrom(synOscASampleFadeOutRelay)
            .withOptionsFrom(synOscBSampleScanRelay)
            .withOptionsFrom(synOscBSampleStretchRelay)
            .withOptionsFrom(synOscBSampleFormantRelay)
            .withOptionsFrom(synOscBSampleSprayRelay)
            .withOptionsFrom(synOscBSampleXfadeRelay)
            .withOptionsFrom(synOscBSampleStartRelay)
            .withOptionsFrom(synOscBSampleEndRelay)
            .withOptionsFrom(synOscBSampleLoopStartRelay)
            .withOptionsFrom(synOscBSampleLoopEndRelay)
            .withOptionsFrom(synOscBSampleLoopModeRelay)
            .withOptionsFrom(synOscBSampleSnapRelay)
            .withOptionsFrom(synOscBSampleFadeInRelay)
            .withOptionsFrom(synOscBSampleFadeOutRelay)
            .withOptionsFrom(synOscCSampleScanRelay)
            .withOptionsFrom(synOscCSampleStretchRelay)
            .withOptionsFrom(synOscCSampleFormantRelay)
            .withOptionsFrom(synOscCSampleSprayRelay)
            .withOptionsFrom(synOscCSampleXfadeRelay)
            .withOptionsFrom(synOscCSampleStartRelay)
            .withOptionsFrom(synOscCSampleEndRelay)
            .withOptionsFrom(synOscCSampleLoopStartRelay)
            .withOptionsFrom(synOscCSampleLoopEndRelay)
            .withOptionsFrom(synOscCSampleLoopModeRelay)
            .withOptionsFrom(synOscCSampleSnapRelay)
            .withOptionsFrom(synOscCSampleFadeInRelay)
            .withOptionsFrom(synOscCSampleFadeOutRelay)
            .withOptionsFrom(synOscDSampleScanRelay)
            .withOptionsFrom(synOscDSampleStretchRelay)
            .withOptionsFrom(synOscDSampleFormantRelay)
            .withOptionsFrom(synOscDSampleSprayRelay)
            .withOptionsFrom(synOscDSampleXfadeRelay)
            .withOptionsFrom(synOscDSampleStartRelay)
            .withOptionsFrom(synOscDSampleEndRelay)
            .withOptionsFrom(synOscDSampleLoopStartRelay)
            .withOptionsFrom(synOscDSampleLoopEndRelay)
            .withOptionsFrom(synOscDSampleLoopModeRelay)
            .withOptionsFrom(synOscDSampleSnapRelay)
            .withOptionsFrom(synOscDSampleFadeInRelay)
            .withOptionsFrom(synOscDSampleFadeOutRelay)
            .withOptionsFrom(synOscDSpectralTypeRelay)
            .withOptionsFrom(synOscDSpectralAmtRelay)
            .withOptionsFrom(synOscDFoldShapeRelay)
            .withOptionsFrom(synOscDFoldAmtRelay)
            .withOptionsFrom(synOscDFrameSpreadRelay)
            .withOptionsFrom(synOscDInterpModeRelay)
            .withOptionsFrom(flowModeRelay).withOptionsFrom(flowArpLatchRelay)
            .withOptionsFrom(flowArpRateRelay).withOptionsFrom(flowArpGateRelay).withOptionsFrom(flowArpVaryRelay).withOptionsFrom(flowArpTrajRelay).withOptionsFrom(flowArpMorphRelay)
            .withOptionsFrom(flowSeqRateRelay).withOptionsFrom(flowSeqGateRelay).withOptionsFrom(flowSeqVaryRelay).withOptionsFrom(flowSeqTrajRelay).withOptionsFrom(flowSeqMorphRelay)
            .withOptionsFrom(flowChopBlendRelay).withOptionsFrom(flowGliBlendRelay).withOptionsFrom(flowArpBlendRelay)
            .withOptionsFrom(flowGliRateRelay).withOptionsFrom(flowGliGateRelay).withOptionsFrom(flowGliVaryRelay).withOptionsFrom(flowGliTrajRelay).withOptionsFrom(flowGliMorphRelay)
            .withOptionsFrom(flowDrfRateRelay).withOptionsFrom(flowDrfGateRelay).withOptionsFrom(flowDrfVaryRelay).withOptionsFrom(flowDrfTrajRelay).withOptionsFrom(flowDrfMorphRelay)
            .withNativeFunction("loadPreset", [this](const juce::Array<juce::var>& args,
                                                      juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.loadPreset(static_cast<int>(args[0]));
                complete({});
            })
            .withNativeFunction("setSynthMod", [this](const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.setSynthModMatrix(args[0].toString());
                complete(juce::var{});
            })
            .withNativeFunction("getSynthMod", [this](const juce::Array<juce::var>&,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.getSynthModMatrix());
            })
            .withNativeFunction("setDelayFreeze", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_FREEZE)->setValueNotifyingHost(
                        static_cast<bool>(args[0]) ? 1.0f : 0.0f);
                complete(juce::var{});
            })
            .withNativeFunction("getPresetName", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                int idx = args.size() > 0 ? static_cast<int>(args[0]) : audioProcessor.currentPresetIndex.load();
                complete(audioProcessor.getPresetName(idx));
            })
            .withNativeFunction("getNumPresets", [this](const juce::Array<juce::var>&,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.getPresetCount());
            })
            .withNativeFunction("getCurrentPresetIndex", [this](const juce::Array<juce::var>&,
                                                                  juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.currentPresetIndex.load());
            })
            .withNativeFunction("saveNewPreset", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::String name = args.size() > 0 ? args[0].toString() : "New Preset";
                juce::String tag  = args.size() > 1 ? args[1].toString() : juce::String();
                int newIdx = audioProcessor.saveNewPreset(name, tag);
                complete(newIdx);
            })
            .withNativeFunction("renamePreset", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                    audioProcessor.renamePreset(static_cast<int>(args[0]), args[1].toString());
                complete({});
            })
            .withNativeFunction("overwritePreset", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.overwritePreset(static_cast<int>(args[0]));
                complete({});
            })
            .withNativeFunction("deletePreset", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.deletePreset(static_cast<int>(args[0]));
                complete({});
            })
            .withNativeFunction("getFactoryPresetCount", [this](const juce::Array<juce::var>&,
                                                                  juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.getFactoryPresetCount());
            })
            .withNativeFunction("getPresetTag", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                int idx = args.size() > 0 ? static_cast<int>(args[0]) : -1;
                complete(audioProcessor.getPresetTag(idx));
            })
            .withNativeFunction("setPresetTag", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                    audioProcessor.setPresetTag(static_cast<int>(args[0]), args[1].toString());
                complete({});
            })
            .withNativeFunction("getCustomTags", [this](const juce::Array<juce::var>&,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.getCustomTags());
            })
            .withNativeFunction("setCustomTags", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.setCustomTags(args[0].toString());
                complete({});
            })
            .withNativeFunction("setXYAutoState", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 3)
                {
                    audioProcessor.xyAutoEnabled.store(static_cast<float>(args[0]));
                    audioProcessor.xyAutoMode.store(static_cast<float>(args[1]));
                    audioProcessor.xyAutoSpeed.store(static_cast<float>(args[2]));
                }
                complete({});
            })
            .withNativeFunction("getXYAutoState", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::var result;
                auto* obj = new juce::DynamicObject();
                obj->setProperty("enabled", audioProcessor.xyAutoEnabled.load());
                obj->setProperty("mode",    audioProcessor.xyAutoMode.load());
                obj->setProperty("speed",   audioProcessor.xyAutoSpeed.load());
                result = juce::var(obj);
                complete(result);
            })
            .withNativeFunction("setGrainSync", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.grainSyncEnabled.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getGrainSync", [this](const juce::Array<juce::var>&,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                auto* obj = new juce::DynamicObject();
                obj->setProperty("enabled", audioProcessor.grainSyncEnabled.load());
                obj->setProperty("bpm",     audioProcessor.currentBPM.load());
                complete(juce::var(obj));
            })
            .withNativeFunction("setGrainEngineEnabled", [this](const juce::Array<juce::var>& args,
                                                                 juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.grainEngineEnabled.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getGrainEngineEnabled", [this](const juce::Array<juce::var>&,
                                                                 juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.grainEngineEnabled.load());
            })
            .withNativeFunction("setTapeEnabled", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.tapeEnabled.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getTapeEnabled", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.tapeEnabled.load());
            })
            .withNativeFunction("setTapeLoopEnabled", [this](const juce::Array<juce::var>& args,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.tapeLoopEnabled.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getTapeLoopEnabled", [this](const juce::Array<juce::var>&,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.tapeLoopEnabled.load());
            })
            .withNativeFunction("setEqPanelOpen", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.eqPanelOpen.store(static_cast<float>(args[0]));
                complete(audioProcessor.eqPanelOpen.load());
            })
            .withNativeFunction("getEqPanelOpen", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.eqPanelOpen.load());
            })
            .withNativeFunction("setEqSolo", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                const int band = args.size() > 0 ? (int) args[0] : -1;
                audioProcessor.eqL.setSolo(band);
                audioProcessor.eqR.setSolo(band);
                complete(band);
            })
            .withNativeFunction("getTapeMachine", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.getAPVTS().getParameter(ParameterIDs::TAPE_MACHINE)))
                    complete(cp->getIndex());
                else
                    complete(0);
            })
            .withNativeFunction("setTapeMachine", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    int idx = static_cast<int>(args[0]);
                    if (auto* param = audioProcessor.getAPVTS().getParameter(ParameterIDs::TAPE_MACHINE))
                        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(idx)));
                }
                complete({});
            })
            .withNativeFunction("setDriftLinked", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.wanderLinked.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getDriftLinked", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.wanderLinked.load());
            })
            .withNativeFunction("setTapeLoopRecord", [this](const juce::Array<juce::var>& args,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.tapeLoopRecording.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getTapeLoopRecord", [this](const juce::Array<juce::var>&,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.tapeLoopRecording.load());
            })
            .withNativeFunction("setTapeLoopPlay", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.tapeLoopPlaying.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getTapeLoopPlay", [this](const juce::Array<juce::var>&,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.tapeLoopPlaying.load());
            })
            .withNativeFunction("clearTapeLoop", [this](const juce::Array<juce::var>&,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                audioProcessor.clearTapeLoop();
                complete({});
            })
            .withNativeFunction("undoTapeLoop", [this](const juce::Array<juce::var>&,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                audioProcessor.undoTapeLoop();
                complete({});
            })
            .withNativeFunction("setSpeedFreeform", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.speedFreeform.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getSpeedFreeform", [this](const juce::Array<juce::var>&,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.speedFreeform.load());
            })
            .withNativeFunction("setPitchLocked", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.pitchLocked.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getPitchLocked", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.pitchLocked.load());
            })
            .withNativeFunction("setFeedToGrain", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.tapeLoopFeedToGrain.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getFeedToGrain", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.tapeLoopFeedToGrain.load());
            })
            .withNativeFunction("exportCapture", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                int dur = args.size() > 0 ? static_cast<int>(args[0]) : 60;
                audioProcessor.exportCapture(dur);
                complete({});
            })
            .withNativeFunction("resetCaptureState", [this](const juce::Array<juce::var>&,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                audioProcessor.captureExportState.store(0);
                complete({});
            })
            .withNativeFunction("updateModConfig", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    auto json = args[0].toString();
                    audioProcessor.modStateJson = json;
                    audioProcessor.modulationEngine.updateConfig(
                        ModulationEngine::parseJSON(json));
                }
                complete({});
            })
            .withNativeFunction("getModState", [this](const juce::Array<juce::var>&,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(juce::var(audioProcessor.modStateJson));
            })
            .withNativeFunction("signalPageReady", [this](const juce::Array<juce::var>&,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                pageReady = true;
                complete({});
            })
            .withNativeFunction("setWireSpaceNoise", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.wireSpaceNoiseEnabled.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getWireSpaceNoise", [this](const juce::Array<juce::var>&,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.wireSpaceNoiseEnabled.load());
            })
            .withNativeFunction("setWireTubeSat", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.wireTubeSatEnabled.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getWireTubeSat", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.wireTubeSatEnabled.load());
            })
            .withNativeFunction("setTapeLinked", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 1)
                    audioProcessor.tapeLinkEnabled.store(static_cast<float>(args[0]));
                complete({});
            })
            .withNativeFunction("getTapeLinked", [this](const juce::Array<juce::var>&,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.tapeLinkEnabled.load());
            })
            .withNativeFunction("setXYPad", [this](const juce::Array<juce::var>& args,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                {
                    // When the pad is disabled, ignore positional writes — the
                    // mod engine should keep seeing the neutral (0.5, 0.5)
                    // forced by setXYEnabled. JS gates clicks/drags/auto-play
                    // too but this is the last line of defense.
                    if (audioProcessor.xyEnabled.load(std::memory_order_relaxed) > 0.5f)
                    {
                        audioProcessor.xyPadX.store(static_cast<float>(args[0]), std::memory_order_relaxed);
                        audioProcessor.xyPadY.store(static_cast<float>(args[1]), std::memory_order_relaxed);
                    }
                }
                complete({});
            })
            .withNativeFunction("setXYEnabled", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const bool on = ((float) args[0]) > 0.5f;
                    audioProcessor.xyEnabled.store(on ? 1.0f : 0.0f, std::memory_order_relaxed);
                    if (! on)
                    {
                        // Neutralize XY mod source: (0.5 - 0.5) * 2 == 0 for
                        // both axes, so any XY-targeted mod contributes nothing
                        // regardless of polarity (bipolar / uni+ / uni-).
                        audioProcessor.xyPadX.store(0.5f, std::memory_order_relaxed);
                        audioProcessor.xyPadY.store(0.5f, std::memory_order_relaxed);
                    }
                }
                complete({});
            })
            .withNativeFunction("getXYEnabled", [this](const juce::Array<juce::var>&,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.xyEnabled.load(std::memory_order_relaxed));
            })
            .withNativeFunction("getSettings", [this](const juce::Array<juce::var>&,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("Waves Crate").getChildFile("Terrain").getChildFile("InstrumentSettings.json");
                complete(f.existsAsFile() ? f.loadFileAsString() : juce::String("{}"));
            })
            .withNativeFunction("saveSettings", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("Waves Crate").getChildFile("Terrain");
                    dir.createDirectory();
                    dir.getChildFile("InstrumentSettings.json").replaceWithText(args[0].toString());

                    // Update capture strip theme
                    auto json = args[0].toString();
                    captureDragStrip.setDarkMode(json.contains("\"dark\""));
                }
                complete({});
            })
            .withNativeFunction("setSynthView", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const bool on = static_cast<int>(args[0]) != 0;
                    captureDragStrip.setSynthViewActive(on);
                    synthPageActive_ = on;   // PEROSC-DRAGGUARD
                }
                complete(juce::var{});
            })
            .withNativeFunction("setChorusEnabled", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.chorusEnabled.store(static_cast<float>(args[0]));
                complete(juce::var{});
            })
            .withNativeFunction("getChorusEnabled", [this](const juce::Array<juce::var>&,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.chorusEnabled.load());
            })
            .withNativeFunction("setDelayEnabled", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.delayEnabled.store(static_cast<float>(args[0]));
                complete(juce::var{});
            })
            .withNativeFunction("getDelayEnabled", [this](const juce::Array<juce::var>&,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete(audioProcessor.delayEnabled.load());
            })
            // Sample drag-drop bridge (Phase C — Task 11). WKWebView eats native
            // file drops at the OS level, so the JS side handles dragover/drop and
            // calls back here with the absolute file path (fast path) or the file
            // bytes as base64 (fallback when file.path isn't available, which is
            // the common case in modern WKWebView for security reasons).
            .withNativeFunction("loadSampleFromPath", [this](const juce::Array<juce::var>& args,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const juce::File f (args[0].toString());
                    if (f.existsAsFile()) loadSampleAsync (f);
                }
                complete ({});
            })
            .withNativeFunction("setRootNote", [this](const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const int midi = juce::jlimit (0, 127, (int) args[0]);
                    if (auto* p = audioProcessor.getAPVTS().getParameter (ParameterIDs::ROOT_NOTE))
                    {
                        // AudioParameterInt expects normalised [0..1]
                        p->setValueNotifyingHost (midi / 127.0f);
                    }
                    audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].rootMidiNote.store (midi);
                }
                complete ({});
            })
            .withNativeFunction("getCachedSamplePayload", [this](const juce::Array<juce::var>&,
                                                                  juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // JS pulls this on hero-overlay init. Returns the cached
                // sample payload JSON (filename + peaks + meta) for the
                // CURRENTLY EDITING layer. Editor close/reopen uses this to
                // restore the visible waveform without a re-decode.
                // Empty string if no sample loaded yet.
                complete (audioProcessor.getCachedSamplePayload());
            })
            .withNativeFunction("getEditingLayerIdx", [this](const juce::Array<juce::var>&,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Mark 2 Phase 1 editor-reopen fix: JS pulls this so the
                // A/B/C/D .active pad class and state.editingLayerIdx mirror
                // are restored on every fresh editor open.
                complete (audioProcessor.editingLayer.load());
            })
            .withNativeFunction("getAllLayerPayloads", [this](const juce::Array<juce::var>&,
                                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Mark 2 Phase 1 editor-reopen fix: returns one rich payload
                // per layer so JS can hydrate state.layerStates[] for all 4
                // pads on every editor open. Each entry is either:
                //   - an empty object {} → no sample loaded
                //   - { filename, sampleRate, lengthSamples, numChannels,
                //       peaksMin, peaksMax,
                //       sliceMode, sampleLoopMode, rootMidiNote, activeSliceIndex }
                //
                // The peaks/meta fields come from the per-layer cached payload
                // (populated by loadSampleAsync / loadSampleIntoLayer); the
                // mode/root atomics are read fresh at call time so they reflect
                // the very latest user edits even if those happened after the
                // sample originally loaded.
                const auto cached = audioProcessor.getAllLayerPayloads();
                juce::Array<juce::var> out;
                out.ensureStorageAllocated (4);
                for (int i = 0; i < 4; ++i)
                {
                    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                    if (cached[(size_t) i].isNotEmpty())
                    {
                        // Parse the cached peaks-payload and copy every top-level
                        // property over to the output object.
                        auto parsed = juce::JSON::parse (cached[(size_t) i]);
                        if (auto* src = parsed.getDynamicObject())
                        {
                            const auto& props = src->getProperties();
                            for (int p = 0; p < props.size(); ++p)
                                obj->setProperty (props.getName (p), props.getValueAt (p));
                        }
                        // Merge in the per-layer mode atomics.
                        const auto& L = audioProcessor.layers[(size_t) i];
                        obj->setProperty ("sliceMode",        L.sliceMode.load());
                        obj->setProperty ("sampleLoopMode",   L.sampleLoopMode.load());
                        obj->setProperty ("rootMidiNote",     L.rootMidiNote.load());
                        obj->setProperty ("activeSliceIndex", L.activeSliceIndex.load());
                    }
                    // Empty layers fall through with no properties set —
                    // JS treats absence of filename/peaks as "no sample".
                    out.add (juce::var (obj.get()));
                }
                complete (juce::var (out));
            })
            .withNativeFunction("setSampleLoopMode", [this](const juce::Array<juce::var>& args,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const int mode = juce::jlimit (0, 1, (int) args[0]);
                    if (auto* p = audioProcessor.getAPVTS().getParameter (ParameterIDs::SAMPLE_LOOP_MODE))
                        p->setValueNotifyingHost (static_cast<float> (mode));  // Choice: 0=ONE-SHOT, 1=LOOP
                    audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].sampleLoopMode.store (mode);
                }
                complete ({});
            })
            .withNativeFunction("getSampleLoopMode", [this](const juce::Array<juce::var>&,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].sampleLoopMode.load());
            })
            .withNativeFunction("setChopFadeMs", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // JS sends a float ms value (0..50). Write to APVTS so it's
                // automatable and persisted; chopFadeMsAtomic is synced from
                // APVTS each processBlock so no separate store needed here.
                if (args.size() > 0)
                {
                    const float ms = juce::jlimit (0.0f, 50.0f, (float) args[0]);
                    if (auto* p = audioProcessor.getAPVTS().getParameter (ParameterIDs::CHOP_FADE_MS))
                        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (ms));
                }
                complete ({});
            })
            .withNativeFunction("getChopFadeMs", [this](const juce::Array<juce::var>&,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].chopFadeMs.load());
            })

            // ────────────────────────────────────────────────────────────
            // Slicer native fns (v0b)
            // ────────────────────────────────────────────────────────────
            .withNativeFunction("setSliceMode", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const int mode = juce::jlimit (0, 1, (int) args[0]);  // 0=PITCH, 1=SLICE
                    // Mark 2 Phase 1 audio-fix: sliceMode is now per-layer.
                    // Engine reads from layer[editingLayer].sliceMode in processBlock,
                    // so writing here is the source of truth for that layer's mode.
                    audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()]
                                  .sliceMode.store (mode);
                    // Keep APVTS in sync so DAW automation, host displays, and V1
                    // preset save format all still see a sensible value. The engine
                    // no longer reads this; it tracks the editing layer's mode.
                    if (auto* p = audioProcessor.getAPVTS().getParameter (ParameterIDs::SLICE_MODE))
                        p->setValueNotifyingHost (static_cast<float> (mode));
                }
                complete ({});
            })
            .withNativeFunction("getSliceMode", [this](const juce::Array<juce::var>&,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Per-layer: read the editing layer's atomic (source of truth post-Phase-1-audio-fix).
                complete (audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()]
                                        .sliceMode.load());
            })
            .withNativeFunction("clearEditingLayer", [this](const juce::Array<juce::var>&,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Mark 2 Phase 1: PITCH-mode double-click "unload" gesture.
                // Drops the editing layer's sample + slices + pitch-mode config
                // so the layer returns to the empty drag-prompt state. Other
                // layers and this layer's mode/root/loop pill settings are
                // preserved — only data clears, so a follow-up drop reuses them.
                const size_t el = (size_t) audioProcessor.editingLayer.load();
                auto& L = audioProcessor.layers[el];

                // Stop voices first so they don't dereference the buffer we're
                // dropping. allowTailOff=true gives a graceful release (no pop).
                L.synth.allNotesOff (0 /* all channels */, true /* allowTailOff */);

                // Drop the audio data + per-layer slice state.
                L.sampleBuffer.store (tw::SampleBuffer::BufferPtr{});
                std::atomic_store (&L.currentSlices, tw::SliceListPtr{});
                L.pitchModeSlice = tw::Slice{};
                L.activeSliceIndex.store (0);
                L.sourceFileName = juce::String();
                L.sourcePath     = juce::String();

                // Bump source version so voices invalidate any cached pointers
                // and the warp cache stops being read.
                audioProcessor.sourceVersionId_.fetch_add (1, std::memory_order_relaxed);

                // Drop the editor-reopen payload cache for this layer.
                audioProcessor.setCachedSamplePayload (juce::String(), (int) el);

                // If clearing layer 0, also wipe the legacy single sample path
                // so DAW close+reopen doesn't trigger a V1-fallback file reload.
                if (el == 0)
                    audioProcessor.setLoadedSamplePath (juce::String());

                complete ({});
            })
            .withNativeFunction("getSlicesJson", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.getSlicesJson());
            })
            .withNativeFunction("getPitchSliceJson", [this](const juce::Array<juce::var>&,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.getPitchSliceJson());
            })
            .withNativeFunction("setSlicesJson", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.setSlicesFromJson (args[0].toString());
                complete ({});
            })
            .withNativeFunction("clearSlices", [this](const juce::Array<juce::var>&,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                audioProcessor.replaceSlices ({});
                complete ({});
            })
            .withNativeFunction("autoDetectSlices", [this](const juce::Array<juce::var>& args,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sensitivity 0..1 (default 0.5)
                const float sensitivity = args.size() > 0
                                            ? juce::jlimit (0.0f, 1.0f, (float) (double) args[0])
                                            : 0.5f;
                auto buf = audioProcessor.getSampleBuffer().load();
                if (! buf || buf->getNumSamples() < 64)
                {
                    complete (juce::var ("no-sample"));
                    return;
                }
                const double sr = audioProcessor.getSampleBuffer().getSampleRate();
                auto detected = tw::detectTransients (*buf, sr > 0.0 ? sr : 48000.0, sensitivity);
                audioProcessor.replaceSlices (std::move (detected));
                complete (audioProcessor.getSlicesJson());
            })
            .withNativeFunction("gridSliceSlices", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = number of slices (positive int)
                const int n = args.size() > 0 ? juce::jlimit (1, 128, (int) args[0]) : 16;
                auto buf = audioProcessor.getSampleBuffer().load();
                if (! buf || buf->getNumSamples() < 64)
                {
                    complete (juce::var ("no-sample"));
                    return;
                }
                auto grid = tw::makeGridSlices (*buf, n, true);
                audioProcessor.replaceSlices (std::move (grid));
                complete (audioProcessor.getSlicesJson());
            })
            .withNativeFunction("setSliceSubMode", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const int sub = juce::jlimit (0, 3, (int) args[0]);  // 0=CHOP, 1=CHROMATIC, 2=RANDOM, 3=LAYER
                    if (auto* p = audioProcessor.getAPVTS().getParameter (ParameterIDs::SLICE_SUB_MODE))
                    {
                        // setValueNotifyingHost wants a NORMALISED value [0,1], not the
                        // choice index. With 2 choices the two ranges coincide, but with
                        // 3+ choices the normalisation must be computed explicitly.
                        const float norm = p->getNormalisableRange().convertTo0to1 (static_cast<float> (sub));
                        p->setValueNotifyingHost (norm);
                    }
                }
                complete ({});
            })
            .withNativeFunction("getSliceSubMode", [this](const juce::Array<juce::var>&,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete ((int) *audioProcessor.getAPVTS().getRawParameterValue (ParameterIDs::SLICE_SUB_MODE));
            })
            .withNativeFunction("setActiveSliceIndex", [this](const juce::Array<juce::var>& args,
                                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const int n = audioProcessor.getNumSlices();
                    int idx = (int) args[0];
                    if (n > 0) idx = juce::jlimit (0, n - 1, idx);
                    else       idx = 0;
                    audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].activeSliceIndex.store (idx);
                }
                complete ({});
            })
            .withNativeFunction("getActiveSliceIndex", [this](const juce::Array<juce::var>&,
                                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].activeSliceIndex.load());
            })
            .withNativeFunction("setEditingLayer", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Mark 2 Phase 1 Task 6: JS pad click switches editingLayer.
                // Clamps to [0,3] — only 4 layers (A/B/C/D) exist.
                if (args.size() > 0)
                {
                    int idx = juce::jlimit (0, 3, (int) args[0]);
                    audioProcessor.editingLayer.store (idx);
                }
                complete ({});
            })
            .withNativeFunction("getLayerHasSample", [this](const juce::Array<juce::var>& args,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Mark 2 Phase 1 Task 7: per-layer empty-state polling.
                // Optional arg = layer index 0..3; default to editingLayer.
                int idx = (args.size() > 0) ? (int) args[0] : audioProcessor.editingLayer.load();
                idx = juce::jlimit (0, 3, idx);
                complete (audioProcessor.layers[(size_t) idx].hasSample());
            })
            // Mark 2 Phase 1 Task 10: per-layer mixer native fns (vol / mute / solo).
            // No APVTS — atomics only. processBlock already applies the mixer math.
            .withNativeFunction("getLayerVolume", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                int idx = juce::jlimit (0, 3, (args.size() > 0) ? (int) args[0] : 0);
                complete (audioProcessor.layers[(size_t) idx].volume.load());
            })
            .withNativeFunction("setLayerVolume", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                {
                    int idx  = juce::jlimit (0, 3, (int) args[0]);
                    float v  = juce::jlimit (0.0f, 2.0f, (float) (double) args[1]);
                    audioProcessor.layers[(size_t) idx].volume.store (v);
                }
                complete ({});
            })
            .withNativeFunction("getLayerMute", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                int idx = juce::jlimit (0, 3, (args.size() > 0) ? (int) args[0] : 0);
                complete (audioProcessor.layers[(size_t) idx].mute.load());
            })
            .withNativeFunction("setLayerMute", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                {
                    int idx = juce::jlimit (0, 3, (int) args[0]);
                    audioProcessor.layers[(size_t) idx].mute.store ((bool) args[1]);
                }
                complete ({});
            })
            .withNativeFunction("getLayerSolo", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                int idx = juce::jlimit (0, 3, (args.size() > 0) ? (int) args[0] : 0);
                complete (audioProcessor.layers[(size_t) idx].solo.load());
            })
            .withNativeFunction("setLayerSolo", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                {
                    int idx = juce::jlimit (0, 3, (int) args[0]);
                    audioProcessor.layers[(size_t) idx].solo.store ((bool) args[1]);
                }
                complete ({});
            })
            .withNativeFunction("isAnyVoicePlaying", [this](const juce::Array<juce::var>&,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Mark 2 layer-pad lit-up indicator. Walks the voice pool and
                // returns true if any SamplerVoice is currently rendering. Used
                // by the A/B/C/D layer pads to light "A" when sound is playing.
                // Once B/C/D become real layers, this expands to per-layer state.
                // Task 5: routes through layers[editingLayer].synth.
                bool any = false;
                const size_t el = (size_t) audioProcessor.editingLayer.load();
                for (int i = 0; i < audioProcessor.layers[el].synth.getNumVoices(); ++i)
                    if (auto* sv = dynamic_cast<tw::SamplerVoice*> (audioProcessor.layers[el].synth.getVoice (i)))
                        if (sv->isPlaying()) { any = true; break; }
                complete (any);
            })
            .withNativeFunction("getLayerVoiceActivity", [this](const juce::Array<juce::var>&,
                                                                  juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Mark 2 Phase 1 task 11: per-layer voice activity for A/B/C/D pad indicators.
                // Returns 4-element bool array — true if that layer has at least one playing voice.
                juce::Array<juce::var> result;
                for (size_t li = 0; li < audioProcessor.layers.size(); ++li)
                {
                    auto& layer = audioProcessor.layers[li];
                    bool any = false;
                    for (int v = 0; v < layer.synth.getNumVoices(); ++v)
                        if (auto* sv = dynamic_cast<tw::SamplerVoice*> (layer.synth.getVoice (v)))
                            if (sv->isPlaying()) { any = true; break; }
                    result.add (any);
                }
                complete (result);
            })
            // ────────────────────────────────────────────────────────────────
            // Mix page Phase 2 (Phase A engine wiring) — native fns for the
            // 4 channel strips, the 5 trigger modes, and the stem capture toggle.
            // Stem-buffer drag fns (beginStemDrag / beginAllStemsDrag) land in
            // Phase D once the rolling stem buffers exist.
            // ────────────────────────────────────────────────────────────────
            .withNativeFunction("setTriggerMode", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.triggerMode.store (juce::jlimit (0, 4, (int) args[0]));
                complete ({});
            })
            .withNativeFunction("getTriggerMode", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.triggerMode.load());
            })
            .withNativeFunction("setRrSyncToBar", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.rrSyncToBar.store ((bool) args[0]);
                complete ({});
            })
            .withNativeFunction("getRrSyncToBar", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.rrSyncToBar.load());
            })
            .withNativeFunction("resetRoundRobin", [this](const juce::Array<juce::var>&,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                audioProcessor.roundRobinPos.store (0);
                complete ({});
            })
            .withNativeFunction("getRoundRobinPos", [this](const juce::Array<juce::var>&,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.roundRobinPos.load());
            })
            .withNativeFunction("setRrShuffle", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.rrShuffle.store ((bool) args[0]);
                complete ({});
            })
            .withNativeFunction("getRrShuffle", [this](const juce::Array<juce::var>&,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.rrShuffle.load());
            })
            .withNativeFunction("setLayerMorph", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.layerMorph.store (juce::jlimit (0.0f, 1.0f, (float)(double) args[0]));
                complete ({});
            })
            .withNativeFunction("getLayerMorph", [this](const juce::Array<juce::var>&,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete ((double) audioProcessor.layerMorph.load());
            })
            .withNativeFunction("setStemSourceMode", [this](const juce::Array<juce::var>& args,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Accepts 0 (DRY) or 1 (MIX); strings "DRY"/"MIX" also accepted for JS convenience.
                if (args.size() > 0)
                {
                    int v = 0;
                    if (args[0].isInt() || args[0].isDouble())      v = juce::jlimit (0, 1, (int) args[0]);
                    else if (args[0].toString().equalsIgnoreCase ("MIX")) v = 1;
                    audioProcessor.stemSourceMode.store (v);
                }
                complete ({});
            })
            .withNativeFunction("getStemSourceMode", [this](const juce::Array<juce::var>&,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.stemSourceMode.load());
            })
            .withNativeFunction("getStemCaptureLevels", [this](const juce::Array<juce::var>&,
                                                                 juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                juce::Array<juce::var> out;
                for (int i = 0; i < 4; ++i)
                    out.add ((double) audioProcessor.stemCaptureLevel[(size_t) i].load (std::memory_order_relaxed));
                complete (juce::var (out));
            })
            .withNativeFunction("clearStemBuffers", [this](const juce::Array<juce::var>&,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                audioProcessor.clearStemBuffers();
                complete ({});
            })
            .withNativeFunction("setLayerPan", [this](const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                {
                    const int   li = juce::jlimit (0, 3, (int) args[0]);
                    const float p  = juce::jlimit (-1.0f, 1.0f, (float) (double) args[1]);
                    audioProcessor.layers[(size_t) li].pan.store (p);
                }
                complete ({});
            })
            .withNativeFunction("getLayerPan", [this](const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                const int li = (args.size() > 0) ? juce::jlimit (0, 3, (int) args[0])
                                                  : audioProcessor.editingLayer.load();
                complete ((double) audioProcessor.layers[(size_t) li].pan.load());
            })
            .withNativeFunction("setLayerPitchJitter", [this](const juce::Array<juce::var>& args,
                                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                {
                    const int   li = juce::jlimit (0, 3, (int) args[0]);
                    const float c  = juce::jlimit (0.0f, 100.0f, (float) (double) args[1]);
                    audioProcessor.layers[(size_t) li].pitchJitterCents.store (c);
                }
                complete ({});
            })
            .withNativeFunction("getLayerPitchJitter", [this](const juce::Array<juce::var>& args,
                                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                const int li = (args.size() > 0) ? juce::jlimit (0, 3, (int) args[0])
                                                  : audioProcessor.editingLayer.load();
                complete ((double) audioProcessor.layers[(size_t) li].pitchJitterCents.load());
            })
            .withNativeFunction("setLayerProbabilityWeight", [this](const juce::Array<juce::var>& args,
                                                                      juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 2)
                {
                    const int   li = juce::jlimit (0, 3, (int) args[0]);
                    const float w  = juce::jlimit (0.0f, 1.0f, (float) (double) args[1]);
                    audioProcessor.layers[(size_t) li].probabilityWeight.store (w);
                }
                complete ({});
            })
            .withNativeFunction("getLayerProbabilityWeight", [this](const juce::Array<juce::var>& args,
                                                                      juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                const int li = (args.size() > 0) ? juce::jlimit (0, 3, (int) args[0])
                                                  : audioProcessor.editingLayer.load();
                complete ((double) audioProcessor.layers[(size_t) li].probabilityWeight.load());
            })
            .withNativeFunction("setLayerKeyZone", [this](const juce::Array<juce::var>& args,
                                                                 juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 3)
                {
                    const int li = juce::jlimit (0, 3,   (int) args[0]);
                    const int lo = juce::jlimit (0, 127, (int) args[1]);
                    const int hi = juce::jlimit (lo, 127, (int) args[2]);
                    audioProcessor.layers[(size_t) li].keyZoneMin.store (lo);
                    audioProcessor.layers[(size_t) li].keyZoneMax.store (hi);
                }
                complete ({});
            })
            .withNativeFunction("getAllKeyZones", [this](const juce::Array<juce::var>&,
                                                                 juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Returns array of {min, max} per layer in A→D order.
                juce::Array<juce::var> out;
                for (size_t li = 0; li < audioProcessor.layers.size(); ++li)
                {
                    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                    obj->setProperty ("min", audioProcessor.layers[li].keyZoneMin.load());
                    obj->setProperty ("max", audioProcessor.layers[li].keyZoneMax.load());
                    out.add (juce::var (obj.get()));
                }
                complete (juce::var (out));
            })
            .withNativeFunction("setLayerVelocityZone", [this](const juce::Array<juce::var>& args,
                                                                 juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() >= 3)
                {
                    const int li  = juce::jlimit (0, 3,   (int) args[0]);
                    const int lo  = juce::jlimit (0, 127, (int) args[1]);
                    const int hi  = juce::jlimit (lo, 127, (int) args[2]);
                    audioProcessor.layers[(size_t) li].velocityZoneMin.store (lo);
                    audioProcessor.layers[(size_t) li].velocityZoneMax.store (hi);
                }
                complete ({});
            })
            .withNativeFunction("getAllVelocityZones", [this](const juce::Array<juce::var>&,
                                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Returns array of {min, max} per layer in A→D order.
                juce::Array<juce::var> out;
                for (size_t li = 0; li < audioProcessor.layers.size(); ++li)
                {
                    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                    obj->setProperty ("min", audioProcessor.layers[li].velocityZoneMin.load());
                    obj->setProperty ("max", audioProcessor.layers[li].velocityZoneMax.load());
                    out.add (juce::var (obj.get()));
                }
                complete (juce::var (out));
            })
            .withNativeFunction("getLayerPeakLevels", [this](const juce::Array<juce::var>&,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Returns array of {l, r} per layer in A→D order for the strip meters.
                juce::Array<juce::var> out;
                for (size_t li = 0; li < audioProcessor.layers.size(); ++li)
                {
                    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                    obj->setProperty ("l", (double) audioProcessor.layers[li].peakLevelL.load());
                    obj->setProperty ("r", (double) audioProcessor.layers[li].peakLevelR.load());
                    out.add (juce::var (obj.get()));
                }
                complete (juce::var (out));
            })
            .withNativeFunction("exportStem", [this](const juce::Array<juce::var>& args,
                                                      juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Mix page Phase D: writes the layer's rolling stem buffer to a
                // timestamped WAV in ~/Documents/Terrain Instrument Stems/.
                // Returns the file path string (empty on failure). After all
                // stems exported, JS calls revealStemsFolder to open Finder.
                if (args.size() < 1) { complete (juce::var (juce::String())); return; }
                const int idx = juce::jlimit (0, 3, (int) args[0]);
                const auto destFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                          .getChildFile ("Waves Crate").getChildFile ("Terrain Instrument").getChildFile ("Stems");
                const auto written = audioProcessor.exportStemToFile (idx, destFolder);
                complete (juce::var (written.getFullPathName()));
            })
            .withNativeFunction("exportAllStems", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Writes all 4 layer stems to ~/Documents/Terrain Instrument Stems/.
                // Returns an array of file path strings (empty string for failures).
                const auto destFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                          .getChildFile ("Waves Crate").getChildFile ("Terrain Instrument").getChildFile ("Stems");
                juce::Array<juce::var> out;
                for (int i = 0; i < 4; ++i)
                {
                    const auto written = audioProcessor.exportStemToFile (i, destFolder);
                    out.add (written.getFullPathName());
                }
                complete (juce::var (out));
            })
            .withNativeFunction("revealStemsFolder", [this](const juce::Array<juce::var>&,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Opens the stems folder in Finder/Explorer for the user.
                const auto destFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                          .getChildFile ("Waves Crate").getChildFile ("Terrain Instrument").getChildFile ("Stems");
                if (! destFolder.exists()) destFolder.createDirectory();
                destFolder.revealToUser();
                complete ({});
            })
            .withNativeFunction("dragStem", [this](const juce::Array<juce::var>& args,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Click-and-drag stem to the DAW. JS calls this when it detects a
                // drag gesture on a stem button (idx 0..3 = single layer, -1 = all 4).
                // Exports the rolling stem(s) to WAV in the Music folder, then starts
                // an OS file drag via performExternalDragDropOfFiles. NOTE: starting an
                // external drag from a WebView-originated event is best-effort on macOS;
                // the click-to-folder path is the guaranteed fallback.
                const int idx = (args.size() > 0) ? (int) args[0] : -1;
                const auto destFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                          .getChildFile ("Waves Crate").getChildFile ("Terrain Instrument").getChildFile ("Stems");
                juce::StringArray paths;
                auto addLayer = [&] (int li)
                {
                    if (li < 0 || li > 3) return;
                    if (! audioProcessor.layers[(size_t) li].hasSample()) return;
                    const auto f = audioProcessor.exportStemToFile (li, destFolder);
                    if (f.existsAsFile()) paths.add (f.getFullPathName());
                };
                if (idx >= 0) addLayer (idx);
                else          for (int i = 0; i < 4; ++i) addLayer (i);

                if (! paths.isEmpty())
                    juce::DragAndDropContainer::performExternalDragDropOfFiles (paths, false, this, nullptr);

                complete (juce::var (paths.size()));
            })
            .withNativeFunction("auditionSlice", [this](const juce::Array<juce::var>& args,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.auditionSlice ((int) args[0]);
                complete ({});
            })
            .withNativeFunction("getSliceGlowLevels", [this](const juce::Array<juce::var>&,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.snapshotSliceGlowLevels());
            })
            .withNativeFunction("setSliceReverse", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex (int, -1 = pitch-mode virtual slice), args[1] = reverse (bool)
                if (args.size() < 2) { complete ({}); return; }
                const int idx = (int) args[0];
                const bool rev = (bool) args[1];
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.reverse = rev; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].reverse = rev;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSlicePitch", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = semitones (-12..+12)
                if (args.size() < 2) { complete ({}); return; }
                const int   idx = (int) args[0];
                const float st  = juce::jlimit (-12.0f, 12.0f, (float) (double) args[1]);
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.pitchOffsetSemis = st; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].pitchOffsetSemis = st;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceWarpMode", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = warp mode enum (0=None, 1=Beats, 2=Tones, 3=Texture)
                if (args.size() < 2) { complete ({}); return; }
                const int idx     = (int) args[0];
                const int modeRaw = (int) args[1];
                const auto mode   = (modeRaw >= 0 && modeRaw <= 3)
                                       ? static_cast<tw::WarpMode> (modeRaw)
                                       : tw::WarpMode::None;
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.warpMode = mode; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].warpMode = mode;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceStretchRatio", [this](const juce::Array<juce::var>& args,
                                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = stretch ratio (0.1..15.0)
                if (args.size() < 2) { complete ({}); return; }
                const int   idx   = (int) args[0];
                const float ratio = juce::jlimit (0.1f, 15.0f, (float) (double) args[1]);
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.stretchRatio = ratio; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].stretchRatio = ratio;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceAttackMs", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = attack ms (>=0) or -1 to inherit global
                if (args.size() < 2) { complete ({}); return; }
                const int   idx = (int) args[0];
                const float raw = (float) (double) args[1];
                const float ms  = raw < 0.0f ? -1.0f : juce::jlimit (0.0f, 2000.0f, raw);
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.attackMs = ms; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].attackMs = ms;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceReleaseMs", [this](const juce::Array<juce::var>& args,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = release ms (>=1) or -1 to inherit global
                if (args.size() < 2) { complete ({}); return; }
                const int   idx = (int) args[0];
                const float raw = (float) (double) args[1];
                const float ms  = raw < 0.0f ? -1.0f : juce::jlimit (1.0f, 5000.0f, raw);
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.releaseMs = ms; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].releaseMs = ms;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceDecayMs", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = decay ms (0..2000). No inheritance —
                // decay is purely per-chop. 0 = skip decay phase (legacy behavior).
                if (args.size() < 2) { complete ({}); return; }
                const int   idx = (int) args[0];
                const float ms  = juce::jlimit (0.0f, 2000.0f, (float) (double) args[1]);
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.decayMs = ms; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].decayMs = ms;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceSustain", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = sustain level (0..1, 1.0 = peak held).
                if (args.size() < 2) { complete ({}); return; }
                const int   idx = (int) args[0];
                const float lv  = juce::jlimit (0.0f, 1.0f, (float) (double) args[1]);
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.sustainLevel = lv; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].sustainLevel = lv;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceVolume", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = volume (0..2 linear; 1=unity, 2=+6 dB boost).
                if (args.size() < 2) { complete ({}); return; }
                const int   idx = (int) args[0];
                const float vol = juce::jlimit (0.0f, 2.0f, (float) (double) args[1]);
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.volume = vol; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].volume = vol;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            // ─── Per-chop FX independence (Mark 2) ──────────────────────────
            // setSliceFxIndependent(idx, bool) — detaches chop from global chain.
            // setSliceFxBool(idx, "grain"|"space"|"delay"|"eq"|"june", bool)
            // setSliceFxTapeMachine(idx, int 0-3) — 0=off, 1=Studio, 2=Cassette, 3=Wire
            .withNativeFunction("setSliceFxIndependent", [this](const juce::Array<juce::var>& args,
                                                                  juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() < 2) { complete ({}); return; }
                const int  idx = (int) args[0];
                const bool on  = (bool) args[1];
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].fxIndependent = on;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceFxBool", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() < 3) { complete ({}); return; }
                const int          idx   = (int) args[0];
                const juce::String name  = args[1].toString();
                const bool         on    = (bool) args[2];
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                auto& s = copy[(size_t) idx];
                if      (name == "grain") s.fxGrain = on;
                else if (name == "space") s.fxSpace = on;
                else if (name == "delay") s.fxDelay = on;
                else if (name == "eq")    s.fxEq    = on;
                else if (name == "june")  s.fxJune  = on;
                else                      { complete ({}); return; }  // unknown name → no-op
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceFxTapeMachine", [this](const juce::Array<juce::var>& args,
                                                                  juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() < 2) { complete ({}); return; }
                const int idx = (int) args[0];
                const int m   = juce::jlimit (0, 3, (int) args[1]);
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].fxTapeMachine = m;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setSliceScanEnabled", [this](const juce::Array<juce::var>& args,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = scanEnabled (bool).
                if (args.size() < 2) { complete ({}); return; }
                const int  idx     = (int) args[0];
                const bool enabled = (bool) args[1];
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.scanEnabled = enabled; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].scanEnabled = enabled;
                const tw::WarpMode wm = copy[(size_t) idx].warpMode;
                const float sr       = copy[(size_t) idx].stretchRatio;
                audioProcessor.replaceSlices (std::move (copy));

                // If scan is being turned ON for a chop that has a warp mode,
                // schedule a background pre-render so the cache is warm by the
                // time the first note triggers scan playback.
                if (enabled && wm != tw::WarpMode::None)
                {
                    tw::WarpRenderCache::Key k;
                    k.sliceIndex      = idx;
                    k.sourceVersionId = audioProcessor.getSourceVersionId();
                    k.stretchRatio    = sr;
                    k.warpMode        = wm;
                    audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].synth.warpCache.prewarm (k);
                }
                complete ({});
            })
            .withNativeFunction("setHoldMode", [this](const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = enabled (bool). HOLD mode = voices ignore note-off
                // and play the chop to natural completion (MPC/FL latch feel).
                // Captured into VoiceConfig at startNote so in-flight voices
                // are unaffected by mid-playback toggles.
                if (args.size() >= 1)
                    audioProcessor.holdMode.store ((bool) args[0], std::memory_order_relaxed);
                complete ({});
            })
            .withNativeFunction("getHoldMode", [this](const juce::Array<juce::var>&,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // JS init pulls this on editor open so the CHOP pill label /
                // .hold-active class can be restored to match the persisted state.
                complete (juce::var ((bool) audioProcessor.holdMode.load (std::memory_order_relaxed)));
            })
            .withNativeFunction("setSliceScanRate", [this](const juce::Array<juce::var>& args,
                                                            juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sliceIndex, args[1] = scanRate (0.1..8.0 × normal speed).
                if (args.size() < 2) { complete ({}); return; }
                const int   idx  = (int) args[0];
                const float rate = juce::jlimit (0.1f, 8.0f, (float) (double) args[1]);
                if (idx == -1) { audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.scanRate = rate; complete ({}); return; }
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].scanRate = rate;
                audioProcessor.replaceSlices (std::move (copy));
                complete ({});
            })
            .withNativeFunction("setPitchSliceBounds", [this](const juce::Array<juce::var>& args,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = startSample (int64), args[1] = endSample (int64).
                // Clamps to [0, sampleLen] and enforces end > start.
                if (args.size() >= 2)
                {
                    const juce::int64 sampleLen = (juce::int64) audioProcessor.getSampleBuffer().getNumSamples();
                    if (sampleLen > 0)
                    {
                        const juce::int64 rawStart = (juce::int64)(double) args[0];
                        const juce::int64 rawEnd   = (juce::int64)(double) args[1];
                        const juce::int64 cs = juce::jlimit ((juce::int64) 0, sampleLen - 1, rawStart);
                        const juce::int64 ce = juce::jlimit (cs + 1,       sampleLen,        rawEnd);
                        audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.startSample = cs;
                        audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.endSample   = ce;
                        // Register bounds with WarpRenderCache under the pitch-mode
                        // sentinel sliceIndex=-1 so that warp+scan in pitch mode
                        // can populate cache entries. Without this, the cache
                        // worker sees bounds.end <= bounds.start and silently
                        // aborts → silent audio block forever.
                        audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].synth.warpCache.setSliceBounds (-1, (int) cs, (int) ce);
                    }
                }
                complete ({});
            })
            .withNativeFunction("deleteSlice", [this](const juce::Array<juce::var>& args,
                                                        juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // Remove slice idx, merging its sample range into the
                // previous slice (or, if first slice, into the next one).
                if (args.size() < 1) { complete ({}); return; }
                const int idx = (int) args[0];
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                if (copy.size() == 1)
                {
                    // Last slice — clear list entirely.
                    copy.clear();
                }
                else if (idx == 0)
                {
                    copy[1].startSample = copy[0].startSample;
                    copy.erase (copy.begin());
                }
                else
                {
                    copy[(size_t) (idx - 1)].endSample = copy[(size_t) idx].endSample;
                    copy.erase (copy.begin() + idx);
                }
                audioProcessor.replaceSlices (std::move (copy));
                complete (audioProcessor.getSlicesJson());
            })
            .withNativeFunction("addMarkerAt", [this](const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = sample position (int64). Splits whichever slice
                // contains that position into two; if no slice list yet,
                // creates [0..pos, pos..end].
                if (args.size() < 1) { complete ({}); return; }
                const juce::int64 pos = (juce::int64) (long long) args[0];
                auto buf = audioProcessor.getSampleBuffer().load();
                if (! buf || buf->getNumSamples() < 2) { complete ({}); return; }
                const juce::int64 total = buf->getNumSamples();
                const juce::int64 snapped = tw::findNearestZeroCrossing (*buf, pos, 384);

                auto cur = audioProcessor.loadSlices();
                tw::SliceList copy;
                if (cur && ! cur->empty())
                {
                    copy = *cur;
                    for (size_t i = 0; i < copy.size(); ++i)
                    {
                        if (snapped > copy[i].startSample && snapped < copy[i].endSample)
                        {
                            tw::Slice newSlice = copy[i];
                            newSlice.startSample = snapped;
                            copy[i].endSample    = snapped;
                            copy.insert (copy.begin() + (long) i + 1, newSlice);
                            break;
                        }
                    }
                }
                else
                {
                    if (snapped > 0 && snapped < total)
                    {
                        copy.push_back ({ 0,       snapped, false, 0.0f });
                        copy.push_back ({ snapped, total,   false, 0.0f });
                    }
                    else
                    {
                        copy.push_back ({ 0, total, false, 0.0f });
                    }
                }
                audioProcessor.replaceSlices (std::move (copy));
                complete (audioProcessor.getSlicesJson());
            })
            .withNativeFunction("moveSliceBoundary", [this](const juce::Array<juce::var>& args,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = marker index (>= 1, the boundary between slice[idx-1] and slice[idx]).
                // args[1] = new sample position (int64).
                // Snaps to nearest zero crossing within ±384 samples; clamps so neither
                // adjacent slice can shrink below 64 samples. Returns updated slices JSON.
                if (args.size() < 2) { complete ({}); return; }
                const int         idx = (int) args[0];
                const juce::int64 rawPos = (juce::int64) (long long) args[1];

                auto buf = audioProcessor.getSampleBuffer().load();
                if (! buf || buf->getNumSamples() < 2) { complete (audioProcessor.getSlicesJson()); return; }

                auto cur = audioProcessor.loadSlices();
                if (! cur || cur->empty()
                    || idx <= 0
                    || idx >= (int) cur->size()) { complete (audioProcessor.getSlicesJson()); return; }

                constexpr juce::int64 kMinSliceLen = 64;
                const juce::int64 lo = (*cur)[(size_t) (idx - 1)].startSample + kMinSliceLen;
                const juce::int64 hi = (*cur)[(size_t) idx].endSample - kMinSliceLen;
                if (hi <= lo) { complete (audioProcessor.getSlicesJson()); return; }

                const juce::int64 clamped = juce::jlimit (lo, hi, rawPos);
                const juce::int64 snapped = juce::jlimit (lo, hi, tw::findNearestZeroCrossing (*buf, clamped, 384));

                tw::SliceList copy = *cur;
                copy[(size_t) (idx - 1)].endSample = snapped;
                copy[(size_t) idx].startSample     = snapped;
                audioProcessor.replaceSlices (std::move (copy));
                complete (audioProcessor.getSlicesJson());
            })

            .withNativeFunction("getScanPosition", [this](const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                const int sliceIndex = args.size() > 0 ? (int) args[0] : -1;
                const float pos = audioProcessor.getScanPosition (sliceIndex);
                complete (juce::var ((double) pos));
            })
            .withNativeFunction("getScanWindowBounds", [this](const juce::Array<juce::var>& args,
                                                               juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                const int sliceIndex = args.size() > 0 ? (int) args[0] : -1;
                const auto b = audioProcessor.getScanWindowBounds (sliceIndex);
                auto* obj = new juce::DynamicObject();
                obj->setProperty ("start", (double) b.startNorm);
                obj->setProperty ("end",   (double) b.endNorm);
                complete (juce::var (obj));
            })
            .withNativeFunction("loadSampleForOsc", [this](const juce::Array<juce::var>& args,
                                                           juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // PEROSC — args[0]=osc letter 'a'..'d', args[1]=filename, args[2]=base64 bytes
                if (args.size() < 3) { complete (juce::var ("bad-args")); return; }
                const juce::String oscStr = args[0].toString();
                const int oscIdx = oscStr.isNotEmpty() ? juce::jlimit (0, 3, oscStr[0] - 'a') : 0;
                const auto filename = args[1].toString();
                const auto b64      = args[2].toString();

                juce::MemoryOutputStream decodedStream;
                if (! juce::Base64::convertFromBase64 (decodedStream, b64))
                {
                    complete (juce::var ("decode-failed"));
                    return;
                }

                auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("Terrain-Instrument-Drops");
                tempDir.createDirectory();

                auto safeName = juce::File::createLegalFileName (filename);
                if (safeName.isEmpty()) safeName = "osc-sample.wav";

                auto tempFile = tempDir.getNonexistentChildFile (
                                   safeName.upToLastOccurrenceOf (".", false, false),
                                   safeName.fromLastOccurrenceOf (".", true, false),
                                   true);
                tempFile.replaceWithData (decodedStream.getData(), decodedStream.getDataSize());

                if (tempFile.existsAsFile())
                {
                    loadOscSampleAsync (oscIdx, tempFile);
                    complete (juce::var ("ok"));
                }
                else
                {
                    complete (juce::var ("temp-write-failed"));
                }
            })
            .withNativeFunction("getOscSamplePayload", [this](const juce::Array<juce::var>& args,
                                                             juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // PEROSC — return the cached peaks JSON for this osc (or "").
                const juce::String oscStr = args.size() > 0 ? args[0].toString() : juce::String();
                const int oscIdx = oscStr.isNotEmpty() ? juce::jlimit (0, 3, oscStr[0] - 'a') : 0;
                complete (juce::var (audioProcessor.getCachedOscPayload (oscIdx)));
            })
            .withNativeFunction("loadSampleFromBase64", [this](const juce::Array<juce::var>& args,
                                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // args[0] = filename (string),  args[1] = base64-encoded bytes (string)
                if (args.size() < 2) { complete ({}); return; }

                const auto filename = args[0].toString();
                const auto b64      = args[1].toString();

                juce::MemoryOutputStream decodedStream;
                if (! juce::Base64::convertFromBase64 (decodedStream, b64))
                {
                    complete (juce::var ("decode-failed"));
                    return;
                }

                // Write to a stable temp file so loadSampleAsync can read it.
                auto tempDir = juce::File::getSpecialLocation (
                                  juce::File::tempDirectory)
                                  .getChildFile ("Terrain-Instrument-Drops");
                tempDir.createDirectory();

                auto safeName = juce::File::createLegalFileName (filename);
                if (safeName.isEmpty()) safeName = "dropped-sample.wav";

                auto tempFile = tempDir.getNonexistentChildFile (
                                   safeName.upToLastOccurrenceOf (".", false, false),
                                   safeName.fromLastOccurrenceOf (".", true, false),
                                   true);
                tempFile.replaceWithData (decodedStream.getData(), decodedStream.getDataSize());

                if (tempFile.existsAsFile())
                {
                    loadSampleAsync (tempFile);
                    complete (juce::var ("ok"));
                }
                else
                {
                    complete (juce::var ("temp-write-failed"));
                }
            })
            .withResourceProvider([this](const auto& url) {
                return getResource(url);
            })
    );

    addAndMakeVisible(*webView);

    // Native capture drag strip below WebView (real mouse events for drag-to-DAW)
    addAndMakeVisible(captureDragStrip);

    // Read saved theme immediately so strip paints with correct color on first frame
    // Also restore EQ panel open state from same settings file (editor-side UI state)
    {
        auto sf = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Waves Crate").getChildFile("Terrain").getChildFile("InstrumentSettings.json");
        if (sf.existsAsFile())
        {
            auto contents = sf.loadFileAsString();
            captureDragStrip.setDarkMode(contents.contains("\"dark\""));

            // Parse eqPanelOpen flag — JSON-encoded as "eqPanelOpen":true / false
            if (auto parsed = juce::JSON::parse(contents); parsed.isObject())
            {
                if (auto* obj = parsed.getDynamicObject())
                {
                    auto v = obj->getProperty("eqPanelOpen");
                    audioProcessor.eqPanelOpen.store(static_cast<bool>(v) ? 1.f : 0.f);
                }
            }
        }
    }

    // Create parameter attachments AFTER webView
    grainSizeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::GRAIN_SIZE), grainSizeRelay, nullptr);

    densityAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DENSITY), densityRelay, nullptr);

    // Batch 1 — LFO 1 rate + depth attachments (binds the mod strip knobs to APVTS).
    lfo1RateAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LFO1_RATE), lfo1RateRelay, nullptr);
    lfo1DepthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LFO1_DEPTH), lfo1DepthRelay, nullptr);
    lfo1ShapeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LFO1_SHAPE), lfo1ShapeRelay, nullptr);
    lfo1SyncAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LFO1_SYNC), lfo1SyncRelay, nullptr);
    lfo1DivAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LFO1_DIV), lfo1DivRelay, nullptr);
    // Mod redesign Stage 2 — LFOs 2..5 attachments
    {
        auto mkAtt = [this](std::unique_ptr<juce::WebSliderParameterAttachment>& att, const char* id, juce::WebSliderRelay& relay)
        { att = std::make_unique<juce::WebSliderParameterAttachment>(*audioProcessor.getAPVTS().getParameter(id), relay, nullptr); };
        // ════ SAMPLE-ENGINE-MKATT (Opus) ════
        mkAtt(synOscASampleScanAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_SCAN, synOscASampleScanRelay);
        mkAtt(synOscASampleStretchAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH, synOscASampleStretchRelay);
        mkAtt(synOscASampleFormantAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT, synOscASampleFormantRelay);
        mkAtt(synOscASampleSprayAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_SPRAY, synOscASampleSprayRelay);
        mkAtt(synOscASampleXfadeAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_XFADE, synOscASampleXfadeRelay);
        mkAtt(synOscASampleStartAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_START, synOscASampleStartRelay);
        mkAtt(synOscASampleEndAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_END, synOscASampleEndRelay);
        mkAtt(synOscASampleLoopStartAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_START, synOscASampleLoopStartRelay);
        mkAtt(synOscASampleLoopEndAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_END, synOscASampleLoopEndRelay);
        mkAtt(synOscASampleLoopModeAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE, synOscASampleLoopModeRelay);
        mkAtt(synOscASampleSnapAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_SNAP, synOscASampleSnapRelay);
        mkAtt(synOscASampleFadeInAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_FADE_IN, synOscASampleFadeInRelay);
        mkAtt(synOscASampleFadeOutAttachment, ParameterIDs::SYN_OSC_A_SAMPLE_FADE_OUT, synOscASampleFadeOutRelay);
        mkAtt(synOscBSampleScanAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_SCAN, synOscBSampleScanRelay);
        mkAtt(synOscBSampleStretchAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_STRETCH, synOscBSampleStretchRelay);
        mkAtt(synOscBSampleFormantAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_FORMANT, synOscBSampleFormantRelay);
        mkAtt(synOscBSampleSprayAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_SPRAY, synOscBSampleSprayRelay);
        mkAtt(synOscBSampleXfadeAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_XFADE, synOscBSampleXfadeRelay);
        mkAtt(synOscBSampleStartAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_START, synOscBSampleStartRelay);
        mkAtt(synOscBSampleEndAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_END, synOscBSampleEndRelay);
        mkAtt(synOscBSampleLoopStartAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_START, synOscBSampleLoopStartRelay);
        mkAtt(synOscBSampleLoopEndAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_END, synOscBSampleLoopEndRelay);
        mkAtt(synOscBSampleLoopModeAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_LOOP_MODE, synOscBSampleLoopModeRelay);
        mkAtt(synOscBSampleSnapAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_SNAP, synOscBSampleSnapRelay);
        mkAtt(synOscBSampleFadeInAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_FADE_IN, synOscBSampleFadeInRelay);
        mkAtt(synOscBSampleFadeOutAttachment, ParameterIDs::SYN_OSC_B_SAMPLE_FADE_OUT, synOscBSampleFadeOutRelay);
        mkAtt(synOscCSampleScanAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_SCAN, synOscCSampleScanRelay);
        mkAtt(synOscCSampleStretchAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_STRETCH, synOscCSampleStretchRelay);
        mkAtt(synOscCSampleFormantAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_FORMANT, synOscCSampleFormantRelay);
        mkAtt(synOscCSampleSprayAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_SPRAY, synOscCSampleSprayRelay);
        mkAtt(synOscCSampleXfadeAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_XFADE, synOscCSampleXfadeRelay);
        mkAtt(synOscCSampleStartAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_START, synOscCSampleStartRelay);
        mkAtt(synOscCSampleEndAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_END, synOscCSampleEndRelay);
        mkAtt(synOscCSampleLoopStartAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_START, synOscCSampleLoopStartRelay);
        mkAtt(synOscCSampleLoopEndAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_END, synOscCSampleLoopEndRelay);
        mkAtt(synOscCSampleLoopModeAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_LOOP_MODE, synOscCSampleLoopModeRelay);
        mkAtt(synOscCSampleSnapAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_SNAP, synOscCSampleSnapRelay);
        mkAtt(synOscCSampleFadeInAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_FADE_IN, synOscCSampleFadeInRelay);
        mkAtt(synOscCSampleFadeOutAttachment, ParameterIDs::SYN_OSC_C_SAMPLE_FADE_OUT, synOscCSampleFadeOutRelay);
        mkAtt(synOscDSampleScanAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_SCAN, synOscDSampleScanRelay);
        mkAtt(synOscDSampleStretchAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_STRETCH, synOscDSampleStretchRelay);
        mkAtt(synOscDSampleFormantAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_FORMANT, synOscDSampleFormantRelay);
        mkAtt(synOscDSampleSprayAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_SPRAY, synOscDSampleSprayRelay);
        mkAtt(synOscDSampleXfadeAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_XFADE, synOscDSampleXfadeRelay);
        mkAtt(synOscDSampleStartAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_START, synOscDSampleStartRelay);
        mkAtt(synOscDSampleEndAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_END, synOscDSampleEndRelay);
        mkAtt(synOscDSampleLoopStartAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_START, synOscDSampleLoopStartRelay);
        mkAtt(synOscDSampleLoopEndAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_END, synOscDSampleLoopEndRelay);
        mkAtt(synOscDSampleLoopModeAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_LOOP_MODE, synOscDSampleLoopModeRelay);
        mkAtt(synOscDSampleSnapAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_SNAP, synOscDSampleSnapRelay);
        mkAtt(synOscDSampleFadeInAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_FADE_IN, synOscDSampleFadeInRelay);
        mkAtt(synOscDSampleFadeOutAttachment, ParameterIDs::SYN_OSC_D_SAMPLE_FADE_OUT, synOscDSampleFadeOutRelay);
        mkAtt(lfo2RateAttachment, ParameterIDs::LFO2_RATE, lfo2RateRelay);  mkAtt(lfo2DepthAttachment, ParameterIDs::LFO2_DEPTH, lfo2DepthRelay);
        mkAtt(lfo2ShapeAttachment, ParameterIDs::LFO2_SHAPE, lfo2ShapeRelay); mkAtt(lfo2SyncAttachment, ParameterIDs::LFO2_SYNC, lfo2SyncRelay); mkAtt(lfo2DivAttachment, ParameterIDs::LFO2_DIV, lfo2DivRelay);
        mkAtt(lfo3RateAttachment, ParameterIDs::LFO3_RATE, lfo3RateRelay);  mkAtt(lfo3DepthAttachment, ParameterIDs::LFO3_DEPTH, lfo3DepthRelay);
        mkAtt(lfo3ShapeAttachment, ParameterIDs::LFO3_SHAPE, lfo3ShapeRelay); mkAtt(lfo3SyncAttachment, ParameterIDs::LFO3_SYNC, lfo3SyncRelay); mkAtt(lfo3DivAttachment, ParameterIDs::LFO3_DIV, lfo3DivRelay);
        mkAtt(lfo4RateAttachment, ParameterIDs::LFO4_RATE, lfo4RateRelay);  mkAtt(lfo4DepthAttachment, ParameterIDs::LFO4_DEPTH, lfo4DepthRelay);
        mkAtt(lfo4ShapeAttachment, ParameterIDs::LFO4_SHAPE, lfo4ShapeRelay); mkAtt(lfo4SyncAttachment, ParameterIDs::LFO4_SYNC, lfo4SyncRelay); mkAtt(lfo4DivAttachment, ParameterIDs::LFO4_DIV, lfo4DivRelay);
        mkAtt(lfo5RateAttachment, ParameterIDs::LFO5_RATE, lfo5RateRelay);  mkAtt(lfo5DepthAttachment, ParameterIDs::LFO5_DEPTH, lfo5DepthRelay);
        mkAtt(lfo5ShapeAttachment, ParameterIDs::LFO5_SHAPE, lfo5ShapeRelay); mkAtt(lfo5SyncAttachment, ParameterIDs::LFO5_SYNC, lfo5SyncRelay); mkAtt(lfo5DivAttachment, ParameterIDs::LFO5_DIV, lfo5DivRelay);
        mkAtt(lfo1PhaseAttachment, ParameterIDs::LFO1_PHASE, lfo1PhaseRelay); mkAtt(lfo2PhaseAttachment, ParameterIDs::LFO2_PHASE, lfo2PhaseRelay); mkAtt(lfo3PhaseAttachment, ParameterIDs::LFO3_PHASE, lfo3PhaseRelay);
        mkAtt(lfo4PhaseAttachment, ParameterIDs::LFO4_PHASE, lfo4PhaseRelay); mkAtt(lfo5PhaseAttachment, ParameterIDs::LFO5_PHASE, lfo5PhaseRelay);
        mkAtt(lfo6RateAttachment, ParameterIDs::LFO6_RATE, lfo6RateRelay);  mkAtt(lfo6DepthAttachment, ParameterIDs::LFO6_DEPTH, lfo6DepthRelay);  mkAtt(lfo6ShapeAttachment, ParameterIDs::LFO6_SHAPE, lfo6ShapeRelay); mkAtt(lfo6SyncAttachment, ParameterIDs::LFO6_SYNC, lfo6SyncRelay); mkAtt(lfo6DivAttachment, ParameterIDs::LFO6_DIV, lfo6DivRelay); mkAtt(lfo6PhaseAttachment, ParameterIDs::LFO6_PHASE, lfo6PhaseRelay);
        mkAtt(lfo7RateAttachment, ParameterIDs::LFO7_RATE, lfo7RateRelay);  mkAtt(lfo7DepthAttachment, ParameterIDs::LFO7_DEPTH, lfo7DepthRelay);  mkAtt(lfo7ShapeAttachment, ParameterIDs::LFO7_SHAPE, lfo7ShapeRelay); mkAtt(lfo7SyncAttachment, ParameterIDs::LFO7_SYNC, lfo7SyncRelay); mkAtt(lfo7DivAttachment, ParameterIDs::LFO7_DIV, lfo7DivRelay); mkAtt(lfo7PhaseAttachment, ParameterIDs::LFO7_PHASE, lfo7PhaseRelay);
        mkAtt(lfo8RateAttachment, ParameterIDs::LFO8_RATE, lfo8RateRelay);  mkAtt(lfo8DepthAttachment, ParameterIDs::LFO8_DEPTH, lfo8DepthRelay);  mkAtt(lfo8ShapeAttachment, ParameterIDs::LFO8_SHAPE, lfo8ShapeRelay); mkAtt(lfo8SyncAttachment, ParameterIDs::LFO8_SYNC, lfo8SyncRelay); mkAtt(lfo8DivAttachment, ParameterIDs::LFO8_DIV, lfo8DivRelay); mkAtt(lfo8PhaseAttachment, ParameterIDs::LFO8_PHASE, lfo8PhaseRelay);
        mkAtt(lfo9RateAttachment, ParameterIDs::LFO9_RATE, lfo9RateRelay);  mkAtt(lfo9DepthAttachment, ParameterIDs::LFO9_DEPTH, lfo9DepthRelay);  mkAtt(lfo9ShapeAttachment, ParameterIDs::LFO9_SHAPE, lfo9ShapeRelay); mkAtt(lfo9SyncAttachment, ParameterIDs::LFO9_SYNC, lfo9SyncRelay); mkAtt(lfo9DivAttachment, ParameterIDs::LFO9_DIV, lfo9DivRelay); mkAtt(lfo9PhaseAttachment, ParameterIDs::LFO9_PHASE, lfo9PhaseRelay);
        mkAtt(lfo10RateAttachment, ParameterIDs::LFO10_RATE, lfo10RateRelay); mkAtt(lfo10DepthAttachment, ParameterIDs::LFO10_DEPTH, lfo10DepthRelay); mkAtt(lfo10ShapeAttachment, ParameterIDs::LFO10_SHAPE, lfo10ShapeRelay); mkAtt(lfo10SyncAttachment, ParameterIDs::LFO10_SYNC, lfo10SyncRelay); mkAtt(lfo10DivAttachment, ParameterIDs::LFO10_DIV, lfo10DivRelay); mkAtt(lfo10PhaseAttachment, ParameterIDs::LFO10_PHASE, lfo10PhaseRelay);
    }

    sprayAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SPRAY), sprayRelay, nullptr);

    pitchAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::PITCH), pitchRelay, nullptr);

    wanderAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::WANDER), wanderRelay, nullptr);

    freezeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::FREEZE), freezeRelay, nullptr);

    grainFilterAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::GRAIN_FILTER), grainFilterRelay, nullptr);

    mixAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::MIX), mixRelay, nullptr);

    wowFlutterAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::WOW_FLUTTER), wowFlutterRelay, nullptr);

    saturationAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SATURATION), saturationRelay, nullptr);

    hissAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::HISS), hissRelay, nullptr);

    wireWowAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::WIRE_WOW), wireWowRelay, nullptr);
    wireSaturationAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::WIRE_SATURATION), wireSaturationRelay, nullptr);
    wireHissAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::WIRE_HISS), wireHissRelay, nullptr);

    studioSculptAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::STUDIO_SCULPT), studioSculptRelay, nullptr);

    studioWeaveAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::STUDIO_WEAVE), studioWeaveRelay, nullptr);

    studioTiltAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::STUDIO_TILT), studioTiltRelay, nullptr);

    outputGainAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::OUTPUT_GAIN), outputGainRelay, nullptr);

    masterMixAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::MASTER_MIX), masterMixRelay, nullptr);

    loopLengthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LOOP_LENGTH), loopLengthRelay, nullptr);

    loopFeedbackAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LOOP_FEEDBACK), loopFeedbackRelay, nullptr);

    loopDegradeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LOOP_DEGRADE), loopDegradeRelay, nullptr);

    loopSpeedAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::LOOP_SPEED), loopSpeedRelay, nullptr);

    spaceSizeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SPACE_SIZE), spaceSizeRelay, nullptr);

    spaceDecayAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SPACE_DECAY), spaceDecayRelay, nullptr);

    spaceToneAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SPACE_TONE), spaceToneRelay, nullptr);

    spaceMixAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SPACE_MIX), spaceMixRelay, nullptr);

    dlyTimeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_TIME), dlyTimeRelay, nullptr);

    dlyFeedbackAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_FEEDBACK), dlyFeedbackRelay, nullptr);

    dlyToneAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_TONE), dlyToneRelay, nullptr);

    dlyCharacterAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_CHARACTER), dlyCharacterRelay, nullptr);

    dlyModAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_MOD), dlyModRelay, nullptr);

    dlyModRateAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_MOD_RATE), dlyModRateRelay, nullptr);

    dlyMixAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_MIX), dlyMixRelay, nullptr);

    dlyDuckAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_DUCK), dlyDuckRelay, nullptr);

    dlyFreezeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_FREEZE), dlyFreezeRelay, nullptr);

    dlySyncAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_SYNC), dlySyncRelay, nullptr);

    dlySyncDivAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_SYNC_DIV), dlySyncDivRelay, nullptr);

    dlyModWaveAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_MOD_WAVE), dlyModWaveRelay, nullptr);

    dlyPitchAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_PITCH), dlyPitchRelay, nullptr);

    dlyWidthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::DLY_WIDTH), dlyWidthRelay, nullptr);

    chorusAmountAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::CHORUS_AMOUNT), chorusAmountRelay, nullptr);

    chorusWidthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::CHORUS_WIDTH), chorusWidthRelay, nullptr);

    chorusCharacterAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::CHORUS_CHARACTER), chorusCharacterRelay, nullptr);

    chopFadeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::CHOP_FADE_MS), chopFadeRelay, nullptr);

    // Parametric EQ — bind each of the 35 EQ APVTS params to its relay so JS
    // setNormalisedValue() actually writes through to APVTS. The order MUST
    // match the eqRelays array order in PluginEditor.h.
    {
        const char* ids[NUM_EQ_RELAYS] = {
            ParameterIDs::EQ_MASTER_BYPASS,
            ParameterIDs::EQ_HP_FREQ, ParameterIDs::EQ_HP_SLOPE, ParameterIDs::EQ_HP_BYPASS,
            ParameterIDs::EQ_LP_FREQ, ParameterIDs::EQ_LP_SLOPE, ParameterIDs::EQ_LP_BYPASS,
            ParameterIDs::EQ_B1_FREQ, ParameterIDs::EQ_B1_GAIN, ParameterIDs::EQ_B1_Q, ParameterIDs::EQ_B1_BYPASS,
            ParameterIDs::EQ_B2_FREQ, ParameterIDs::EQ_B2_GAIN, ParameterIDs::EQ_B2_Q, ParameterIDs::EQ_B2_BYPASS,
            ParameterIDs::EQ_B3_FREQ, ParameterIDs::EQ_B3_GAIN, ParameterIDs::EQ_B3_Q, ParameterIDs::EQ_B3_BYPASS,
            ParameterIDs::EQ_B4_FREQ, ParameterIDs::EQ_B4_GAIN, ParameterIDs::EQ_B4_Q, ParameterIDs::EQ_B4_BYPASS,
            ParameterIDs::EQ_B5_FREQ, ParameterIDs::EQ_B5_GAIN, ParameterIDs::EQ_B5_Q, ParameterIDs::EQ_B5_BYPASS,
            ParameterIDs::EQ_B6_FREQ, ParameterIDs::EQ_B6_GAIN, ParameterIDs::EQ_B6_Q, ParameterIDs::EQ_B6_BYPASS,
            ParameterIDs::EQ_B7_FREQ, ParameterIDs::EQ_B7_GAIN, ParameterIDs::EQ_B7_Q, ParameterIDs::EQ_B7_BYPASS,
            ParameterIDs::EQ_B1_HP_MODE, ParameterIDs::EQ_B7_LP_MODE,
        };
        for (int i = 0; i < NUM_EQ_RELAYS; ++i)
        {
            eqAttachments[i] = std::make_unique<juce::WebSliderParameterAttachment>(
                *audioProcessor.getAPVTS().getParameter(ids[i]), eqRelays[i], nullptr);
        }
    }

    synOscAEngineAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_ENGINE),
        synOscAEngineRelay, nullptr);

    synOscAOctAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_OCT),
        synOscAOctRelay, nullptr);

    synOscASemiAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_SEMI),
        synOscASemiRelay, nullptr);

    synOscACentAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_CENT),
        synOscACentRelay, nullptr);

    synOscALevelAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_LEVEL),
        synOscALevelRelay, nullptr);

    synOscAPanAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_PAN),
        synOscAPanRelay, nullptr);

    synFilter1CutAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER1_CUT),
        synFilter1CutRelay, nullptr);

    synFilter1ResAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER1_RES),
        synFilter1ResRelay, nullptr);

    // Batch 1 Filter — bind new param attachments. Without these, the JS
    // dropdown / DRV / ENV writes silently fail (relay exists but has no
    // backing param). This was the bug where every filter "sounded the same".
    synFilter1TypeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER1_TYPE),
        synFilter1TypeRelay, nullptr);
    synFilter1DrvAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER1_DRV),
        synFilter1DrvRelay, nullptr);
    synFilter1EnvAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER1_ENV),
        synFilter1EnvRelay, nullptr);
    synFilterSlotAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER_SLOT),
        synFilterSlotRelay, nullptr);
    synFilter2TypeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER2_TYPE),
        synFilter2TypeRelay, nullptr);
    synFilter2CutAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER2_CUT),
        synFilter2CutRelay, nullptr);
    synFilter2ResAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER2_RES),
        synFilter2ResRelay, nullptr);
    synFilter2DrvAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER2_DRV),
        synFilter2DrvRelay, nullptr);
    synFilter2EnvAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER2_ENV),
        synFilter2EnvRelay, nullptr);
    synFilter1MixAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER1_MIX),
        synFilter1MixRelay, nullptr);
    synFilter2MixAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER2_MIX),
        synFilter2MixRelay, nullptr);
    synFilterRoutingAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_FILTER_ROUTING),
        synFilterRoutingRelay, nullptr);
    synEnvFltAAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_A),
        synEnvFltARelay, nullptr);
    synEnvFltDAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_D),
        synEnvFltDRelay, nullptr);
    synEnvFltSAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_S),
        synEnvFltSRelay, nullptr);
    synEnvFltRAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_R),
        synEnvFltRRelay, nullptr);

    synEnvAmpAAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_A),
        synEnvAmpARelay, nullptr);

    synEnvAmpDAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_D),
        synEnvAmpDRelay, nullptr);

    synEnvAmpSAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_S),
        synEnvAmpSRelay, nullptr);

    synEnvAmpRAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_R),
        synEnvAmpRRelay, nullptr);
    synEnvAmpDlyAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_DLY),
        synEnvAmpDlyRelay, nullptr);
    synEnvAmpHAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_H),
        synEnvAmpHRelay, nullptr);
    synEnvAmpCaAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_CA),
        synEnvAmpCaRelay, nullptr);
    synEnvAmpCdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_CD),
        synEnvAmpCdRelay, nullptr);
    synEnvAmpCrAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_CR),
        synEnvAmpCrRelay, nullptr);
    synEnvAmpLoopAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_AMP_LOOP),
        synEnvAmpLoopRelay, nullptr);
    synEnvFltDlyAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_DLY),
        synEnvFltDlyRelay, nullptr);
    synEnvFltHAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_H),
        synEnvFltHRelay, nullptr);
    synEnvFltCaAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_CA),
        synEnvFltCaRelay, nullptr);
    synEnvFltCdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_CD),
        synEnvFltCdRelay, nullptr);
    synEnvFltCrAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_CR),
        synEnvFltCrRelay, nullptr);
    synEnvFltLoopAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_FLT_LOOP),
        synEnvFltLoopRelay, nullptr);
    synEnvPitDlyAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_DLY),
        synEnvPitDlyRelay, nullptr);
    synEnvPitAAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_A),
        synEnvPitARelay, nullptr);
    synEnvPitHAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_H),
        synEnvPitHRelay, nullptr);
    synEnvPitDAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_D),
        synEnvPitDRelay, nullptr);
    synEnvPitSAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_S),
        synEnvPitSRelay, nullptr);
    synEnvPitRAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_R),
        synEnvPitRRelay, nullptr);
    synEnvPitCaAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_CA),
        synEnvPitCaRelay, nullptr);
    synEnvPitCdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_CD),
        synEnvPitCdRelay, nullptr);
    synEnvPitCrAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_CR),
        synEnvPitCrRelay, nullptr);
    synEnvPitDepthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_DEPTH),
        synEnvPitDepthRelay, nullptr);
    // Per-envelope ROUTING attachments (envs 2–5: DEST choice + DEPTH float).
    synEnv2DestAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV2_DEST),  synEnv2DestRelay,  nullptr);
    synEnv2DepthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV2_DEPTH), synEnv2DepthRelay, nullptr);
    synEnv3DestAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV3_DEST),  synEnv3DestRelay,  nullptr);
    synEnv3DepthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV3_DEPTH), synEnv3DepthRelay, nullptr);
    synEnv4DestAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV4_DEST),  synEnv4DestRelay,  nullptr);
    synEnv4DepthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV4_DEPTH), synEnv4DepthRelay, nullptr);
    synEnv5DestAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV5_DEST),  synEnv5DestRelay,  nullptr);
    synEnv5DepthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV5_DEPTH), synEnv5DepthRelay, nullptr);
    synEnvPitLoopAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_PIT_LOOP),
        synEnvPitLoopRelay, nullptr);
    synEnvM1DlyAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_DLY),
        synEnvM1DlyRelay, nullptr);
    synEnvM1AAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_A),
        synEnvM1ARelay, nullptr);
    synEnvM1HAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_H),
        synEnvM1HRelay, nullptr);
    synEnvM1DAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_D),
        synEnvM1DRelay, nullptr);
    synEnvM1SAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_S),
        synEnvM1SRelay, nullptr);
    synEnvM1RAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_R),
        synEnvM1RRelay, nullptr);
    synEnvM1CaAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_CA),
        synEnvM1CaRelay, nullptr);
    synEnvM1CdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_CD),
        synEnvM1CdRelay, nullptr);
    synEnvM1CrAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_CR),
        synEnvM1CrRelay, nullptr);
    synEnvM1LoopAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M1_LOOP),
        synEnvM1LoopRelay, nullptr);
    synEnvM2DlyAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_DLY),
        synEnvM2DlyRelay, nullptr);
    synEnvM2AAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_A),
        synEnvM2ARelay, nullptr);
    synEnvM2HAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_H),
        synEnvM2HRelay, nullptr);
    synEnvM2DAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_D),
        synEnvM2DRelay, nullptr);
    synEnvM2SAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_S),
        synEnvM2SRelay, nullptr);
    synEnvM2RAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_R),
        synEnvM2RRelay, nullptr);
    synEnvM2CaAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_CA),
        synEnvM2CaRelay, nullptr);
    synEnvM2CdAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_CD),
        synEnvM2CdRelay, nullptr);
    synEnvM2CrAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_CR),
        synEnvM2CrRelay, nullptr);
    synEnvM2LoopAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_ENV_M2_LOOP),
        synEnvM2LoopRelay, nullptr);

    // Phase 2A — wavetable preset + frame
    synOscAWtPresetAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_WT_PRESET),
        synOscAWtPresetRelay, nullptr);

    synOscAWtFrameAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_WT_FRAME),
        synOscAWtFrameRelay, nullptr);

    // Phase 2C — warp mode + amount
    synOscAWarpModeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_WARP_MODE),
        synOscAWarpModeRelay, nullptr);
    synOscAWarp2ModeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_WARP2_MODE),
        synOscAWarp2ModeRelay, nullptr);
    synOscAWarp2AmtAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_WARP2_AMT),
        synOscAWarp2AmtRelay, nullptr);
    synOscBWarp2ModeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_WARP2_MODE),
        synOscBWarp2ModeRelay, nullptr);
    synOscBWarp2AmtAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_WARP2_AMT),
        synOscBWarp2AmtRelay, nullptr);
    synOscAPhaseModeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_PHASE_MODE),
        synOscAPhaseModeRelay, nullptr);
    synOscAWaverAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_WAVER),
        synOscAWaverRelay, nullptr);
    synOscAKeytrackAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_KEYTRACK),
        synOscAKeytrackRelay, nullptr);
    synOscAKeytrackDestAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_KEYTRACK_DEST),
        synOscAKeytrackDestRelay, nullptr);
    synOscARouteSrcAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_ROUTE_SRC),
        synOscARouteSrcRelay, nullptr);
    synOscARouteDestAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_ROUTE_DEST),
        synOscARouteDestRelay, nullptr);
    synOscARouteAmtAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_ROUTE_AMT),
        synOscARouteAmtRelay, nullptr);
    synOscAUnisonAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_UNISON),
        synOscAUnisonRelay, nullptr);
    synOscAUdetuneAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_UDETUNE),
        synOscAUdetuneRelay, nullptr);
    synOscAUblendAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_UBLEND),
        synOscAUblendRelay, nullptr);
    synOscAUwidthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_UWIDTH),
        synOscAUwidthRelay, nullptr);

    synOscAWarpAmountAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_WARP_AMOUNT),
        synOscAWarpAmountRelay, nullptr);

    // Phase 9 — OSC B attachments
    synOscBEngineAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_ENGINE),
        synOscBEngineRelay, nullptr);
    synOscBOctAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_OCT),
        synOscBOctRelay, nullptr);
    synOscBSemiAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_SEMI),
        synOscBSemiRelay, nullptr);
    synOscBCentAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_CENT),
        synOscBCentRelay, nullptr);
    synOscBLevelAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_LEVEL),
        synOscBLevelRelay, nullptr);
    synOscBPanAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_PAN),
        synOscBPanRelay, nullptr);
    synOscBWtPresetAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_WT_PRESET),
        synOscBWtPresetRelay, nullptr);
    synOscBWtFrameAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_WT_FRAME),
        synOscBWtFrameRelay, nullptr);
    synOscBWarpModeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_WARP_MODE),
        synOscBWarpModeRelay, nullptr);
    synOscBPhaseModeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_PHASE_MODE),
        synOscBPhaseModeRelay, nullptr);
    synOscBWaverAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_WAVER),
        synOscBWaverRelay, nullptr);
    synOscBKeytrackAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_KEYTRACK),
        synOscBKeytrackRelay, nullptr);
    synOscBKeytrackDestAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_KEYTRACK_DEST),
        synOscBKeytrackDestRelay, nullptr);
    synOscBRouteSrcAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_ROUTE_SRC),
        synOscBRouteSrcRelay, nullptr);
    synOscBRouteDestAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_ROUTE_DEST),
        synOscBRouteDestRelay, nullptr);
    synOscBRouteAmtAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_ROUTE_AMT),
        synOscBRouteAmtRelay, nullptr);
    synOscBUnisonAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_UNISON),
        synOscBUnisonRelay, nullptr);
    synOscBUdetuneAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_UDETUNE),
        synOscBUdetuneRelay, nullptr);
    synOscBUblendAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_UBLEND),
        synOscBUblendRelay, nullptr);
    synOscBUwidthAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_UWIDTH),
        synOscBUwidthRelay, nullptr);
    synOscBWarpAmountAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_WARP_AMOUNT),
        synOscBWarpAmountRelay, nullptr);

    // Phase 11a — OSC A wavetable rework attachments
    synOscASpectralTypeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_SPECTRAL_TYPE),
        synOscASpectralTypeRelay, nullptr);
    synOscASpectralAmtAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_SPECTRAL_AMT),
        synOscASpectralAmtRelay, nullptr);
    synOscAFoldShapeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_FOLD_SHAPE),
        synOscAFoldShapeRelay, nullptr);
    synOscAFoldAmtAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_FOLD_AMT),
        synOscAFoldAmtRelay, nullptr);
    synOscAFrameSpreadAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_FRAME_SPREAD),
        synOscAFrameSpreadRelay, nullptr);
    synOscAInterpModeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_A_INTERP_MODE),
        synOscAInterpModeRelay, nullptr);
    // Phase 11a — OSC B wavetable rework attachments
    synOscBSpectralTypeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_SPECTRAL_TYPE),
        synOscBSpectralTypeRelay, nullptr);
    synOscBSpectralAmtAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_SPECTRAL_AMT),
        synOscBSpectralAmtRelay, nullptr);
    synOscBFoldShapeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_FOLD_SHAPE),
        synOscBFoldShapeRelay, nullptr);
    synOscBFoldAmtAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_FOLD_AMT),
        synOscBFoldAmtRelay, nullptr);
    synOscBFrameSpreadAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_FRAME_SPREAD),
        synOscBFrameSpreadRelay, nullptr);
    synOscBInterpModeAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_OSC_B_INTERP_MODE),
        synOscBInterpModeRelay, nullptr);

    // Synth section — Phase 8a (Voice settings + flagship features)
    synVoicesAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_VOICES),
        synVoicesRelay, nullptr);
    synUnisonAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_UNISON),
        synUnisonRelay, nullptr);
    synSpreadAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_SPREAD),
        synSpreadRelay, nullptr);
    synErosionAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_EROSION),
        synErosionRelay, nullptr);
    synHorizonAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_HORIZON),
        synHorizonRelay, nullptr);
    synPortaAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_PORTA),
        synPortaRelay, nullptr);
    synGlideCurveAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_GLIDE_CURVE),
        synGlideCurveRelay, nullptr);
    synGlideAlwaysAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_GLIDE_ALWAYS),
        synGlideAlwaysRelay, nullptr);
    synGlideScaledAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_GLIDE_SCALED),
        synGlideScaledRelay, nullptr);
    synMonoAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_MONO),
        synMonoRelay, nullptr);
    synLegatoAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::SYN_LEGATO),
        synLegatoRelay, nullptr);

    // ── FLOW attachments (mode + latch + 20 per-mode knobs) ──
    {
        auto mkF = [this](std::unique_ptr<juce::WebSliderParameterAttachment>& att, const char* id, juce::WebSliderRelay& relay)
        { att = std::make_unique<juce::WebSliderParameterAttachment>(*audioProcessor.getAPVTS().getParameter(id), relay, nullptr); };
        mkF(flowModeAttachment, ParameterIDs::FLOW_MODE, flowModeRelay);
        mkF(flowArpLatchAttachment, ParameterIDs::FLOW_ARP_LATCH, flowArpLatchRelay);
        mkF(flowArpRateAttachment, ParameterIDs::FLOW_ARP_RATE, flowArpRateRelay); mkF(flowArpGateAttachment, ParameterIDs::FLOW_ARP_GATE, flowArpGateRelay); mkF(flowArpVaryAttachment, ParameterIDs::FLOW_ARP_VARY, flowArpVaryRelay); mkF(flowArpTrajAttachment, ParameterIDs::FLOW_ARP_TRAJ, flowArpTrajRelay); mkF(flowArpMorphAttachment, ParameterIDs::FLOW_ARP_MORPH, flowArpMorphRelay);
        mkF(flowSeqRateAttachment, ParameterIDs::FLOW_SEQ_RATE, flowSeqRateRelay); mkF(flowSeqGateAttachment, ParameterIDs::FLOW_SEQ_GATE, flowSeqGateRelay); mkF(flowSeqVaryAttachment, ParameterIDs::FLOW_SEQ_VARY, flowSeqVaryRelay); mkF(flowSeqTrajAttachment, ParameterIDs::FLOW_SEQ_TRAJ, flowSeqTrajRelay); mkF(flowSeqMorphAttachment, ParameterIDs::FLOW_SEQ_MORPH, flowSeqMorphRelay);
        mkF(flowChopBlendAttachment, ParameterIDs::FLOW_CHOP_BLEND, flowChopBlendRelay);
        mkF(flowGliBlendAttachment, ParameterIDs::FLOW_GLI_BLEND, flowGliBlendRelay);
        mkF(flowArpBlendAttachment, ParameterIDs::FLOW_ARP_BLEND, flowArpBlendRelay);
        mkF(flowGliRateAttachment, ParameterIDs::FLOW_GLI_RATE, flowGliRateRelay); mkF(flowGliGateAttachment, ParameterIDs::FLOW_GLI_GATE, flowGliGateRelay); mkF(flowGliVaryAttachment, ParameterIDs::FLOW_GLI_VARY, flowGliVaryRelay); mkF(flowGliTrajAttachment, ParameterIDs::FLOW_GLI_TRAJ, flowGliTrajRelay); mkF(flowGliMorphAttachment, ParameterIDs::FLOW_GLI_MORPH, flowGliMorphRelay);
        mkF(flowDrfRateAttachment, ParameterIDs::FLOW_DRF_RATE, flowDrfRateRelay); mkF(flowDrfGateAttachment, ParameterIDs::FLOW_DRF_GATE, flowDrfGateRelay); mkF(flowDrfVaryAttachment, ParameterIDs::FLOW_DRF_VARY, flowDrfVaryRelay); mkF(flowDrfTrajAttachment, ParameterIDs::FLOW_DRF_TRAJ, flowDrfTrajRelay); mkF(flowDrfMorphAttachment, ParameterIDs::FLOW_DRF_MORPH, flowDrfMorphRelay);
    }

    // ── OSC C + D attachments (4-osc) ──
    {
        auto mkO = [this](std::unique_ptr<juce::WebSliderParameterAttachment>& att, const char* id, juce::WebSliderRelay& relay)
        { att = std::make_unique<juce::WebSliderParameterAttachment>(*audioProcessor.getAPVTS().getParameter(id), relay, nullptr); };
        mkO(synOscCEngineAttachment, ParameterIDs::SYN_OSC_C_ENGINE, synOscCEngineRelay);
        mkO(synOscCOctAttachment, ParameterIDs::SYN_OSC_C_OCT, synOscCOctRelay);
        mkO(synOscCSemiAttachment, ParameterIDs::SYN_OSC_C_SEMI, synOscCSemiRelay);
        mkO(synOscCCentAttachment, ParameterIDs::SYN_OSC_C_CENT, synOscCCentRelay);
        mkO(synOscCLevelAttachment, ParameterIDs::SYN_OSC_C_LEVEL, synOscCLevelRelay);
        mkO(synOscCPanAttachment, ParameterIDs::SYN_OSC_C_PAN, synOscCPanRelay);
        mkO(synOscCWtPresetAttachment, ParameterIDs::SYN_OSC_C_WT_PRESET, synOscCWtPresetRelay);
        mkO(synOscCWtFrameAttachment, ParameterIDs::SYN_OSC_C_WT_FRAME, synOscCWtFrameRelay);
        mkO(synOscCWarpModeAttachment, ParameterIDs::SYN_OSC_C_WARP_MODE, synOscCWarpModeRelay);
        mkO(synOscCWarpAmountAttachment, ParameterIDs::SYN_OSC_C_WARP_AMOUNT, synOscCWarpAmountRelay);
        mkO(synOscCWarp2ModeAttachment, ParameterIDs::SYN_OSC_C_WARP2_MODE, synOscCWarp2ModeRelay);
        mkO(synOscCWarp2AmtAttachment, ParameterIDs::SYN_OSC_C_WARP2_AMT, synOscCWarp2AmtRelay);
        mkO(synOscCPhaseModeAttachment, ParameterIDs::SYN_OSC_C_PHASE_MODE, synOscCPhaseModeRelay);
        mkO(synOscCWaverAttachment, ParameterIDs::SYN_OSC_C_WAVER, synOscCWaverRelay);
        mkO(synOscCKeytrackAttachment, ParameterIDs::SYN_OSC_C_KEYTRACK, synOscCKeytrackRelay);
        mkO(synOscCKeytrackDestAttachment, ParameterIDs::SYN_OSC_C_KEYTRACK_DEST, synOscCKeytrackDestRelay);
        mkO(synOscCRouteSrcAttachment, ParameterIDs::SYN_OSC_C_ROUTE_SRC, synOscCRouteSrcRelay);
        mkO(synOscCRouteDestAttachment, ParameterIDs::SYN_OSC_C_ROUTE_DEST, synOscCRouteDestRelay);
        mkO(synOscCRouteAmtAttachment, ParameterIDs::SYN_OSC_C_ROUTE_AMT, synOscCRouteAmtRelay);
        mkO(synOscCUnisonAttachment, ParameterIDs::SYN_OSC_C_UNISON, synOscCUnisonRelay);
        mkO(synOscCUdetuneAttachment, ParameterIDs::SYN_OSC_C_UDETUNE, synOscCUdetuneRelay);
        mkO(synOscCUblendAttachment, ParameterIDs::SYN_OSC_C_UBLEND, synOscCUblendRelay);
        mkO(synOscCUwidthAttachment, ParameterIDs::SYN_OSC_C_UWIDTH, synOscCUwidthRelay);
        mkO(synOscCSpectralTypeAttachment, ParameterIDs::SYN_OSC_C_SPECTRAL_TYPE, synOscCSpectralTypeRelay);
        mkO(synOscCSpectralAmtAttachment, ParameterIDs::SYN_OSC_C_SPECTRAL_AMT, synOscCSpectralAmtRelay);
        mkO(synOscCFoldShapeAttachment, ParameterIDs::SYN_OSC_C_FOLD_SHAPE, synOscCFoldShapeRelay);
        mkO(synOscCFoldAmtAttachment, ParameterIDs::SYN_OSC_C_FOLD_AMT, synOscCFoldAmtRelay);
        mkO(synOscCFrameSpreadAttachment, ParameterIDs::SYN_OSC_C_FRAME_SPREAD, synOscCFrameSpreadRelay);
        mkO(synOscCInterpModeAttachment, ParameterIDs::SYN_OSC_C_INTERP_MODE, synOscCInterpModeRelay);
        mkO(synOscDEngineAttachment, ParameterIDs::SYN_OSC_D_ENGINE, synOscDEngineRelay);
        mkO(synOscDOctAttachment, ParameterIDs::SYN_OSC_D_OCT, synOscDOctRelay);
        mkO(synOscDSemiAttachment, ParameterIDs::SYN_OSC_D_SEMI, synOscDSemiRelay);
        mkO(synOscDCentAttachment, ParameterIDs::SYN_OSC_D_CENT, synOscDCentRelay);
        mkO(synOscDLevelAttachment, ParameterIDs::SYN_OSC_D_LEVEL, synOscDLevelRelay);
        mkO(synOscDPanAttachment, ParameterIDs::SYN_OSC_D_PAN, synOscDPanRelay);
        mkO(synOscDWtPresetAttachment, ParameterIDs::SYN_OSC_D_WT_PRESET, synOscDWtPresetRelay);
        mkO(synOscDWtFrameAttachment, ParameterIDs::SYN_OSC_D_WT_FRAME, synOscDWtFrameRelay);
        mkO(synOscDWarpModeAttachment, ParameterIDs::SYN_OSC_D_WARP_MODE, synOscDWarpModeRelay);
        mkO(synOscDWarpAmountAttachment, ParameterIDs::SYN_OSC_D_WARP_AMOUNT, synOscDWarpAmountRelay);
        mkO(synOscDWarp2ModeAttachment, ParameterIDs::SYN_OSC_D_WARP2_MODE, synOscDWarp2ModeRelay);
        mkO(synOscDWarp2AmtAttachment, ParameterIDs::SYN_OSC_D_WARP2_AMT, synOscDWarp2AmtRelay);
        mkO(synOscDPhaseModeAttachment, ParameterIDs::SYN_OSC_D_PHASE_MODE, synOscDPhaseModeRelay);
        mkO(synOscDWaverAttachment, ParameterIDs::SYN_OSC_D_WAVER, synOscDWaverRelay);
        mkO(synOscDKeytrackAttachment, ParameterIDs::SYN_OSC_D_KEYTRACK, synOscDKeytrackRelay);
        mkO(synOscDKeytrackDestAttachment, ParameterIDs::SYN_OSC_D_KEYTRACK_DEST, synOscDKeytrackDestRelay);
        mkO(synOscDRouteSrcAttachment, ParameterIDs::SYN_OSC_D_ROUTE_SRC, synOscDRouteSrcRelay);
        mkO(synOscDRouteDestAttachment, ParameterIDs::SYN_OSC_D_ROUTE_DEST, synOscDRouteDestRelay);
        mkO(synOscDRouteAmtAttachment, ParameterIDs::SYN_OSC_D_ROUTE_AMT, synOscDRouteAmtRelay);
        mkO(synOscDUnisonAttachment, ParameterIDs::SYN_OSC_D_UNISON, synOscDUnisonRelay);
        mkO(synOscDUdetuneAttachment, ParameterIDs::SYN_OSC_D_UDETUNE, synOscDUdetuneRelay);
        mkO(synOscDUblendAttachment, ParameterIDs::SYN_OSC_D_UBLEND, synOscDUblendRelay);
        mkO(synOscDUwidthAttachment, ParameterIDs::SYN_OSC_D_UWIDTH, synOscDUwidthRelay);
        mkO(synOscDSpectralTypeAttachment, ParameterIDs::SYN_OSC_D_SPECTRAL_TYPE, synOscDSpectralTypeRelay);
        mkO(synOscDSpectralAmtAttachment, ParameterIDs::SYN_OSC_D_SPECTRAL_AMT, synOscDSpectralAmtRelay);
        mkO(synOscDFoldShapeAttachment, ParameterIDs::SYN_OSC_D_FOLD_SHAPE, synOscDFoldShapeRelay);
        mkO(synOscDFoldAmtAttachment, ParameterIDs::SYN_OSC_D_FOLD_AMT, synOscDFoldAmtRelay);
        mkO(synOscDFrameSpreadAttachment, ParameterIDs::SYN_OSC_D_FRAME_SPREAD, synOscDFrameSpreadRelay);
        mkO(synOscDInterpModeAttachment, ParameterIDs::SYN_OSC_D_INTERP_MODE, synOscDInterpModeRelay);
    }

    // Load embedded web content
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    // Set size AFTER webView is created (setSize triggers resized())
    setSize (820, 640 + CAPTURE_STRIP_HEIGHT);

    // Start visualization timer at 60Hz for smooth LFO/mod display
    startTimerHz(60);

    // Auto-reload the previously-loaded sample(s).
    //
    // Case 1 — cache hit (editor close+reopen, same processor instance):
    //   JS pulls the cached payload via getCachedSamplePayload and restores the
    //   waveform for the *currently editing* layer instantly. No decode needed —
    //   the audio buffers in all 4 layers were never released.
    //
    // Case 2 — cache miss + path set (DAW project reload, fresh processor):
    //   Audio buffers are empty; we must re-decode from disk.  For V1 presets
    //   only layers[0] has a path; for V2 presets any of the 4 layers may have
    //   one.  We iterate all 4 and fire an async decode for every non-empty path
    //   that does not already have a loaded buffer.  loadSampleIntoLayer targets
    //   the correct layer index without touching editingLayer.
    juce::Component::SafePointer<TerrainInstrumentAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr) return;

        // Case 1: cache hit for the currently-editing layer → JS handles display.
        // We still need to reload audio buffers for the other layers, so don't
        // bail out entirely — fall through and check all 4 layers below.
        // (The cache only holds the waveform display payload for ONE layer at a
        // time; audio buffers for all layers are either live or need reloading.)

        for (int li = 0; li < 4; ++li)
        {
            auto& L = safeThis->audioProcessor.layers[(size_t) li];

            // If the audio buffer is already populated, nothing to do for this layer.
            if (L.hasSample()) continue;

            const juce::String path = L.sourcePath;
            if (path.isEmpty()) continue;

            const juce::File f (path);
            if (! f.existsAsFile()) continue;

            // Fire the per-layer async decode.
            safeThis->loadSampleIntoLayer (f, li);
        }

        // PEROSC-RELOAD — reload each oscillator's saved sample from disk (DAW project reload).
        // In-session reopen is already covered by the persistent buffer + cached payload; this
        // path handles a fresh processor whose oscSampleBuffers_ are empty but paths were restored.
        for (int oi = 0; oi < 4; ++oi)
        {
            if (safeThis->audioProcessor.getOscSampleBuffer (oi).getNumSamples() > 0) continue;
            const juce::String op = safeThis->audioProcessor.oscSourcePath (oi);
            if (op.isEmpty()) continue;
            const juce::File of (op);
            if (! of.existsAsFile()) continue;
            safeThis->loadOscSampleAsync (oi, of);
        }
    });
}

TerrainInstrumentAudioProcessorEditor::~TerrainInstrumentAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void TerrainInstrumentAudioProcessorEditor::timerCallback()
{
    if (webView == nullptr) return;

    // Read grain count
    int grainCount = audioProcessor.activeGrainCount.load();

    // Read scope buffer
    juce::String scopeData;
    scopeData.preallocateBytes(2048);
    scopeData << "[";
    for (int i = 0; i < TerrainInstrumentAudioProcessor::SCOPE_SIZE; ++i)
    {
        if (i > 0) scopeData << ",";
        float val = audioProcessor.scopeBuffer[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        scopeData << juce::String(val, 4);
    }
    scopeData << "]";

    // Read BPM for grain sync display
    float bpm = audioProcessor.currentBPM.load();

    // Read tape loop state
    float tapeLoopRec  = audioProcessor.tapeLoopRecording.load();
    float tapeLoopPlay = audioProcessor.tapeLoopPlaying.load();
    float tapeLoopProg = audioProcessor.getTapeLoopProgress();
    bool  tapeLoopHas  = audioProcessor.getTapeLoopHasContent();
    bool  tapeLoopUndo = audioProcessor.getTapeLoopHasUndo();
    int   countInBeat  = audioProcessor.getTapeLoopCountInBeat();
    bool  feedToGrain  = audioProcessor.tapeLoopFeedToGrain.load() > 0.5f;

    // Push visualization data to JS
    juce::String js;
    js << "if(window.updateVisualization){"
       << "window.updateVisualization(" << grainCount << "," << scopeData << "," << juce::String(bpm, 1) << ");}";
    js << "if(window.updateTapeLoopState){"
       << "window.updateTapeLoopState("
       << (tapeLoopRec > 0.5f ? "true" : "false") << ","
       << (tapeLoopPlay > 0.5f ? "true" : "false") << ","
       << (tapeLoopHas ? "true" : "false") << ","
       << juce::String(tapeLoopProg, 4) << ","
       << (tapeLoopUndo ? "true" : "false") << ","
       << countInBeat << ");}";
    js << "if(window.updateFeedState){"
       << "window.updateFeedState(" << (feedToGrain ? "true" : "false") << ");}";
    int captureState = audioProcessor.captureExportState.load();
    float captureAvail = audioProcessor.getCaptureAvailableSeconds();
    js << "if(window.updateCaptureState){"
       << "window.updateCaptureState("
       << captureState << ","
       << juce::String(captureAvail, 1) << ");}";

    // Update native drag strip state
    captureDragStrip.updateState(captureState, captureAvail);

    // Push LFO outputs from C++ engine to JS for visualization (mod rings, waveform preview)
    {
        float lfo0 = audioProcessor.modulationEngine.lfoOutputsAtomic[0].load(std::memory_order_relaxed);
        float lfo1 = audioProcessor.modulationEngine.lfoOutputsAtomic[1].load(std::memory_order_relaxed);
        float lfo2 = audioProcessor.modulationEngine.lfoOutputsAtomic[2].load(std::memory_order_relaxed);
        float p0 = audioProcessor.modulationEngine.lfoPhasesAtomic[0].load(std::memory_order_relaxed);
        float p1 = audioProcessor.modulationEngine.lfoPhasesAtomic[1].load(std::memory_order_relaxed);
        float p2 = audioProcessor.modulationEngine.lfoPhasesAtomic[2].load(std::memory_order_relaxed);
        js << "if(window.updateLFOOutputs){window.updateLFOOutputs("
           << juce::String(lfo0, 4) << "," << juce::String(lfo1, 4) << "," << juce::String(lfo2, 4) << ","
           << juce::String(p0, 4) << "," << juce::String(p1, 4) << "," << juce::String(p2, 4) << ");}";
    }

    // ── Envelope follower (playhead dot) ──
    // Push the most-active voice's live AMP-env level to the WebUI. -1 = no voice
    // sounding (JS hides/parks the dot). The graph reads the value back onto the
    // exact same bias curve it already draws, so the dot rides the visible line.
    {
        float envFollow = audioProcessor.ampEnvVis.load(std::memory_order_relaxed);
        float envStage  = audioProcessor.ampEnvFollowVis.load(std::memory_order_relaxed);
        js << "if(window.updateEnvFollower){window.updateEnvFollower("
           << juce::String(envFollow, 4) << "," << juce::String(envStage, 4) << ");}";
    }

    // ── Synth LFO 1 live value (Batch 1) — drives the modulation strip's scope/dot. ──
    {
        float lfo1 = audioProcessor.synthLfo1Vis.load(std::memory_order_relaxed);
        js << "if(window.updateSynthLFO){window.updateSynthLFO(" << juce::String(lfo1, 4) << ");}";
    }

    // ── Mod state lifecycle ──
    // Before pageReady: RESTORE — push saved state every tick (JS may not be ready yet)
    // After pageReady:  SAVE    — pull serialized state from JS every 5 ticks (~83ms)
    // CRITICAL: never save before pageReady or we'd overwrite saved data with empty/default state
    modStateTickCount++;

    if (!pageReady)
    {
        // Push mod state JSON restore (repeat every tick until page signals ready)
        if (audioProcessor.modStateJson.isNotEmpty())
        {
            auto escaped = audioProcessor.modStateJson.replace("\\", "\\\\").replace("'", "\\'");
            js << "if(typeof restoreModState==='function'){restoreModState('" << escaped << "');}";
        }

        // Push grain sync, XY pad, pitch locked, and wire mode state
        float grainSync  = audioProcessor.grainSyncEnabled.load();
        float xyEnabled  = audioProcessor.xyAutoEnabled.load();
        float xyMode     = audioProcessor.xyAutoMode.load();
        float xySpeed    = audioProcessor.xyAutoSpeed.load();
        float pitchLock  = audioProcessor.pitchLocked.load();
        float wireSpace  = audioProcessor.wireSpaceNoiseEnabled.load();
        float wireTube   = audioProcessor.wireTubeSatEnabled.load();
        js << "if(typeof restoreUIState==='function'){restoreUIState("
           << juce::String(grainSync, 1) << ","
           << juce::String(xyEnabled, 1) << ","
           << juce::String(xyMode, 1) << ","
           << juce::String(xySpeed, 3) << ","
           << juce::String(pitchLock, 1) << ","
           << juce::String(wireSpace, 1) << ","
           << juce::String(wireTube, 1) << ");}";

        // Push plugin settings (theme) during restore window
        auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Waves Crate").getChildFile("Terrain").getChildFile("InstrumentSettings.json");
        if (settingsFile.existsAsFile())
        {
            auto sJson = settingsFile.loadFileAsString().replace("\\", "\\\\").replace("'", "\\'");
            js << "if(typeof restoreSettings==='function'){restoreSettings('" << sJson << "');}";
            captureDragStrip.setDarkMode(sJson.contains("\"dark\""));
        }
    }

    webView->evaluateJavascript(js);

    // Only save AFTER the page has signaled ready (prevents overwriting stored state with defaults)
    if (pageReady && (modStateTickCount % 5 == 0))
    {
        webView->evaluateJavascript(
            "typeof serializeModState==='function'?serializeModState():''",
            [this](juce::WebBrowserComponent::EvaluationResult result)
            {
                if (auto* val = result.getResult())
                {
                    auto json = val->toString();
                    if (json.isNotEmpty())
                    {
                        // Defensive guard: if the polled JSON has zero assignments
                        // but our currently-saved JSON has assignments, refuse to
                        // overwrite. This protects against a race where the SAVE
                        // poll fires BEFORE JS init finishes restoring modState
                        // from a DAW project reload — without it, that race
                        // wipes the restored modulation. JS init also calls
                        // persistModState after restoreModState as a primary
                        // mitigation; this guard is belt-and-suspenders.
                        const bool incomingEmpty
                            = ! json.contains ("\"target\"");
                        const bool currentHasAssignments
                            = audioProcessor.modStateJson.contains ("\"target\"");
                        if (incomingEmpty && currentHasAssignments)
                            return;

                        audioProcessor.modStateJson = json;
                        audioProcessor.modulationEngine.updateConfig(
                            ModulationEngine::parseJSON(json));
                    }
                }
            });
    }

    // Push pre/post EQ analyzer bins to WebView every 60 Hz tick. Combined with
    // SpectrumAnalyzer's 75% FFT overlap (~47 fresh frames/s @ 48 kHz), this
    // gives a smooth 60 Hz visual on the EQ canvas.
    {
        const float* preBins  = audioProcessor.analyzerPre.readLatest();
        const float* postBins = audioProcessor.analyzerPost.readLatest();
        if (preBins != nullptr && postBins != nullptr && webView != nullptr)
        {
            // Build a JS call: window.__terrainEqAnalyzer({pre:[...], post:[...]});
            // ~80 KB string at 60 Hz. Modern WebView handles it.
            juce::String s = "window.__terrainEqAnalyzer && window.__terrainEqAnalyzer({pre:[";
            for (int i = 0; i < SpectrumAnalyzer::NUM_BINS; ++i)
            {
                if (i > 0) s += ",";
                s += juce::String (preBins[i], 6);
            }
            s += "],post:[";
            for (int i = 0; i < SpectrumAnalyzer::NUM_BINS; ++i)
            {
                if (i > 0) s += ",";
                s += juce::String (postBins[i], 6);
            }
            s += "]});";
            webView->evaluateJavascript (s, nullptr);
        }
    }
}

//==============================================================================
void TerrainInstrumentAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void TerrainInstrumentAudioProcessorEditor::resized()
{
    auto b = getLocalBounds();
    captureDragStrip.setBounds(b.removeFromBottom(CAPTURE_STRIP_HEIGHT));
    if (webView != nullptr)
        webView->setBounds(b);
}

//==============================================================================
// CaptureDragStrip implementation
//==============================================================================
void TerrainInstrumentAudioProcessorEditor::CaptureDragStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // synthViewActive forces the synth panel's own dark bg (#1A1A2E) so the strip is
    // seamless under the synth view regardless of the global light/dark theme.
    const bool dark = isDarkMode || synthViewActive;
    g.fillAll(synthViewActive ? juce::Colour(0xFF1A1A2E)
                              : (isDarkMode ? juce::Colour(0xFF232340) : juce::Colour(0xFFE8E4EF)));

    if (state == 2) // ready — green, drag to DAW
    {
        g.setColour(juce::Colour(0xFF059669));
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText(juce::String::fromUTF8("DRAG TO DAW \u2193"), b, juce::Justification::centred);
    }
    else if (state == 1) // exporting
    {
        g.setColour(juce::Colour(0xFF92400E));
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText("SAVING...", b, juce::Justification::centred);
    }
    else // idle
    {
        int mins = static_cast<int>(avail) / 60;
        int secs = static_cast<int>(avail) % 60;
        if (avail < 1.0f)
        {
            g.setColour(dark ? juce::Colour(0x44606080) : juce::Colour(0x44857399));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("CAPTURE: LISTENING...", b, juce::Justification::centred);
        }
        else
        {
            g.setColour(dark ? juce::Colour(0xFF9B93B0) : juce::Colour(0xFF6B5B7B));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("CAPTURE: " + juce::String(mins) + "m " + juce::String(secs) + "s  \u2014  DRAG TO DAW",
                       b, juce::Justification::centred);
        }
    }
}

void TerrainInstrumentAudioProcessorEditor::CaptureDragStrip::mouseDown (const juce::MouseEvent&)
{
    mouseWasDown = true;
    isDragging = false;

    // Click when idle: instantly export ALL available capture
    if (state == 0 && avail >= 1.0f)
    {
        int durSeconds = static_cast<int>(avail);
        processor.exportCapture(durSeconds);
    }
}

void TerrainInstrumentAudioProcessorEditor::CaptureDragStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (!mouseWasDown || isDragging) return;
    if (state != 2) return; // only drag when ready

    if (e.getDistanceFromDragStart() < 4) return;

    isDragging = true;

    auto filePath = processor.getLastCaptureFilePath();
    if (filePath.isEmpty()) return;

    juce::File file(filePath);
    if (!file.existsAsFile()) return;

    juce::DragAndDropContainer::performExternalDragDropOfFiles(
        { filePath }, false, nullptr, [this]()
        {
            juce::MessageManager::callAsync([this]()
            {
                processor.captureExportState.store(0);
            });
        }
    );
}

void TerrainInstrumentAudioProcessorEditor::CaptureDragStrip::mouseUp (const juce::MouseEvent&)
{
    if (mouseWasDown && !isDragging && state == 2)
    {
        // Single click on ready strip — reset to idle
        processor.captureExportState.store(0);
    }
    mouseWasDown = false;
    isDragging = false;
}

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource> TerrainInstrumentAudioProcessorEditor::getResource (const juce::String& url)
{
    // All JS is inlined into index.html — only one resource to serve
    juce::String html (juce::CharPointer_UTF8 (reinterpret_cast<const char*>(BinaryData::index_html)),
                       static_cast<size_t>(BinaryData::index_htmlSize));

    // Inject saved theme into HTML so the page loads with the correct theme from frame one
    auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("Waves Crate").getChildFile("Terrain").getChildFile("InstrumentSettings.json");
    if (settingsFile.existsAsFile() && settingsFile.loadFileAsString().contains("\"dark\""))
        html = html.replace("<html lang=\"en\">", "<html lang=\"en\" data-theme=\"dark\">");

    // Inject JS-side file drag-drop bridge (Phase C — Task 11). WKWebView
    // intercepts native file drops at the OS level, so we have to handle them
    // in JS and call back into C++. Two paths:
    //   1) FAST: file.path (some WKWebView contexts expose it). Works for
    //      arbitrarily large files since C++ reads the file directly.
    //   2) FALLBACK: read file as base64 in JS, send to C++. Works everywhere
    //      but slower for big files. Acceptable up to ~100MB.
    //
    // Also injects a temporary status overlay so the user gets visible feedback
    // (Phase D will replace this with the proper hero canvas drag-hover + loading
    // states; this is a Phase C diagnostic affordance).
    const juce::String dropBridge = R"(
<style>
  .ti-drop-status {
    position: fixed; left: 50%; bottom: 16px;
    transform: translateX(-50%);
    background: rgba(35,35,64,0.95);
    border: 1px solid #A78BFA;
    color: #F5F3FF;
    padding: 10px 18px; border-radius: 8px;
    font: 500 11px/1.4 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.02em;
    opacity: 0; transition: opacity 200ms ease;
    z-index: 99999; pointer-events: none;
    max-width: 90%;
    text-align: left;
    word-break: break-all;
    white-space: pre-wrap;
  }
  .ti-drop-status.visible { opacity: 1; }
</style>
<script>
(function(){
  function ensureStatus () {
    var el = document.getElementById('ti-drop-status');
    if (!el) {
      el = document.createElement('div');
      el.id = 'ti-drop-status';
      el.className = 'ti-drop-status';
      document.body.appendChild(el);
    }
    return el;
  }
  function showStatus (msg, holdMs) {
    var el = ensureStatus();
    el.textContent = msg;
    el.classList.add('visible');
    clearTimeout(el._hideTimer);
    el._hideTimer = setTimeout(function(){ el.classList.remove('visible'); }, holdMs || 3000);
  }
  function getNativeFn (name) {
    // V1 page exposes the helper as window.Juce (capital J) — see line ~3945
    // of the served index.html.  Lowercase window.juce is the JUCE 8 helper
    // module pattern; not used here. Try both capitalizations defensively.
    var bridge = window.Juce || window.juce;
    if (bridge && typeof bridge.getNativeFunction === 'function') {
      try { return bridge.getNativeFunction(name); } catch (_) {}
    }
    if (window.__JUCE__ && window.__JUCE__.backend && typeof window.__JUCE__.backend.invokeMethod === 'function') {
      return function () {
        var argsArr = Array.prototype.slice.call(arguments);
        return window.__JUCE__.backend.invokeMethod(name, argsArr);
      };
    }
    return null;
  }

  function readAsBase64 (file) {
    return new Promise(function (resolve, reject) {
      var reader = new FileReader();
      reader.onload = function () {
        var dataUrl = reader.result || '';
        var commaIdx = dataUrl.indexOf(',');
        resolve(commaIdx >= 0 ? dataUrl.substring(commaIdx + 1) : '');
      };
      reader.onerror = function () { reject(reader.error); };
      reader.readAsDataURL(file);
    });
  }

  document.addEventListener('dragover', function(e){
    e.preventDefault();
    if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy';
    if (window.onDragHover) window.onDragHover(true);
  }, true);

  document.addEventListener('dragleave', function(e){
    if (e.relatedTarget === null && window.onDragHover) window.onDragHover(false);
  }, true);

  document.addEventListener('drop', function(e){
    e.preventDefault();
    if (window.onDragHover) window.onDragHover(false);

    var f = (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]) ? e.dataTransfer.files[0] : null;
    if (!f) { showStatus('Drop: no file payload', 4000); return; }

    showStatus('Loading "' + f.name + '" (' + Math.round(f.size / 1024) + ' KB)…', 8000);

    // Path 1: try fast file.path (most WKWebView contexts strip this for security)
    var path = f.path || f.fullPath || '';
    if (path) {
      var fnPath = getNativeFn('loadSampleFromPath');
      if (fnPath) {
        fnPath(path);
        showStatus('Loaded via path: ' + f.name, 4000);
        return;
      }
    }

    // Path 2: bytes fallback. Hard cap matches the 10-min RAM cap (≈230MB
    // stereo float32 at 48kHz = ~115MB compressed WAV). Bigger files fail
    // here rather than waste time decoding.
    var maxBytes = 256 * 1024 * 1024;
    if (f.size > maxBytes) {
      showStatus('File too large (' + Math.round(f.size / 1024 / 1024) + ' MB > 256 MB)', 6000);
      return;
    }

    var fnB64 = getNativeFn('loadSampleFromBase64');
    if (!fnB64) {
      // Diagnostic dump — figure out which bridge paths exist.
      var diag = [];
      diag.push('juce=' + (typeof window.juce));
      if (window.juce) {
        diag.push('juce.keys=[' + Object.keys(window.juce).join(',') + ']');
        diag.push('juce.getNativeFunction=' + typeof window.juce.getNativeFunction);
      }
      diag.push('__JUCE__=' + (typeof window.__JUCE__));
      if (window.__JUCE__) {
        diag.push('__JUCE__.keys=[' + Object.keys(window.__JUCE__).join(',') + ']');
        if (window.__JUCE__.backend) {
          diag.push('backend.keys=[' + Object.keys(window.__JUCE__.backend).join(',') + ']');
        }
      }
      // Try a direct lookup via getNativeFunction with logging
      if (window.juce && typeof window.juce.getNativeFunction === 'function') {
        try {
          var rawResult = window.juce.getNativeFunction('loadSampleFromBase64');
          diag.push('rawLookup=' + typeof rawResult);
        } catch (err) {
          diag.push('rawLookupErr=' + (err && err.message));
        }
      }
      // Also try loadPreset to confirm the bridge generally works
      if (window.juce && typeof window.juce.getNativeFunction === 'function') {
        try {
          var presetFn = window.juce.getNativeFunction('loadPreset');
          diag.push('loadPresetLookup=' + typeof presetFn);
        } catch (err) {
          diag.push('loadPresetErr=' + (err && err.message));
        }
      }
      showStatus(diag.join(' | '), 30000);
      console.log('Terrain bridge diag:', diag.join(' | '));
      return;
    }

    readAsBase64(f).then(function (b64) {
      if (!b64) { showStatus('Could not encode file bytes', 6000); return; }
      fnB64(f.name, b64);
      showStatus('Loaded via bytes: ' + f.name, 4000);
    }).catch(function (err) {
      showStatus('Read error: ' + (err && err.message ? err.message : 'unknown'), 6000);
    });
  }, true);

  // ─── Click-to-load: opens native file picker when the empty hero is clicked.
  // Reuses the same byte-bridge pipeline as drag-drop. Fires on left-click
  // OR right-click of the empty hero area only (ignored once a sample is loaded
  // so the right-click lab-card menu still works on chops).
  var pickerInput = document.createElement('input');
  pickerInput.type = 'file';
  pickerInput.accept = 'audio/*,.wav,.aif,.aiff,.flac,.mp3,.ogg,.m4a';
  pickerInput.style.display = 'none';
  document.body.appendChild(pickerInput);

  function loadPickedFile (f) {
    if (!f) return;
    showStatus('Loading "' + f.name + '" (' + Math.round(f.size / 1024) + ' KB)…', 8000);

    var path = f.path || f.fullPath || '';
    if (path) {
      var fnPath = getNativeFn('loadSampleFromPath');
      if (fnPath) { fnPath(path); showStatus('Loaded via path: ' + f.name, 4000); return; }
    }

    var maxBytes = 256 * 1024 * 1024;
    if (f.size > maxBytes) {
      showStatus('File too large (' + Math.round(f.size / 1024 / 1024) + ' MB > 256 MB)', 6000);
      return;
    }

    var fnB64 = getNativeFn('loadSampleFromBase64');
    if (!fnB64) { showStatus('No native bridge available for click-to-load', 6000); return; }

    readAsBase64(f).then(function (b64) {
      if (!b64) { showStatus('Could not encode file bytes', 6000); return; }
      fnB64(f.name, b64);
      showStatus('Loaded via bytes: ' + f.name, 4000);
    }).catch(function (err) {
      showStatus('Read error: ' + (err && err.message ? err.message : 'unknown'), 6000);
    });
  }

  pickerInput.addEventListener('change', function () {
    if (pickerInput.files && pickerInput.files[0]) loadPickedFile(pickerInput.files[0]);
    pickerInput.value = '';  // reset so re-selecting the same file works
  });

  function maybeOpenPicker (e) {
    var hero = document.getElementById('hero');
    if (!hero || !hero.classList.contains('empty-state')) return;
    // Don't hijack clicks on interactive overlays (pills, buttons, dropdowns)
    var t = e.target;
    if (t && t.closest && t.closest('button, .ti-mode-pill, .ti-play-pill, .ti-layer-pad, .ti-seq-play, .ti-seq-sync, .ti-seq-pill, .ti-bpm-display, #ti-root-picker, #ti-bottom-pills, #ti-layer-pads, #ti-top-right-cluster, #ti-bottom-right-cluster, #ti-slice-controls, #ti-xy-readout, #ti-slices-pill, #ti-slices-drawer')) return;
    e.preventDefault();
    pickerInput.click();
  }

  document.addEventListener('click', maybeOpenPicker, true);
  document.addEventListener('contextmenu', maybeOpenPicker, true);
})();
</script>
)";
    html = html.replace ("</body>", dropBridge + "</body>");

    // ────────────────────────────────────────────────────────────────────────
    // Phase D Task 12 — Hero canvas overlay (waveform display + chrome)
    //
    // Adds, on top of the existing terrain-canvas mesh:
    //   - waveform-canvas: edge-to-edge filled-white waveform (Serum style),
    //     mirror-symmetric around centerline. Driven by peaks JSON pushed
    //     from C++ on sample load.
    //   - empty-state label + dimmed mesh until first sample loads
    //     (continuity moment with FX side per user's brainstorm)
    //   - drag-hover purple border + dim
    //   - mode toggle (PITCH ↔ SLICE; SLICE disabled in v0a with tooltip)
    //   - root-note picker (lower-left, editable in PITCH mode)
    //
    // Wired to the JS event hooks already pushed from C++:
    //   window.onDragHover(active)
    //   window.onLoadingStarted(filename)
    //   window.onLoadingProgress(0..1)
    //   window.onSampleLoaded({ filename, sampleRate, lengthSamples,
    //                            numChannels, peaksMin[], peaksMax[] })
    //   window.onLoadError(msg)
    // ────────────────────────────────────────────────────────────────────────
    // NOTE: split into 4 runtime-concatenated raw-string pieces. A single
    // literal of the full ~245KB exceeds the toolchain's embeddable string
    // length and silently drops its tail (Phase D stem UI went missing).
    // Each piece stays well under 64KB; juce::String + rejoins at runtime.
    const juce::String heroOverlay = juce::String (R"TIHX(
<style>
  /* ─── Waveform canvas (overlay on top of terrain mesh) ─── */
  #waveform-canvas {
    position: absolute; inset: 0;
    width: 100%; height: 100%;
    z-index: 2;
    opacity: 0;
    pointer-events: none;
    transition: opacity 600ms ease-out;
  }
  #hero.has-sample #waveform-canvas { opacity: 1; }

  /* ─── Terrain mesh: dim to 35% empty state, fully fade on sample load ─── */
  #hero #terrain-canvas {
    transition: opacity 600ms ease-out;
  }
  #hero.empty-state #terrain-canvas { opacity: 0.35; }
  #hero.has-sample #terrain-canvas { opacity: 0; pointer-events: none; }

  /* ─── Empty-state label, centered, fades out on first load ─── */
  #ti-empty-label {
    position: absolute; left: 50%; top: 50%;
    transform: translate(-50%, -50%);
    z-index: 3;
    color: rgba(245, 243, 255, 0.55);
    font: 600 11px/1.2 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.25em; text-transform: uppercase;
    text-align: center;
    white-space: pre-line;
    pointer-events: none;
    transition: opacity 600ms ease-out;
  }
  #hero.has-sample #ti-empty-label { opacity: 0; }
  #hero.has-sample-missing #ti-empty-label {
    color: #ff6b6b;
    opacity: 1 !important;
  }

  /* ─── Drag-hover purple frame ─── */
  #ti-drop-overlay {
    position: absolute; inset: 6px;
    z-index: 4;
    border: 2px dashed rgba(167, 139, 250, 0.65);
    border-radius: 6px;
    background: rgba(139, 92, 246, 0.08);
    box-shadow: inset 0 0 28px rgba(139, 92, 246, 0.25);
    opacity: 0;
    pointer-events: none;
    transition: opacity 200ms ease;
  }
  #hero.drag-hover #ti-drop-overlay { opacity: 1; }

  /* ─── Bottom pill cluster: PITCH/SLICE + 1-SHOT/LOOP side-by-side ───
     Lives in a single flex wrapper at bottom-center so the two pill
     groups align symmetrically with ROOT (bottom-left) and the XY
     readout (bottom-right). Top of hero stays clean for the waveform. */
  #ti-bottom-pills {
    position: absolute; bottom: 12px; left: 50%;
    transform: translateX(-50%);
    z-index: 5;
    display: flex; gap: 10px; align-items: center;
  }
  /* Ghost-glass containers — barely-there bg, no border, blur for depth.
     Active pill gets fill only (no glow ring) so it doesn't "kid-color"
     the strip. */
  #ti-mode-toggle,
  #ti-play-mode-toggle {
    position: relative;
    display: flex; gap: 4px;
    background: rgba(255, 255, 255, 0.035);
    padding: 3px; border-radius: 6px;
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
  }
  .ti-play-pill {
    padding: 3px 9px;
    font: 700 9px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.16em;
    border-radius: 3px;
    color: rgba(245, 243, 255, 0.42);
    cursor: pointer;
    transition: all 150ms ease;
    user-select: none;
  }
  .ti-play-pill.active {
    background: linear-gradient(135deg, #8B5CF6, #7C3AED);
    color: white;
  }
  .ti-mode-pill {
    padding: 4px 12px;
    font: 700 10px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.18em;
    border-radius: 3px;
    color: rgba(245, 243, 255, 0.42);
    cursor: pointer;
    transition: all 150ms ease;
    user-select: none;
  }
  .ti-mode-pill.active {
    background: linear-gradient(135deg, #8B5CF6, #7C3AED);
    color: white;
  }
  .ti-mode-pill.disabled {
    opacity: 0.35; cursor: not-allowed;
  }

  /* ─── Root-note picker, lower-left — click-hold-vertical-drag ─── */
  #ti-root-picker {
    position: absolute; bottom: 12px; left: 12px;
    z-index: 5;
    display: inline-flex; gap: 7px; align-items: center;
    background: rgba(255, 255, 255, 0.035);
    padding: 5px 11px; border-radius: 6px;
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
    cursor: ns-resize;
    user-select: none;
    transition: background-color 150ms ease;
  }
  #ti-root-picker:hover { background: rgba(255, 255, 255, 0.07); }
  #ti-root-picker.dragging {
    background: rgba(139, 92, 246, 0.22);
  }
  #ti-root-picker .ti-root-label {
    font: 600 10px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.15em;
    color: rgba(245, 243, 255, 0.45);
  }
  #ti-root-picker .ti-root-value {
    font: 700 11px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.05em;
    color: #A78BFA;
    min-width: 26px;
    text-align: center;
  }

  /* ─── Layer pads (A/B/C/D) — Mark 2 placeholders for sampler layers.
     Sits to the right of ROOT in the bottom-left zone. A is the active layer
     (current single sampler), B/C/D are dimmed placeholders until the Mark 2
     layer-architecture DSP lands. A lights up briefly while any voice is
     sounding via #ti-layer-pads polling. */
  #ti-layer-pads {
    position: absolute; bottom: 12px; left: 70px;
    z-index: 5;
    display: flex; gap: 3px; align-items: center;
  }
  .ti-layer-pad {
    width: 22px; height: 20px;
    display: flex; align-items: center; justify-content: center;
    font: 700 10px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.06em;
    color: rgba(245, 243, 255, 0.55);
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 3px;
    cursor: pointer;
    user-select: none;
    transition: background-color 150ms ease, color 150ms ease, border-color 150ms ease;
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
  }
  .ti-layer-pad:hover {
    background: rgba(255, 255, 255, 0.08);
    color: rgba(245, 243, 255, 0.85);
  }
  .ti-layer-pad.active {
    background: linear-gradient(135deg, #8B5CF6, #7C3AED);
    color: white;
    border-color: transparent;
  }
  .ti-layer-pad.placeholder {
    opacity: 0.42;
    border-style: dashed;
    cursor: default;
  }
  .ti-layer-pad.placeholder:hover {
    background: rgba(255, 255, 255, 0.035);
    color: rgba(245, 243, 255, 0.55);
  }
  /* Subtle background tint when a voice is sounding — no glow, just a fill bump */
  .ti-layer-pad.playing {
    background: rgba(167, 139, 250, 0.28);
    border-color: rgba(167, 139, 250, 0.55);
  }
  .ti-layer-pad.active.playing {
    background: linear-gradient(135deg, #A78BFA, #8B5CF6);
  }

  /* ─── Sequencer transport (TOP-RIGHT) + BPM/SEQ (BOTTOM-RIGHT) ─────────
     Mark 2 placeholders for the sequencer chrome. Inert v1, will wire when
     the sequencer DSP lands. Sits in the slots vacated by the removed XY
     controls (top-right) and XY readout (bottom-right). */
  #ti-top-right-cluster {
    position: absolute; top: 14px; right: 14px;
    z-index: 5;
    display: flex; gap: 8px; align-items: center;
  }
  .ti-seq-play {
    width: 26px; height: 26px;
    display: flex; align-items: center; justify-content: center;
    background: rgba(255, 255, 255, 0.035);
    border: 1px solid rgba(255, 255, 255, 0.10);
    border-radius: 50%;
    color: rgba(245, 243, 255, 0.75);
    cursor: pointer;
    padding: 0;
    transition: background-color 150ms ease, color 150ms ease, border-color 150ms ease;
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
  }
  .ti-seq-play:hover {
    background: rgba(139, 92, 246, 0.18);
    color: white;
    border-color: rgba(139, 92, 246, 0.45);
  }
  .ti-seq-play svg { margin-left: 1px; /* optical centering of triangle */ }
  .ti-seq-sync {
    display: flex; align-items: center; gap: 6px;
    padding: 5px 10px;
    background: rgba(255, 255, 255, 0.035);
    border-radius: 6px;
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
    cursor: pointer;
    user-select: none;
    transition: background-color 150ms ease;
  }
  .ti-seq-sync:hover { background: rgba(255, 255, 255, 0.07); }
  .ti-seq-sync.active .ti-sync-dot {
    background: #8B5CF6;
    box-shadow: 0 0 4px rgba(139, 92, 246, 0.6);
  }
  .ti-sync-dot {
    width: 7px; height: 7px;
    border-radius: 50%;
    background: rgba(255, 255, 255, 0.18);
    transition: background-color 150ms ease, box-shadow 150ms ease;
  }
  .ti-sync-label {
    font: 700 9px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.15em;
    color: rgba(245, 243, 255, 0.55);
  }

  #ti-bottom-right-cluster {
    position: absolute; bottom: 12px; right: 12px;
    z-index: 5;
    display: flex; gap: 8px; align-items: center;
  }
  .ti-seq-pill {
    padding: 5px 11px;
    background: rgba(255, 255, 255, 0.035);
    border: none;
    border-radius: 6px;
    color: rgba(245, 243, 255, 0.55);
    font: 700 10px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.15em;
    cursor: pointer;
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
    transition: background-color 150ms ease, color 150ms ease;
  }
  .ti-seq-pill:hover {
    background: rgba(139, 92, 246, 0.18);
    color: white;
  }
  .ti-bpm-display {
    display: flex; align-items: center; gap: 5px;
    padding: 5px 11px;
    background: rgba(255, 255, 255, 0.035);
    border-radius: 6px;
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
    user-select: none;
  }
  .ti-bpm-value {
    font: 700 10px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.05em;
    color: #A78BFA;
    min-width: 22px;
    text-align: right;
  }
  .ti-bpm-label {
    font: 600 9px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.18em;
    color: rgba(245, 243, 255, 0.45);
  }
  .ti-bpm-lock {
    display: flex; align-items: center; justify-content: center;
    width: 11px; height: 11px;
    color: rgba(167, 139, 250, 0.7);
    cursor: pointer;
    transition: color 150ms ease;
    margin-left: 2px;
  }
  .ti-bpm-lock:hover { color: #A78BFA; }

  /* ──────────────────────────────────────────────────────────────────
     SLICER UI (v0c) — bottom-strip pill that opens a pull-up drawer.
     The bottom strip language stays sacred (PITCH/SLICE, 1-SHOT/LOOP,
     ROOT, XY readout) — slicer settings live in a self-contained
     drawer that only appears on click. Pattern is reusable for any
     future "extra menu" we add to the bottom strip.
     ────────────────────────────────────────────────────────────────── */
  /* The wrapper is the relative anchor for the drawer. Sits inside
     #ti-bottom-pills as a sibling of the mode + play toggles. */
  #ti-slices-wrap { position: relative; }
  /* PITCH mode: hide the SLICES pill entirely so the bottom strip is
     identical to its pre-slicer look. */
  #ti-slices-wrap.hidden { display: none; }

  /* The trigger pill — same ghost-glass language as the existing pill
     clusters, with a subtle "·N" count showing the active chop count. */
  #ti-slices-btn {
    padding: 4px 12px;
    font: 700 10px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.18em;
    border-radius: 6px;
    color: rgba(245,243,255,0.6);
    background: rgba(255,255,255,0.035);
    backdrop-filter: blur(8px); -webkit-backdrop-filter: blur(8px);
    cursor: pointer;
    transition: all 150ms ease;
    user-select: none;
    display: flex; align-items: center; gap: 6px;
  }
  #ti-slices-btn:hover { background: rgba(255,255,255,0.07); color: rgba(245,243,255,0.85); }
  #ti-slices-btn.open {
    background: linear-gradient(135deg, #8B5CF6, #7C3AED);
    color: white;
  }
  #ti-slices-btn .ti-slices-count {
    color: #A78BFA;
    font-weight: 700;
  }
  #ti-slices-btn.open .ti-slices-count { color: rgba(255,255,255,0.9); }

  /* The drawer — pulls UP from the button, ghost-glass surface.
     Always laid out (display:flex) but kept invisible until .open via
     opacity+transform so it can fade in / out softly instead of snapping. */
  #ti-slicer-drawer {
    position: absolute;
    bottom: calc(100% + 8px);
    left: 50%;
    transform: translate(-50%, -4px);
    z-index: 6;
    display: flex;
    flex-direction: column; gap: 8px;
    padding: 10px 12px; border-radius: 8px;
    background: rgba(20, 18, 32, 0.92);
    backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px);
    box-shadow: 0 6px 20px rgba(0, 0, 0, 0.35);
    user-select: none;
    white-space: nowrap;
    opacity: 0;
    pointer-events: none;
    transition: opacity 200ms ease, transform 220ms ease;
  }
  #ti-slicer-drawer.open {
    opacity: 1;
    pointer-events: auto;
    transform: translateX(-50%);
  }

  /* When the drawer is open, soften the waveform + slice markers so the
     drawer doesn't look harshly slapped on top. Transition is defined on
     the BASE selector so the effect fades IN and OUT smoothly. */
  #waveform-canvas,
  #ti-slice-overlays {
    transition: filter 240ms ease, opacity 240ms ease;
  }
  #hero.drawer-open #waveform-canvas,
  #hero.drawer-open #ti-slice-overlays {
    filter: blur(3px);
    opacity: 0.32;
  }
  .ti-drawer-row { display: flex; gap: 4px; align-items: center; justify-content: center; }

  /* GRID pills inline in the drawer — no dropdown, all sizes visible. */
  .ti-grid-pill {
    padding: 5px 11px;
    font: 700 10px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.10em;
    border-radius: 4px; cursor: pointer;
    color: rgba(245,243,255,0.55);
    background: rgba(255,255,255,0.04);
    transition: all 140ms ease;
    min-width: 28px; text-align: center;
  }
  .ti-grid-pill:hover { background: rgba(167,139,250,0.18); color: white; }
  .ti-grid-pill.active {
    background: linear-gradient(135deg, #8B5CF6, #7C3AED);
    color: white;
  }

  /* Sub-mode pills (CHOP / CHROMATIC) — same bottom-strip pill language. */
  #ti-submode-toggle { display: flex; gap: 4px; }
  .ti-submode-pill {
    padding: 5px 12px;
    font: 700 10px/1 -apple-system, sans-serif; letter-spacing: 0.18em;
    border-radius: 4px; cursor: pointer;
    color: rgba(245,243,255,0.42);
    transition: all 150ms ease;
  }
  .ti-submode-pill:hover { color: rgba(245,243,255,0.85); }
  .ti-submode-pill.active {
    background: linear-gradient(135deg, #8B5CF6, #7C3AED);
    color: white;
  }
  /* HOLD state on the CHOP pill — amber gradient + soft glow so the user can
     see at a glance that note-off is being ignored. Inherits .active styling
     via class stacking; this rule wins on background/box-shadow. */
  .ti-submode-pill.hold-active {
    background: linear-gradient(135deg, #F59E0B, #D97706);
    color: white;
    box-shadow: 0 0 8px rgba(245,158,11,0.45);
  }

  /* Action buttons inside the drawer — RANDOM:5TH/7TH/OCT etc. Ghost-glass
     base, fills purple on hover, flashes white on click. */
  .ti-action-btn {
    padding: 5px 12px;
    font: 700 10px/1 -apple-system, sans-serif; letter-spacing: 0.18em;
    border-radius: 4px; cursor: pointer;
    color: rgba(245,243,255,0.78);
    background: rgba(255,255,255,0.04);
    transition: background 150ms ease, color 150ms ease, transform 80ms ease;
    user-select: none;
  }
  .ti-action-btn:hover {
    background: rgba(167,139,250,0.18);
    color: white;
  }
  .ti-action-btn:active {
    transform: scale(0.97);
    background: linear-gradient(135deg, #8B5CF6, #7C3AED);
    color: white;
  }
  /* Prefix label inline with the action buttons — "RANDOM:" before the
     5TH/7TH/OCT triplet. Tonal grayscale (not boxed) so it reads as a
     label, not a tappable button. */
  .ti-action-label {
    padding: 5px 4px 5px 8px;
    font: 700 10px/1 -apple-system, sans-serif; letter-spacing: 0.18em;
    color: rgba(245,243,255,0.55);
    user-select: none;
  }

  /* Chop Fade row — compact slider in the slicer drawer */
  #ti-fade-row {
    display: flex; align-items: center; gap: 6px;
  }
  #ti-fade-row .ti-action-label { padding-left: 0; }
  #ti-fade-slider {
    -webkit-appearance: none; appearance: none;
    width: 80px; height: 3px;
    background: rgba(139,92,246,0.35);
    border-radius: 2px; outline: none; cursor: pointer;
    accent-color: #8B5CF6;
  }
  #ti-fade-slider::-webkit-slider-thumb {
    -webkit-appearance: none; width: 10px; height: 10px;
    border-radius: 50%; background: #8B5CF6; cursor: pointer;
  }
  #ti-fade-value {
    font: 600 10px/1 -apple-system, sans-serif;
    color: rgba(245,243,255,0.65); min-width: 52px; text-align: right;
    display: inline-block; font-variant-numeric: tabular-nums;
  }

  /* Slice markers + bodies — drawn on top of the waveform canvas */
  #ti-slice-overlays {
    position: absolute; left: 0; right: 0;
    pointer-events: none;  /* parent doesn't intercept, children re-enable */
    z-index: 4;
  }
  /* Scan-line viz canvas — same size as ti-slice-overlays, drawn by
     pollScanViz() at ~30 Hz. pointer-events:none so clicks pass through. */
  #ti-scan-viz-canvas {
    position: absolute; left: 0; right: 0; top: 0; bottom: 0;
    width: 100%; height: 100%;
    pointer-events: none;
    /* Bumped from z=1 to z=4 (above markers at z=3) so the scan line is
       guaranteed to paint over per-chop waveform canvases and slice markers.
       At z=1 the line was technically above auto-z chop bodies but could be
       hidden by other overlay strata. */
    z-index: 4;
    /* Always visible — drawScanViz() clears when no voice is scanning,
       so an idle canvas is just transparent. !important to defeat any
       inherited cascade if ti-slicer-active toggling races with paint. */
    display: block !important;
  }
  .ti-slice-marker {
    position: absolute; top: 0; bottom: 0; width: 1px;
    background: rgba(167,139,250,0.45);
    pointer-events: auto;
    cursor: ew-resize;
    transition: background 140ms ease, box-shadow 140ms ease;
  }
  /* Invisible wider hit-zone for easier grabbing — visible line stays 1px. */
  .ti-slice-marker::after {
    content: '';
    position: absolute; top: 0; bottom: 0;
    left: -4px; right: -4px;
    cursor: ew-resize;
  }
  .ti-slice-marker:hover {
    background: rgba(167,139,250,0.95);
    box-shadow: 0 0 8px rgba(139,92,246,0.55);
  }
  .ti-slice-marker.dragging {
    background: #C4B5FD;
    box-shadow: 0 0 12px rgba(139,92,246,0.9);
  }
  .ti-slice-marker .ti-slice-label {
    position: absolute; top: 2px; left: -10px; width: 20px;
    text-align: center;
    font: 700 9px/1 sans-serif; letter-spacing: 0.05em;
    color: #A78BFA;
    background: rgba(45,37,69,0.8);
    padding: 2px 0; border-radius: 2px;
  }
  /* ── Pitch-mode IN / OUT bound markers ──────────────────────────────── */
  .ti-pitch-bound-marker {
    position: absolute; top: 0; bottom: 0; width: 1.5px;
    background: rgba(255,255,255,0.70);
    cursor: ew-resize;
    z-index: 3;
    pointer-events: auto;
    transition: background 120ms ease, box-shadow 120ms ease;
  }
  /* Wider invisible hit-zone for easier grabbing */
  .ti-pitch-bound-marker::before {
    content: '';
    position: absolute; top: 0; bottom: 0;
    left: -6px; width: 14px;
    cursor: ew-resize;
  }
  .ti-pitch-bound-marker:hover {
    background: rgba(255,255,255,0.95);
    box-shadow: 0 0 6px rgba(255,255,255,0.5);
  }
  .ti-pitch-bound-marker.dragging {
    background: #FFFFFF;
    box-shadow: 0 0 10px rgba(255,255,255,0.75);
  }
  /* .ti-pitch-bound-label intentionally empty — labels removed (Bug B) */
  /* ── Dimmer overlay for regions outside IN/OUT ───────────────────────── */
  .ti-pitch-bound-dim {
    position: absolute; top: 0; bottom: 0;
    background: rgba(10, 8, 24, 0.55);
    pointer-events: none;
    z-index: 2;
  }
  .ti-slice-body {
    position: absolute; top: 0; bottom: 0;
    pointer-events: auto; cursor: pointer;
    transition: background 140ms ease;
  }
  .ti-slice-body:hover { background: rgba(139,92,246,0.07); }
  /* Play-glow — driven by --glow-alpha which the JS poll updates per
     slice from C++. Renders behind the slice body so the click hit area
     stays clean. No CSS transition: the audio thread is already smoothing
     the envelope, and a CSS transition on top would chase a moving target
     and feel laggy. */
  .ti-slice-body { --glow-alpha: 0; }
  .ti-slice-body::before {
    content: '';
    position: absolute;
    inset: -10px -6px -10px -6px;
    pointer-events: none;
    background: radial-gradient(
      ellipse 58% 70% at 50% 48%,
      rgba(139,92,246, calc(var(--glow-alpha) * 0.50)) 0%,
      rgba(139,92,246, calc(var(--glow-alpha) * 0.28)) 38%,
      rgba(139,92,246, calc(var(--glow-alpha) * 0.10)) 66%,
      rgba(139,92,246, 0) 88%);
    filter: blur(7px);
    z-index: -1;
  }
  .ti-slice-body.dragging { background: rgba(139,92,246,0.22); }
  /* Inline pitch meter — sits inside the slice body, near the bottom.
     A horizontal bar centered on a midline (= 0 semitones) that extends
     right for positive pitch and left for negative. Plus a clean number
     readout above. Shown only when pitch != 0. */
  .ti-slice-pitch-meter {
    position: absolute;
    left: 4px; right: 4px;
    bottom: 6px;
    display: flex; flex-direction: column; align-items: center; gap: 3px;
    pointer-events: none;
    user-select: none;
  }
  .ti-slice-pitch-num {
    font: 700 10px/1 -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif;
    color: rgba(167,139,250,0.95);
    /* Dark pill background so +12 / -12 stays readable on top of loud
       waveforms (peak-scale can't push the wave fully clear of the bottom
       label zone without making it tiny — see state.peakScale doc). */
    background: rgba(15, 10, 30, 0.85);
    padding: 1px 5px;
    border-radius: 3px;
  }
  .ti-slice-pitch-bar {
    position: relative;
    width: 100%;
    height: 2px;
    background: rgba(255,255,255,0.10);
    border-radius: 1px;
  }
  /* Center marker — the 0-semitone reference line. */
  .ti-slice-pitch-bar::before {
    content: ''; position: absolute;
    left: 50%; top: -2px; bottom: -2px;
    width: 1px; margin-left: -0.5px;
    background: rgba(255,255,255,0.32);
  }
  .ti-slice-pitch-bar-fill {
    position: absolute; top: 0; bottom: 0;
    background: #A78BFA;
    border-radius: 1px;
    transition: width 90ms ease, left 90ms ease, right 90ms ease;
  }
  /* Reverse letter — bare "R" at bottom-right of chop body. Deliberately
     unboxed so it reads as a status mark, not a clickable target (the boxed
     warp letter T/B/X is interactive; R isn't). Sits to the left of the
     warp letter when present, else hugs the corner. Same hide-when-narrow
     gate as the other body overlays. */
  .ti-slice-rev-letter {
    position: absolute; bottom: 26px;
    font: 700 9px/1 -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif;
    color: rgba(167,139,250,0.55);
    text-shadow: 0 1px 2px rgba(0,0,0,0.55);
    pointer-events: none;
    user-select: none;
  }
  /* Warp mode letter — bottom-right of chop body, above the pitch meter row.
     Out of the way of the boundary marker label that sits top-left of the
     next chop. Single character (B / T / X). Clickable to cycle modes.
     Soft-blurred backdrop, no purple-on-purple hard edges. */
  .ti-slice-warp-letter {
    position: absolute; bottom: 24px; right: 4px;
    width: 14px; height: 14px;
    display: flex; align-items: center; justify-content: center;
    border-radius: 3px;
    font: 700 9px/1 -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif;
    color: #A78BFA;
    background: rgba(20,18,32,0.55);
    backdrop-filter: blur(4px);
    cursor: pointer;
    user-select: none;
    pointer-events: auto;
    transition: background 160ms ease, color 160ms ease, transform 160ms ease;
  }
  .ti-slice-warp-letter:hover {
    background: rgba(139,92,246,0.35);
    color: white;
    transform: scale(1.08);
  }
  /* Stretched chop body — subtle inset purple border so visual stretch
     is reinforced by a slight chrome. No z-index hack needed now that
     chops use a cumulative layout (no overlap). */
  .ti-slice-body.is-stretched {
    box-shadow: 0 0 0 1px rgba(167,139,250,0.3) inset;
  }
  /* Per-chop waveform canvas — sits inside each chop body, renders the
     chop's source segment scaled to fit the body's visual width. When the
     chop is stretched, the body grows and so does this canvas, visually
     stretching the waveform itself (Ableton-style warp). */
  .ti-slice-waveform {
    position: absolute;
    left: 0; top: 0;
    pointer-events: none;
  }
  /* When the slicer is active, hide the global uniform-mapping waveform —
     the per-chop canvases above each chop body are now the visual
     waveform. !important needed because #hero.has-sample #waveform-canvas
     already sets opacity:1 with higher specificity than a class-on-body
     selector. */
  body.ti-slicer-active #waveform-canvas { opacity: 0 !important; }
  /* Stretch ratio label — visible only when ratio != 1.0. Bottom-aligned
     with the R letter (bottom-right at bottom:26px) and the warp letter's
     text baseline so the bottom-corner badges read as one symmetric row. */
  .ti-slice-stretch-label {
    position: absolute; left: 4px; bottom: 26px;
    font: 600 9px/1 -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif;
    color: rgba(167,139,250,0.7);
    text-shadow: 0 1px 2px rgba(0,0,0,0.55);
    pointer-events: none;
    user-select: none;
  }
  /* Floating tooltip shown during shift+drag stretch. Matches the right-
     click menu translucency so we stay coherent. Fades in/out 200ms. */
  #ti-stretch-tooltip {
    position: fixed; z-index: 9999; pointer-events: none;
    padding: 5px 9px; border-radius: 4px;
    font: 700 11px/1 -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif;
    color: white;
    background: rgba(20,18,32,0.92); backdrop-filter: blur(12px);
    box-shadow: 0 4px 16px rgba(0,0,0,0.4);
    display: none; opacity: 0;
    transition: opacity 200ms ease;
  }
  #ti-stretch-tooltip.visible { display: block; opacity: 1; }

  /* ─── Chop overlay (Lab Card v2) — right-click on a chop → floating panel
     with full ADSR canvas + Volume / Pitch / Stretch emblem-knobs ──────── */
  #ti-chop-backdrop {
    position: fixed; inset: 0; z-index: 4000;
    background: rgba(8,6,16,0.55);
    backdrop-filter: blur(14px) saturate(115%);
    -webkit-backdrop-filter: blur(14px) saturate(115%);
    opacity: 0; pointer-events: none;
    transition: opacity 200ms cubic-bezier(0.16, 1, 0.3, 1);
  }
  #ti-chop-backdrop.open { opacity: 1; pointer-events: auto; }

  #ti-chop-panel {
    position: fixed; z-index: 4001;
    left: 50%; top: 50%;
    transform: translate(-50%, -50%) scale(0.97);
    transform-origin: center;
    width: 400px;
    background:
      radial-gradient(140% 120% at 50% 0%, rgba(255,255,255,0.04), transparent 60%),
      linear-gradient(180deg, #1f1a2e, #15121f);
    border: 1px solid rgba(255,255,255,0.10);
    border-radius: 4px;
    box-shadow:
      0 30px 60px rgba(0,0,0,0.55),
      0 4px 10px rgba(0,0,0,0.35),
      inset 0 1px 0 rgba(255,255,255,0.06);
    padding: 18px 20px 16px;
    opacity: 0; pointer-events: none;
    transition: opacity 220ms cubic-bezier(0.16, 1, 0.3, 1),
                transform 260ms cubic-bezier(0.16, 1, 0.3, 1);
    color: rgba(245,243,255,0.92);
    /* Terrain font — unchanged from the rest of the plugin. */
    font: 500 13px/1.4 -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif;
  }
  #ti-chop-panel.open {
    opacity: 1; pointer-events: auto;
    transform: translate(-50%, -50%) scale(1);
  }
  /* index-card hole-punch + ruled lines (decorative, behind content) */
  #ti-chop-panel::before {
    content: '';
    position: absolute; top: 14px; left: 14px;
    width: 5px; height: 5px;
    border-radius: 50%;
    background: rgba(0,0,0,0.45);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.06);
  }
  #ti-chop-panel::after {
    content: '';
    position: absolute; inset: 38px 14px 60px 14px; pointer-events: none;
    background-image: repeating-linear-gradient(
      to bottom,
      transparent 0,
      transparent 23px,
      rgba(245,243,255,0.022) 24px);
  }
  /* Kill italics globally inside the panel — Terrain has no italic typography. */
  #ti-chop-panel em, #ti-chop-panel i { font-style: normal; }

  /* header — title perfectly centered, close button anchored to the right */
  #ti-chop-panel .ov-head {
    display: flex; justify-content: center; align-items: baseline;
    margin-bottom: 14px;
    padding: 0 18px;
    position: relative; z-index: 1;
  }
  #ti-chop-panel .ov-head .name {
    display: inline-flex; align-items: baseline;   /* baseline-align CHOP + 03 */
    font: 700 12px/1 -apple-system, sans-serif; letter-spacing: 0.22em;
    color: rgba(245,243,255,0.92);
  }
  #ti-chop-panel .ov-head .name .num {
    color: #8b5cf6;
    font: 600 13px/1 -apple-system, sans-serif;     /* same family — kills the "slanted" look */
    letter-spacing: 0.04em;
    margin-left: 8px;
  }
  #ti-chop-panel .ov-head .ov-close {
    position: absolute; right: 14px; top: 50%;
    transform: translateY(-50%);
    width: 22px; height: 22px; border-radius: 4px;
    display: grid; place-items: center; cursor: pointer;
    color: rgba(245,243,255,0.40);
    transition: background 140ms, color 140ms;
  }
  #ti-chop-panel .ov-head .ov-close:hover {
    background: rgba(255,255,255,0.05);
    color: rgba(245,243,255,0.92);
  }

  /* mode pills row */
  #ti-chop-panel .ov-modes {
    display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px;
    margin-bottom: 14px; padding-left: 18px;
    position: relative; z-index: 1;
  }
  #ti-chop-panel .ov-mode {
    position: relative;
    background: rgba(0,0,0,0.18);
    border: 1px solid rgba(255,255,255,0.06);
    border-radius: 3px;
    /* Emblem-only buttons (label removed) — symmetric padding for a
       square-ish symbol cell rather than the tall portrait the labeled
       version had. */
    padding: 12px 6px;
    display: flex; align-items: center; justify-content: center;
    cursor: pointer; user-select: none;
    transition: all 160ms ease;
  }
  #ti-chop-panel .ov-mode:hover {
    background: rgba(255,255,255,0.04);
    border-color: rgba(255,255,255,0.10);
  }
  #ti-chop-panel .ov-mode.active {
    border-color: rgba(139,92,246,0.55);
    background: rgba(139,92,246,0.10);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.06), 0 0 14px rgba(139,92,246,0.18);
  }
  #ti-chop-panel .ov-mode .emblem {
    display: grid; place-items: center;
    color: rgba(245,243,255,0.55); transition: color 160ms;
  }
  #ti-chop-panel .ov-mode:hover .emblem { color: rgba(245,243,255,0.85); }
  #ti-chop-panel .ov-mode.active .emblem { color: #8b5cf6; }

  /* MOTION row — between mode emblems and ADSR, holds SCAN pill + RATE display */
  #ti-chop-panel .motion-row {
    display: flex; align-items: center; gap: 10px;
    padding: 6px 10px;
    background: rgba(80, 60, 130, 0.18);
    border: 1px solid rgba(140, 100, 220, 0.18);
    border-radius: 8px;
    margin: 0 0 14px 18px;
    height: 28px;
    box-sizing: border-box;
    position: relative; z-index: 1;
  }
  #ti-chop-panel .motion-label {
    font: 700 9px/1 -apple-system; letter-spacing: 1.5px;
    color: rgba(245,243,255,0.55); text-transform: uppercase;
  }
  #ti-chop-panel .scan-pill {
    display: inline-flex; align-items: center;
    height: 18px; padding: 0 10px;
    background: rgba(140, 100, 220, 0.28);
    border: 1px solid rgba(168, 136, 255, 0.5);
    border-radius: 10px;
    font: 700 9px/1 -apple-system; letter-spacing: 1.5px; text-transform: uppercase;
    color: #fff; cursor: pointer; user-select: none;
    transition: all 160ms ease;
  }
  #ti-chop-panel .scan-pill.off {
    background: rgba(80, 60, 130, 0.12);
    border-color: rgba(140, 100, 220, 0.2);
    opacity: 0.55;
  }
  #ti-chop-panel .rate-display {
    margin-left: auto; display: inline-flex; align-items: baseline; gap: 6px;
    cursor: ns-resize; user-select: none; font: 500 10px/1 -apple-system;
  }
  #ti-chop-panel .rate-display.dim { cursor: default; }
  #ti-chop-panel .rate-display .rate-label {
    font: 700 9px/1 -apple-system; letter-spacing: 1.5px; text-transform: uppercase;
    color: rgba(245,243,255,0.45);
  }
  #ti-chop-panel .rate-display .rate-value { color: rgba(245,243,255,0.92); font-weight: 600; }
  #ti-chop-panel .rate-display.dim .rate-value { color: rgba(245,243,255,0.30); }
  /* Mod ring: active LFO assignment on activeChopScanRate */
  #ti-chop-panel .rate-display.mod-active .rate-value {
    text-shadow: 0 0 5px rgba(168, 136, 255, 0.8);
  }
  #ti-chop-panel .rate-display.mod-active .rate-label {
    color: rgba(168, 136, 255, 0.7);
    transition: color 200ms ease;
  }

  /* ADSR envelope canvas — the centrepiece */
  #ti-chop-panel .ov-env {
    margin: 0 0 14px 18px;
    height: 168px;
    background: linear-gradient(180deg, rgba(255,255,255,0.02), transparent 70%);
    border: 1px solid rgba(255,255,255,0.06);
    border-radius: 3px;
    position: relative; z-index: 1;
    overflow: hidden;
  }
  #ti-chop-panel .ov-env-svg {
    position: absolute; inset: 14px 14px 12px 14px;
    width: calc(100% - 28px); height: calc(100% - 26px);
    pointer-events: none;
  }
  #ti-chop-panel .ov-env-fill { fill: rgba(139,92,246,0.12); stroke: none; }
  #ti-chop-panel .ov-env-line {
    fill: none; stroke: #8b5cf6; stroke-width: 1.6;
    stroke-linejoin: round; stroke-linecap: round;
    filter: drop-shadow(0 0 5px rgba(139,92,246,0.45));
  }
  /* draggable handles — solid filled purple, sit centered on the envelope line */
  #ti-chop-panel .ov-env-handle {
    position: absolute;
    width: 9px; height: 9px; border-radius: 50%;
    background: #8b5cf6;
    border: none;
    box-shadow: 0 0 7px rgba(139,92,246,0.55);
    transform: translate(-50%, -50%);
    cursor: grab; z-index: 2;
    transition: box-shadow 140ms, transform 140ms;
  }
  #ti-chop-panel .ov-env-handle:hover {
    box-shadow: 0 0 12px rgba(139,92,246,0.85);
    background: #a78bfa;
  }
  #ti-chop-panel .ov-env-handle:active,
  #ti-chop-panel .ov-env-handle.dragging { cursor: grabbing; }

  /* three emblem controls — Volume / Pitch / Stretch. No text labels. */
  #ti-chop-panel .ov-ctrls {
    display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px;
    padding: 0 0 0 18px; margin-bottom: 14px;
    position: relative; z-index: 1;
  }
  #ti-chop-panel .ov-ctrl {
    display: flex; align-items: center; gap: 10px;
    padding: 9px 10px;
    background: rgba(0,0,0,0.22);
    border: 1px solid rgba(255,255,255,0.06);
    border-radius: 3px;
    cursor: ns-resize;
    transition: border-color 140ms, background 140ms;
    /* min-width:0 + overflow:hidden = grid columns stay locked at 1fr no
       matter how wide the value text is. Without this, "15.00×" pushes the
       stretch column wider than its siblings and shifts the emblem. */
    min-width: 0;
    overflow: hidden;
  }
  #ti-chop-panel .ov-ctrl:hover {
    background: rgba(0,0,0,0.34);
    border-color: rgba(255,255,255,0.10);
  }
  #ti-chop-panel .ov-ctrl:hover .ov-emblem { animation-play-state: paused; }
  #ti-chop-panel .ov-ctrl .ov-emblem {
    color: #8b5cf6; flex-shrink: 0;
  }
  #ti-chop-panel .ov-ctrl .ov-val {
    font: 600 12px/1 -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif;
    font-variant-numeric: tabular-nums;   /* fixed-width digits — no column shift on value change */
    white-space: nowrap;                  /* "+12 st" never wraps onto two lines */
    color: rgba(245,243,255,0.92); letter-spacing: 0.02em;
    flex: 1; min-width: 0;                /* fill remaining ctrl space, allow shrinking under tight columns */
    text-align: right;                    /* value hugs the right edge — emblem stays fixed on the left */
  }
  /* Stretch ctrl stays in the 3-column grid for symmetry — when warp=none
     it grays out and ignores pointer events rather than disappearing. */
  #ti-chop-panel[data-warp="none"] .ov-ctrl.warp-only {
    opacity: 0.30; cursor: not-allowed; pointer-events: none;
  }

  /* idle animations — subtle motion, paused on hover */
  /* Speaker sound waves gently breathe outward — opacity + tiny x-shift so
     the speaker reads as "playing audio" without ever shifting layout. */
  @keyframes ti-vol-wave {
    0%, 100% { opacity: 0.65; transform: translateX(0); }
    50%      { opacity: 1.0;  transform: translateX(0.4px); }
  }
  #ti-chop-panel .ov-emblem-vol .wave {
    animation: ti-vol-wave 2.2s cubic-bezier(0.4, 0, 0.6, 1) infinite;
    transform-origin: left center;
  }
  #ti-chop-panel .ov-emblem-vol .wave-2 { animation-delay: 0.35s; }
  /* Tuning-fork prong vibration — subtle, static (NOT value-driven). */
  @keyframes ti-fork-vibrate-l {
    0%, 100% { transform: translateX(0); }
    50%      { transform: translateX(-0.4px); }
  }
  @keyframes ti-fork-vibrate-r {
    0%, 100% { transform: translateX(0); }
    50%      { transform: translateX(0.4px); }
  }
  #ti-chop-panel .ov-emblem-pitch .prong-l {
    transform-origin: 11px 11px;
    animation: ti-fork-vibrate-l 1.4s cubic-bezier(0.4, 0, 0.6, 1) infinite;
  }
  #ti-chop-panel .ov-emblem-pitch .prong-r {
    transform-origin: 11px 11px;
    animation: ti-fork-vibrate-r 1.4s cubic-bezier(0.4, 0, 0.6, 1) infinite;
  }
  @keyframes ti-stretch-breathe {
    0%,100% { transform: scaleX(1); }
    50%     { transform: scaleX(1.06); }
  }
  #ti-chop-panel .ov-emblem-stretch .group {
    transform-origin: center;
    animation: ti-stretch-breathe 3.8s cubic-bezier(0.4, 0, 0.6, 1) infinite;
  }

  /* ─── FX section (Mark 2) — per-chop FX independence ──────────────────
     INDEPENDENT off (default) = chop follows the global FX chain; chips
     dim to ~35% and ignore clicks. INDEPENDENT on = chop detached, chips
     become interactive; user re-enables the engines they want. */
  #ti-chop-panel .ov-fx {
    padding: 10px 0 0 18px; margin-bottom: 12px;
    position: relative; z-index: 1;
    border-top: 1px solid var(--line, rgba(255,255,255,0.06));
  }
  #ti-chop-panel .ov-fx-header {
    display: flex; align-items: stretch;
    margin-bottom: 10px;
  }
  /* INDEPENDENT pill — full-width row, same height as the VOL/PITCH/STRETCH knob pills above */
  #ti-chop-panel .ov-indy {
    flex: 1;
    padding: 9px 10px;
    display: flex; align-items: center; justify-content: center;
    font: 700 9px/1 -apple-system; letter-spacing: 0.18em;
    color: rgba(245,243,255,0.40); text-transform: uppercase;
    background: rgba(0,0,0,0.22);
    border: 1px solid rgba(255,255,255,0.06); border-radius: 3px;
    cursor: pointer; user-select: none;
    transition: all 160ms ease;
  }
  #ti-chop-panel .ov-indy:hover { background: rgba(255,255,255,0.04); color: rgba(245,243,255,0.92); }
  #ti-chop-panel .ov-indy.on {
    background: linear-gradient(135deg, #8b5cf6, #7C3AED);
    color: white; border-color: #8b5cf6;
    box-shadow: 0 0 12px rgba(139,92,246,0.40);
  }
  /* 6 FX chips in a 3-column × 2-row grid */
  #ti-chop-panel .ov-fx-grid {
    display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px;
  }
  #ti-chop-panel .ov-fx-chip {
    padding: 7px 6px;
    font: 700 9px/1 -apple-system; letter-spacing: 0.18em;
    color: rgba(245,243,255,0.40); text-transform: uppercase;
    background: rgba(0,0,0,0.18);
    border: 1px solid rgba(255,255,255,0.06); border-radius: 3px;
    text-align: center; cursor: pointer; user-select: none;
    transition: all 160ms ease;
    white-space: nowrap; overflow: hidden;
  }
  #ti-chop-panel .ov-fx-chip:hover {
    background: rgba(255,255,255,0.04); color: rgba(245,243,255,0.92);
    border-color: rgba(255,255,255,0.10);
  }
  #ti-chop-panel .ov-fx-chip.on {
    background: rgba(139,92,246,0.18);
    color: rgba(245,243,255,0.92); border-color: rgba(139,92,246,0.55);
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.06);
  }
  #ti-chop-panel .ov-fx-chip .sub {
    color: #a78bfa; margin-left: 4px;
  }
  /* Inheriting state: dim and disable the chip grid */
  #ti-chop-panel .ov-fx.inheriting .ov-fx-grid { opacity: 0.30; pointer-events: none; }

  /* action row — small icon+label combos */
  #ti-chop-panel .ov-actions {
    display: flex; justify-content: space-between; align-items: center;
    padding-left: 18px;
    position: relative; z-index: 1;
  }
  #ti-chop-panel .ov-actions .group { display: flex; gap: 14px; }
  #ti-chop-panel .ov-act {
    display: flex; align-items: center; gap: 7px;
    font: 700 9px/1 -apple-system; letter-spacing: 0.18em;
    color: rgba(245,243,255,0.40);
    cursor: pointer; user-select: none;
    text-transform: uppercase;
    transition: color 140ms;
  }
  #ti-chop-panel .ov-act:hover { color: rgba(245,243,255,0.92); }
  #ti-chop-panel .ov-act.danger:hover { color: #f43f5e; }

  /* ─── XY readout — relocate to bottom-right (mirror of root picker) ─── */
  #hero .xy-readout {
    top: auto !important;
    bottom: 12px !important;
    left: auto !important;
    right: 12px !important;
    background: rgba(255, 255, 255, 0.035) !important;
    border: none !important;
    padding: 5px 11px !important;
    border-radius: 6px !important;
    backdrop-filter: blur(8px) !important;
    -webkit-backdrop-filter: blur(8px) !important;
    font: 600 10px/1 -apple-system, BlinkMacSystemFont, sans-serif !important;
    letter-spacing: 0.12em !important;
    color: rgba(245, 243, 255, 0.5) !important;
    /* Must sit ABOVE the slice overlay (z-index 4) so click-to-toggle
       isn't intercepted by a slice body in SLICE mode. */
    z-index: 7 !important;
  }

  /* ─── XY pad UI hidden in Terrain Instrument (2026-05-26) ─────────────────
     XY modulation isn't in the instrument's product scope — all modulation
     lives in the MOD section. We keep the underlying XY DOM nodes intact
     (13+ JS references would break if we deleted them) and just hide them
     visually. The hero-corner cluster (xy-play / xy-mode / xy-speed) is now
     reserved for the Mark 2 sequencer's transport / mode controls. */
  #hero .xy-readout,
  #hero .xy-controls {
    display: none !important;
  }
</style>

<script>
(function () {
  // ── DOM injection inside #hero ────────────────────────────────────────────
  function injectHeroOverlays () {
    var hero = document.getElementById('hero');
    if (!hero) {
      // Hero not present yet — try again next frame.
      requestAnimationFrame(injectHeroOverlays);
      return;
    }
    if (document.getElementById('waveform-canvas')) return; // already injected

    // Waveform canvas (placed before all overlays so other UI sits on top)
    var wave = document.createElement('canvas');
    wave.id = 'waveform-canvas';
    hero.insertBefore(wave, hero.firstChild ? hero.firstChild.nextSibling : null);

    // Drag-hover overlay
    var drop = document.createElement('div');
    drop.id = 'ti-drop-overlay';
    hero.appendChild(drop);

    // Empty state label
    var empty = document.createElement('div');
    empty.id = 'ti-empty-label';
    empty.textContent = 'DRAG SAMPLE OR CLICK TO LOAD';
    hero.appendChild(empty);

    // Bottom pill cluster — single flex wrapper so PITCH/SLICE and 1-SHOT/LOOP
    // sit side-by-side at bottom-center of the hero, symmetric with ROOT
    // (bottom-left) and the XY readout (bottom-right). Top stays clean.
    var bottomPills = document.createElement('div');
    bottomPills.id = 'ti-bottom-pills';
    hero.appendChild(bottomPills);

    // Mode toggle (PITCH / SLICE — both functional as of v0b)
    var modeWrap = document.createElement('div');
    modeWrap.id = 'ti-mode-toggle';
    modeWrap.innerHTML =
      '<div class="ti-mode-pill active" data-mode="PITCH">PITCH</div>' +
      '<div class="ti-mode-pill" data-mode="SLICE">SLICE</div>';
    bottomPills.appendChild(modeWrap);

    // Play-mode toggle (1-SHOT vs forward LOOP)
    var playWrap = document.createElement('div');
    playWrap.id = 'ti-play-mode-toggle';
    playWrap.title = 'Sample playback: one-shot or forward loop';
    playWrap.innerHTML =
      '<div class="ti-play-pill active" data-play="0">1-SHOT</div>' +
      '<div class="ti-play-pill" data-play="1">LOOP</div>';
    bottomPills.appendChild(playWrap);

    // Root note picker
    var rootWrap = document.createElement('div');
    rootWrap.id = 'ti-root-picker';
    rootWrap.title = 'Root note — click to cycle, shift-click to go down';
    rootWrap.innerHTML =
      '<span class="ti-root-value" id="ti-root-value">C4</span>';
    hero.appendChild(rootWrap);

    // ─── A/B/C/D layer pads (Mark 2 placeholders) ─────────────────────────
    // Visual chrome for the four sampler layers (A/B/C/D) that will land in
    // the Mark 2 layer-architecture phase. A is shown as active by default
    // (current single sampler is conceptually layer A); B/C/D are dimmed
    // placeholders. The A pad lights up briefly while any voice is sounding,
    // wired via the isAnyVoicePlaying native function below.
    var layerPads = document.createElement('div');
    layerPads.id = 'ti-layer-pads';
    layerPads.innerHTML =
      '<div class="ti-layer-pad active" data-layer="A" data-layer-idx="0" title="Layer A">A</div>' +
      '<div class="ti-layer-pad"        data-layer="B" data-layer-idx="1" title="Layer B">B</div>' +
      '<div class="ti-layer-pad"        data-layer="C" data-layer-idx="2" title="Layer C">C</div>' +
      '<div class="ti-layer-pad"        data-layer="D" data-layer-idx="3" title="Layer D">D</div>';
    hero.appendChild(layerPads);

    // ─── Sequencer transport (TOP-RIGHT) — Mark 2 placeholders ───────────
    // Play button + SYNC toggle. Inert v1 — wired with the sequencer DSP.
    var topRight = document.createElement('div');
    topRight.id = 'ti-top-right-cluster';
    topRight.innerHTML =
      '<button class="ti-seq-play" id="ti-seq-play" title="Sequencer play — coming in Mark 2">' +
        '<svg viewBox="0 0 12 12" width="10" height="10"><path d="M3 2 L10 6 L3 10 Z" fill="currentColor"/></svg>' +
      '</button>' +
      '<div class="ti-seq-sync" id="ti-seq-sync" title="DAW transport sync — coming in Mark 2">' +
        '<span class="ti-sync-dot"></span>' +
        '<span class="ti-sync-label">SYNC</span>' +
      '</div>';
    hero.appendChild(topRight);

    // ─── SEQ pill + BPM display (BOTTOM-RIGHT) — Mark 2 placeholders ────
    // SEQ opens the sequencer menu (reverse / triplets / time-sigs / etc).
    // BPM shows current tempo with a lock for DAW-sync toggle.
    var bottomRight = document.createElement('div');
    bottomRight.id = 'ti-bottom-right-cluster';
    bottomRight.innerHTML =
      '<button class="ti-seq-pill" id="ti-seq-pill" title="Sequencer settings — coming in Mark 2">SEQ</button>' +
      '<div class="ti-bpm-display" id="ti-bpm-display" title="Tempo — click lock to toggle DAW sync, coming in Mark 2">' +
        '<span class="ti-bpm-value">120</span>' +
        '<span class="ti-bpm-label">BPM</span>' +
        '<span class="ti-bpm-lock" id="ti-bpm-lock">' +
          '<svg viewBox="0 0 10 12" width="7" height="9">' +
            '<rect x="1.5" y="5" width="7" height="6" rx="1" stroke="currentColor" stroke-width="1.2" fill="none"/>' +
            '<path d="M3 5 V3.5 C3 2.4 3.9 1.5 5 1.5 C6.1 1.5 7 2.4 7 3.5 V5" stroke="currentColor" stroke-width="1.2" fill="none"/>' +
          '</svg>' +
        '</span>' +
      '</div>';
    hero.appendChild(bottomRight);

    // ─── SLICES pill + pull-up drawer ────────────────────────────────────
    // Lives inside the bottom-strip cluster. Pill is the trigger; click
    // opens a drawer ABOVE it with GRID picker + sub-mode pills. The
    // pill is only visible in SLICE mode (.hidden in PITCH mode).
    // Reusable pattern — drop another wrap+pill+drawer into bottomPills
    // for any future "extra menu" we want.
    var slicesWrap = document.createElement('div');
    slicesWrap.id = 'ti-slices-wrap';
    slicesWrap.classList.add('hidden');  // PITCH default
    slicesWrap.innerHTML =
      '<div id="ti-slices-btn" title="Slicer settings">' +
        '<span class="ti-slices-label">SLICES</span>' +
        '<span class="ti-slices-count" id="ti-slices-count" style="display:none"></span>' +
      '</div>' +
      '<div id="ti-slicer-drawer">' +
        '<div class="ti-drawer-row" id="ti-grid-row">' +
          '<div class="ti-grid-pill" data-n="4">4</div>' +
          '<div class="ti-grid-pill" data-n="8">8</div>' +
          '<div class="ti-grid-pill" data-n="16">16</div>' +
          '<div class="ti-grid-pill" data-n="24">24</div>' +
          '<div class="ti-grid-pill" data-n="32">32</div>' +
        '</div>' +
        '<div class="ti-drawer-row" id="ti-submode-toggle">' +
          '<div class="ti-submode-pill active" data-sub="0">CHOP</div>' +
          '<div class="ti-submode-pill" data-sub="1">CHROMATIC</div>' +
          '<div class="ti-submode-pill" data-sub="2">RANDOM</div>' +
          '<div class="ti-submode-pill" data-sub="3">LAYER</div>' +
        '</div>' +
        '<div class="ti-drawer-row" id="ti-action-row">' +
          '<span class="ti-action-label">RANDOM:</span>' +
          '<div class="ti-action-btn" data-rand="5"  title="Random 5th per chop — picks -7, 0, or +7 semitones">5TH</div>' +
          '<div class="ti-action-btn" data-rand="7"  title="Random b7 per chop — picks -10, 0, or +10 semitones">7TH</div>' +
          '<div class="ti-action-btn" data-rand="12" title="Random octave per chop — picks -12, 0, or +12 semitones">OCT</div>' +
        '</div>' +
        '<div class="ti-drawer-row" id="ti-fade-row">' +
          '<span class="ti-action-label" title="Anti-click fade at slice boundaries">FADE:</span>' +
          '<input type="range" id="ti-fade-slider" min="0" max="50" step="0.1" value="5" title="Chop Fade 0-50ms">' +
          '<span id="ti-fade-value">5ms</span>' +
        '</div>' +
      '</div>';
    bottomPills.appendChild(slicesWrap);

    // Slice marker overlay container (positioned over the waveform).
    var sliceOverlays = document.createElement('div');
    sliceOverlays.id = 'ti-slice-overlays';
    hero.appendChild(sliceOverlays);

    // Scan-line viz canvas — child of sliceOverlays so it shares the same
    // stacking context as the chop markers. Preserved across redrawSliceOverlay
    // rebuilds via the detach/reattach pattern in that function.
    var scanVizCanvas = document.createElement('canvas');
    scanVizCanvas.id = 'ti-scan-viz-canvas';
    sliceOverlays.appendChild(scanVizCanvas);

    // Floating chop overlay (replaces the old cramped context menu —
    // panel sits over the whole UI in a fixed-position layer so it never
    // clips at the canvas edges and reads like the Effects modulation menu).
    var backdrop = document.createElement('div');
    backdrop.id = 'ti-chop-backdrop';
    document.body.appendChild(backdrop);

    var panel = document.createElement('div');
    panel.id = 'ti-chop-panel';
    panel.setAttribute('role', 'dialog');
    panel.setAttribute('aria-label', 'Chop settings');
    panel.setAttribute('data-warp', 'none');
    panel.innerHTML =
      // header — CHOP title centered, close button anchored top-right
      '<div class="ov-head">' +
        '<div class="name">CHOP<span class="num" id="ti-chop-num">01</span></div>' +
        '<div class="ov-close" id="ti-chop-close" title="Close">' +
          '<svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><path d="M5 5 L19 19 M19 5 L5 19"/></svg>' +
        '</div>' +
      '</div>' +
      // Mode pills — emblem-only (no text labels). Hover surfaces the mode
      // name via `title` for discoverability; otherwise the visuals speak
      // for themselves. SVG sizes bumped ~1.7× to fill the cell now that
      // the label row is gone, so buttons read as a clean symbol grid.
      '<div class="ov-modes">' +
        '<div class="ov-mode" data-mode="0" title="None">' +
          '<div class="emblem"><svg width="36" height="22" viewBox="0 0 22 14" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><path d="M2 7 L20 7"/></svg></div>' +
        '</div>' +
        '<div class="ov-mode" data-mode="1" title="Beats">' +
          '<div class="emblem"><svg width="36" height="26" viewBox="0 0 22 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 12 V5"/><path d="M8 13 V8"/><path d="M13 11 V4"/><path d="M18 12 V7"/></svg></div>' +
        '</div>' +
        '<div class="ov-mode" data-mode="2" title="Tones">' +
          '<div class="emblem"><svg width="40" height="22" viewBox="0 0 24 14" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><path d="M2 7 C5 1, 8 1, 12 7 S19 13, 22 7"/></svg></div>' +
        '</div>' +
        '<div class="ov-mode" data-mode="3" title="Texture">' +
          '<div class="emblem"><svg width="36" height="26" viewBox="0 0 22 16" fill="currentColor"><circle cx="3" cy="9" r="1.2"/><circle cx="7" cy="4" r="1.2"/><circle cx="9" cy="12" r="1.2"/><circle cx="13" cy="7" r="1.2"/><circle cx="15" cy="3" r="1.2"/><circle cx="18" cy="11" r="1.2"/><circle cx="20" cy="6" r="1.2"/></svg></div>' +
        '</div>' +
      '</div>' +
      // MOTION row — SCAN on/off pill + RATE vertical-drag display.
      // Lives between the mode emblems and the ADSR canvas (Layout C).
      '<div class="motion-row">' +
        '<span class="motion-label">MOTION</span>' +
        '<span class="scan-pill off" id="scan-pill">SCAN OFF</span>' +
        '<span class="rate-display dim" id="rate-display" ' +
              'title="Drag vertically to set scan rate. Rates &lt; 1.0 drop pitch (varispeed character). ' +
                     'For pitch-locked slow scan, set warp mode to TONES with stretch ratio = 1 / rate ' +
                     '(e.g. rate 0.5 + stretch 2.0).">' +
          '<span class="rate-label">RATE</span>' +
          '<span class="rate-value" id="rate-value">1.00\xd7</span>' +
        '</span>' +
      '</div>' +
      // ADSR envelope canvas: SVG path + 4 absolute-positioned handles.
      // Handles carry no numeric tooltips — the curve shape IS the readout.
      '<div class="ov-env" id="ti-env">' +
        '<svg class="ov-env-svg" id="ti-env-svg" viewBox="0 0 332 142" preserveAspectRatio="none">' +
          '<path class="ov-env-fill" id="ti-env-fill" d=""/>' +
          '<path class="ov-env-line" id="ti-env-line" d=""/>' +
        '</svg>' +
        '<div class="ov-env-handle" data-h="A"></div>' +
        '<div class="ov-env-handle" data-h="D"></div>' +
        '<div class="ov-env-handle" data-h="S"></div>' +
        '<div class="ov-env-handle" data-h="R"></div>' +
      '</div>' +
      // 3 emblem-knob controls (Volume / Pitch / Stretch) — no labels, glyph + value only.
      // Stretch is hidden when warp = none.
      '<div class="ov-ctrls">' +
        '<div class="ov-ctrl" data-ctrl="volume" title="volume — drag vertically">' +
          // Classic speaker + sound-wave arcs — universal "volume" language,
          // visually distinct from the stretch emblem's vertical bars. Waves
          // gently pulse outward as the idle animation (NOT value-driven).
          '<svg class="ov-emblem ov-emblem-vol" width="22" height="20" viewBox="0 0 22 20">' +
            '<rect x="2" y="7" width="3" height="6" fill="currentColor"/>' +
            '<path d="M 5 7 L 10 3 L 10 17 L 5 13 Z" fill="currentColor"/>' +
            '<path class="wave wave-1" d="M 12.5 7.5 Q 14.5 10 12.5 12.5" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>' +
            '<path class="wave wave-2" d="M 15.5 5 Q 18.5 10 15.5 15" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>' +
          '</svg>' +
          '<div class="ov-val">100%</div>' +
        '</div>' +
        '<div class="ov-ctrl" data-ctrl="pitch" title="pitch — drag vertically">' +
          // Tuning fork — matches the existing grain-engine PITCH emblem in
          // Terrain FX (forkW=0.35r, forkH=1.2r, handle+base dot). Prongs
          // vibrate gently as an idle animation, but the glyph is static
          // with respect to value so the layout never shifts.
          '<svg class="ov-emblem ov-emblem-pitch" width="22" height="20" viewBox="0 0 22 22" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round">' +
            '<path class="prong-l" d="M 8 3 Q 7.2 7 11 11"/>' +
            '<path class="prong-r" d="M 14 3 Q 14.8 7 11 11"/>' +
            '<line x1="11" y1="11" x2="11" y2="17"/>' +
            '<circle cx="11" cy="18" r="1.4" fill="currentColor" stroke="none"/>' +
          '</svg>' +
          '<div class="ov-val">+0 st</div>' +
        '</div>' +
        '<div class="ov-ctrl warp-only" data-ctrl="stretch" title="stretch — drag vertically">' +
          '<svg class="ov-emblem ov-emblem-stretch" width="22" height="20" viewBox="0 0 22 20" fill="currentColor" stroke="none">' +
            '<g class="group">' +
              '<rect x="2"  y="4" width="2" height="12" rx="0.8"/>' +
              '<rect x="7"  y="4" width="2" height="12" rx="0.8"/>' +
              '<rect x="13" y="4" width="2" height="12" rx="0.8"/>' +
              '<rect x="18" y="4" width="2" height="12" rx="0.8"/>' +
            '</g>' +
          '</svg>' +
          '<div class="ov-val">1.00</div>' +
        '</div>' +
      '</div>' +
      // ─── FX section (Mark 2 — per-chop FX independence) ──────────────
      // Header: small "FX" label + INDEPENDENT toggle pill + meta tag-line.
      // Grid: 6 chips below — Grain / Tape (4-state) / Space / Delay / Eq / June.
      // Grayed when INDEPENDENT is off (chop is inheriting the global chain).
      '<div class="ov-fx" id="ti-fx-section">' +
        '<div class="ov-fx-header">' +
          '<div class="ov-indy" id="ti-fx-indy">INDEPENDENT</div>' +
        '</div>' +
        '<div class="ov-fx-grid">' +
          '<div class="ov-fx-chip" data-fx="grain">GRAIN</div>' +
          '<div class="ov-fx-chip" data-fx="tape" id="ti-fx-tape">TAPE</div>' +
          '<div class="ov-fx-chip" data-fx="space">SPACE</div>' +
          '<div class="ov-fx-chip" data-fx="delay">DELAY</div>' +
          '<div class="ov-fx-chip" data-fx="eq">EQ</div>' +
          '<div class="ov-fx-chip" data-fx="june">JUNE</div>' +
        '</div>' +
      '</div>' +
      // actions
      '<div class="ov-actions">' +
        '<div class="group">' +
          '<div class="ov-act" data-act="rev">' +
            '<svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M9 14 L 4 9 L 9 4"/><path d="M4 9 H 16 A 4 4 0 0 1 20 13 V 20"/></svg>' +
            'REVERSE' +
          '</div>' +
          '<div class="ov-act" data-act="resetPitch">' +
            '<svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 12 A 9 9 0 1 0 6 5"/><path d="M3 3 V 8 H 8"/></svg>' +
            'RESET' +
          '</div>' +
          '<div class="ov-act danger" data-act="del">' +
            '<svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"><path d="M5 5 L 19 19 M 19 5 L 5 19"/></svg>' +
            'DELETE' +
          '</div>' +
        '</div>' +
      '</div>';
    document.body.appendChild(panel);
)TIHX")
      + juce::String (R"TIHX(
    // Initial state: empty.
    hero.classList.add('empty-state');

    wireInteractions();
    drawWaveform(); // draws nothing initially (no peaks)
  }

  // ── Note name helpers ─────────────────────────────────────────────────────
  var NOTE_NAMES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
  function midiToName (m) {
    if (m == null || isNaN(m)) return '—';
    m = Math.max(0, Math.min(127, Math.round(m)));
    var oct = Math.floor(m / 12) - 1;
    return NOTE_NAMES[m % 12] + oct;
  }

  // ── Native fn helper (capital J) ──────────────────────────────────────────
  function getNativeFn (name) {
    var bridge = window.Juce || window.juce;
    if (bridge && typeof bridge.getNativeFunction === 'function') {
      try { return bridge.getNativeFunction(name); } catch (_) {}
    }
    return null;
  }

  // ── JS PARAMS metadata + MODULATABLE list ────────────────────────────────
  // These entries mirror the C++ ModulationEngine ParamIndex enum (same order).
  // Used by the mod panel to populate the target dropdown and by updateModRings()
  // to decorate UI elements when an LFO is assigned to a param.
  var PARAMS = {
    grainSize:          { label: 'Grain Size',    section: 'grain',  min: 5,    max: 500,   default: 60,  unit: 'ms', desc: 'Grain length.', tip: '' },
    density:            { label: 'Density',        section: 'grain',  min: 1,    max: 100,   default: 50,  unit: '',   desc: 'Grains per second.', tip: '' },
    spray:              { label: 'Spray',          section: 'grain',  min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Randomise grain start position.', tip: '' },
    pitch:              { label: 'Pitch',          section: 'grain',  min: -12,  max: 12,    default: 0,   unit: 'st', desc: 'Grain pitch shift.', tip: '' },
    freeze:             { label: 'Freeze',         section: 'grain',  min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Freeze grain playhead.', tip: '' },
    wander:             { label: 'Wander',         section: 'grain',  min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Grain position wander.', tip: '' },
    grainFilter:        { label: 'Filter',         section: 'grain',  min: 0,    max: 100,   default: 100, unit: '',   desc: 'Grain filter cutoff.', tip: '' },
    mix:                { label: 'Mix',            section: 'grain',  min: 0,    max: 100,   default: 50,  unit: '%',  desc: 'Dry/wet mix.', tip: '' },
    wowFlutter:         { label: 'Wow/Flutter',    section: 'tape',   min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Tape wow and flutter depth.', tip: '' },
    saturation:         { label: 'Saturation',     section: 'tape',   min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Tape saturation.', tip: '' },
    hiss:               { label: 'Hiss',           section: 'tape',   min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Tape hiss level.', tip: '' },
    loopFeedback:       { label: 'Loop Feedback',  section: 'loop',   min: 0,    max: 100,   default: 50,  unit: '',   desc: 'Loop feedback level.', tip: '' },
    loopDegrade:        { label: 'Loop Degrade',   section: 'loop',   min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Loop degrade amount.', tip: '' },
    loopSpeed:          { label: 'Loop Speed',     section: 'loop',   min: 0,    max: 9,     default: 4,   unit: '',   desc: 'Tape loop speed.', tip: '' },
    spaceSize:          { label: 'Space Size',     section: 'space',  min: 0,    max: 100,   default: 50,  unit: '',   desc: 'Reverb room size.', tip: '' },
    spaceDecay:         { label: 'Space Decay',    section: 'space',  min: 0,    max: 100,   default: 50,  unit: '',   desc: 'Reverb decay time.', tip: '' },
    spaceTone:          { label: 'Space Tone',     section: 'space',  min: 0,    max: 100,   default: 50,  unit: '',   desc: 'Reverb tone.', tip: '' },
    spaceMix:           { label: 'Space Mix',      section: 'space',  min: 0,    max: 100,   default: 0,   unit: '%',  desc: 'Reverb wet/dry.', tip: '' },
    wireWow:            { label: 'Wire Wow',       section: 'wire',   min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Wire wow depth.', tip: '' },
    wireSaturation:     { label: 'Wire Sat',       section: 'wire',   min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Wire saturation.', tip: '' },
    wireHiss:           { label: 'Wire Hiss',      section: 'wire',   min: 0,    max: 100,   default: 0,   unit: '',   desc: 'Wire hiss level.', tip: '' },
    activeChopScanRate: {
      label: 'Scan Rate', section: 'scan',
      min: 0.1, max: 8.0, default: 1.0,
      unit: 'x', desc: "Active chop's scan rate (ping-pong speed).", tip: ''
    },
    activeChopScanWindow: {
      label: 'Scan Window', section: 'scan',
      min: 0.05, max: 1.0, default: 1.0,
      unit: '', desc: "Active chop's scan window (narrows ping-pong range).", tip: ''
    },
    chopFadeMs: {
      label: 'Chop Fade', section: 'slice',
      min: 0, max: 50, default: 5,
      unit: 'ms', desc: 'Anti-click fade-in/out at slice boundaries.', tip: ''
    }
  };

  // Ordered list — index must match C++ ModulationEngine::ParamIndex enum.
  // EQ bands are not individually listed here to keep the dropdown manageable;
  // add them when the mod panel gains EQ target support.
  var MODULATABLE = [
    'grainSize', 'density', 'spray', 'pitch', 'freeze', 'wander',
    'grainFilter', 'mix',
    'wowFlutter', 'saturation', 'hiss',
    'loopFeedback', 'loopDegrade', 'loopSpeed',
    'spaceSize', 'spaceDecay', 'spaceTone', 'spaceMix',
    'wireWow', 'wireSaturation', 'wireHiss',
    'activeChopScanRate', 'activeChopScanWindow'
  ];

  // ── Mod-state mirror (kept by restoreModState / updateModConfig calls) ────
  // Allows JS to know which params have LFO assignments without parsing JSON.
  var _modAssignments = [];  // [{source, target (string), depth, enabled}, ...]

  function _refreshModRings () {
    // Light up the RATE display when activeChopScanRate has an active assignment.
    var rateDisplay = document.getElementById('rate-display');
    if (rateDisplay) {
      var scanRateAssigned = _modAssignments.some(function (a) {
        return a.target === 'activeChopScanRate' && a.enabled && a.depth !== 0;
      });
      rateDisplay.classList.toggle('mod-active', scanRateAssigned);
    }
  }

  // Called by C++ timer every tick until page signals ready.
  window.restoreModState = function (jsonStr) {
    try {
      var cfg = JSON.parse(jsonStr);
      _modAssignments = (cfg.assignments || []);
      _refreshModRings();
    } catch (_) {}
  };

  // Called by C++ at ~60 Hz with LFO values; refresh rings here too.
  // index.html already defines window.updateLFOOutputs to push the live LFO
  // values into the global modState (drives the LFO dots + assigned-knob
  // modulation in updateModulation). This heroOverlay runs AFTER index.html,
  // so we must DELEGATE to that base handler instead of clobbering it —
  // otherwise modState.lfoOutputs stays [0,0,0] and all modulation freezes.
  var _terrainBaseUpdateLFO = window.updateLFOOutputs;
  window.updateLFOOutputs = function (lfo0, lfo1, lfo2, ph0, ph1, ph2) {
    if (typeof _terrainBaseUpdateLFO === 'function')
      _terrainBaseUpdateLFO (lfo0, lfo1, lfo2, ph0, ph1, ph2);
    _refreshModRings();
  };

  // ── State ─────────────────────────────────────────────────────────────────
  // Default pitchModeSlice — mirrors Slice struct defaults.
  function makePitchModeSliceDefault() {
    return {
      startSample: 0,
      endSample: 0,   // 0 = "not yet set" — treated as full sample in renderPitchBoundMarkers
      reverse: false,
      pitch: 0,
      warpMode: 0,
      stretchRatio: 1.0,
      attackMs: 30.0,
      releaseMs: 800.0,
      decayMs: 300.0,
      sustainLevel: 0.7,
      volume: 1.0,
      scanEnabled: false,
      scanRate: 1.0,
      scanWindow: 1.0,
      fxIndependent: false,
      fxGrain: false,
      fxTapeMachine: 0,
      fxSpace: false,
      fxDelay: false,
      fxEq: false,
      fxJune: false
    };
  }

  var state = {
    peaksMin: null,
    peaksMax: null,
    // Visual-only display gain. 1.0 = native amplitude (peak fills 95% of half-height).
    // Recomputed on sample load: loud samples (peak > 0.80) scale DOWN so peak hits 80%
    // of half-height, leaving ~20% top + bottom clearance for the dice button / chop
    // number labels / bottom button strip. Quiet samples (peak <= 0.80) keep peakScale=1.0
    // so one-shots and dynamic samples render at full native amplitude.
    peakScale: 1.0,
    progress: 0,    // 0..1 during loading; 1 when fully loaded
    loading: false,
    rootNote: 60,
    // Slicer state
    sliceMode: 0,           // 0 = PITCH (whole sample), 1 = SLICE
    sliceSubMode: 0,        // 0 = CHOP, 1 = CHROMATIC, 2 = RANDOM, 3 = LAYER
    holdMode: false,        // CHOP pill double-tap toggle. true = voices ignore
                            // note-off, play chop to natural end (MPC/FL latch).
                            // CONSTRAINTS: only allowed when sampleLoopMode === 0
                            // (1-SHOT) AND sliceMode === 1 (SLICE) AND sliceSubMode
                            // === 0 (CHOP). Switching to LOOP / PITCH / any other
                            // submode auto-clears HOLD. LOOP+HOLD = forever loop
                            // (intentionally blocked).
    sampleLoopMode: 0,      // 0 = 1-SHOT, 1 = LOOP. Mirror of C++ atomic,
                            // synced via getSampleLoopMode on init + each click.
    slices: [],             // [{start, end, reverse, pitch}, ...]
    activeSliceIndex: 0,    // active in CHROMATIC sub-mode
    gridN: 16,              // last grid count used
    sampleLengthSamples: 0, // total length of loaded sample (for marker positioning)
    sliceGlow: new Float32Array(256),  // per-slice glow [0..1], polled from C++ at ~60Hz
    pitchModeSlice: makePitchModeSliceDefault(),  // virtual slice for PITCH mode
    // Mark 2 Phase 1 visual-fix: per-layer JS mirrors.
    //   editingLayerIdx mirrors the C++ editingLayer atomic.
    //   layerStates[i] = null when layer i has never been populated (fresh).
    //     Otherwise: snapshot object produced by snapshotCurrentLayer().
    //   On pad click → snapshot leaving layer, restore entering layer.
    //   On sample-load → write to layerStates[editingLayerIdx] so the load
    //     populates the mirror naturally. Non-editing-layer loads (V2 preset
    //     migration) populate via window.onLayerSampleMirror.
    editingLayerIdx: 0,
    layerStates: [null, null, null, null]
  };

  // ── Per-layer state mirror helpers (Mark 2 Phase 1 visual fix) ────────────
  // Per-layer fields that snapshot/restore on pad click. Sample-load fields
  // (peaksMin/peaksMax) are immutable after load so shared refs are safe.
  // Slice list contains mutable objects → deep-copy on snapshot.
  function snapshotCurrentLayer () {
    return {
      peaksMin: state.peaksMin,
      peaksMax: state.peaksMax,
      peakScale: state.peakScale,
      sampleLengthSamples: state.sampleLengthSamples,
      rootNote: state.rootNote,
      sliceMode: state.sliceMode,
      sliceSubMode: state.sliceSubMode,
      holdMode: state.holdMode,
      sampleLoopMode: state.sampleLoopMode,
      slices: state.slices.map(function (s) { return Object.assign({}, s); }),
      activeSliceIndex: state.activeSliceIndex,
      gridN: state.gridN,
      pitchModeSlice: Object.assign({}, state.pitchModeSlice)
    };
  }

  // Restore JS state.* from a snapshot. null = fresh layer (factory defaults).
  function restoreLayerSnapshot (snap) {
    if (snap) {
      state.peaksMin            = snap.peaksMin;
      state.peaksMax            = snap.peaksMax;
      state.peakScale           = snap.peakScale;
      state.sampleLengthSamples = snap.sampleLengthSamples;
      state.rootNote            = snap.rootNote;
      state.sliceMode           = snap.sliceMode;
      state.sliceSubMode        = snap.sliceSubMode;
      state.holdMode            = snap.holdMode;
      state.sampleLoopMode      = snap.sampleLoopMode;
      state.slices              = snap.slices.map(function (s) { return Object.assign({}, s); });
      state.activeSliceIndex    = snap.activeSliceIndex;
      state.gridN               = snap.gridN;
      state.pitchModeSlice      = Object.assign({}, snap.pitchModeSlice);
    } else {
      // Fresh layer — match initial state.* defaults so empty-state prompt shows.
      state.peaksMin            = null;
      state.peaksMax            = null;
      state.peakScale           = 1.0;
      state.sampleLengthSamples = 0;
      state.rootNote            = 60;
      state.sliceMode           = 0;
      state.sliceSubMode        = 0;
      state.holdMode            = false;
      state.sampleLoopMode      = 0;
      state.slices              = [];
      state.activeSliceIndex    = 0;
      state.gridN               = 16;
      state.pitchModeSlice      = makePitchModeSliceDefault();
    }
  }

  // Re-render every per-layer UI surface from current state.*. Called by
  // switchEditingLayer after restoring the snapshot.
  function applyLayerStateToUI () {
    // Mode pill (PITCH / SLICE). setSliceModeUI also handles HOLD auto-clear,
    // SLICES drawer visibility, and redrawSliceOverlay().
    if (typeof setSliceModeUI === 'function') setSliceModeUI(state.sliceMode);
    // Sub-mode pill (CHOP / CHROMATIC / RANDOM / LAYER).
    if (typeof setSubModeUI === 'function') setSubModeUI(state.sliceSubMode);
    // 1-SHOT / LOOP pill.
    var loopMode = state.sampleLoopMode;
    document.querySelectorAll('#ti-play-mode-toggle .ti-play-pill').forEach(function (p) {
      p.classList.toggle('active', parseInt(p.dataset.play, 10) === loopMode);
    });
    // HOLD pill label + .hold-active class (CHOP pill doubles as HOLD).
    var chopPill = document.querySelector('#ti-submode-toggle .ti-submode-pill[data-sub="0"]');
    if (chopPill) {
      chopPill.textContent = state.holdMode ? 'HOLD' : 'CHOP';
      chopPill.classList.toggle('hold-active', !!state.holdMode);
    }
    if (typeof updateRootDisplay === 'function') updateRootDisplay();
    if (typeof drawWaveform === 'function') drawWaveform();
    if (typeof redrawSliceOverlay === 'function') redrawSliceOverlay();
  }

  // Public: switch the editing layer. Called by the A/B/C/D pad click handler.
  // Snapshot leaving layer → flip C++ atomic → restore entering layer → render.
  window.switchEditingLayer = function (newIdx) {
    newIdx = Math.max(0, Math.min(3, parseInt(newIdx, 10) || 0));
    if (newIdx === state.editingLayerIdx) return;

    // Snapshot the layer we are leaving (overwrites any stale mirror).
    state.layerStates[state.editingLayerIdx] = snapshotCurrentLayer();

    // Flip the C++ editingLayer atomic so native fn routing follows the UI.
    var fn = getNativeFn('setEditingLayer');
    if (fn) { try { fn(newIdx); } catch (_) {} }
    state.editingLayerIdx = newIdx;

    // Restore the target layer's mirror (or defaults if never populated).
    restoreLayerSnapshot(state.layerStates[newIdx]);

    // #hero CSS class management — fixes the has-sample / empty-state war.
    // The 10Hz pollLayerEmptyState would catch this eventually, but doing it
    // synchronously here avoids a visible flash of the wrong waveform.
    var hero = document.getElementById('hero');
    if (hero) {
      hero.classList.remove('drag-hover');
      hero.classList.remove('has-sample-missing');
      var hasSample = !!(state.peaksMin && state.peaksMin.length);
      hero.classList.toggle('has-sample', hasSample);
      hero.classList.toggle('empty-state', !hasSample);
    }

    // Re-render every per-layer surface from the restored state.
    applyLayerStateToUI();

    // C++ per-layer pitchModeSlice is the source of truth for warp/scan/ADSR
    // bounds (per-layer atomics). Re-pull it so JS mirror matches engine.
    if (typeof syncPitchSliceFromCpp === 'function') syncPitchSliceFromCpp();

    // C++ per-layer slice list is the source of truth for SLICE-mode chops.
    // Mirror only carries factory-default empty slices on first pad-touch;
    // pull from C++ so the chop overlay + lab cards reflect the layer's data.
    // getSlicesJson routes through editingLayer in C++ which we just flipped.
    var fnSlices = getNativeFn('getSlicesJson');
    if (fnSlices) {
      try {
        var r = fnSlices();
        if (r && typeof r.then === 'function') r.then(applySlicesJson);
        else applySlicesJson(r);
      } catch (_) {}
    }
  };

  // Receives sample peaks for ANY layer (editing or non-editing) so:
  //   1. V2 preset migration populates all 4 mirrors at load time
  //      (loadSampleIntoLayer fires this for each layer with peaks + meta only).
  //   2. Editor reopen populates all 4 mirrors from the per-layer cache
  //      (getAllLayerPayloads fires this with peaks + meta + mode atomics).
  // The `info` payload's mode/root fields are OPTIONAL — when present they
  // override the factory defaults so the mirror reflects the layer's true
  // current state (path 2 above). When absent, defaults are used (path 1).
  // Editing-layer loads ALSO fire onSampleLoaded (which updates state.*
  // directly); this callback only writes to the mirror array.
  window.onLayerSampleMirror = function (layerIdx, info) {
    layerIdx = Math.max(0, Math.min(3, parseInt(layerIdx, 10) || 0));
    if (!info) return;
    // Treat empty-object / no-filename payloads as "layer has no sample".
    // getAllLayerPayloads emits an empty object {} for unloaded layers.
    var hasSample = !!(info.peaksMin && info.peaksMax && info.lengthSamples);
    if (!hasSample) {
      state.layerStates[layerIdx] = null;
      return;
    }
    // Helper: parse a numeric field from info with a fallback default.
    function num (key, dflt) {
      var v = parseFloat(info[key]);
      return isFinite(v) ? v : dflt;
    }
    function intf (key, dflt) {
      var v = parseInt(info[key], 10);
      return isFinite(v) ? v : dflt;
    }
    state.layerStates[layerIdx] = {
      peaksMin: info.peaksMin || null,
      peaksMax: info.peaksMax || null,
      peakScale: (function () {
        // Replicate peakScale computation from onSampleLoaded so mirrored
        // non-editing layers render at the correct scale when later visited.
        var ps = 1.0;
        var mn = info.peaksMin, mx = info.peaksMax;
        if (mn && mx && mn.length === mx.length) {
          var maxAbs = 0;
          for (var i = 0; i < mn.length; ++i) {
            var a = mx[i]; if (a < 0) a = -a;
            var b = mn[i]; if (b < 0) b = -b;
            if (a > maxAbs) maxAbs = a;
            if (b > maxAbs) maxAbs = b;
          }
          if (maxAbs > 0.70) ps = 0.70 / maxAbs;
        }
        return ps;
      })(),
      sampleLengthSamples: intf('lengthSamples', 0),
      // Mode/root atomics: take from rich payload if present (editor reopen),
      // else fall back to factory defaults (mid-session load path).
      rootNote:           intf('rootMidiNote',     60),
      sliceMode:          intf('sliceMode',        0),
      sliceSubMode:       0,        // JS-only state; not in C++ payload
      holdMode:           false,    // global atomic; restored separately
      sampleLoopMode:     intf('sampleLoopMode',   0),
      slices:             [],       // hydrated by switchEditingLayer via getSlicesJson
      activeSliceIndex:   intf('activeSliceIndex', 0),
      gridN:              16,
      pitchModeSlice:     makePitchModeSliceDefault()
    };
  };

  // ── Pitch-mode slice helpers ───────────────────────────────────────────────
  // Returns state.pitchModeSlice for idx==-1, state.slices[idx] otherwise.
  function getSliceData (idx) {
    return idx === -1 ? state.pitchModeSlice : state.slices[idx];
  }

  // Apply a pitchSlice JSON object (from getPitchSliceJson native fn) into state.
  function applyPitchSliceJson (json) {
    if (!json || typeof json !== 'string') return;
    try {
      var s = JSON.parse(json);
      if (!s || typeof s !== 'object') return;
      var wm = parseInt(s.warpMode, 10);
      var sr = parseFloat(s.stretchRatio);
      var am = parseFloat(s.attackMs);
      var rm = parseFloat(s.releaseMs);
      var dm = parseFloat(s.decayMs);
      var sl = parseFloat(s.sustainLevel);
      var vl = parseFloat(s.volume);
      var scRt = parseFloat(s.scanRate);
      var scWn = parseFloat(s.scanWindow);
      var ssRaw = parseInt(s.startSample, 10);
      var seRaw = parseInt(s.endSample,   10);
      state.pitchModeSlice = {
        startSample:  isFinite(ssRaw) && ssRaw >= 0 ? ssRaw : 0,
        endSample:    isFinite(seRaw) && seRaw >  0 ? seRaw : 0,
        reverse:      !!s.reverse,
        pitch:        parseFloat(s.pitch) || 0,
        warpMode:     (isFinite(wm) && wm >= 0 && wm <= 3) ? wm : 0,
        stretchRatio: isFinite(sr) ? Math.max(0.1, Math.min(15.0, sr)) : 1.0,
        attackMs:     isFinite(am) ? am : -1,
        releaseMs:    isFinite(rm) ? rm : -1,
        decayMs:      isFinite(dm) ? Math.max(0, Math.min(2000, dm)) : 0,
        sustainLevel: isFinite(sl) ? Math.max(0, Math.min(1, sl)) : 1,
        volume:       isFinite(vl) ? Math.max(0, Math.min(2, vl)) : 1,
        scanEnabled:  !!s.scanEnabled,
        scanRate:     (isFinite(scRt) && scRt >= 0.05) ? Math.max(0.1, Math.min(8.0, scRt)) : 1.0,
        scanWindow:   (isFinite(scWn) && scWn >= 0.04) ? Math.max(0.05, Math.min(1.0, scWn)) : 1.0,
        fxIndependent: false,
        fxGrain: false, fxTapeMachine: 0,
        fxSpace: false, fxDelay: false, fxEq: false, fxJune: false
      };
    } catch (_) {}
  }

  // Pull pitchModeSlice from C++ and refresh. Called after sample load and on init.
  function syncPitchSliceFromCpp () {
    var fn = getNativeFn('getPitchSliceJson');
    if (!fn) return;
    try {
      var r = fn();
      if (r && typeof r.then === 'function') {
        r.then(function (json) {
          applyPitchSliceJson(json);
          // Re-render pitch markers with updated startSample/endSample from C++.
          if (state.sliceMode === 0) redrawSliceOverlay();
        });
      } else {
        applyPitchSliceJson(r);
        if (state.sliceMode === 0) redrawSliceOverlay();
      }
    } catch (_) {}
  }

  // ── Waveform drawing ──────────────────────────────────────────────────────
  function drawWaveform () {
    var c = document.getElementById('waveform-canvas');
    if (!c) return;
    var dpr = window.devicePixelRatio || 1;
    var rect = c.getBoundingClientRect();
    if (rect.width === 0 || rect.height === 0) return;
    c.width  = Math.round(rect.width * dpr);
    c.height = Math.round(rect.height * dpr);
    var ctx = c.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, rect.width, rect.height);

    if (!state.peaksMin || !state.peaksMax) return;

    var w = rect.width, h = rect.height;
    var cy = h / 2;
    var n = state.peaksMin.length;
    if (n === 0) return;
    var visibleN = Math.max(1, Math.floor(n * Math.max(0.001, Math.min(1, state.progress))));
    // Display gain — 0.95 = fixed top/bottom margin; peakScale = peak-aware shrink for
    // loud samples (1.0 = no change). Quiet samples keep full visual amplitude.
    var vGain = cy * 0.95 * (state.peakScale || 1.0);

    // Filled body (mirrored around centerline)
    ctx.fillStyle = 'rgba(245, 243, 255, 0.10)';
    ctx.beginPath();
    ctx.moveTo(0, cy);
    var i;
    for (i = 0; i < visibleN; i++) {
      var x = (i / Math.max(1, n - 1)) * w;
      var yMax = cy - state.peaksMax[i] * vGain;
      ctx.lineTo(x, yMax);
    }
    for (i = visibleN - 1; i >= 0; i--) {
      var x2 = (i / Math.max(1, n - 1)) * w;
      var yMin = cy - state.peaksMin[i] * vGain;
      ctx.lineTo(x2, yMin);
    }
    ctx.closePath();
    ctx.fill();

    // Top edge stroke
    ctx.strokeStyle = 'rgba(245, 243, 255, 0.92)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (i = 0; i < visibleN; i++) {
      var xx = (i / Math.max(1, n - 1)) * w;
      var ym = cy - state.peaksMax[i] * vGain;
      if (i === 0) ctx.moveTo(xx, ym); else ctx.lineTo(xx, ym);
    }
    ctx.stroke();

    // Bottom edge stroke
    ctx.strokeStyle = 'rgba(245, 243, 255, 0.85)';
    ctx.beginPath();
    for (i = 0; i < visibleN; i++) {
      var xx2 = (i / Math.max(1, n - 1)) * w;
      var ym2 = cy - state.peaksMin[i] * vGain;
      if (i === 0) ctx.moveTo(xx2, ym2); else ctx.lineTo(xx2, ym2);
    }
    ctx.stroke();
  }

  // ── Slicer helpers ────────────────────────────────────────────────────────
  function setSliceModeUI (modeIdx) {
    state.sliceMode = modeIdx;
    // Switching to PITCH mode: fade out any lingering numeric scan-viz
    // entries from the prior slice-mode session so they don't ghost on
    // the pitch waveform. drawScanViz's slice-mode loop is already gated
    // on !isPitchMode (defense in depth), but clearing opacityTarget here
    // ensures clean state if the user toggles back to SLICE later.
    if (modeIdx !== 1) {
      Object.keys(_scanInterp).forEach(function (k) {
        if (k !== 'pitch' && _scanInterp[k]) {
          _scanInterp[k].opacityTarget = 0;
          _scanInterp[k].truth         = null;
          _scanInterp[k].velocity      = 0;
        }
      });
    }
    // HOLD is a slice-mode CHOP-submode feature; leaving slice mode (i.e.
    // entering PITCH) auto-clears it. See state.holdMode for the full
    // constraint table.
    if (modeIdx !== 1 && state.holdMode) {
      state.holdMode = false;
      var chopPill = document.querySelector('#ti-submode-toggle .ti-submode-pill[data-sub="0"]');
      if (chopPill) {
        chopPill.textContent = 'CHOP';
        chopPill.classList.remove('hold-active');
      }
      var holdFn = getNativeFn('setHoldMode');
      if (holdFn) { try { holdFn(false); } catch (_) {} }
    }
    document.querySelectorAll('#ti-mode-toggle .ti-mode-pill').forEach(function (p) {
      p.classList.toggle('active', (p.dataset.mode === 'SLICE') === (modeIdx === 1));
    });
    // Show/hide the SLICES pill in the bottom strip; PITCH mode hides
    // it entirely so the strip is identical to its pre-slicer look.
    var wrap = document.getElementById('ti-slices-wrap');
    if (wrap) wrap.classList.toggle('hidden', modeIdx !== 1);
    // PITCH mode also closes the drawer if it was left open.
    if (modeIdx !== 1) closeSlicerDrawer();
    redrawSliceOverlay();
  }

  function setSubModeUI (subIdx) {
    state.sliceSubMode = subIdx;
    document.querySelectorAll('#ti-submode-toggle .ti-submode-pill').forEach(function (p) {
      p.classList.toggle('active', parseInt(p.dataset.sub, 10) === subIdx);
    });
    redrawSliceOverlay();  // active-slice ring visibility depends on this
  }

  // Update the SLICES pill label with the current chop count, and
  // highlight whichever GRID pill matches that count (so the drawer
  // shows the user what's currently applied without extra state).
  function updateChopCount () {
    var n = state.slices.length;
    var countEl = document.getElementById('ti-slices-count');
    if (countEl) {
      if (n > 0) {
        countEl.textContent = ' ' + n;   // "SLICES 16" — no separator dot (rendered as Â· under Latin-1 fallback and the dot is visual noise anyway)
        countEl.style.display = '';
      } else {
        countEl.textContent = '';
        countEl.style.display = 'none';
      }
    }
    document.querySelectorAll('#ti-grid-row .ti-grid-pill').forEach(function (p) {
      p.classList.toggle('active', parseInt(p.dataset.n, 10) === n);
    });
  }

  function openSlicerDrawer () {
    var btn  = document.getElementById('ti-slices-btn');
    var dr   = document.getElementById('ti-slicer-drawer');
    var hero = document.getElementById('hero');
    if (btn)  btn.classList.add('open');
    if (dr)   dr.classList.add('open');
    if (hero) hero.classList.add('drawer-open');
  }
  function closeSlicerDrawer () {
    var btn  = document.getElementById('ti-slices-btn');
    var dr   = document.getElementById('ti-slicer-drawer');
    var hero = document.getElementById('hero');
    if (btn)  btn.classList.remove('open');
    if (dr)   dr.classList.remove('open');
    if (hero) hero.classList.remove('drawer-open');
  }
  function toggleSlicerDrawer () {
    var dr = document.getElementById('ti-slicer-drawer');
    if (dr && dr.classList.contains('open')) closeSlicerDrawer();
    else openSlicerDrawer();
  }

  // Parse the JSON-encoded slice list returned from C++ and refresh the UI.
  function applySlicesJson (json) {
    if (!json || typeof json !== 'string') return;
    try {
      var obj = JSON.parse(json);
      var arr = (obj && obj.slices) ? obj.slices : [];
      // Snapshot OLD slice bounds before overwriting, so we can detect which
      // chops had their start/end change and reset the scan-viz interpolator
      // for just those chops. Without this, a marker drag leaves the previous
      // chop's scan position cached and the white scan line draws at the OLD
      // chop's position multiplied by the NEW chop width, appearing "stuck."
      var _oldBounds = (state.slices || []).map(function (s) {
        return { start: s ? s.start : -1, end: s ? s.end : -1 };
      });
      state.slices = arr.map(function (s) {
        // Preserve warp fields across round-trips. Earlier this mapper
        // dropped warpMode + stretchRatio, so any regular drag that
        // returned the slice list via moveSliceBoundary would silently
        // reset the JS-side warp state — the T letter and stretch label
        // would vanish even though the C++ engine kept stretching. JS
        // state must mirror C++.
        var wm = parseInt(s.warpMode, 10);
        var sr = parseFloat(s.stretchRatio);
        // Per-chop ADSR — same inheritance sentinel as C++. -1 = follow global.
        var am = parseFloat(s.attackMs);
        var rm = parseFloat(s.releaseMs);
        // Full ADSR additions (always concrete, no inheritance sentinel).
        var dm = parseFloat(s.decayMs);
        var sl = parseFloat(s.sustainLevel);
        var vl = parseFloat(s.volume);
        // Scan mode fields (Task 2 — must be extracted here or the C++ → JS
        // round-trip silently overwrites any JS-side knob writes; see the
        // WebSliderRelay gotcha in project memory).
        var scEn = !!s.scanEnabled;
        var scRt = parseFloat(s.scanRate);
        var scWn = parseFloat(s.scanWindow);
        // Per-chop FX independence (Mark 2). All default to false / 0 so
        // legacy presets without these keys load as inheriting/clean.
        var tm = parseInt(s.fxTapeMachine, 10);
        return {
          start:        parseInt(s.start, 10) || 0,
          end:          parseInt(s.end,   10) || 0,
          reverse:      !!s.reverse,
          pitch:        parseFloat(s.pitch) || 0,
          warpMode:     (isFinite(wm) && wm >= 0 && wm <= 3) ? wm : 0,
          stretchRatio: (isFinite(sr) ? Math.max(0.1, Math.min(15.0, sr)) : 1.0),
          attackMs:     isFinite(am) ? am : -1,
          releaseMs:    isFinite(rm) ? rm : -1,
          decayMs:      isFinite(dm) ? Math.max(0,  Math.min(2000, dm)) : 0,
          sustainLevel: isFinite(sl) ? Math.max(0,  Math.min(1,    sl)) : 1,
          volume:       isFinite(vl) ? Math.max(0,  Math.min(2,    vl)) : 1,
          scanEnabled:  scEn,
          scanRate:     (isFinite(scRt) && scRt >= 0.05) ? Math.max(0.1, Math.min(8.0, scRt)) : 1.0,
          scanWindow:   (isFinite(scWn) && scWn >= 0.04) ? Math.max(0.05, Math.min(1.0, scWn)) : 1.0,
          fxIndependent: !!s.fxIndependent,
          fxGrain:       !!s.fxGrain,
          fxTapeMachine: (isFinite(tm) && tm >= 0 && tm <= 3) ? tm : 0,
          fxSpace:       !!s.fxSpace,
          fxDelay:       !!s.fxDelay,
          fxEq:          !!s.fxEq,
          fxJune:        !!s.fxJune
        };
      });
      // Clamp activeSliceIndex.
      if (state.activeSliceIndex >= state.slices.length)
        state.activeSliceIndex = Math.max(0, state.slices.length - 1);
      // Scan-viz reset: for each chop whose bounds changed, drop the cached
      // _scanInterp entry so the next 60Hz poll repopulates from C++ against
      // the new chop layout. Without this the white scan line stays at the
      // pre-drag position until the user replays the chop. Voice activeConfig
      // is frozen at startNote so the AUDIO still completes the old range —
      // visual just clears so it isn't misleading.
      for (var _si = 0; _si < state.slices.length; ++_si) {
        var _ob = _oldBounds[_si];
        var _nb = state.slices[_si];
        if (!_ob || _ob.start !== _nb.start || _ob.end !== _nb.end) {
          if (_scanInterp[_si]) {
            _scanInterp[_si].truth         = null;
            _scanInterp[_si].velocity      = 0;
            _scanInterp[_si].opacityTarget = 0;
          }
        }
      }
      updateChopCount();
      redrawSliceOverlay();
    } catch (_) {}
  }

  // Render slice markers + bodies + active overlay on top of the waveform.
  // Re-runs on slice list change, mode change, sub-mode change, sample change,
  // active-slice change, and on window resize.
  function redrawSliceOverlay () {
    var hero = document.getElementById('hero');
    var waveCanvas = document.getElementById('waveform-canvas');
    var overlays = document.getElementById('ti-slice-overlays');
    if (!hero || !waveCanvas || !overlays) return;

    // Preserve the scan canvas across innerHTML wipe — it lives inside
    // overlays but must survive every marker rebuild.
    var scanCanvas = document.getElementById('ti-scan-viz-canvas');
    if (scanCanvas && scanCanvas.parentNode === overlays) overlays.removeChild(scanCanvas);
    overlays.innerHTML = '';
    if (scanCanvas) overlays.appendChild(scanCanvas);

    if (state.sliceMode !== 1 || state.slices.length === 0 || state.sampleLengthSamples <= 0) {
      document.body.classList.remove('ti-slicer-active');
      // In PITCH mode, draw in/out bound markers if a sample is loaded.
      if (state.sliceMode === 0 && state.sampleLengthSamples > 0) renderPitchBoundMarkers(overlays);
      return;
    }
    document.body.classList.add('ti-slicer-active');

    // Position the overlay container over the waveform canvas region,
    // BUT clamp the bottom so slice bodies don't extend over the bottom
    // strip. Without this clamp the slice body's `top:0; bottom:0`
    // makes it the full hero height — and any click on the XY readout
    // (or future bottom-strip elements that sit at z-index < 4) gets
    // intercepted by the slice body underneath.
    var heroRect      = hero.getBoundingClientRect();
    var waveRect      = waveCanvas.getBoundingClientRect();
    var BOTTOM_RESERVE = 50; // matches bottom strip + breathing room
    overlays.style.top    = (waveRect.top - heroRect.top) + 'px';
    overlays.style.height = Math.max(0, waveRect.height - BOTTOM_RESERVE) + 'px';

    var W = waveRect.width;
    var H = Math.max(0, waveRect.height - BOTTOM_RESERVE);
    // Per-chop waveform canvas uses the FULL hero height (not the reduced
    // body height) so its centerline matches the PITCH-mode waveform's
    // centerline (heroHeight/2). The body/markers still use H (with the
    // 50 px reserve so they don't intercept the bottom strip), but the
    // <canvas> child of each body is allowed to overflow downward by
    // BOTTOM_RESERVE — pointer-events:none on the canvas means the
    // overflow doesn't steal clicks, and the bottom-pills z-index (5) sits
    // above the slice overlay (z-index 4) so the overflowed canvas is
    // visually covered by the bottom strip. User asked: "we should just
    // keep it at the same position as the pitch and just add the warp
    // markers" — this gives that exact behavior.
    var H_canvas = waveRect.height;
    var totalSamples = state.sampleLengthSamples;
    var isChromatic  = state.sliceSubMode === 1;
    var activeIdx    = state.activeSliceIndex;

    // SCALE-TO-FIT CUMULATIVE LAYOUT ──────────────────────────────────────
    // Each chop's visual width is proportional to (sourceWidth * stretchRatio).
    // Total chop widths sum to W (the canvas width) — so all chops always
    // fit, and stretched chops visually dominate proportionally without
    // overlapping neighbors or extending past the canvas edge. This mirrors
    // Ableton's clip-warp UX: total clip width constant, internal warp
    // markers redistribute audio between them.
    // Stretch only counts visually when warp is engaged. Right-clicking a
    // chop and choosing Warp: None used to leave the chop visually stretched
    // (because the cumulative-layout used the stored stretchRatio regardless
    // of warpMode) — user reported the body still looked chopped and the
    // "2.16x" label still showed. Resolve by reading the EFFECTIVE stretch
    // ratio: stored value when warp engaged, 1.0 when None. stretchRatio
    // stays in slice state so re-enabling warp restores the previous value.
    function effectiveSr (s) {
      var wm = (typeof s.warpMode === 'number') ? s.warpMode : 0;
      if (!wm) return 1.0;
      var sr = (typeof s.stretchRatio === 'number') ? s.stretchRatio : 1.0;
      return sr;
    }

    var totalWeight = 0;
    state.slices.forEach(function (s) {
      var sr = effectiveSr(s);
      totalWeight += Math.max(1, (s.end - s.start)) * sr;
    });
    if (totalWeight <= 0) totalWeight = 1;

    var cumulative = 0;
    var chopLayouts = state.slices.map(function (s, i) {
      var sr = effectiveSr(s);
      var weight = Math.max(1, (s.end - s.start)) * sr;
      var visualWidth = (weight / totalWeight) * W;
      var layout = {
        srcStart: s.start,
        srcEnd: s.end,
        stretchRatio: sr,
        visualLeft: cumulative,
        visualWidth: visualWidth,
        isStretched: Math.abs(sr - 1.0) > 0.005
      };
      cumulative += visualWidth;
      return layout;
    });
    // Expose to gesture handlers so cursor↔source mapping respects the
    // cumulative/weighted layout (otherwise drags use uniform mapping and
    // markers snap-back after commit because the new layout placement
    // disagrees with where the cursor said the marker was). Also store
    // the current waveform-canvas pixel width — used by predictMarkerVisualX
    // to simulate the post-commit layout in real time.
    state.chopLayouts        = chopLayouts;
    state.waveformPixelWidth = W;

    state.slices.forEach(function (s, i) {
      var layout = chopLayouts[i];
      var leftPx  = layout.visualLeft;
      var widthPx = layout.visualWidth;
      if (widthPx < 1) return;

      var isStretched = layout.isStretched;

      // Body (clickable / draggable region) sits at its cumulative position.
      var body = document.createElement('div');
      body.className = 'ti-slice-body' + (isStretched ? ' is-stretched' : '');
      body.style.left  = leftPx  + 'px';
      body.style.width = widthPx + 'px';
      body.dataset.idx = i;
      attachSliceGestures(body, i);
      overlays.appendChild(body);

      // Per-chop waveform canvas — renders THIS chop's source segment
      // scaled to the chop's visualWidth, so the waveform itself visually
      // stretches with the chop. This is the Ableton-warp visual.
      var wfCanvas = document.createElement('canvas');
      wfCanvas.className = 'ti-slice-waveform';
      body.appendChild(wfCanvas);
      drawChopWaveform(wfCanvas, widthPx, H_canvas, layout.srcStart, layout.srcEnd, totalSamples);

      // Hide all body overlays (pitch meter, REV tag, warp letter, stretch
      // label) when the chop is too narrow to fit them legibly. Avoids the
      // cramped overlap that happens when many chops squeeze the layout or
      // when one chop is heavily compressed by stretching its neighbors.
      var SMALL_CHOP_PX = 50;
      var hideOverlays = widthPx < SMALL_CHOP_PX;

      // Pitch meter — inline horizontal bar + number, only when pitch != 0.
      // Bar centers on a 0-semitone midline; fills right for positive,
      // left for negative, length proportional to |pitch| / 12. Capped at
      // half the bar width so range ±12 maps cleanly to the edges.
      if (s.pitch && s.pitch !== 0 && !hideOverlays) {
        var meter = document.createElement('div');
        meter.className = 'ti-slice-pitch-meter';
        var num = document.createElement('div');
        num.className = 'ti-slice-pitch-num';
        num.textContent = (s.pitch > 0 ? '+' : '') + (s.pitch | 0);
        var bar = document.createElement('div');
        bar.className = 'ti-slice-pitch-bar';
        var fill = document.createElement('div');
        fill.className = 'ti-slice-pitch-bar-fill';
        var pct = Math.min(50, (Math.abs(s.pitch) / 12) * 50);
        if (s.pitch > 0) {
          fill.style.left  = '50%';
          fill.style.width = pct + '%';
        } else {
          fill.style.right = '50%';
          fill.style.width = pct + '%';
        }
        bar.appendChild(fill);
        meter.appendChild(num);
        meter.appendChild(bar);
        body.appendChild(meter);
      }
      // Reverse letter — bottom-right of chop body, sits next to the warp
      // letter (T/B/X) when present. Same width as the warp letter (14px) so
      // they line up visually. Pushed left by 18px (14 + 4 gap) when a warp
      // letter is also being rendered, otherwise hugs the right corner.
      if (s.reverse && !hideOverlays) {
        var rev = document.createElement('div');
        rev.className = 'ti-slice-rev-letter';
        rev.textContent = 'R';
        rev.title = 'Reversed playback';
        // Sit a few px to the left of the warp letter (T/B/X is at right:4px
        // and 14px wide → R's right edge at ~22px puts it just past T's left).
        rev.style.right = (s.warpMode && s.warpMode > 0) ? '22px' : '6px';
        body.appendChild(rev);
      }

      // Warp mode letter — top-right corner. Visible only when warpMode > 0.
      // Click cycles to next mode (Phase 1 only ships Tones, so the cycle is
      // currently Tones -> None; Phase 2 expands to None -> B -> T -> X).
      if (s.warpMode && s.warpMode > 0 && !hideOverlays) {
        var wl = document.createElement('div');
        wl.className = 'ti-slice-warp-letter';
        var letters = { 1: 'B', 2: 'T', 3: 'X' };
        wl.textContent = letters[s.warpMode] || '';
        wl.title = ({ 1: 'Beats (Phase 2)', 2: 'Tones', 3: 'Texture (Phase 2)' })[s.warpMode] || 'Warp';
        // Eat mousedown so the body's drag handler doesn't fire.
        wl.addEventListener('mousedown', function (ev) {
          if (ev.button !== 0) return;
          ev.preventDefault();
          ev.stopPropagation();
        });
        wl.addEventListener('click', function (ev) {
          ev.stopPropagation();
          // 3-way cycle: Beats (1) -> Tones (2) -> None (0) -> Beats ...
          // Texture lands in the next commit alongside its dedicated engine.
          var current = s.warpMode | 0;
          var next;
          if      (current === 1) next = 2;   // Beats   -> Tones
          else if (current === 2) next = 0;   // Tones   -> None
          else                    next = 1;   // None    -> Beats
          s.warpMode = next;
          var fn = getNativeFn('setSliceWarpMode');
          if (fn) { try { fn(i, next); } catch (_) {} }
          redrawSliceOverlay();
        });
        body.appendChild(wl);
      }

      // Stretch ratio label — visible only when WARP is engaged AND stretched
      // away from unity. Gating on s.warpMode > 0 (in addition to ratio) so
      // right-click → Warp: None instantly hides the label even if the
      // user previously stretched the chop. The stretch value is preserved
      // in slice state so re-engaging Tones/Beats restores the visual.
      if (s.warpMode > 0
          && s.stretchRatio && Math.abs(s.stretchRatio - 1.0) > 0.005
          && !hideOverlays) {
        var sl = document.createElement('div');
        sl.className = 'ti-slice-stretch-label';
        sl.textContent = s.stretchRatio.toFixed(2) + 'x';
        body.appendChild(sl);
      }

      // Marker (left edge — index label sits at top). In the cumulative
      // layout, the marker for chop i sits at chop i's visualLeft —
      // exactly the boundary between chop i-1 and chop i.
      if (i > 0) {
        var marker = document.createElement('div');
        marker.className = 'ti-slice-marker';
        marker.style.left = layout.visualLeft + 'px';
        var label = document.createElement('div');
        label.className = 'ti-slice-label';
        label.textContent = (i + 1);
        marker.appendChild(label);
        attachMarkerDrag(marker, i);
        overlays.appendChild(marker);
      } else {
        // Slice 1 still shows its label at left edge of waveform.
        var label0 = document.createElement('div');
        label0.className = 'ti-slice-label';
        label0.textContent = '1';
        label0.style.position = 'absolute';
        label0.style.left = '4px';
        label0.style.top = '2px';
        overlays.appendChild(label0);
      }
    });
  }

  // ── Pitch-mode IN/OUT bound markers ───────────────────────────────────────
  // Renders two draggable markers (IN / OUT) on the waveform overlay when
  // PITCH mode is active. Called by redrawSliceOverlay() with the overlays
  // container already cleared. pitchModeSlice.startSample / endSample drive
  // the positions; default endSample=0 means "full sample" (uses sampleLen).
  //
  // Drag state is module-level so both mousemove/mouseup share it reliably.
  var _pitchBoundDrag = null;   // { which:'start'|'end', overlayRect, W }

  function renderPitchBoundMarkers (overlays) {
    if (!overlays) overlays = document.getElementById('ti-slice-overlays');
    if (!overlays) return;

    // Remove any stale markers and dimmers left from a previous call
    overlays.querySelectorAll('.ti-pitch-bound-marker').forEach(function(el) { el.remove(); });
    overlays.querySelectorAll('.ti-pitch-bound-dim').forEach(function(el) { el.remove(); });;

    var totalSamples = state.sampleLengthSamples;
    if (totalSamples <= 0) return;

    var ps = state.pitchModeSlice;
    if (!ps) return;

    // Position the overlay over the waveform in PITCH mode (the SLICE path
    // sets top/height after the early return we added; PITCH mode doesn't go
    // through that block so we set it here to give markers the right area).
    // Overlays keeps the BOTTOM_RES2 safe zone so markers stay above the
    // bottom button row; the dim divs override with the full wave height so
    // the gray-out visually covers the full waveform (bottom buttons sit at
    // z-index 5 so they cover the dim below them naturally).
    var hero = document.getElementById('hero');
    var waveCanvas = document.getElementById('waveform-canvas');
    var BOTTOM_RES2 = 50;
    var fullWaveHeightPx = 0;
    if (hero && waveCanvas) {
      var heroRect2 = hero.getBoundingClientRect();
      var waveRect2 = waveCanvas.getBoundingClientRect();
      fullWaveHeightPx = waveRect2.height;
      overlays.style.top    = (waveRect2.top - heroRect2.top) + 'px';
      overlays.style.height = Math.max(0, waveRect2.height - BOTTOM_RES2) + 'px';
    }

    // Resolve effective bounds — endSample=0 means "not yet set, use full sample".
    var startS = ps.startSample || 0;
    var endS   = (ps.endSample && ps.endSample > 0) ? ps.endSample : totalSamples;

    var W = overlays.clientWidth;
    if (W <= 0) return;

    var startNorm = Math.max(0, Math.min(1, startS / totalSamples));
    var endNorm   = Math.max(0, Math.min(1, endS   / totalSamples));

    function makeMarker (cls, norm) {
      var el = document.createElement('div');
      el.className = 'ti-pitch-bound-marker ' + cls;
      el.style.left = (norm * W) + 'px';
      overlays.appendChild(el);
      return el;
    }

    var startEl = makeMarker('ti-pitch-bound-start', startNorm);
    var endEl   = makeMarker('ti-pitch-bound-end',   endNorm);

    // Bug C — gray-out regions outside [IN, OUT].
    // Always create both dim divs (even at width=0) so live drag updates
    // can resize them without needing a full overlay rebuild. The CSS sets
    // top:0/bottom:0; we override `height` so the dim spans the full wave
    // canvas height (not the reduced overlays height), keeping the bottom
    // of the waveform covered. Bottom-strip buttons (z-index 5) sit above
    // the dim (z-index 2) so the dim is naturally hidden behind them.
    function makeDim (leftPx, widthPx) {
      var d = document.createElement('div');
      d.className = 'ti-pitch-bound-dim';
      d.style.left   = Math.max(0, leftPx) + 'px';
      d.style.width  = Math.max(0, widthPx) + 'px';
      if (fullWaveHeightPx > 0) {
        d.style.top    = '0px';
        d.style.height = fullWaveHeightPx + 'px';
      }
      overlays.appendChild(d);
    }
    makeDim(0,            startNorm * W);               // left of IN
    makeDim(endNorm * W,  (1.0 - endNorm) * W);         // right of OUT

    // Attach mousedown on each marker to begin drag.
    function onMarkerMousedown (which, el) {
      return function (ev) {
        if (state.sampleLengthSamples <= 0) return;
        el.classList.add('dragging');
        _pitchBoundDrag = {
          which: which,
          overlayRect: overlays.getBoundingClientRect(),
          W: overlays.clientWidth,
          el: el
        };
        ev.preventDefault();
        ev.stopPropagation();
      };
    }
    startEl.addEventListener('mousedown', onMarkerMousedown('start', startEl));
    endEl.addEventListener(  'mousedown', onMarkerMousedown('end',   endEl));

    // Wire global drag handlers once — guard against double-wiring.
    if (!window._tiPitchBoundDragWired) {
      window._tiPitchBoundDragWired = true;

      window.addEventListener('mousemove', function (ev) {
        if (!_pitchBoundDrag) return;
        var d = _pitchBoundDrag;
        var x = ev.clientX - d.overlayRect.left;
        var norm = Math.max(0, Math.min(1, x / d.W));
        var samplePos = Math.round(norm * state.sampleLengthSamples);

        var ps2 = state.pitchModeSlice;
        var curEnd   = (ps2.endSample   && ps2.endSample   > 0) ? ps2.endSample   : state.sampleLengthSamples;
        var curStart = ps2.startSample || 0;

        if (d.which === 'start') {
          ps2.startSample = Math.max(0, Math.min(samplePos, curEnd - 1));
        } else {
          ps2.endSample = Math.max(curStart + 1, Math.min(samplePos, state.sampleLengthSamples));
        }

        // Update marker visual position live (avoid full redraw for speed).
        var newNorm = (d.which === 'start')
          ? ps2.startSample / state.sampleLengthSamples
          : ps2.endSample   / state.sampleLengthSamples;
        d.el.style.left = (newNorm * d.W) + 'px';

        // Update dimmers live during drag.
        var curStartNorm = ps2.startSample / state.sampleLengthSamples;
        var curEndNorm   = ((ps2.endSample && ps2.endSample > 0)
                              ? ps2.endSample : state.sampleLengthSamples) / state.sampleLengthSamples;
        var dims = overlays.querySelectorAll('.ti-pitch-bound-dim');
        if (dims.length === 2) {
          dims[0].style.width = (curStartNorm * d.W) + 'px';
          dims[1].style.left  = (curEndNorm * d.W) + 'px';
          dims[1].style.width = ((1.0 - curEndNorm) * d.W) + 'px';
        }

        // Push to C++.
        var fn = window.Juce && window.Juce.getNativeFunction
          ? window.Juce.getNativeFunction('setPitchSliceBounds') : null;
        if (fn) { try { fn(ps2.startSample, ps2.endSample); } catch(_){} }
      });

      window.addEventListener('mouseup', function () {
        if (_pitchBoundDrag && _pitchBoundDrag.el)
          _pitchBoundDrag.el.classList.remove('dragging');
        _pitchBoundDrag = null;
      });
    }
  }

  // Render JUST one chop's source-segment waveform into a per-chop canvas,
  // sized to the chop's visual width. The chop's source range [srcStart,
  // srcEnd] is mapped onto the full canvas width — so a stretched chop
  // visually spreads the same audio data over more pixels (the Ableton
  // warp effect).
  function drawChopWaveform (canvas, visualWidthCss, visualHeightCss, srcStart, srcEnd, totalSamples) {
    var peaksMin = state.peaksMin;
    var peaksMax = state.peaksMax;
    if (!peaksMin || !peaksMax) return;
    var n = peaksMin.length;
    if (n === 0 || visualWidthCss < 1 || visualHeightCss < 1) return;

    var dpr = window.devicePixelRatio || 1;
    canvas.width  = Math.max(1, Math.round(visualWidthCss  * dpr));
    canvas.height = Math.max(1, Math.round(visualHeightCss * dpr));
    canvas.style.width  = visualWidthCss  + 'px';
    canvas.style.height = visualHeightCss + 'px';

    var ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, visualWidthCss, visualHeightCss);

    // Peak index range covering this chop's source bounds.
    var pStart = Math.floor((srcStart / totalSamples) * n);
    var pEnd   = Math.ceil ((srcEnd   / totalSamples) * n);
    pStart = Math.max(0, Math.min(n - 1, pStart));
    pEnd   = Math.max(pStart + 1, Math.min(n, pEnd));
    var pCount = pEnd - pStart;

    var w  = visualWidthCss, h = visualHeightCss;
    var cy = h / 2;
    var i;
    // Match drawWaveform — peak-aware display gain so loud chops don't slam into the
    // chop number row / dice button / bottom strip.
    var vGain = cy * 0.95 * (state.peakScale || 1.0);

    // Filled body (mirrored around centerline).
    ctx.fillStyle = 'rgba(245, 243, 255, 0.10)';
    ctx.beginPath();
    ctx.moveTo(0, cy);
    for (i = 0; i < pCount; i++) {
      var x   = (i / Math.max(1, pCount - 1)) * w;
      var yMax = cy - peaksMax[pStart + i] * vGain;
      ctx.lineTo(x, yMax);
    }
    for (i = pCount - 1; i >= 0; i--) {
      var x2  = (i / Math.max(1, pCount - 1)) * w;
      var yMin = cy - peaksMin[pStart + i] * vGain;
      ctx.lineTo(x2, yMin);
    }
    ctx.closePath();
    ctx.fill();

    // Top edge stroke.
    ctx.strokeStyle = 'rgba(245, 243, 255, 0.92)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (i = 0; i < pCount; i++) {
      var xx = (i / Math.max(1, pCount - 1)) * w;
      var ym = cy - peaksMax[pStart + i] * vGain;
      if (i === 0) ctx.moveTo(xx, ym); else ctx.lineTo(xx, ym);
    }
    ctx.stroke();

    // Bottom edge stroke.
    ctx.strokeStyle = 'rgba(245, 243, 255, 0.85)';
    ctx.beginPath();
    for (i = 0; i < pCount; i++) {
      var xx2 = (i / Math.max(1, pCount - 1)) * w;
      var ym2 = cy - peaksMin[pStart + i] * vGain;
      if (i === 0) ctx.moveTo(xx2, ym2); else ctx.lineTo(xx2, ym2);
    }
    ctx.stroke();
  }

  // Drag a marker line horizontally to move the boundary between
  // slice[i-1] and slice[i]. Live visual updates during drag (no DOM
  // thrash); on release, send the final position to C++ which snaps to
  // the nearest zero crossing and clamps to keep neither slice below
  // 64 samples. C++ returns the authoritative slice list which we apply.
  // Singleton floating tooltip for the shift+drag stretch gesture.
  // Created lazily; reused across gestures.
  function ensureStretchTooltip () {
    var el = document.getElementById('ti-stretch-tooltip');
    if (!el) {
      el = document.createElement('div');
      el.id = 'ti-stretch-tooltip';
      document.body.appendChild(el);
    }
    return el;
  }

  // Begin a stretch gesture on `sliceIdx`. Element `target` gets the
  // visual `dragging` class. Auto-engages Tones the moment the gesture
  // actually moves (not on mousedown — so a no-movement click can fall
  // through to options.fallbackClick if provided, used by the body
  // gesture path to preserve audition / set-active on a plain click).
  function beginStretchDrag (downEvent, sliceIdx, target, options) {
    options = options || {};
    var slice = state.slices[sliceIdx];
    if (!slice) return;

    var startRatio = (typeof slice.stretchRatio === 'number') ? slice.stretchRatio : 1.0;
    var startX     = downEvent.clientX;
    var moved      = false;
    var tooltip    = ensureStretchTooltip();

    function activateStretchVisuals (mev) {
      moved = true;
      document.body.style.cursor = 'ew-resize';
      if (target) target.classList.add('dragging');
      tooltip.style.display = 'block';
      tooltip.classList.add('visible');
      tooltip.textContent = startRatio.toFixed(2) + 'x';
      tooltip.style.left  = (mev.clientX + 14) + 'px';
      tooltip.style.top   = (mev.clientY - 28) + 'px';
      // Auto-engage Tones if currently None — only on actual movement,
      // so a stray no-drag click on a None chop doesn't silently flip
      // it into warp mode.
      if (!slice.warpMode || slice.warpMode === 0) {
        slice.warpMode = 2;
        var fnM = getNativeFn('setSliceWarpMode');
        if (fnM) { try { fnM(sliceIdx, 2); } catch (_) {} }
      }
    }

    function onMove (mev) {
      var dx = mev.clientX - startX;
      // 3 px threshold prevents click jitter from triggering stretch
      // when the user actually intended a plain click (audition).
      if (!moved && Math.abs(dx) < 3) return;
      if (!moved) activateStretchVisuals (mev);

      // Exponential drag sensitivity — each 100 px is 2x of startRatio.
      // Reaches 15x in ~390 px, 0.1x in ~330 px the other way.
      var factor = Math.pow(2, dx / 100.0);
      var newRatio = Math.max(0.1, Math.min(15.0, startRatio * factor));
      slice.stretchRatio = newRatio;
      var fnR = getNativeFn('setSliceStretchRatio');
      if (fnR) { try { fnR(sliceIdx, newRatio); } catch (_) {} }

      tooltip.textContent = newRatio.toFixed(2) + 'x';
      tooltip.style.left  = (mev.clientX + 14) + 'px';
      tooltip.style.top   = (mev.clientY - 28) + 'px';

      redrawSliceOverlay();
    }
    function onUp () {
      document.removeEventListener('mousemove', onMove, true);
      document.removeEventListener('mouseup',   onUp,   true);
      document.body.style.cursor = '';
      if (target) target.classList.remove('dragging');
      tooltip.classList.remove('visible');
      setTimeout(function () {
        if (!tooltip.classList.contains('visible')) tooltip.style.display = 'none';
      }, 220);

      // No movement and a fallbackClick was provided — fire it (audition
      // / set-active for body taps that didn't turn into stretch drags).
      if (!moved && typeof options.fallbackClick === 'function') {
        options.fallbackClick();
      }
    }
    document.addEventListener('mousemove', onMove, true);
    document.addEventListener('mouseup',   onUp,   true);
  }

  // Reverse-map a clientX coordinate into a source sample using the
  // CURRENT cumulative chop layout. Walks chopLayouts to find which chop
  // contains the cursor's x, then interpolates within that chop's source
  // range proportional to the cursor's position in the chop's visualWidth.
  //
  // Without this, gesture handlers used uniform `frac = (x-left)/W → sample
  // = frac*total` which is wrong when chops are stretched (pixels-per-
  // source-sample is non-uniform). That mismatch produced the marker
  // snap-back: drag mapped cursor X to one sample, applySlicesJson then
  // re-rendered the marker at the SAME-sample's true cumulative-layout X,
  // which is a different visual position.
  function clientXToSourceSample (clientX, waveRectLeft, W) {
    var x = clientX - waveRectLeft;
    if (W <= 0) return 0;
    if (x < 0) x = 0;
    if (x > W) x = W;

    var layouts = state.chopLayouts;
    if (!layouts || layouts.length === 0) {
      return Math.round((x / W) * state.sampleLengthSamples);
    }
    for (var i = 0; i < layouts.length; i++) {
      var L = layouts[i];
      var right = L.visualLeft + L.visualWidth;
      if (x < right || i === layouts.length - 1) {
        var localX   = Math.max(0, x - L.visualLeft);
        var localFrac = L.visualWidth > 0 ? Math.min(1, localX / L.visualWidth) : 0;
        var srcLen   = L.srcEnd - L.srcStart;
        return Math.round(L.srcStart + localFrac * srcLen);
      }
    }
    return layouts[layouts.length - 1].srcEnd;
  }

  // Predict where marker `markerIdx` (boundary between chop markerIdx-1 and
  // chop markerIdx) will visually land after applySlicesJson commits the
  // new boundary at `sampleAtBoundary`. Simulates the cumulative layout
  // with the moved boundary so the marker tracks the cursor without
  // snap-back at release.
  function predictMarkerVisualX (sampleAtBoundary, markerIdx, W) {
    var totalWeight = 0;
    var weights = new Array(state.slices.length);
    for (var i = 0; i < state.slices.length; i++) {
      var s = state.slices[i];
      var sStart = (i === markerIdx)     ? sampleAtBoundary : s.start;
      var sEnd   = (i === markerIdx - 1) ? sampleAtBoundary : s.end;
      var sr = (typeof s.stretchRatio === 'number') ? s.stretchRatio : 1.0;
      var w = Math.max(1, (sEnd - sStart)) * sr;
      weights[i] = w;
      totalWeight += w;
    }
    if (totalWeight <= 0) totalWeight = 1;
    var cumulative = 0;
    for (var j = 0; j < markerIdx; j++) cumulative += (weights[j] / totalWeight) * W;
    return cumulative;
  }

  function attachMarkerDrag (marker, i) {
    // Hover hint: ew-resize cursor when shift is held (stretch left chop).
    // Plain drag = boundary move (works for ALL chops, including warped —
    // user reported "sticky stretch" was making non-warped markers feel
    // locked when adjacent to warped chops).
    marker.addEventListener('mousemove', function (ev) {
      marker.style.cursor = ev.shiftKey ? 'ew-resize' : '';
    });
    marker.addEventListener('mousedown', function (ev) {
      if (ev.button !== 0) return;
      ev.preventDefault();
      ev.stopPropagation();

      // STRETCH BRANCH — only shift+drag now. The previous "sticky" path
      // (any drag on a warped chop's right marker = stretch) made boundary
      // movement feel locked next to warped chops because plain drag was
      // hijacked. Stretch now always requires the explicit shift modifier.
      // Bodies still have their own sticky stretch (separate gesture).
      if (ev.shiftKey) {
        beginStretchDrag(ev, i - 1, marker);
        return;
      }

      var waveCanvas = document.getElementById('waveform-canvas');
      if (!waveCanvas) return;
      var waveRect = waveCanvas.getBoundingClientRect();
      var W = waveRect.width;
      var total = state.sampleLengthSamples;
      if (W <= 0 || total <= 0) return;

      var MIN_LEN = 64;
      var slicePrev = state.slices[i - 1];
      var sliceNext = state.slices[i];
      if (!slicePrev || !sliceNext) return;
      var minSample = slicePrev.start + MIN_LEN;
      var maxSample = sliceNext.end   - MIN_LEN;
      if (maxSample <= minSample) return;

      marker.classList.add('dragging');
      document.body.style.cursor = 'ew-resize';

      // Bug D fix: compute the visual clientX of the marker's boundary edge
      // (its left CSS position + waveRect.left). The cursor may be anywhere
      // INSIDE the marker div at mousedown — offset all subsequent X reads
      // by the difference so the boundary doesn't jump on first move.
      // This eliminates the offset reported at high slice counts (many narrow
      // markers → cursor often lands off-center from the boundary edge).
      var markerEdgeClientX = waveRect.left + parseFloat(marker.style.left || '0');
      var clickOffset = ev.clientX - markerEdgeClientX;  // px cursor is right of edge
)TIHX")
      + juce::String (R"TIHX(
      function cursorToSample (x) {
        var s = clientXToSourceSample(x - clickOffset, waveRect.left, W);
        return Math.max(minSample, Math.min(maxSample, s));
      }
      function onMove (mev) {
        var s = cursorToSample(mev.clientX);
        // Predict the post-commit visual position so the marker tracks
        // the cursor accurately through the cumulative layout. Without
        // this, the marker visually sticks to its OLD cumulative-X during
        // drag and then snaps to its NEW position on release because the
        // surrounding totalWeight has shifted.
        marker.style.left = predictMarkerVisualX(s, i, W) + 'px';
      }
      function onUp (mev) {
        document.removeEventListener('mousemove', onMove, true);
        document.removeEventListener('mouseup',   onUp,   true);
        document.body.style.cursor = '';
        marker.classList.remove('dragging');
        var finalSample = cursorToSample(mev.clientX);
        var fn = getNativeFn('moveSliceBoundary');
        if (fn) {
          fn(i, finalSample).then(applySlicesJson).catch(function(){});
        } else {
          state.slices[i - 1] = Object.assign({}, slicePrev, { end: finalSample });
          state.slices[i]     = Object.assign({}, sliceNext, { start: finalSample });
          redrawSliceOverlay();
        }
      }
      document.addEventListener('mousemove', onMove, true);
      document.addEventListener('mouseup',   onUp,   true);
    });
  }

  function attachSliceGestures (body, idx) {
    var dragState = null;
    var clicked = false;

    body.addEventListener('mousedown', function (ev) {
      if (ev.button !== 0) return;  // left-click only here
      ev.stopPropagation();         // don't let the XY pad eat this
      if (ev.detail >= 2) { ev.preventDefault(); return; }

      // Stretch on body — shift+drag OR sticky (this chop already warp-
      // engaged). The body path is the only way to stretch chop 0 (first)
      // and chop N-1 (last), since those have no draggable boundary
      // marker on one side. Mid-chops also support body stretch as a
      // convenient alternative to grabbing their right-edge marker.
      // beginStretchDrag uses a movement threshold so a click without
      // drag still fires fallbackClick (audition / set-active).
      var slice = state.slices[idx];
      var sticky = slice && slice.warpMode && slice.warpMode > 0;
      if (ev.shiftKey || sticky) {
        ev.preventDefault();
        beginStretchDrag(ev, idx, body, {
          fallbackClick: function () {
            if (state.sliceSubMode === 1) {
              state.activeSliceIndex = idx;
              var fnA = getNativeFn('setActiveSliceIndex');
              if (fnA) { try { fnA(idx); } catch (_) {} }
              redrawSliceOverlay();
            } else {
              var fnB = getNativeFn('auditionSlice');
              if (fnB) { try { fnB(idx); } catch (_) {} }
            }
          }
        });
        return;
      }

      clicked = true;
      dragState = { startY: ev.clientY, startX: ev.clientX, idx: idx, fired: false, startPitch: state.slices[idx].pitch || 0 };
      body.classList.add('dragging');
      document.addEventListener('mousemove', onSliceMove, true);
      document.addEventListener('mouseup',   onSliceUp,   true);
      ev.preventDefault();
    });

    body.addEventListener('dblclick', function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      var waveCanvas = document.getElementById('waveform-canvas');
      if (!waveCanvas || state.sampleLengthSamples <= 0) return;
      var waveRect = waveCanvas.getBoundingClientRect();
      if (waveRect.width <= 0) return;
      // Use the cumulative-layout-aware mapping. Previously this used
      // uniform `frac = (clientX - left) / W * total`, which placed new
      // markers at the wrong source position when the surrounding chops
      // were stretched — user reported "places it in a random spot."
      var samplePos = clientXToSourceSample(ev.clientX, waveRect.left, waveRect.width);
      var fn = getNativeFn('addMarkerAt');
      if (fn) fn(samplePos).then(applySlicesJson).catch(function(){});
    });

    // Hover + scroll wheel = adjust pitch ±12 semitones. Accumulates
    // deltaY across wheel events and only steps once a threshold is met
    // — mouse wheel clicks (large deltaY ~100) step instantly, but
    // trackpad swipes (many small deltaY events) take several events to
    // accumulate to one semitone. Without this, trackpad scrolling flew
    // through the entire ±12 range in milliseconds and couldn't be
    // landed on a target semitone.
    var pitchScrollAccum = 0;
    var PITCH_SCROLL_THRESHOLD = 100;  // px of accumulated deltaY per semi
    body.addEventListener('wheel', function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      // Scroll UP (deltaY < 0) = pitch UP. Negate so positive accum = pitch up.
      pitchScrollAccum -= ev.deltaY;
      var steps = (pitchScrollAccum / PITCH_SCROLL_THRESHOLD) | 0;  // integer steps
      if (steps === 0) return;
      pitchScrollAccum -= steps * PITCH_SCROLL_THRESHOLD;           // keep remainder
      var cur = state.slices[idx].pitch || 0;
      var next = Math.max(-12, Math.min(12, cur + steps));
      if (next === cur) return;
      state.slices[idx] = Object.assign({}, state.slices[idx], { pitch: next });
      var fn = getNativeFn('setSlicePitch');
      if (fn) { try { fn(idx, next); } catch (_) {} }
      redrawSliceOverlay();
    }, { passive: false });

    body.addEventListener('contextmenu', function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      openSliceContextMenu(ev, idx);
    });

    function onSliceMove (ev) {
      if (!dragState) return;
      var dy = dragState.startY - ev.clientY;     // up positive
      var dx = ev.clientX - dragState.startX;
      // 3px threshold prevents click jitter from triggering pitch drag.
      if (!dragState.fired && (Math.abs(dy) > 3 || Math.abs(dx) > 3)) dragState.fired = true;
      if (!dragState.fired) return;

      // 8 px = 1 semitone. Shift = fine (24 px per semi).
      var pxPerSemi = ev.shiftKey ? 24 : 8;
      var deltaSemi = Math.round(dy / pxPerSemi);
      var newPitch = Math.max(-12, Math.min(12, dragState.startPitch + deltaSemi));
      if (newPitch !== state.slices[dragState.idx].pitch) {
        state.slices[dragState.idx].pitch = newPitch;
        var fn = getNativeFn('setSlicePitch');
        if (fn) { try { fn(dragState.idx, newPitch); } catch (_) {} }
        redrawSliceOverlay();
      }
      ev.preventDefault();
    }

    function onSliceUp (ev) {
      document.removeEventListener('mousemove', onSliceMove, true);
      document.removeEventListener('mouseup',   onSliceUp,   true);
      body.classList.remove('dragging');
      // Click (no significant drag): audition (CHOP) or set-active (CHROMATIC).
      if (clicked && dragState && !dragState.fired) {
        if (state.sliceSubMode === 1) {
          // CHROMATIC: set active slice
          state.activeSliceIndex = dragState.idx;
          var fnA = getNativeFn('setActiveSliceIndex');
          if (fnA) { try { fnA(dragState.idx); } catch (_) {} }
          redrawSliceOverlay();
        } else {
          // CHOP: audition
          var fnB = getNativeFn('auditionSlice');
          if (fnB) { try { fnB(dragState.idx); } catch (_) {} }
        }
      }
      dragState = null;
      clicked = false;
      ev.preventDefault();
    }
  }

  // ╔══════════════════════════════════════════════════════════════════════╗
  // ║  Chop overlay — Lab Card v2                                          ║
  // ║  Full ADSR canvas with 4 draggable handles, plus 3 emblem-knobs      ║
  // ║  (Volume / Pitch / Stretch). NO slider bars, NO text labels on       ║
  // ║  parameter controls. Drag emblems vertically to change value.        ║
  // ╚══════════════════════════════════════════════════════════════════════╝

  // APVTS defaults for global Attack/Release — used to populate the per-chop
  // values when the chop has the inheritance sentinel (-1). If user has moved
  // the global knob, the chop will still show this baseline until they drag.
  var GLOBAL_ATTACK_DEFAULT  = 5.0;
  var GLOBAL_RELEASE_DEFAULT = 800.0;

  // ADSR canvas geometry (matches the SVG viewBox in the DOM).
  var ENV_VB_W       = 332;
  var ENV_VB_H       = 142;
  var ENV_PEAK_Y     = 10;       // y at envelope peak (top)
  var ENV_BASE_Y     = 132;      // y at envelope baseline
  var ENV_ATTACK_W   = 60;       // px allocated to attack zone at maxAttackMs
  var ENV_DECAY_W    = 60;       // px allocated to decay zone at maxDecayMs
  var ENV_PLATEAU_W  = 80;       // fixed visual width of sustain plateau
  var ENV_RELEASE_W  = 120;      // px allocated to release zone at maxReleaseMs
  // SVG element is inset 14 14 12 14 inside the .ov-env container.
  var ENV_INSET_L    = 14;
  var ENV_INSET_T    = 14;

  // Skew-aware normalize/denormalize so the envelope zones feel like the
  // global ATTACK/RELEASE knobs (most travel covers small ms).
  function denormSkew (t, min, max, skew) {
    t = Math.max(0, Math.min(1, t));
    return min + (max - min) * Math.pow(t, 1.0 / skew);
  }
  function normSkew (v, min, max, skew) {
    v = Math.max(min, Math.min(max, v));
    return Math.pow((v - min) / Math.max(1e-9, max - min), skew);
  }

  // Per-control range descriptors.
  var OV_RANGES = {
    attack:  { min: 0.0,  max: 2000.0, skew: 0.4 },
    decay:   { min: 0.0,  max: 2000.0, skew: 0.4 },
    sustain: { min: 0.0,  max: 1.0,    skew: 1.0 },
    release: { min: 1.0,  max: 5000.0, skew: 0.4 },
    volume:  { min: 0.0,  max: 2.0,    skew: 1.0 },
    pitch:   { min: -12,  max: 12,     skew: 1.0 },
    stretch: { min: 0.1,  max: 15.0,   skew: 0.35 }
  };

  // Pull the current value for a control out of the slice state, applying
  // inheritance fallbacks for attack/release.
  // idx == -1 reads from state.pitchModeSlice (pitch mode virtual slice).
  function ovValueFromState (idx, key) {
    var s = getSliceData(idx);
    if (!s) return null;
    if (key === 'attack')  return (s.attackMs  == null || s.attackMs  < 0) ? GLOBAL_ATTACK_DEFAULT  : Number(s.attackMs);
    if (key === 'decay')   return Number(s.decayMs      || 0);
    if (key === 'sustain') return s.sustainLevel == null ? 1.0 : Number(s.sustainLevel);
    if (key === 'release') return (s.releaseMs == null || s.releaseMs < 0) ? GLOBAL_RELEASE_DEFAULT : Number(s.releaseMs);
    if (key === 'volume')  return s.volume == null ? 1.0 : Number(s.volume);
    if (key === 'pitch')   return Number(s.pitch || 0);
    if (key === 'stretch') return Number(s.stretchRatio || 1.0);
    return null;
  }

  // Compute the four envelope handle positions (in SVG viewBox coords) from
  // the current chop's A / D / S / R values.
  function ovEnvelopePoints (idx) {
    var a  = ovValueFromState(idx, 'attack');
    var d  = ovValueFromState(idx, 'decay');
    var sv = ovValueFromState(idx, 'sustain');
    var r  = ovValueFromState(idx, 'release');
    var aT = normSkew(a, OV_RANGES.attack.min,  OV_RANGES.attack.max,  OV_RANGES.attack.skew);
    var dT = normSkew(d, OV_RANGES.decay.min,   OV_RANGES.decay.max,   OV_RANGES.decay.skew);
    var rT = normSkew(r, OV_RANGES.release.min, OV_RANGES.release.max, OV_RANGES.release.skew);
    var aPx = aT * ENV_ATTACK_W;
    var dPx = dT * ENV_DECAY_W;
    var rPx = rT * ENV_RELEASE_W;
    var sustainY = ENV_BASE_Y - sv * (ENV_BASE_Y - ENV_PEAK_Y);
    var xA = aPx;
    var xD = xA + dPx;
    var xS = xD + ENV_PLATEAU_W * 0.5;
    var xPlateauEnd = xD + ENV_PLATEAU_W;
    var xR = xPlateauEnd + rPx;
    return {
      A: { x: xA, y: ENV_PEAK_Y, val: a, ms: a },
      D: { x: xD, y: sustainY, val: d, ms: d },
      S: { x: xS, y: sustainY, val: sv },
      R: { x: xR, y: ENV_BASE_Y, val: r, ms: r },
      sustainY: sustainY,
      xPlateauEnd: xPlateauEnd
    };
  }

  function fmtMs (ms)   { return Math.round(ms) + ' ms'; }
  function fmtPct (v)   { return Math.round(v * 100) + '%'; }
  function fmtPitch (v) { var s = Math.round(v); return (s >= 0 ? '+' : '') + s + ' st'; }
  // Drop the unit suffix entirely — the accordion emblem already reads as
  // "stretch", and the multiplication-sign glyph mojibake'd in the WebView
  // (showed up as "A + macron + extras" at small sizes). Just the number.
  function fmtStretch(v){ return v.toFixed(2); }

  // Redraw envelope path + reposition the 4 handles + update tooltips.
  function ovRedrawEnvelope (idx) {
    var panel = document.getElementById('ti-chop-panel');
    var env   = document.getElementById('ti-env');
    if (!panel || !env) return;
    var pts = ovEnvelopePoints(idx);

    // Envelope path: baseline → attack peak → decay end → plateau → release → baseline
    var d = 'M 0 ' + ENV_BASE_Y +
            ' L ' + pts.A.x + ' ' + pts.A.y +
            ' L ' + pts.D.x + ' ' + pts.D.y +
            ' L ' + pts.xPlateauEnd + ' ' + pts.sustainY +
            ' L ' + pts.R.x + ' ' + pts.R.y +
            ' L ' + ENV_VB_W + ' ' + ENV_BASE_Y;
    var dFill = d + ' Z';
    document.getElementById('ti-env-line').setAttribute('d', d);
    document.getElementById('ti-env-fill').setAttribute('d', dFill);

    // Position handles using the ENV CONTAINER's HTML metrics — NOT the SVG's
    // offset properties. SVGSVGElement does not expose HTMLElement.offsetLeft
    // / offsetWidth / offsetHeight; in WKWebView they return 0/undefined,
    // which collapsed every handle to the top-left corner.
    //
    // .ov-env has padding:0, so its clientWidth/clientHeight equal its
    // content-box. CSS absolute-positioning measures style.left/top from the
    // offsetParent's PADDING-BOX — the same origin the SVG's `inset: 14 14
    // 12 14` uses. So both share the coordinate space: add the hardcoded
    // inset (14 left / 14 top) to viewBox coords and the math just works,
    // regardless of border width, regardless of any open-transition transform.
    var contentW = env.clientWidth;
    var contentH = env.clientHeight;
    var svgW = contentW - 28;   // 14 left + 14 right inset
    var svgH = contentH - 26;   // 14 top  + 12 bottom inset
    var sx = svgW / ENV_VB_W;
    var sy = svgH / ENV_VB_H;
    var handles = panel.querySelectorAll('.ov-env-handle');
    handles.forEach(function (h) {
      var which = h.dataset.h;
      var p = pts[which];
      if (!p) return;
      h.style.left = (14 + p.x * sx) + 'px';
      h.style.top  = (14 + p.y * sy) + 'px';
    });
  }

  // Emblem-knob value readouts. Glyph shapes are deliberately STATIC vs the
  // value — only the numeric text changes. Idle CSS animations (volume bars,
  // fork prongs, stretch breath) keep the panel feeling alive without
  // shifting the layout when the value changes width (e.g. "+12 st" vs "+0").
  function ovRedrawEmblems (idx) {
    var s = getSliceData(idx);
    if (!s) return;
    var panel = document.getElementById('ti-chop-panel');
    panel.querySelectorAll('.ov-ctrl').forEach(function (ctrl) {
      var key = ctrl.dataset.ctrl;
      var v = ovValueFromState(idx, key);
      var valEl = ctrl.querySelector('.ov-val');
      if (!valEl) return;
      if      (key === 'volume')  valEl.textContent = fmtPct(v);
      else if (key === 'pitch')   valEl.textContent = fmtPitch(v);
      else if (key === 'stretch') valEl.textContent = fmtStretch(v);
    });
  }

  function ovEnsureWired () {
    var panel = document.getElementById('ti-chop-panel');
    if (!panel || panel.dataset.wired === '1') return;
    panel.dataset.wired = '1';

    var backdrop = document.getElementById('ti-chop-backdrop');
    backdrop.addEventListener('click', closeChopOverlay);
    document.getElementById('ti-chop-close').addEventListener('click', closeChopOverlay);
    document.addEventListener('keydown', function (ev) {
      if (ev.key === 'Escape' && panel.classList.contains('open')) closeChopOverlay();
    });

    // Click-outside dismiss: close on mousedown anywhere outside the panel so
    // the user only needs one click (the same click that closes also reaches
    // whatever UI element they tapped — backdrop pointer-events won't block it).
    if (!window._tiChopClickOutsideWired) {
      window._tiChopClickOutsideWired = true;
      document.addEventListener('mousedown', function (ev) {
        var pn = document.getElementById('ti-chop-panel');
        if (!pn || !pn.classList.contains('open')) return;
        if (pn.contains(ev.target)) return;  // inside panel — do nothing
        closeChopOverlay();
      }, true /* capture phase — fires before backdrop swallows it */);
    }

    // Mode pill clicks.
    panel.querySelectorAll('.ov-mode').forEach(function (el) {
      el.addEventListener('click', function () {
        if (el.classList.contains('soon')) return;
        var idx = parseInt(panel.dataset.targetIdx, 10);
        if (isNaN(idx)) return;
        var s = getSliceData(idx); if (!s) return;
        var mode = parseInt(el.dataset.mode, 10) || 0;
        s.warpMode = mode;
        var fn = getNativeFn('setSliceWarpMode');
        if (fn) { try { fn(idx, mode); } catch (_) {} }
        ovApplyState(idx);
        redrawSliceOverlay();
      });
    });

    // ENV handle drag — each handle controls one envelope parameter.
    // A: drag X = attack ms.  D: drag X = decay ms (relative to A).
    // S: drag Y = sustain level.  R: drag X = release ms (after plateau).
    panel.querySelectorAll('.ov-env-handle').forEach(function (h) {
      var which = h.dataset.h;
      h.addEventListener('mousedown', function (e) {
        h.classList.add('dragging');
        var svg = document.getElementById('ti-env-svg');
        function move (ev) {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          var s = getSliceData(idx); if (!s) return;
          var rect = svg.getBoundingClientRect();
          // convert pointer to SVG-viewBox space
          var vx = (ev.clientX - rect.left) / Math.max(1, rect.width)  * ENV_VB_W;
          var vy = (ev.clientY - rect.top ) / Math.max(1, rect.height) * ENV_VB_H;
          if (which === 'A') {
            var t = Math.max(0, Math.min(1, vx / ENV_ATTACK_W));
            var ms = denormSkew(t, OV_RANGES.attack.min, OV_RANGES.attack.max, OV_RANGES.attack.skew);
            s.attackMs = ms;
            var fn = getNativeFn('setSliceAttackMs'); if (fn) { try { fn(idx, ms); } catch (_) {} }
          } else if (which === 'D') {
            // D's x = A.x + decayPx → decayPx = vx - aPx
            var aT = normSkew(s.attackMs >= 0 ? s.attackMs : GLOBAL_ATTACK_DEFAULT,
                              OV_RANGES.attack.min, OV_RANGES.attack.max, OV_RANGES.attack.skew);
            var aPx = aT * ENV_ATTACK_W;
            var dPx = Math.max(0, Math.min(ENV_DECAY_W, vx - aPx));
            var dT = dPx / ENV_DECAY_W;
            var ms = denormSkew(dT, OV_RANGES.decay.min, OV_RANGES.decay.max, OV_RANGES.decay.skew);
            s.decayMs = ms;
            var fn = getNativeFn('setSliceDecayMs'); if (fn) { try { fn(idx, ms); } catch (_) {} }
          } else if (which === 'S') {
            // map vy [PEAK..BASE] → level [1..0]
            var lvl = 1.0 - (vy - ENV_PEAK_Y) / (ENV_BASE_Y - ENV_PEAK_Y);
            lvl = Math.max(0, Math.min(1, lvl));
            s.sustainLevel = lvl;
            var fn = getNativeFn('setSliceSustain'); if (fn) { try { fn(idx, lvl); } catch (_) {} }
          } else if (which === 'R') {
            // R's x = plateauEnd + releasePx → releasePx = vx - plateauEnd
            var aT2 = normSkew(s.attackMs >= 0 ? s.attackMs : GLOBAL_ATTACK_DEFAULT,
                               OV_RANGES.attack.min, OV_RANGES.attack.max, OV_RANGES.attack.skew);
            var dT2 = normSkew(s.decayMs || 0,
                               OV_RANGES.decay.min, OV_RANGES.decay.max, OV_RANGES.decay.skew);
            var plateauEnd = aT2 * ENV_ATTACK_W + dT2 * ENV_DECAY_W + ENV_PLATEAU_W;
            var rPx = Math.max(0, Math.min(ENV_RELEASE_W, vx - plateauEnd));
            var rT = rPx / ENV_RELEASE_W;
            var ms = denormSkew(rT, OV_RANGES.release.min, OV_RANGES.release.max, OV_RANGES.release.skew);
            s.releaseMs = ms;
            var fn = getNativeFn('setSliceReleaseMs'); if (fn) { try { fn(idx, ms); } catch (_) {} }
          }
          ovRedrawEnvelope(idx);
        }
        function up () {
          h.classList.remove('dragging');
          document.removeEventListener('mousemove', move);
          document.removeEventListener('mouseup', up);
        }
        document.addEventListener('mousemove', move);
        document.addEventListener('mouseup', up);
        e.preventDefault(); e.stopPropagation();
      });
      // Double-click → reset this envelope parameter to its default.
      h.addEventListener('dblclick', function (e) {
        e.preventDefault(); e.stopPropagation();
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          var s = getSliceData(idx); if (!s) return;
          if (which === 'A') {
            s.attackMs = -1;
            var fn = getNativeFn('setSliceAttackMs'); if (fn) { try { fn(idx, -1); } catch (_) {} }
          } else if (which === 'D') {
            s.decayMs = 0;
            var fn = getNativeFn('setSliceDecayMs');  if (fn) { try { fn(idx, 0); } catch (_) {} }
          } else if (which === 'S') {
            s.sustainLevel = 1.0;
            var fn = getNativeFn('setSliceSustain');  if (fn) { try { fn(idx, 1.0); } catch (_) {} }
          } else if (which === 'R') {
            s.releaseMs = -1;
            var fn = getNativeFn('setSliceReleaseMs');if (fn) { try { fn(idx, -1); } catch (_) {} }
          }
          requestAnimationFrame(function () {
            try { if (getSliceData(idx)) ovRedrawEnvelope(idx); } catch (_) {}
          });
        } catch (_) {}
      });
    });

    // Emblem-knob ctrl drag (vertical) — Volume / Pitch / Stretch.
    // Drag 200 px = full range. Skew-aware so small Y deltas at low values
    // feel as responsive as the knob arc range.
    panel.querySelectorAll('.ov-ctrl').forEach(function (ctrl) {
      var key = ctrl.dataset.ctrl;
      var range = OV_RANGES[key];
      if (!range) return;
      ctrl.addEventListener('mousedown', function (e) {
        var startY = e.clientY;
        var startV = ovValueFromState(parseInt(panel.dataset.targetIdx, 10), key);
        if (startV == null) return;
        var startT = normSkew(startV, range.min, range.max, range.skew);
        function move (ev) {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          var s2 = getSliceData(idx); if (!s2) return;
          var deltaY = startY - ev.clientY;     // up = positive
          var deltaT = deltaY / 200.0;           // 200 px = full range
          var t = Math.max(0, Math.min(1, startT + deltaT));
          var v = denormSkew(t, range.min, range.max, range.skew);
          if (key === 'pitch') v = Math.round(v);
          if (key === 'volume') {
            s2.volume = v;
            var fn = getNativeFn('setSliceVolume'); if (fn) { try { fn(idx, v); } catch (_) {} }
          } else if (key === 'pitch') {
            s2.pitch = v;
            var fn = getNativeFn('setSlicePitch'); if (fn) { try { fn(idx, v); } catch (_) {} }
            redrawSliceOverlay();
          } else if (key === 'stretch') {
            s2.stretchRatio = v;
            var fn = getNativeFn('setSliceStretchRatio'); if (fn) { try { fn(idx, v); } catch (_) {} }
            redrawSliceOverlay();
          }
          ovRedrawEmblems(idx);
        }
        function up () {
          document.removeEventListener('mousemove', move);
          document.removeEventListener('mouseup', up);
        }
        document.addEventListener('mousemove', move);
        document.addEventListener('mouseup', up);
        e.preventDefault();
      });
      // Double-click → reset this emblem-knob to its default.
      ctrl.addEventListener('dblclick', function (e) {
        e.preventDefault(); e.stopPropagation();
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          var s2 = getSliceData(idx); if (!s2) return;
          if (key === 'volume') {
            s2.volume = 1.0;
            var fn = getNativeFn('setSliceVolume'); if (fn) { try { fn(idx, 1.0); } catch (_) {} }
          } else if (key === 'pitch') {
            s2.pitch = 0;
            var fn = getNativeFn('setSlicePitch');  if (fn) { try { fn(idx, 0); } catch (_) {} }
          } else if (key === 'stretch') {
            s2.stretchRatio = 1.0;
            var fn = getNativeFn('setSliceStretchRatio'); if (fn) { try { fn(idx, 1.0); } catch (_) {} }
          }
          requestAnimationFrame(function () {
            try {
              if (!getSliceData(idx)) return;
              ovRedrawEmblems(idx);
              if (key === 'pitch' || key === 'stretch') redrawSliceOverlay();
            } catch (_) {}
          });
        } catch (_) {}
      });
    });

    // Action buttons (reverse / reset / delete) — defense-hardened.
    // Each branch is wrapped so any JS exception can't tear the panel down
    // mid-state, and the click event is consumed so it can't bubble back to
    // any sibling handler (e.g. the slicer canvas underneath). Redraws are
    // deferred to the next frame so the native fn's slice-list swap settles
    // before the hero canvas reads the new state.
    panel.querySelectorAll('.ov-act').forEach(function (el) {
      el.addEventListener('click', function (e) {
        e.preventDefault();
        e.stopPropagation();
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          var sa = getSliceData(idx); if (!sa) return;
          var act = el.dataset.act;

          if (act === 'rev') {
            var nextRev = !sa.reverse;
            sa.reverse = nextRev;
            var fnR = getNativeFn('setSliceReverse');
            if (fnR) { try { fnR(idx, nextRev); } catch (_) {} }
            requestAnimationFrame(function () { try { redrawSliceOverlay(); } catch (_) {} });
          }
          else if (act === 'resetPitch') {
            sa.pitch = 0;
            var fnP = getNativeFn('setSlicePitch');
            if (fnP) { try { fnP(idx, 0); } catch (_) {} }
            requestAnimationFrame(function () {
              try {
                if (!getSliceData(idx)) return;
                ovApplyState(idx);
                redrawSliceOverlay();
              } catch (_) {}
            });
          }
          else if (act === 'del') {
            // Snapshot idx + close panel BEFORE calling delete. After delete
            // the slice index no longer points at the same chop (could be a
            // different chop or out of range entirely), and any deferred
            // handler that re-reads state.slices[idx] could crash.
            var delIdx = idx;
            closeChopOverlay();
            var fnD = getNativeFn('deleteSlice');
            if (fnD) {
              try {
                var r = fnD(delIdx);
                if (r && typeof r.then === 'function') {
                  r.then(function (json) {
                    try { applySlicesJson(json); } catch (_) {}
                  }, function () {});
                } else if (typeof r === 'string') {
                  applySlicesJson(r);
                }
              } catch (_) {}
            }
          }
        } catch (_) { /* swallow — better silent than crashed host */ }
      });
    });

    // Reposition handles when the panel size changes (e.g. on open).
    window.addEventListener('resize', function () {
      var idx = parseInt(panel.dataset.targetIdx, 10);
      if (!isNaN(idx) && getSliceData(idx)) ovRedrawEnvelope(idx);
    });

    // ── FX section (Mark 2) ────────────────────────────────────────────
    // INDEPENDENT pill: toggles fxIndependent. Per-FX state is preserved
    // underneath, so flipping off → on restores the user's last selection.
    var indyEl = document.getElementById('ti-fx-indy');
    if (indyEl) {
      indyEl.addEventListener('click', function (ev) {
        ev.stopPropagation();
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          if (idx === -1) return;  // FX independence not available in pitch mode
          var sf = getSliceData(idx); if (!sf) return;
          var on = !sf.fxIndependent;
          sf.fxIndependent = on;
          var fn = getNativeFn('setSliceFxIndependent');
          if (fn) { try { fn(idx, on); } catch (_) {} }
          ovRedrawFx(idx);
        } catch (_) {}
      });
    }
    // FX chips: GRAIN/SPACE/DELAY/EQ/JUNE toggle a bool; TAPE cycles
    // OFF → STU → CAS → WIR → OFF on each click.
    panel.querySelectorAll('.ov-fx-chip').forEach(function (chip) {
      chip.addEventListener('click', function (ev) {
        ev.stopPropagation();
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx) || idx === -1) return;  // FX chips not in pitch mode
          var s = getSliceData(idx); if (!s) return;
          if (!s.fxIndependent) return;   // chip is grayed in inherit mode
          var fx = chip.dataset.fx;
          if (fx === 'tape') {
            var next = ((Number(s.fxTapeMachine) || 0) + 1) % 4;
            s.fxTapeMachine = next;
            var fnT = getNativeFn('setSliceFxTapeMachine');
            if (fnT) { try { fnT(idx, next); } catch (_) {} }
          } else {
            var key = 'fx' + fx.charAt(0).toUpperCase() + fx.slice(1);
            var nextOn = !s[key];
            s[key] = nextOn;
            var fnB = getNativeFn('setSliceFxBool');
            if (fnB) { try { fnB(idx, fx, nextOn); } catch (_) {} }
          }
          ovRedrawFx(idx);
        } catch (_) {}
      });
    });

    // ── MOTION row — SCAN pill click ───────────────────────────────────────
    var scanPillEl = document.getElementById('scan-pill');
    if (scanPillEl) {
      scanPillEl.addEventListener('click', function (ev) {
        ev.stopPropagation();
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          var ss = getSliceData(idx); if (!ss) return;
          var newEnabled = !ss.scanEnabled;
          ss.scanEnabled = newEnabled;
          ovRedrawScan(idx);
          var fn = getNativeFn('setSliceScanEnabled');
          if (fn) { try { fn(idx, newEnabled); } catch (_) {} }
        } catch (_) {}
      });
    }

    // RATE display — vertical drag, exponential: 100px up = 2× base rate.
    var scanRateDragStart = null;
    var rateDisplayEl = document.getElementById('rate-display');
    if (rateDisplayEl) {
      rateDisplayEl.addEventListener('mousedown', function (e) {
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          var ss = getSliceData(idx); if (!ss) return;
          if (!ss.scanEnabled) return;   // no drag when scan is off
          scanRateDragStart = { y: e.clientY, baseRate: ss.scanRate || 1.0 };
          e.preventDefault();
        } catch (_) {}
      });
      // Double-click → reset scan rate to default (1.00x). Matches the
      // pattern used by ADSR handles + VOL/PITCH/STRETCH emblem-knobs.
      rateDisplayEl.addEventListener('dblclick', function (e) {
        e.preventDefault(); e.stopPropagation();
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) return;
          var ss = getSliceData(idx); if (!ss) return;
          ss.scanRate = 1.0;
          var rv = document.getElementById('rate-value');
          if (rv) rv.textContent = '1.00\xd7';
          var fn = getNativeFn('setSliceScanRate');
          if (fn) { try { fn(idx, 1.0); } catch (_) {} }
        } catch (_) {}
      });
    }
    if (!panel._scanWindowListenersAttached) {
      panel._scanWindowListenersAttached = true;
      window.addEventListener('mousemove', function (e) {
        if (!scanRateDragStart) return;
        try {
          var idx = parseInt(panel.dataset.targetIdx, 10);
          if (isNaN(idx)) { scanRateDragStart = null; return; }
          var ss = getSliceData(idx);
          if (!ss) { scanRateDragStart = null; return; }
          var dy = scanRateDragStart.y - e.clientY;
          var newRate = Math.max(0.1, Math.min(8.0,
              scanRateDragStart.baseRate * Math.pow(2, dy / 100)));
          ss.scanRate = newRate;
          var rv = document.getElementById('rate-value');
          if (rv) rv.textContent = newRate.toFixed(2) + '\xd7';
          var fn = getNativeFn('setSliceScanRate');
          if (fn) { try { fn(idx, newRate); } catch (_) {} }
        } catch (_) {}
      });
      window.addEventListener('mouseup', function () { scanRateDragStart = null; });
    }
  }

  // Render the FX section from current chop state. Called from ovApplyState
  // on open + after every FX click.
  function ovRedrawFx (idx) {
    var panel = document.getElementById('ti-chop-panel');
    var section = document.getElementById('ti-fx-section');
    if (!panel || !section) return;
    var s = getSliceData(idx);
    if (!s) return;

    var indyOn = !!s.fxIndependent;
    section.classList.toggle('inheriting', !indyOn);

    var indyEl = document.getElementById('ti-fx-indy');
    if (indyEl) indyEl.classList.toggle('on', indyOn);

    // Per-chip on/off + TAPE sub-machine label.
    var TAPE_NAMES = ['', 'STU', 'CAS', 'WIR'];
    panel.querySelectorAll('.ov-fx-chip').forEach(function (chip) {
      var fx = chip.dataset.fx;
      var on = false;
      var label = chip.dataset.fx.toUpperCase();
      if (fx === 'tape') {
        var tm = Number(s.fxTapeMachine) || 0;
        on = tm > 0;
        chip.innerHTML = on
            ? 'TAPE<span class="sub">·' + TAPE_NAMES[tm] + '</span>'
            : 'TAPE';
      } else {
        var key = 'fx' + fx.charAt(0).toUpperCase() + fx.slice(1);
        on = !!s[key];
      }
      chip.classList.toggle('on', on);
    });
  }

  // ── MOTION row (scan UI) ──────────────────────────────────────────────────
  function ovRedrawScan (idx) {
    var scanPill    = document.getElementById('scan-pill');
    var rateDisplay = document.getElementById('rate-display');
    var rateValue   = document.getElementById('rate-value');
    if (!scanPill || !rateDisplay || !rateValue) return;
    var s = getSliceData(idx);
    if (!s) return;
    var on   = !!s.scanEnabled;
    var rate = (typeof s.scanRate === 'number' && s.scanRate > 0.05) ? s.scanRate : 1.0;
    scanPill.classList.toggle('off', !on);
    scanPill.textContent = on ? 'SCAN ON' : 'SCAN OFF';
    rateDisplay.classList.toggle('dim', !on);
    rateValue.textContent = rate.toFixed(2) + '\xd7';
  }

  function ovApplyState (idx) {
    var panel = document.getElementById('ti-chop-panel');
    if (!panel) return;
    var s = getSliceData(idx);
    if (!s) return;
    panel.dataset.targetIdx = idx;
    var numEl = document.getElementById('ti-chop-num');
    if (idx === -1) {
      // PITCH MODE: replace "CHOP NN" title with "PITCH MODE"
      var nameEl = numEl ? numEl.parentElement : null;
      if (nameEl) nameEl.innerHTML = 'PITCH MODE';
      // Hide DELETE button, show REVERSE and RESET
      var delEl = panel.querySelector('.ov-act.danger[data-act="del"]');
      if (delEl) delEl.style.display = 'none';
    } else {
      // Restore normal CHOP header
      var nameEl2 = numEl ? numEl.parentElement : document.querySelector('.ov-head .name');
      if (nameEl2 && !document.getElementById('ti-chop-num')) {
        nameEl2.innerHTML = 'CHOP<span class="num" id="ti-chop-num">01</span>';
      }
      var freshNumEl = document.getElementById('ti-chop-num');
      if (freshNumEl) freshNumEl.textContent = (idx + 1 < 10 ? '0' : '') + (idx + 1);
      var delEl2 = panel.querySelector('.ov-act.danger[data-act="del"]');
      if (delEl2) delEl2.style.display = '';
    }

    // Mode pill highlight + warp-only visibility.
    var mode = Number(s.warpMode || 0);
    var modeName = ['none','beats','tones','texture'][mode] || 'none';
    panel.setAttribute('data-warp', modeName);
    panel.querySelectorAll('.ov-mode').forEach(function (el) {
      el.classList.toggle('active', parseInt(el.dataset.mode, 10) === mode);
    });

    ovRedrawEnvelope(idx);
    ovRedrawEmblems(idx);
    ovRedrawFx(idx);
    ovRedrawScan(idx);
  }

  function openChopOverlay (idx) {
    // idx === -1 = pitch-mode virtual slice; regular idx guards state.slices array.
    if (idx !== -1 && !state.slices[idx]) return;
    if (idx === -1 && !state.pitchModeSlice) return;
    ovEnsureWired();
    document.getElementById('ti-chop-backdrop').classList.add('open');
    document.getElementById('ti-chop-panel').classList.add('open');
    // Apply state AFTER opening so getBoundingClientRect returns real sizes.
    requestAnimationFrame(function () { ovApplyState(idx); });
  }

  function closeChopOverlay () {
    var bd = document.getElementById('ti-chop-backdrop');
    var pn = document.getElementById('ti-chop-panel');
    if (bd) bd.classList.remove('open');
    if (pn) pn.classList.remove('open');
  }

  // Legacy aliases — older call sites still reference these names.
  function openSliceContextMenu (ev, idx) { openChopOverlay(idx); }
  function closeSliceContextMenu ()       { closeChopOverlay(); }

  // ── Interactions ──────────────────────────────────────────────────────────
  function wireInteractions () {
    // Mode toggle: PITCH / SLICE — both live; switching SLICE shows the slicer panel.
    document.querySelectorAll('#ti-mode-toggle .ti-mode-pill').forEach(function (pill) {
      pill.addEventListener('click', function (ev) {
        ev.stopPropagation();
        var newMode = (pill.dataset.mode === 'SLICE') ? 1 : 0;
        if (newMode === state.sliceMode) return;
        setSliceModeUI(newMode);
        var fn = getNativeFn('setSliceMode');
        if (fn) { try { fn(newMode); } catch (_) {} }
      });
    });

    // SLICES pill: click toggles the drawer. ev.stopPropagation so the
    // document-level "click outside drawer" listener below doesn't see
    // this same click and immediately re-close it.
    var slicesBtn = document.getElementById('ti-slices-btn');
    if (slicesBtn) {
      slicesBtn.addEventListener('click', function (ev) {
        ev.stopPropagation();
        toggleSlicerDrawer();
      });
    }

    // GRID pills inside the drawer — apply N chops via gridSliceSlices.
    // Drawer stays open so the user can A/B different sizes (per their
    // explicit preference, see /gsd-discuss-phase trail). Active pill
    // is highlighted by updateChopCount() reading state.slices.length
    // back from the C++ result.
    document.querySelectorAll('#ti-grid-row .ti-grid-pill').forEach(function (pill) {
      pill.addEventListener('click', function (ev) {
        ev.stopPropagation();
        var n = parseInt(pill.dataset.n, 10) || 16;
        var fn = getNativeFn('gridSliceSlices');
        if (!fn) return;
        try {
          var r = fn(n);
          if (r && typeof r.then === 'function') r.then(applySlicesJson);
          else applySlicesJson(r);
        } catch (_) {}
      });
    });

    // Sub-mode pills (CHOP / CHROMATIC / RANDOM / LAYER) — selector unchanged
    // from v0b since #ti-submode-toggle moved into the drawer with the same id.
    //
    // Special-case for CHOP pill (data-sub="0"): clicking the pill while CHOP
    // is already active TOGGLES holdMode. Cycle is CHOP → HOLD → CHOP. The
    // pill label flips between "CHOP" and "HOLD" to reflect the current state.
    // Selecting any other submode pill auto-disables hold (keeps the meaning
    // tied to the CHOP pill specifically). MPC/FL "latch" feel — see C++
    // SamplerVoice::stopNote for the audio behavior.
    document.querySelectorAll('#ti-submode-toggle .ti-submode-pill').forEach(function (p) {
      p.addEventListener('click', function (ev) {
        ev.stopPropagation();
        var sub = parseInt(p.dataset.sub, 10) || 0;
        var pushHold = function (val) {
          var fn = getNativeFn('setHoldMode');
          if (fn) { try { fn(!!val); } catch (_) {} }
        };
        // CHOP pill click while CHOP is already the active sub-mode → toggle HOLD.
        // CONSTRAINT (user 2026-05-25): HOLD only allowed in 1-SHOT mode. Clicking
        // CHOP while in LOOP is a no-op — LOOP+HOLD = infinite loop, explicitly
        // blocked by design. User must switch to 1-SHOT first.
        if (sub === 0 && state.sliceSubMode === 0) {
          if (state.sampleLoopMode !== 0 && !state.holdMode) {
            // In LOOP mode: silently refuse to enter HOLD. (If HOLD is already
            // active we still allow disabling it, but the LOOP toggle handler
            // also auto-clears HOLD when LOOP is selected, so this branch is
            // unreachable in practice — defense in depth.)
            return;
          }
          state.holdMode = !state.holdMode;
          p.textContent = state.holdMode ? 'HOLD' : 'CHOP';
          p.classList.toggle('hold-active', state.holdMode);
          pushHold(state.holdMode);
          return;
        }
        // Switching to a different submode disables HOLD and restores CHOP label.
        if (state.holdMode) {
          state.holdMode = false;
          pushHold(false);
        }
        var chopPill = document.querySelector('#ti-submode-toggle .ti-submode-pill[data-sub="0"]');
        if (chopPill) {
          chopPill.textContent = 'CHOP';
          chopPill.classList.remove('hold-active');
        }
        setSubModeUI(sub);
        var fn = getNativeFn('setSliceSubMode');
        if (fn) { try { fn(sub); } catch (_) {} }
      });
    });

    // RANDOM: 5TH / 7TH / OCT — assign each chop a random pitch from a
    // 3-element ± interval set. Stacks beautifully with RANDOM sub-mode:
    // random chop selection per note + random per-chop voicing = chord-ish
    // textures from a single sample. Sends the whole updated list via
    // setSlicesJson so it's one round-trip instead of N setSlicePitch calls.
    //
    // CRITICAL: spread the existing slice with Object.assign so every field
    // survives the round-trip (warpMode, stretchRatio, attackMs, releaseMs,
    // decayMs, sustainLevel, volume, scanEnabled/scanRate/scanWindow, …).
    // Earlier the handler explicitly copied only {start, end, reverse,
    // pitch} and the C++ side rebuilt the slice with defaults for everything
    // else — a single Random Octave click wiped Tones/Beats markers and
    // reset stretchRatio to 1.0 on every chop. Same family of bug as the
    // existing "applySlicesJson must preserve ALL Slice fields" gotcha but
    // in the JS→C++ direction.
    function applyRandomInterval (semitones) {
      if (state.slices.length === 0) return;
      var CHOICES = [-semitones, 0, semitones];
      var updated = state.slices.map(function (s) {
        return Object.assign({}, s, {
          pitch: CHOICES[Math.floor(Math.random() * 3)]
        });
      });
      var json = JSON.stringify({ slices: updated });
      var fn = getNativeFn('setSlicesJson');
      if (fn) { try { fn(json); } catch (_) {} }
      state.slices = updated;
      redrawSliceOverlay();
    }
    document.querySelectorAll('#ti-action-row .ti-action-btn[data-rand]').forEach(function (btn) {
      btn.addEventListener('click', function (ev) {
        ev.stopPropagation();
        var n = parseInt(btn.dataset.rand, 10);
        if (!isNaN(n) && n > 0) applyRandomInterval(n);
      });
    });

    // Chop overlay wires its own actions on first open (ensureChopOverlayWired).
    // No legacy ctx-menu handler needed here.
    // Click outside the SLICES drawer closes it. Drawer interactions
    // already stopPropagation so they don't reach this listener.
    document.addEventListener('click', function (ev) {
      var dr = document.getElementById('ti-slicer-drawer');
      if (!dr || !dr.classList.contains('open')) return;
      var wrap = document.getElementById('ti-slices-wrap');
      if (wrap && !wrap.contains(ev.target)) closeSlicerDrawer();
    });

    // PITCH MODE right-click on waveform → open lab card for the virtual slice.
    // Only fires when sliceMode === 0 (PITCH) and a sample is loaded.
    // Slice-mode chop bodies already have their own contextmenu handlers
    // that stopPropagation, so this document-level handler only fires in PITCH mode.
    document.addEventListener('contextmenu', function (ev) {
      if (state.sliceMode !== 0) return;   // SLICE mode has per-chop handlers
      if (state.sampleLengthSamples <= 0) return;  // no sample loaded
      var waveCanvas = document.getElementById('waveform-canvas');
      if (!waveCanvas) return;
      var waveRect = waveCanvas.getBoundingClientRect();
      // Check click is inside the waveform area
      if (ev.clientX < waveRect.left || ev.clientX > waveRect.right) return;
      if (ev.clientY < waveRect.top  || ev.clientY > waveRect.bottom) return;
      ev.preventDefault();
      ev.stopPropagation();
      openChopOverlay(-1);
    });


    // Play-mode toggle (1-SHOT / LOOP) — writes APVTS via setSampleLoopMode.
    // Switching to LOOP auto-disables HOLD (LOOP+HOLD = infinite loop, blocked
    // by design per user clarification 2026-05-25). Mirrors state.sampleLoopMode
    // so the CHOP pill HOLD activation can gate on 1-SHOT.
    document.querySelectorAll('#ti-play-mode-toggle .ti-play-pill').forEach(function (pill) {
      pill.addEventListener('click', function () {
        var mode = parseInt(pill.dataset.play, 10) || 0;
        state.sampleLoopMode = mode;
        document.querySelectorAll('#ti-play-mode-toggle .ti-play-pill').forEach(function (p) {
          p.classList.toggle('active', parseInt(p.dataset.play, 10) === mode);
        });
        // Auto-clear HOLD when switching to LOOP — HOLD can only live in 1-SHOT.
        if (mode === 1 && state.holdMode) {
          state.holdMode = false;
          var chopPill = document.querySelector('#ti-submode-toggle .ti-submode-pill[data-sub="0"]');
          if (chopPill) {
            chopPill.textContent = 'CHOP';
            chopPill.classList.remove('hold-active');
          }
          var holdFn = getNativeFn('setHoldMode');
          if (holdFn) { try { holdFn(false); } catch (_) {} }
        }
        var setFn = getNativeFn('setSampleLoopMode');
        if (setFn) { try { setFn(mode); } catch (_) {} }
      });
    });
    // Pull initial state from C++ (in case DAW restored it from session).
    (function () {
      var getFn = getNativeFn('getSampleLoopMode');
      if (!getFn) return;
      try {
        var p = getFn();
        if (p && typeof p.then === 'function') {
          p.then(function (mode) {
            var m = parseInt(mode, 10) || 0;
            state.sampleLoopMode = m;
            document.querySelectorAll('#ti-play-mode-toggle .ti-play-pill').forEach(function (q) {
              q.classList.toggle('active', parseInt(q.dataset.play, 10) === m);
            });
          });
        }
      } catch (_) {}
    })();
    // Pull HOLD mode from C++ on init so the CHOP pill label + .hold-active
    // class are restored after editor close/reopen or DAW project reload.
    // Gated on 1-SHOT (sampleLoopMode === 0) for symmetry with the LOOP+HOLD
    // block; if the persisted state is somehow HOLD + LOOP (e.g. saved in an
    // old build), force HOLD off and push the correction back to C++.
    (function () {
      var getFn = getNativeFn('getHoldMode');
      if (!getFn) return;
      try {
        var p = getFn();
        if (p && typeof p.then === 'function') {
          p.then(function (enabled) {
            var on = !!enabled;
            if (on && state.sampleLoopMode !== 0) on = false;  // safety
            state.holdMode = on;
            var chopPill = document.querySelector('#ti-submode-toggle .ti-submode-pill[data-sub="0"]');
            if (chopPill) {
              chopPill.textContent = on ? 'HOLD' : 'CHOP';
              chopPill.classList.toggle('hold-active', on);
            }
            if (!enabled !== !on) {
              var setFn = getNativeFn('setHoldMode');
              if (setFn) { try { setFn(on); } catch (_) {} }
            }
          });
        }
      } catch (_) {}
    })();

    // FADE slider — anti-click fade at slice boundaries (CHOP_FADE_MS).
    // Writes to APVTS via setChopFadeMs native fn; reads initial value back on load.
    (function () {
      var slider = document.getElementById('ti-fade-slider');
      var label  = document.getElementById('ti-fade-value');
      if (!slider || !label) return;

      function applyFade (ms) {
        var v = parseFloat(ms) || 0;
        label.textContent = v.toFixed(v < 10 ? 1 : 0) + 'ms';
        var fn = getNativeFn('setChopFadeMs');
        if (fn) { try { fn(v); } catch (_) {} }
      }

      slider.addEventListener('input', function () { applyFade(slider.value); });
      // Double-click → reset FADE to its default (5ms). Matches the
      // reset-to-default convention used elsewhere in the lab card.
      slider.addEventListener('dblclick', function (e) {
        e.preventDefault(); e.stopPropagation();
        slider.value = 5;
        applyFade(5);
      });

      // Restore initial value from C++.
      var getFn = getNativeFn('getChopFadeMs');
      if (getFn) {
        try {
          var p = getFn();
          if (p && typeof p.then === 'function') {
            p.then(function (ms) {
              var v = parseFloat(ms) || 5;
              slider.value = v;
              label.textContent = v.toFixed(v < 10 ? 1 : 0) + 'ms';
            });
          }
        } catch (_) {}
      }
    })();

    // Root picker: click-hold-vertical-drag (knob style). 8 px = 1 semitone.
    // Shift-drag = fine (24 px = 1 semitone). Up = pitch up, down = pitch down.
    var picker = document.getElementById('ti-root-picker');
    if (picker) {
      var dragState = null;

      function onMove (ev) {
        if (!dragState) return;
        var dy = dragState.startY - ev.clientY;     // up is positive
        var pxPerSemi = ev.shiftKey ? 24 : 8;
        var deltaSemi = Math.round(dy / pxPerSemi);
        var newRoot = Math.max(0, Math.min(127, dragState.startRoot + deltaSemi));
        if (newRoot !== state.rootNote) {
          state.rootNote = newRoot;
          updateRootDisplay();
          var fn = getNativeFn('setRootNote');
          if (fn) { try { fn(state.rootNote); } catch (_) {} }
        }
        ev.preventDefault();
      }
      function onUp (ev) {
        if (!dragState) return;
        picker.classList.remove('dragging');
        document.removeEventListener('mousemove', onMove, true);
        document.removeEventListener('mouseup', onUp, true);
        dragState = null;
        ev.preventDefault();
      }
      picker.addEventListener('mousedown', function (ev) {
        if (ev.button !== 0) return;
        dragState = { startY: ev.clientY, startRoot: state.rootNote };
        picker.classList.add('dragging');
        document.addEventListener('mousemove', onMove, true);
        document.addEventListener('mouseup', onUp, true);
        ev.preventDefault();
      });
      // Mousewheel as a quick alternative (1 click = 1 semitone)
      picker.addEventListener('wheel', function (ev) {
        var step = (ev.deltaY < 0) ? 1 : -1;
        state.rootNote = Math.max(0, Math.min(127, state.rootNote + step));
        updateRootDisplay();
        var fn = getNativeFn('setRootNote');
        if (fn) { try { fn(state.rootNote); } catch (_) {} }
        ev.preventDefault();
      }, { passive: false });
      // Double-click → reset root note to C4 (MIDI 60). Standard reset
      // convention applied to the root picker for consistency.
      picker.addEventListener('dblclick', function (ev) {
        ev.preventDefault(); ev.stopPropagation();
        state.rootNote = 60;
        updateRootDisplay();
        var fn = getNativeFn('setRootNote');
        if (fn) { try { fn(60); } catch (_) {} }
      });
    }

    // Repaint on window resize.
    window.addEventListener('resize', function () {
      drawWaveform();
      redrawSliceOverlay();
    });

    // ── Pull initial slicer state from C++ ────────────────────────────────────
    // Cover three scenarios cleanly:
    //   1. Fresh plugin instance: empty list, sub-mode 0, mode PITCH.
    //   2. Editor close+reopen on same instance: slice list survives.
    //   3. DAW project reload: slicesJson restored from state info.
    (function () {
      // Restore pitch-mode virtual slice from C++ (DAW save/restore or editor reopen).
      syncPitchSliceFromCpp();

      var fnSlices = getNativeFn('getSlicesJson');
      if (fnSlices) {
        try {
          var r = fnSlices();
          if (r && typeof r.then === 'function') r.then(applySlicesJson);
          else applySlicesJson(r);
        } catch (_) {}
      }
      var fnSub = getNativeFn('getSliceSubMode');
      if (fnSub) {
        try {
          var s = fnSub();
          if (s && typeof s.then === 'function')
            s.then(function (v) { setSubModeUI(parseInt(v, 10) || 0); });
          else setSubModeUI(parseInt(s, 10) || 0);
        } catch (_) {}
      }
      var fnAct = getNativeFn('getActiveSliceIndex');
      if (fnAct) {
        try {
          var a = fnAct();
          if (a && typeof a.then === 'function')
            a.then(function (v) { state.activeSliceIndex = parseInt(v, 10) || 0; redrawSliceOverlay(); });
          else { state.activeSliceIndex = parseInt(a, 10) || 0; redrawSliceOverlay(); }
        } catch (_) {}
      }
      var fnMode = getNativeFn('getSliceMode');
      if (fnMode) {
        try {
          var m = fnMode();
          if (m && typeof m.then === 'function')
            m.then(function (v) { setSliceModeUI(parseInt(v, 10) || 0); });
          else setSliceModeUI(parseInt(m, 10) || 0);
        } catch (_) {}
      }
    })();

    // ── Restore ALL layer state on editor reopen (Mark 2 Phase 1 fix) ─────────
    // The processor survives editor close/reopen with all 4 layer atomics +
    // sample buffers intact. We hydrate JS state from C++ in this order:
    //   1. Pull editingLayer idx → set state.editingLayerIdx + .active pad
    //   2. Pull all 4 cached payloads via getAllLayerPayloads (rich format:
    //      peaks + meta + sliceMode + sampleLoopMode + rootMidiNote + activeSliceIndex).
    //   3. For each populated layer, dispatch onLayerSampleMirror so the
    //      state.layerStates[] entry is built and a future pad-click can
    //      restore the layer's UI.
    //   4. For the editing layer specifically, ALSO dispatch onSampleLoaded so
    //      the visible state.* is fully populated and the waveform renders.
    //   5. Apply the editing layer's per-layer UI surfaces (mode pills, ROOT,
    //      sample-loop pill) from the just-populated state.*.
    //   6. Pull the editing layer's pitch slice + slices from C++ (these still
    //      live in per-layer C++ state and aren't shipped in the payload).
    (function () {
      var elFn = getNativeFn('getEditingLayerIdx');
      var loadFn = getNativeFn('getAllLayerPayloads');
      if (!elFn || !loadFn) {
        // Fallback for backward compat: pull only the editing layer's payload
        // via the legacy single-payload native fn.
        var legacy = getNativeFn('getCachedSamplePayload');
        if (!legacy) return;
        try {
          var p = legacy();
          if (p && typeof p.then === 'function') {
            p.then(function (json) {
              if (!json || typeof json !== 'string' || json.length === 0) return;
              try { if (window.onSampleLoaded) window.onSampleLoaded(JSON.parse(json)); } catch (_) {}
            });
          }
        } catch (_) {}
        return;
      }

      // Step 1: read editingLayer + set state + .active pad class.
      var p1 = elFn();
      var p2 = loadFn();
      Promise.all([
        (p1 && typeof p1.then === 'function') ? p1 : Promise.resolve(p1),
        (p2 && typeof p2.then === 'function') ? p2 : Promise.resolve(p2)
      ]).then(function (results) {
        var elIdx = Math.max(0, Math.min(3, parseInt(results[0], 10) || 0));
        var payloads = results[1];
        state.editingLayerIdx = elIdx;
        // .active pad class
        var pads = document.querySelectorAll('#ti-layer-pads .ti-layer-pad');
        for (var i = 0; i < pads.length && i < 4; ++i) {
          pads[i].classList.toggle('active', i === elIdx);
        }

        // Step 2-3: dispatch onLayerSampleMirror for each layer.
        if (payloads && payloads.length) {
          for (var j = 0; j < 4 && j < payloads.length; ++j) {
            if (window.onLayerSampleMirror) window.onLayerSampleMirror(j, payloads[j]);
          }
        }

        // Step 4-5: editing layer drives the visible UI.
        var elMirror = state.layerStates[elIdx];
        if (elMirror) {
          // onSampleLoaded populates state.* (peaks, peakScale, length) and
          // triggers drawWaveform + redrawSliceOverlay + syncPitchSliceFromCpp.
          if (window.onSampleLoaded) window.onSampleLoaded({
            filename:      '',
            sampleRate:    0,
            lengthSamples: elMirror.sampleLengthSamples,
            numChannels:   2,
            peaksMin:      elMirror.peaksMin,
            peaksMax:      elMirror.peaksMax
          });
          // Also restore the mode/root state.* from the mirror so the pills
          // show the right values (onSampleLoaded above doesn't touch these).
          state.rootNote       = elMirror.rootNote;
          state.sliceMode      = elMirror.sliceMode;
          state.sampleLoopMode = elMirror.sampleLoopMode;
          state.activeSliceIndex = elMirror.activeSliceIndex;
          if (typeof applyLayerStateToUI === 'function') applyLayerStateToUI();
        } else {
          // Editing layer had no sample → make sure #hero shows empty-state.
          var hero = document.getElementById('hero');
          if (hero) {
            hero.classList.remove('has-sample');
            hero.classList.remove('has-sample-missing');
            hero.classList.add('empty-state');
          }
        }
      }).catch(function () {});
    })();
  }

  function updateRootDisplay () {
    var el = document.getElementById('ti-root-value');
    if (el) el.textContent = midiToName(state.rootNote);
  }

  // ── JUCE event hooks (called from C++ via webView->evaluateJavascript) ────
  window.onDragHover = function (active) {
    var hero = document.getElementById('hero');
    if (!hero) return;
    if (active) hero.classList.add('drag-hover');
    else hero.classList.remove('drag-hover');
  };

  window.onLoadingStarted = function (filename) {
    state.loading = true;
    state.progress = 0;
    var hero = document.getElementById('hero');
    if (hero) {
      hero.classList.remove('empty-state');
      hero.classList.remove('has-sample-missing');
      hero.classList.add('has-sample');
    }
    drawWaveform();
  };

  window.onLoadingProgress = function (p) {
    state.progress = Math.max(0, Math.min(1, p));
    drawWaveform();
  };
)TIHX")
      + juce::String (R"TIHX(
  window.onSampleLoaded = function (info) {
    if (!info) return;
    state.peaksMin = info.peaksMin || null;
    state.peaksMax = info.peaksMax || null;
    // Recompute peak-aware display scale. Loud samples (peak > 0.80) scale down so the
    // waveform doesn't slam into the dice/randomize/help buttons or the chop number row.
    // Quiet samples render at peakScale=1.0 (native amplitude) — see state.peakScale doc.
    state.peakScale = 1.0;
    if (state.peaksMin && state.peaksMax && state.peaksMin.length === state.peaksMax.length) {
      var maxAbs = 0;
      var n = state.peaksMin.length;
      for (var pi = 0; pi < n; ++pi) {
        var a = state.peaksMax[pi]; if (a < 0) a = -a;
        var b = state.peaksMin[pi]; if (b < 0) b = -b;
        if (a > maxAbs) maxAbs = a;
        if (b > maxAbs) maxAbs = b;
      }
      var CEILING = 0.70;
      if (maxAbs > CEILING) state.peakScale = CEILING / maxAbs;
    }
    state.progress = 1;
    state.loading = false;
    // Total length in samples — used by redrawSliceOverlay to position markers.
    // Field name is `lengthSamples` per the C++ payload shape.
    state.sampleLengthSamples = parseInt(info.lengthSamples, 10) || 0;
    var hero = document.getElementById('hero');
    if (hero) {
      hero.classList.remove('empty-state');
      hero.classList.remove('has-sample-missing');
      hero.classList.add('has-sample');
    }
    drawWaveform();
    redrawSliceOverlay();  // re-position markers now that we know sample length
    // Sync pitch-mode virtual slice bounds from C++ after new sample load.
    // C++ has already reset startSample/endSample; we pull the preserved
    // warp/ADSR/scan fields so JS state stays in sync.
    syncPitchSliceFromCpp();
    // Mark 2 Phase 1 visual-fix: persist this load into the per-layer mirror
    // so a later pad click swap → swap-back preserves the loaded sample.
    state.layerStates[state.editingLayerIdx] = snapshotCurrentLayer();
  };

  window.onLoadError = function (msg) {
    state.loading = false;
    if (typeof showStatus === 'function') showStatus(msg || 'Load error', 5000);
    // If we never had a sample, return to empty state.
    if (!state.peaksMin) {
      var hero = document.getElementById('hero');
      if (hero) {
        hero.classList.remove('has-sample');
        hero.classList.add('empty-state');
      }
    }
  };

  // ── Native function hook: setRootNote → APVTS ─────────────────────────────
  // (Registered on the C++ side as a no-op until later — for now JS keeps a
  // local rootNote that the audio thread reads via the existing rootNoteMidi
  // atomic. If the native function exists, it's used; if not, the local
  // value still drives the JS UI.)

  // ── Slice play-glow polling ───────────────────────────────────────────────
  // Pulls per-slice envelope levels from C++ at ~60 Hz and writes them onto
  // each slice body as a CSS custom property (--glow-alpha). The CSS uses
  // that property to drive a radial-gradient bloom layered behind the body.
  // Defensive — silently no-ops until the native fn + overlays exist.
  function pollSliceGlow () {
    var fn = getNativeFn('getSliceGlowLevels');
    if (!fn) return;
    fn().then(function (vals) {
      if (!vals || !vals.length) return;
      var n = Math.min(vals.length, state.sliceGlow.length);
      for (var i = 0; i < n; ++i) state.sliceGlow[i] = vals[i];
      applySliceGlow();
    }).catch(function () {});

    // Per-layer .playing indicator — each A/B/C/D pad lights up independently.
    // Mark 2 Phase 1 task 11: uses getLayerVoiceActivity (4-element bool array).
    var actFn = getNativeFn('getLayerVoiceActivity');
    if (actFn) {
      actFn().then(function (arr) {
        if (!arr || arr.length !== 4) return;
        var pads = document.querySelectorAll('#ti-layer-pads .ti-layer-pad');
        for (var i = 0; i < pads.length && i < 4; ++i) {
          if (arr[i]) pads[i].classList.add('playing');
          else        pads[i].classList.remove('playing');
        }
      }).catch(function () {});
    }
  }

  function applySliceGlow () {
    var overlays = document.getElementById('ti-slice-overlays');
    if (!overlays) return;
    var bodies = overlays.querySelectorAll('.ti-slice-body');
    for (var i = 0; i < bodies.length; ++i) {
      var g = state.sliceGlow[i] || 0;
      bodies[i].style.setProperty('--glow-alpha', g.toFixed(3));
    }
  }

  setInterval(pollSliceGlow, 16);   // ~60 Hz

  // ── Per-layer empty-state prompt polling (Mark 2 Phase 1 Task 7) ─────────
  // Polls ~10 Hz — slow is fine, only changes on layer-switch or sample-load.
  // When the editing layer has no sample, #hero gets 'empty-state' so the
  // "DRAG SAMPLE OR CLICK TO LOAD" prompt appears. When it has a sample, the
  // class is removed and the waveform is shown instead.
  function pollLayerEmptyState () {
    var fn = getNativeFn('getLayerHasSample');
    if (!fn) return;
    fn().then(function (has) {
      var hero = document.getElementById('hero');
      if (!hero) return;
      if (has) hero.classList.remove('empty-state');
      else     hero.classList.add('empty-state');
    }).catch(function () {});
  }

  setInterval(pollLayerEmptyState, 100);  // 10 Hz

  // ── Scan-line viz polling ─────────────────────────────────────────────────
  // Draws a 1.5px purple line on each scan-active chop while a note is
  // sounding. Also draws a faint window-highlight + dashed borders when
  // scanWindow < 1.0 (narrowed via mod).
  //
  // Architecture (two-loop):
  //   pollScanViz() fires at ~30 Hz (33 ms setInterval) — fetches truth from C++
  //     and stores per-slice truth + inferred velocity.
  //   tickScanViz() runs at display refresh rate (rAF) — interpolates visual
  //     position between polls and lerps opacity for fade in/out.
  //
  // Per-slice entry in _scanInterp[idx]:
  //   truth        — last values from C++ { pos, winStart, winEnd, timestamp }
  //   predictedPos — interpolated draw position [0..1]
  //   velocity     — dPos/ms (EMA-smoothed), used to extrapolate between polls
  //   opacity      — current draw opacity [0..1], lerps toward opacityTarget
  //   opacityTarget— 1.0 when voice active, 0.0 when silent
  //
  // chopLayouts / waveformPixelWidth are set by redrawSliceOverlay().

  var _scanInterp = {};  // keyed by slice index (string)

  // Poll: fetch truth from C++ and update velocity estimate.
  function pollScanViz () {
    var getScanPos    = getNativeFn('getScanPosition');
    var getScanBounds = getNativeFn('getScanWindowBounds');
    if (!getScanPos || !getScanBounds) return;
    var now = performance.now();

    // Bug D — pitch mode scan viz: poll sliceIndex=-1 when in Whole-sample mode.
    if (state.sliceMode === 0) {
      var ps = state.pitchModeSlice;
      var pitchScanEnabled = ps && ps.scanEnabled;
      var pitchKey = 'pitch';
      if (!pitchScanEnabled) {
        if (_scanInterp[pitchKey]) {
          _scanInterp[pitchKey].opacityTarget = 0.0;
          _scanInterp[pitchKey].truth = null;
        }
      } else {
        if (!_scanInterp[pitchKey]) {
          _scanInterp[pitchKey] = { truth: null, predictedPos: 0, velocity: 0, opacity: 0.0, opacityTarget: 0.0 };
        }
        var pEntry = _scanInterp[pitchKey];
        var pPosPromise    = getScanPos(-1);
        var pBoundsPromise = getScanBounds(-1);
        var pPosVal = null, pBoundsVal = null, pGotPos = false, pGotBounds = false;
        function tryMergePitch () {
          if (!pGotPos || !pGotBounds) return;
          var active = (typeof pPosVal === 'number' && pPosVal >= 0);
          pEntry.opacityTarget = active ? 1.0 : 0.0;
          if (active) {
            var winStart = (pBoundsVal && typeof pBoundsVal.start === 'number') ? pBoundsVal.start : 0;
            var winEnd   = (pBoundsVal && typeof pBoundsVal.end   === 'number') ? pBoundsVal.end   : 1;
            if (pEntry.truth && pEntry.truth.pos >= 0) {
              var dt = now - pEntry.truth.timestamp;
              if (dt > 0) {
                var dPos = pPosVal - pEntry.truth.pos;
                if (Math.abs(dPos) < 0.3) {
                  var rawVel = dPos / dt;
                  var prevSign = pEntry.velocity > 0 ? 1 : (pEntry.velocity < 0 ? -1 : 0);
                  var newSign  = rawVel  > 0 ? 1 : (rawVel  < 0 ? -1 : 0);
                  if (prevSign !== 0 && newSign !== 0 && prevSign !== newSign) {
                    pEntry.velocity = rawVel;
                  } else {
                    pEntry.velocity = (pEntry.velocity === 0) ? rawVel : 0.7 * pEntry.velocity + 0.3 * rawVel;
                  }
                } else { pEntry.velocity = 0; }
              }
            } else { pEntry.velocity = 0; }
            pEntry.truth = { pos: pPosVal, winStart: winStart, winEnd: winEnd, timestamp: now };
            pEntry.predictedPos = pPosVal;
          } else {
            pEntry.truth = null; pEntry.velocity = 0;
          }
        }
        pPosPromise.then(function (v) {
          pPosVal = (typeof v === 'number') ? v : -1; pGotPos = true; tryMergePitch();
        }).catch(function () { pPosVal = -1; pGotPos = true; tryMergePitch(); });
        pBoundsPromise.then(function (v) {
          pBoundsVal = v; pGotBounds = true; tryMergePitch();
        }).catch(function () { pBoundsVal = null; pGotBounds = true; tryMergePitch(); });
      }
      return;  // In pitch mode, don't poll slice array
    }

    if (!state.slices || state.slices.length === 0) {
      _scanInterp = {};
      return;
    }
    var n = state.slices.length;
    for (var i = 0; i < n; ++i) {
      (function (idx) {
        var s = state.slices[idx];
        var key = '' + idx;
        if (!s || !s.scanEnabled) {
          // Mark as inactive (let opacity lerp to 0 in rAF loop).
          if (_scanInterp[key]) {
            _scanInterp[key].opacityTarget = 0.0;
            _scanInterp[key].truth = null;
          }
          return;
        }
        // Ensure entry exists.
        if (!_scanInterp[key]) {
          _scanInterp[key] = {
            truth:         null,
            predictedPos:  0,
            velocity:      0,
            opacity:       0.0,
            opacityTarget: 0.0
          };
        }
        var entry = _scanInterp[key];
        var posPromise    = getScanPos(idx);
        var boundsPromise = getScanBounds(idx);
        var posVal = null, boundsVal = null, gotPos = false, gotBounds = false;
        function tryMerge () {
          if (!gotPos || !gotBounds) return;
          var active = (typeof posVal === 'number' && posVal >= 0);
          entry.opacityTarget = active ? 1.0 : 0.0;
          if (active) {
            var winStart = (boundsVal && typeof boundsVal.start === 'number') ? boundsVal.start : 0;
            var winEnd   = (boundsVal && typeof boundsVal.end   === 'number') ? boundsVal.end   : 1;
            // Compute velocity from previous truth (ignore flips / wraps).
            if (entry.truth && entry.truth.pos >= 0) {
              var dt = now - entry.truth.timestamp;
              if (dt > 0) {
                var dPos = posVal - entry.truth.pos;
                // If jump > 0.3 of full range, it's a wrap or flip — skip velocity.
                if (Math.abs(dPos) < 0.3) {
                  var rawVel = dPos / dt;
                  // Bug C — direction-flip damping: when the sign of velocity flips
                  // (ping-pong turnaround), snap velocity to zero immediately instead
                  // of EMA-blending through zero. This prevents the interpolator from
                  // "coasting" past the boundary in the old direction for one poll
                  // interval, which produced the overshooting jitter at direction flips.
                  var prevSign = entry.velocity > 0 ? 1 : (entry.velocity < 0 ? -1 : 0);
                  var newSign  = rawVel  > 0 ? 1 : (rawVel  < 0 ? -1 : 0);
                  if (prevSign !== 0 && newSign !== 0 && prevSign !== newSign) {
                    // Direction flipped — use raw velocity directly; no EMA blend.
                    entry.velocity = rawVel;
                  } else {
                    // Same direction — EMA smooth: blend 30% new reading into estimate.
                    entry.velocity = (entry.velocity === 0)
                      ? rawVel
                      : 0.7 * entry.velocity + 0.3 * rawVel;
                  }
                } else {
                  entry.velocity = 0;  // reset on large discontinuity (wrap / flip missed by EMA)
                }
              }
            } else {
              entry.velocity = 0;
            }
            entry.truth = { pos: posVal, winStart: winStart, winEnd: winEnd, timestamp: now };
            entry.predictedPos = posVal;  // rAF loop will extrapolate from here
          } else {
            entry.truth    = null;
            entry.velocity = 0;
          }
        }
        posPromise.then(function (v) {
          posVal = (typeof v === 'number') ? v : -1;
          gotPos = true;
          tryMerge();
        }).catch(function () { posVal = -1; gotPos = true; tryMerge(); });
        boundsPromise.then(function (v) {
          boundsVal = v;
          gotBounds = true;
          tryMerge();
        }).catch(function () { boundsVal = null; gotBounds = true; tryMerge(); });
      })(i);
    }
  }

  // rAF loop: interpolate position, lerp opacity, then draw.
  var _scanRafId = null;
  function tickScanViz () {
    var now = performance.now();
    var keys = Object.keys(_scanInterp);
    for (var k = 0; k < keys.length; ++k) {
      var key   = keys[k];
      var entry = _scanInterp[key];
      // Opacity lerp — ~100ms fade (0.15 at 60 fps ≈ 9 frames to 75%).
      var diff = entry.opacityTarget - entry.opacity;
      entry.opacity += diff * 0.15;
      if (entry.opacity < 0.005 && entry.opacityTarget === 0.0) entry.opacity = 0.0;
      if (entry.opacity > 0.995 && entry.opacityTarget === 1.0) entry.opacity = 1.0;
      // Position extrapolation.
      if (entry.truth && entry.truth.pos >= 0 && entry.velocity !== 0) {
        var dt = now - entry.truth.timestamp;
        var pred = entry.truth.pos + entry.velocity * dt;
        // Clamp to window bounds.
        var lo = entry.truth.winStart;
        var hi = entry.truth.winEnd;
        pred = Math.max(lo, Math.min(hi, pred));
        entry.predictedPos = pred;
      }
    }
    // Wrap drawScanViz in try/catch so a thrown exception (e.g. layouts
    // undefined, see drawScanViz comments) cannot prevent the next rAF
    // from being scheduled. Previously a single throw here killed the
    // entire scan-viz loop for the life of the editor session.
    try { drawScanViz(); } catch (_) {}
    _scanRafId = requestAnimationFrame(tickScanViz);
  }

  function drawScanViz () {
    var canvas = document.getElementById('ti-scan-viz-canvas');
    if (!canvas) return;

    // Size canvas to match the waveform canvas bounds (same as ti-slice-overlays).
    var waveCanvas = document.getElementById('waveform-canvas');
    if (!waveCanvas) return;
    var layouts = state.chopLayouts;
    // In pitch mode (sliceMode===0) layouts is empty — don't return early, we
    // still need to draw the pitch-mode scan line below the layout loop.
    var isPitchMode = (state.sliceMode === 0);
    if ((!layouts || layouts.length === 0) && !isPitchMode) {
      var ctx0 = canvas.getContext('2d');
      var dpr0 = window.devicePixelRatio || 1;
      canvas.width  = Math.max(1, Math.round(canvas.offsetWidth  * dpr0));
      canvas.height = Math.max(1, Math.round(canvas.offsetHeight * dpr0));
      ctx0.clearRect(0, 0, canvas.width, canvas.height);
      return;
    }

    var dpr = window.devicePixelRatio || 1;
    var W   = state.waveformPixelWidth || canvas.offsetWidth;
    var waveRect = waveCanvas.getBoundingClientRect();
    var BOTTOM_RESERVE = 50;
    var H = Math.max(0, waveRect.height - BOTTOM_RESERVE);

    var cW = Math.max(1, Math.round(W   * dpr));
    var cH = Math.max(1, Math.round(H   * dpr));
    if (canvas.width !== cW || canvas.height !== cH) {
      canvas.width  = cW;
      canvas.height = cH;
      canvas.style.width  = W + 'px';
      canvas.style.height = H + 'px';
    }

    var ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);

    // Guard against state.chopLayouts being undefined — happens whenever
    // the editor opens in PITCH mode, because redrawSliceOverlay's slice
    // path (which populates chopLayouts) never runs. Without this guard,
    // `layouts.length` throws TypeError, tickScanViz doesn't schedule its
    // next rAF, and the entire scan-viz loop dies permanently — every
    // subsequent slice/pitch mode entry shows no scan line at all.
    //
    // Also: when in pitch mode, SKIP the slice-mode loop entirely. Stale
    // _scanInterp numeric entries from a prior slice-mode session keep
    // their opacity until the fade lerp finishes, and chopLayouts isn't
    // cleared on mode switch — without this guard, the old chop scan line
    // renders as a ghost vertical bar on top of the pitch-mode waveform
    // until opacity fades to 0. User-visible bug 2026-05-25.
    var n = (!isPitchMode && layouts && layouts.length) ? layouts.length : 0;
    for (var i = 0; i < n; ++i) {
      var key   = '' + i;
      var entry = _scanInterp[key];
      if (!entry || entry.opacity < 0.005) continue;  // nothing to draw

      var layout = layouts[i];
      var chopX  = layout.visualLeft;
      var chopW  = layout.visualWidth;
      if (chopW < 1) continue;

      var op = entry.opacity;  // [0..1]

      // Window highlight — only when narrowed (winEnd - winStart < ~full).
      if (entry.truth && (entry.truth.winEnd - entry.truth.winStart) < 0.99) {
        var wx  = chopX + entry.truth.winStart * chopW;
        var ww  = (entry.truth.winEnd - entry.truth.winStart) * chopW;
        ctx.fillStyle = 'rgba(255, 255, 255, ' + (0.04 * op).toFixed(3) + ')';
        ctx.fillRect(wx, 0, ww, H);
        ctx.strokeStyle = 'rgba(255, 255, 255, ' + (0.25 * op).toFixed(3) + ')';
        ctx.lineWidth = 1;
        ctx.setLineDash([3, 3]);
        ctx.beginPath();
        ctx.moveTo(wx,      0); ctx.lineTo(wx,      H);
        ctx.moveTo(wx + ww, 0); ctx.lineTo(wx + ww, H);
        ctx.stroke();
        ctx.setLineDash([]);
      }

      // Scan line — 1.5px white at 70% peak opacity (no more purple-on-purple).
      var lineX = chopX + entry.predictedPos * chopW;
      ctx.fillStyle = 'rgba(255, 255, 255, ' + (op * 0.7).toFixed(3) + ')';
      ctx.fillRect(Math.floor(lineX) - 0.75, 0, 1.5, H);
    }

    // Bug D — pitch mode scan line: draw over the full-width waveform in Whole mode.
    if (state.sliceMode === 0) {
      var pitchEntry = _scanInterp['pitch'];
      if (pitchEntry && pitchEntry.opacity >= 0.005) {
        var ps = state.pitchModeSlice;
        var totalS = state.sampleLengthSamples || 1;
        var inNorm  = ps ? Math.max(0, Math.min(1, (ps.startSample || 0) / totalS)) : 0;
        var outNorm = ps ? Math.max(0, Math.min(1, ((ps.endSample && ps.endSample > 0) ? ps.endSample : totalS) / totalS)) : 1;
        var rangeW  = (outNorm - inNorm) * W;
        // predictedPos is 0..1 within the scan window, which is within [inNorm..outNorm].
        // Map to absolute pixel: start of IN range + predictedPos * range width.
        var scanPixel = inNorm * W + pitchEntry.predictedPos * rangeW;
        ctx.fillStyle = 'rgba(255, 255, 255, ' + (pitchEntry.opacity * 0.7).toFixed(3) + ')';
        ctx.fillRect(Math.floor(scanPixel) - 0.75, 0, 1.5, H);
      }
    }
  }

  // Start the rAF draw loop once, and keep the slower C++ poll running too.
  requestAnimationFrame(tickScanViz);
  setInterval(pollScanViz, 16);   // ~60 Hz truth updates from C++ (Bug C: halved lag at direction flips)

  // Initial render kick after DOM ready (handles mid-page-load injection too).
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', injectHeroOverlays);
  } else {
    injectHeroOverlays();
  }

  // ── Mark 2 Phase 1 Task 6 (visual-fix update): A/B/C/D pad clicks ────────
  // Event delegation on document — works even if pads re-inject. The
  // existing click-to-load handler (maybeOpenPicker) already skips
  // #ti-layer-pads in its closest() guard, so there is no conflict.
  //
  // switchEditingLayer() handles the C++ atomic flip, snapshot/restore of
  // per-layer JS mirrors, and a full UI re-render. The .active pad lighting
  // is purely cosmetic and stays here so it updates instantly on click.
  document.addEventListener('click', function (e) {
    var pad = e.target && e.target.closest && e.target.closest('#ti-layer-pads .ti-layer-pad');
    if (!pad) return;
    var idxStr = pad.getAttribute('data-layer-idx');
    if (idxStr == null) return;
    var idx = parseInt(idxStr, 10);
    if (!isFinite(idx) || idx < 0 || idx > 3) return;

    // Visual: exactly one pad holds .active at a time
    var pads = document.querySelectorAll('#ti-layer-pads .ti-layer-pad');
    for (var i = 0; i < pads.length; ++i) pads[i].classList.remove('active');
    pad.classList.add('active');

    // Snapshot leaving layer → flip C++ atomic → restore entering layer →
    // re-render. No-op if user clicks the already-active pad.
    if (typeof window.switchEditingLayer === 'function') {
      window.switchEditingLayer(idx);
    }
  });

  // ── PITCH-mode double-click on waveform → unload editing layer's sample ──
  // User-requested gesture: in PITCH mode, double-click the waveform to wipe
  // the loaded sample on the currently-viewed layer (returns to drag-prompt).
  // SLICE mode keeps its existing behavior — slice-body dblclick adds a chop
  // marker (different element, no conflict). Other layers untouched. Mode /
  // root / 1-SHOT settings preserved so the next sample drop reuses them.
  document.addEventListener('dblclick', function (e) {
    if (state.sliceMode !== 0) return;                          // PITCH only
    var hero = document.getElementById('hero');
    if (!hero || !hero.classList.contains('has-sample')) return; // nothing to clear
    var waveCanvas = document.getElementById('waveform-canvas');
    if (!waveCanvas) return;
    // React anywhere in the waveform zone. In PITCH mode a full-width
    // .ti-slice-body (pointer-events:auto) sits OVER the canvas and would
    // otherwise swallow the dblclick — so accept the slice-overlay / scan-viz
    // layers too. Pads / pills / buttons live outside these, so they're excluded.
    var inWave = (e.target === waveCanvas) || waveCanvas.contains(e.target)
              || (e.target.closest && e.target.closest('#ti-slice-overlays, #ti-scan-viz-canvas'));
    if (! inWave) return;

    e.preventDefault();
    e.stopPropagation();

    // 1. Tell C++ to drop the layer's audio + slices + cache + warp state.
    var fn = getNativeFn('clearEditingLayer');
    if (fn) { try { fn(); } catch (_) {} }

    // 2. Reset JS state.* per-layer DATA fields. Mode / root / loop pill
    //    settings are intentionally preserved — see header comment.
    state.peaksMin            = null;
    state.peaksMax            = null;
    state.peakScale           = 1.0;
    state.sampleLengthSamples = 0;
    state.slices              = [];
    state.activeSliceIndex    = 0;
    state.pitchModeSlice      = makePitchModeSliceDefault();

    // 3. Mark this layer's mirror as fresh-empty so a future pad-switch sees
    //    "no sample" and restores empty-state correctly.
    state.layerStates[state.editingLayerIdx] = null;

    // 4. Flip the hero DOM to empty-state (the 10Hz poller would catch this
    //    eventually; doing it synchronously avoids a visible 100ms flash of
    //    the cleared waveform).
    hero.classList.remove('has-sample');
    hero.classList.remove('has-sample-missing');
    hero.classList.remove('drag-hover');
    hero.classList.add('empty-state');

    // 5. Repaint so the canvas + slice overlay clear immediately.
    if (typeof drawWaveform === 'function') drawWaveform();
    if (typeof redrawSliceOverlay === 'function') redrawSliceOverlay();
  });

  // Helper accessible to other scripts (status pill from drag-drop bridge).
  window.tiSetRootNote = function (m) {
    state.rootNote = Math.max(0, Math.min(127, m));
    updateRootDisplay();
  };
)TIHX")
      + juce::String (R"TIHX(

  // ════════════════════════════════════════════════════════════════════════
  // Mix page Phase B — 4 channel strips (LEFT half of bottom panel)
  // Phase C will fill #mix-trigger-area with the 5-pill trigger row.
  // Phase D will fill #mix-stem-area with the stem capture controls.
  // ════════════════════════════════════════════════════════════════════════
  (function setupMixPage () {
    function injectMixPanelStyles () {
      if (document.getElementById('mix-panel-styles')) return;
      var style = document.createElement('style');
      style.id = 'mix-panel-styles';
      // Terrain identity (signed-off mockup): soft glass tiles, arc knobs,
      // thin faders w/ circle handle, GREEN meters, outlined pills, breathing
      // room. No grid, no tick marks, no segmented LEDs. Theme-var driven so
      // light + dark both read clean ("scientific minimalism").
      style.textContent = ''
        + '#mix-panel{display:none;width:820px;height:272px;flex-shrink:0;'
        +   'border-top:1px solid var(--border);z-index:15;'
        +   'position:relative;flex-direction:row;color:var(--text-primary);'
        +   'background:var(--bg-surface);overflow:hidden;}'
        + '#mix-panel.open{display:flex;}'
        + '#mix-strips{display:flex;flex-direction:row;align-items:stretch;'
        +   'padding:11px 12px;gap:9px;width:50%;box-sizing:border-box;}'
        // Slightly transparent glass box (panel shows through). [data-theme]
        // overrides below tune the tint per theme. Border anchors the box.
        + '.mix-strip{flex:1;display:flex;flex-direction:column;align-items:center;'
        +   'gap:6px;padding:9px 4px 8px;border-radius:13px;'
        +   'background:rgba(124,92,191,0.06);border:1px solid var(--border);'
        +   'box-shadow:inset 0 1px 0 rgba(255,255,255,0.05);'
        +   'min-width:0;color:var(--text-primary);}'
        + '.mix-strip-header{font-weight:600;font-size:12px;display:flex;'
        +   'align-items:center;gap:5px;letter-spacing:1px;}'
        + '.mix-strip-header .play-dot{width:5px;height:5px;border-radius:50%;'
        +   'background:var(--border-strong);transition:background 0.1s;}'
        // Playing dot: purple on light theme, white on dark theme (per-theme contrast).
        + '.mix-strip.playing .mix-strip-header .play-dot{'
        +   'background:var(--purple-500);box-shadow:0 0 7px var(--purple-400);}'
        + '[data-theme="dark"] .mix-strip.playing .mix-strip-header .play-dot{'
        +   'background:#fff;box-shadow:0 0 8px rgba(255,255,255,0.65);}'
        // Arc knob — thin ring + purple arc swept by --val (0..1), masked center.
        + '.mix-strip-knob{width:25px;height:25px;border-radius:50%;cursor:pointer;'
        +   'position:relative;flex-shrink:0;'
        +   'background:conic-gradient(from 225deg,'
        +     'var(--purple-500) 0deg,'
        +     'var(--purple-500) calc(var(--val,0.5) * 270deg),'
        +     'var(--knob-track) calc(var(--val,0.5) * 270deg),'
        +     'var(--knob-track) 270deg,'
        +     'transparent 270deg);'
        +   '-webkit-mask:radial-gradient(closest-side,transparent 66%,#000 67%);'
        +   'mask:radial-gradient(closest-side,transparent 66%,#000 67%);}'
        + '.mix-strip-knob[data-fn="jitter"]{width:21px;height:21px;}'
        + '.mix-strip-knob-label{font-size:7.5px;letter-spacing:0.8px;'
        +   'color:var(--text-muted);text-transform:uppercase;margin-top:-3px;}'
        + '.mix-strip-fader-meter{flex:1;display:flex;flex-direction:row;'
        +   'align-items:stretch;justify-content:center;gap:7px;width:100%;'
        +   'min-height:48px;}'
        // Thin fader: track + bottom-up fill + circle handle (no ticks).
        + '.mix-strip-fader{position:relative;width:4px;border-radius:2px;'
        +   'background:var(--knob-track);cursor:pointer;}'
        + '.mix-strip-fader-fill{position:absolute;left:0;right:0;bottom:0;'
        +   'border-radius:2px;background:linear-gradient(to top,var(--purple-600),var(--purple-400));}'
        + '.mix-strip-fader-handle{position:absolute;left:50%;width:15px;height:15px;'
        +   'border-radius:50%;transform:translate(-50%,50%);'
        +   'background:var(--bg-card);border:2px solid var(--purple-500);'
        +   'box-shadow:0 1px 4px rgba(0,0,0,0.25);}'
        // Meter — smooth fill, no segments, soft glow. Purple on light theme
        // (white-on-white is invisible), white on dark theme.
        + '.mix-strip-meter{position:relative;width:3px;border-radius:2px;'
        +   'background:var(--knob-track);overflow:hidden;}'
        + '.mix-strip-meter-fill{position:absolute;left:0;right:0;bottom:0;'
        +   'border-radius:2px;'
        +   'background:linear-gradient(to top,var(--purple-600),var(--purple-400));'
        +   'box-shadow:0 0 6px rgba(139,92,246,0.5);'
        +   'transition:height 0.04s linear;}'
        + '[data-theme="dark"] .mix-strip-meter-fill{'
        +   'background:linear-gradient(to top,rgba(255,255,255,0.75),#ffffff);'
        +   'box-shadow:0 0 6px rgba(255,255,255,0.55);}'
        // M / S outlined pills.
        + '.mix-strip-buttons{display:flex;flex-direction:row;gap:5px;}'
        + '.mix-strip-btn{width:20px;height:16px;font-size:8.5px;font-weight:700;'
        +   'border:1px solid var(--border-strong);background:transparent;'
        +   'color:var(--text-muted);border-radius:6px;cursor:pointer;'
        +   'padding:0;font-family:inherit;}'
        + '.mix-strip-btn:hover{border-color:var(--purple-500);}'
        + '.mix-strip-btn.active[data-fn="mute"]{background:var(--text-secondary);color:var(--bg-surface);border-color:var(--text-secondary);}'
        + '.mix-strip-btn.active[data-fn="solo"]{background:var(--purple-500);color:#fff;border-color:var(--purple-500);}'
        + '#mix-right{flex:1;display:flex;flex-direction:column;padding:11px 12px;'
        +   'gap:7px;box-sizing:border-box;}'
        + '#mix-trigger-area,#mix-stem-area{border-radius:13px;'
        +   'background:rgba(124,92,191,0.05);border:1px solid var(--border);'
        +   'box-shadow:inset 0 1px 0 rgba(255,255,255,0.04);'
        +   'padding:9px 12px;color:var(--text-secondary);font-size:11px;'
        +   'box-sizing:border-box;}'
        // No bottom padding for the trigger tile — KEYTRACK / VEL bars (and the
        // LAYER morph / RR controls / RANDOM last row) sit flush with the bottom edge.
        + '#mix-trigger-area{padding-bottom:0;}'
        // Trigger area absorbs leftover height and clips tall modes (RANDOM/VEL)
        // INSIDE the panel; stem area keeps its natural height + stays fully
        // visible. Panel overflow:hidden is the hard guard against footer bleed.
        + '#mix-trigger-area{flex:1 1 auto;min-height:0;overflow:hidden;display:flex;flex-direction:column;}'
        + '#mix-stem-area{flex:0 0 auto;padding-top:5px;}'
        // Slightly-transparent box tints, per theme (panel shows through).
        + '[data-theme="dark"] .mix-strip,'
        + '[data-theme="dark"] #mix-trigger-area,'
        + '[data-theme="dark"] #mix-stem-area{background:rgba(255,255,255,0.05);}'
        ;
      document.head.appendChild(style);
    }

    function buildMixPanel () {
      if (document.getElementById('mix-panel')) return;
      var panel = document.createElement('div');
      panel.id = 'mix-panel';

      var strips = document.createElement('div');
      strips.id = 'mix-strips';
      panel.appendChild(strips);

      var letters = ['A', 'B', 'C', 'D'];
      for (var i = 0; i < 4; ++i) {
        var letter = letters[i];
        var strip = document.createElement('div');
        strip.className = 'mix-strip';
        strip.setAttribute('data-layer', String(i));
        strip.innerHTML = ''
          + '<div class="mix-strip-header">' + letter + ' <span class="play-dot"></span></div>'
          + '<div class="mix-strip-knob" data-fn="pan" title="Pan ' + letter + ' - drag, dbl-click resets"></div>'
          + '<div class="mix-strip-knob-label">PAN</div>'
          + '<div class="mix-strip-fader-meter">'
          +   '<div class="mix-strip-fader"><div class="mix-strip-fader-fill"></div><div class="mix-strip-fader-handle"></div></div>'
          +   '<div class="mix-strip-meter"><div class="mix-strip-meter-fill" data-channel="l" style="height:0%"></div></div>'
          +   '<div class="mix-strip-meter"><div class="mix-strip-meter-fill" data-channel="r" style="height:0%"></div></div>'
          + '</div>'
          + '<div class="mix-strip-knob" data-fn="jitter" title="Pitch Jitter ' + letter + ' - drag, dbl-click resets"></div>'
          + '<div class="mix-strip-knob-label">JIT</div>'
          + '<div class="mix-strip-buttons">'
          +   '<button class="mix-strip-btn" data-fn="mute">M</button>'
          +   '<button class="mix-strip-btn" data-fn="solo">S</button>'
          + '</div>'
          ;
        strips.appendChild(strip);
      }

      var right = document.createElement('div');
      right.id = 'mix-right';
      right.innerHTML = ''
        + '<div id="mix-trigger-area"></div>'
        + '<div id="mix-stem-area"></div>'
        ;
      panel.appendChild(right);

      // Insert as a sibling of #mod-panel so it lives in the same DOM flow
      // (both panels swap with #controls via display:none/flex).
      var modPanel = document.getElementById('mod-panel');
      if (modPanel && modPanel.parentNode) {
        modPanel.parentNode.insertBefore(panel, modPanel.nextSibling);
      } else {
        document.body.appendChild(panel);
      }
    }

    // Arc knobs use --val (0..1) sweeping the conic-gradient ring.
    function setPanVisual (knob, p)     { knob.style.setProperty('--val', String((p + 1) / 2)); }   // -1..1 -> 0..1
    function setJitterVisual (knob, c)  { knob.style.setProperty('--val', String(c / 100)); }        // 0..100 -> 0..1
    // Fader: vol 0..2 -> 0..100% of travel. Sets fill height + handle bottom.
    function setFaderVisual (fader, vol) {
      var pct = (vol / 2.0) * 100;
      var f = fader.querySelector('.mix-strip-fader-fill');
      var h = fader.querySelector('.mix-strip-fader-handle');
      if (f) f.style.height = pct + '%';
      if (h) h.style.bottom = pct + '%';
    }

    function wireStrips () {
      document.querySelectorAll('.mix-strip').forEach(function (strip) {
        var idx = parseInt(strip.getAttribute('data-layer'), 10);

        // ── Fader (vertical drag, 0..2 linear gain) ──
        var fader = strip.querySelector('.mix-strip-fader');
        setFaderVisual(fader, 1.0);
        fader.addEventListener('mousedown', function (ev) {
          ev.preventDefault();
          var rect = fader.getBoundingClientRect();
          function onMove (e) {
            var rel = (e.clientY - rect.top) / rect.height;
            rel = Math.max(0, Math.min(1, rel));
            var vol = (1.0 - rel) * 2.0;
            setFaderVisual(fader, vol);
            var fn = getNativeFn('setLayerVolume');
            if (fn) { try { fn(idx, vol); } catch (_) {} }
          }
          function onUp () {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
          }
          onMove(ev);
          document.addEventListener('mousemove', onMove);
          document.addEventListener('mouseup', onUp);
        });
        fader.addEventListener('dblclick', function (ev) {
          ev.preventDefault();
          setFaderVisual(fader, 1.0);
          var fn = getNativeFn('setLayerVolume');
          if (fn) { try { fn(idx, 1.0); } catch (_) {} }
        });

        // ── Pan knob (vertical drag → ±1, dbl-click resets to 0) ──
        var panKnob = strip.querySelector('.mix-strip-knob[data-fn="pan"]');
        var panState = { v: 0 };
        panKnob.addEventListener('dblclick', function (ev) {
          ev.preventDefault();
          panState.v = 0;
          setPanVisual(panKnob, 0);
          var fn = getNativeFn('setLayerPan');
          if (fn) { try { fn(idx, 0); } catch (_) {} }
        });
        panKnob.addEventListener('mousedown', function (ev) {
          ev.preventDefault();
          var startY = ev.clientY;
          var startV = panState.v;
          function onMove (e) {
            var v = Math.max(-1, Math.min(1, startV + (startY - e.clientY) / 100));
            panState.v = v;
            setPanVisual(panKnob, v);
            var fn = getNativeFn('setLayerPan');
            if (fn) { try { fn(idx, v); } catch (_) {} }
          }
          function onUp () {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
          }
          document.addEventListener('mousemove', onMove);
          document.addEventListener('mouseup', onUp);
        });

        // ── Pitch jitter knob (vertical drag → 0..100 cents) ──
        var jitterKnob = strip.querySelector('.mix-strip-knob[data-fn="jitter"]');
        var jitterState = { v: 0 };
        jitterKnob.addEventListener('dblclick', function (ev) {
          ev.preventDefault();
          jitterState.v = 0;
          setJitterVisual(jitterKnob, 0);
          var fn = getNativeFn('setLayerPitchJitter');
          if (fn) { try { fn(idx, 0); } catch (_) {} }
        });
        jitterKnob.addEventListener('mousedown', function (ev) {
          ev.preventDefault();
          var startY = ev.clientY;
          var startV = jitterState.v;
          function onMove (e) {
            var v = Math.max(0, Math.min(100, startV + (startY - e.clientY) / 2));
            jitterState.v = v;
            setJitterVisual(jitterKnob, v);
            var fn = getNativeFn('setLayerPitchJitter');
            if (fn) { try { fn(idx, v); } catch (_) {} }
          }
          function onUp () {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
          }
          document.addEventListener('mousemove', onMove);
          document.addEventListener('mouseup', onUp);
        });

        // ── M / S buttons ──
        strip.querySelector('[data-fn="mute"]').addEventListener('click', function (ev) {
          var btn = ev.currentTarget;
          var active = ! btn.classList.contains('active');
          btn.classList.toggle('active', active);
          var fn = getNativeFn('setLayerMute');
          if (fn) { try { fn(idx, active); } catch (_) {} }
        });
        strip.querySelector('[data-fn="solo"]').addEventListener('click', function (ev) {
          var btn = ev.currentTarget;
          var active = ! btn.classList.contains('active');
          btn.classList.toggle('active', active);
          var fn = getNativeFn('setLayerSolo');
          if (fn) { try { fn(idx, active); } catch (_) {} }
        });
      });
    }

    function restoreStripsFromCpp () {
      document.querySelectorAll('.mix-strip').forEach(function (strip) {
        var idx = parseInt(strip.getAttribute('data-layer'), 10);
        var fader      = strip.querySelector('.mix-strip-fader');
        var panKnob    = strip.querySelector('.mix-strip-knob[data-fn="pan"]');
        var jitterKnob = strip.querySelector('.mix-strip-knob[data-fn="jitter"]');
        var muteBtn    = strip.querySelector('[data-fn="mute"]');
        var soloBtn    = strip.querySelector('[data-fn="solo"]');

        function unwrap (p, then) {
          if (p && typeof p.then === 'function') p.then(then).catch(function () {});
          else then(p);
        }
        var volFn = getNativeFn('getLayerVolume');
        if (volFn) { try { unwrap(volFn(idx), function (v) { setFaderVisual(fader, (typeof v === 'number' ? v : 1.0)); }); } catch (_) {} }
        var panFn = getNativeFn('getLayerPan');
        if (panFn) { try { unwrap(panFn(idx), function (v) { setPanVisual(panKnob, (typeof v === 'number' ? v : 0)); }); } catch (_) {} }
        var jFn = getNativeFn('getLayerPitchJitter');
        if (jFn)  { try { unwrap(jFn(idx),   function (v) { setJitterVisual(jitterKnob, (typeof v === 'number' ? v : 0)); }); } catch (_) {} }
        var mFn = getNativeFn('getLayerMute');
        if (mFn)  { try { unwrap(mFn(idx),   function (m) { muteBtn.classList.toggle('active', !!m); }); } catch (_) {} }
        var sFn = getNativeFn('getLayerSolo');
        if (sFn)  { try { unwrap(sFn(idx),   function (s) { soloBtn.classList.toggle('active', !!s); }); } catch (_) {} }
      });
    }

    function pollMixMeters () {
      var panel = document.getElementById('mix-panel');
      if (! panel || ! panel.classList.contains('open')) return;
      var fn = getNativeFn('getLayerPeakLevels');
      if (! fn) return;
      try {
        fn().then(function (arr) {
          if (! arr || arr.length < 4) return;
          for (var i = 0; i < 4; ++i) {
            var strip = document.querySelector('.mix-strip[data-layer="' + i + '"]');
            if (! strip) continue;
            var l = (arr[i] && arr[i].l) || 0;
            var r = (arr[i] && arr[i].r) || 0;
            var mL = strip.querySelector('.mix-strip-meter-fill[data-channel="l"]');
            var mR = strip.querySelector('.mix-strip-meter-fill[data-channel="r"]');
            if (mL) mL.style.height = (Math.min(1, l) * 100) + '%';
            if (mR) mR.style.height = (Math.min(1, r) * 100) + '%';
          }
        }).catch(function () {});
      } catch (_) {}
    }

    function pollStripsPlayingDots () {
      var panel = document.getElementById('mix-panel');
      if (! panel || ! panel.classList.contains('open')) return;
      var fn = getNativeFn('getLayerVoiceActivity');
      if (! fn) return;
      try {
        fn().then(function (arr) {
          if (! arr || arr.length < 4) return;
          for (var i = 0; i < 4; ++i) {
            var strip = document.querySelector('.mix-strip[data-layer="' + i + '"]');
            if (strip) strip.classList.toggle('playing', !! arr[i]);
          }
        }).catch(function () {});
      } catch (_) {}
    }

    function setupMixPillWiring () {
      var mixBtn = document.getElementById('mix-btn');
      var panel  = document.getElementById('mix-panel');
      if (! mixBtn || ! panel) return;

      mixBtn.addEventListener('click', function () {
        var willOpen = ! panel.classList.contains('open');
        // Close other panels first (existing setActivePanel handles eq/dly/mod).
        if (willOpen && typeof setActivePanel === 'function') setActivePanel(null);
        panel.classList.toggle('open', willOpen);
        mixBtn.classList.toggle('active', willOpen);
        var ctrls = document.getElementById('controls');
        if (ctrls) ctrls.style.display = willOpen ? 'none' : '';
        if (willOpen) {
          restoreStripsFromCpp();
          pullTriggerStateFromCpp();
          pullStemStateFromCpp();
        }
      });

      // When EQ/DLY/MOD pills are clicked, also close the Mix panel.
      ['eq-btn', 'delay-btn', 'mod-btn'].forEach(function (id) {
        var el = document.getElementById(id);
        if (! el) return;
        el.addEventListener('click', function () {
          if (panel.classList.contains('open')) {
            panel.classList.remove('open');
            mixBtn.classList.remove('active');
            var ctrls = document.getElementById('controls');
            if (ctrls) ctrls.style.display = '';
          }
        });
      });
    }

    // ── Phase C: trigger-mode pills + contextual control area ──────────────
    function injectTriggerStyles () {
      if (document.getElementById('mix-trigger-styles')) return;
      var s = document.createElement('style');
      s.id = 'mix-trigger-styles';
      s.textContent = ''
        + '#trigger-pills{display:flex;gap:6px;margin-bottom:7px;}'
        + '.trigger-pill{flex:1;padding:6px 4px;font-size:9.5px;font-weight:700;'
        +   'letter-spacing:0.6px;background:transparent;color:var(--text-secondary);'
        +   'border:1px solid var(--border-strong);border-radius:9px;cursor:pointer;'
        +   'font-family:inherit;}'
        + '.trigger-pill:hover{border-color:var(--purple-500);}'
        + '.trigger-pill.active{background:var(--purple-500);color:#fff;border-color:var(--purple-500);'
        +   'box-shadow:0 0 10px rgba(139,92,246,0.35);}'
        + '#trigger-context{position:relative;font-size:11px;'
        +   'color:var(--text-secondary);flex:1;min-height:0;display:flex;flex-direction:column;}'
        + '.trigger-panel{display:none;}'
        + '#trigger-context[data-mode="0"] .trigger-panel[data-panel="layer"]{display:block;}'
        + '#trigger-context[data-mode="1"] .trigger-panel[data-panel="rr"]{display:block;}'
        + '#trigger-context[data-mode="2"] .trigger-panel[data-panel="random"]{display:block;}'
        // KEYTRACK / VEL panels fill the trigger context vertically so the bar
        // can grow into the empty space below it.
        + '#trigger-context[data-mode="3"] .trigger-panel[data-panel="keytrack"]{display:flex;flex-direction:column;flex:1;min-height:0;}'
        + '#trigger-context[data-mode="4"] .trigger-panel[data-panel="velocity"]{display:flex;flex-direction:column;flex:1;min-height:0;}'
        + '.trigger-panel{padding:2px 0 0 0;}'
        // Hints hidden — the controls are self-explanatory (user will demo in video).
        + '.trigger-hint{display:none;}'
        // Dots span the full width (flex:1 each) so the row reads balanced.
        + '.layer-status-dots{display:flex;gap:6px;margin-top:5px;}'
        + '.layer-status-dot{flex:1;text-align:center;padding:4px 0;border-radius:6px;'
        +   'font-weight:700;background:var(--bg-card);color:var(--text-muted);'
        +   'font-size:11px;border:1px solid var(--border);}'
        + '.layer-status-dot.lit{background:var(--purple-500);color:#fff;border-color:var(--purple-500);}'
        // RR next-to-fire highlight — purple fill, white text, no glow.
        + '.layer-status-dot.next{background:var(--purple-500);color:#fff;border-color:var(--purple-500);}'
        // RR control row — edge-balanced: RESET left, SHUFFLE center, SYNC right.
        + '.rr-controls{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-top:10px;}'
        // LAYER MORPH — full-width control; blend focus travels A..D.
        + '.layer-morph{margin-top:11px;}'
        + '.morph-track{position:relative;height:10px;border-radius:5px;background:var(--knob-track);'
        +   'box-shadow:inset 0 0 2px rgba(0,0,0,0.25);cursor:pointer;}'
        + '.morph-fill{position:absolute;inset:0;border-radius:5px;opacity:0.45;'
        +   'background:linear-gradient(90deg,var(--purple-600),var(--purple-400),var(--purple-600));}'
        + '.morph-handle{position:absolute;top:50%;width:16px;height:16px;border-radius:50%;'
        +   'transform:translate(-50%,-50%);background:var(--bg-card);border:2px solid var(--purple-500);'
        +   'box-shadow:0 1px 5px rgba(0,0,0,0.3);}'
        // Dark theme: white-ice slider (per user) — light theme stays purple.
        + '[data-theme="dark"] .morph-fill{opacity:0.7;'
        +   'background:linear-gradient(90deg,rgba(255,255,255,0.35),rgba(255,255,255,0.95),rgba(255,255,255,0.35));}'
        + '[data-theme="dark"] .morph-handle{border-color:#fff;}'
        + '.morph-stops{display:flex;justify-content:space-between;margin-top:5px;}'
        + '.morph-stops span{width:20px;text-align:center;font-weight:700;font-size:11px;'
        +   'color:var(--text-muted);transition:color 0.08s,text-shadow 0.08s,opacity 0.08s;}'
        + '.morph-label{text-align:center;font-size:8px;letter-spacing:2px;text-transform:uppercase;'
        +   'color:var(--text-muted);margin-top:3px;}'
        + '.trigger-btn{padding:3px 8px;font-size:10px;font-weight:700;'
        +   'letter-spacing:0.4px;background:var(--bg-card);color:var(--text-secondary);'
        +   'border:1px solid var(--border-strong);border-radius:3px;cursor:pointer;'
        +   'font-family:inherit;margin-right:6px;}'
        + '.trigger-btn:hover{border-color:var(--purple-500);}'
        + '.trigger-btn.active{background:var(--purple-500);color:#fff;border-color:var(--purple-500);}'
        + '.trigger-toggle{display:inline-flex;align-items:center;gap:4px;'
        +   'font-size:10px;letter-spacing:0.4px;cursor:pointer;}'
        + '.trigger-toggle input{margin:0;accent-color:var(--purple-500);}'
        + '.rr-pos-row{margin-top:6px;display:flex;gap:6px;align-items:center;}'
        + '.rr-pos-row span{font-size:10px;letter-spacing:0.4px;}'
        + '.prob-row{display:flex;align-items:center;gap:6px;margin-bottom:1px;}'
        + '.prob-letter{width:14px;font-weight:700;font-size:11px;color:var(--text-primary);}'
        + '.prob-slider{flex:1;height:8px;border-radius:4px;'
        +   'position:relative;cursor:pointer;overflow:hidden;'
        +   'background:var(--knob-track);'
        +   'box-shadow:inset 0 0 2px rgba(0,0,0,0.25);}'
        // No CSS transition on the fill — it lags behind the cursor during drag.
        + '.prob-fill{position:absolute;left:0;top:0;bottom:0;'
        +   'background:linear-gradient(90deg,var(--purple-600),var(--purple-400));'
        +   'border-radius:4px;}'
        + '.prob-value{width:32px;font-size:10px;color:var(--text-secondary);'
        +   'text-align:right;}'
        + '#keytrack-bar{position:relative;flex:1;min-height:30px;max-height:55px;border-radius:4px;'
        +   'margin-bottom:0;overflow:hidden;'
        +   'background:var(--knob-track);'
        +   'box-shadow:inset 0 0 3px rgba(0,0,0,0.25);}'
        + '.kt-zone{position:absolute;top:0;bottom:0;display:flex;flex-direction:column;'
        +   'align-items:center;justify-content:center;font-size:11px;line-height:1.1;'
        +   'font-weight:700;color:#fff;text-shadow:0 1px 2px rgba(0,0,0,0.5);overflow:hidden;}'
        + '.kt-zone .kt-range{font-size:7px;font-weight:600;opacity:0.85;letter-spacing:0;}'
        // Terrain palette — cool progression A→D, ice through purple.
        + '.kt-zone[data-layer="0"]{background:linear-gradient(135deg,rgba(199,222,255,0.55),rgba(167,189,255,0.5));}'
        + '.kt-zone[data-layer="1"]{background:linear-gradient(135deg,rgba(190,162,250,0.55),rgba(167,139,250,0.5));}'
        + '.kt-zone[data-layer="2"]{background:linear-gradient(135deg,rgba(139,92,246,0.6),rgba(124,58,237,0.55));}'
        + '.kt-zone[data-layer="3"]{background:linear-gradient(135deg,rgba(99,52,196,0.65),rgba(67,30,138,0.6));}'
        + '.kt-handle{position:absolute;top:0;bottom:0;width:4px;'
        +   'background:var(--text-primary);cursor:ew-resize;transform:translateX(-50%);'
        +   'box-shadow:0 0 4px rgba(0,0,0,0.4);z-index:2;}'
        + '.kt-handle:hover{background:var(--purple-500);}'
        + '#velocity-bar{position:relative;flex:1;min-height:30px;max-height:55px;border-radius:4px;'
        +   'margin-bottom:0;overflow:hidden;'
        +   'background:var(--knob-track);'
        +   'box-shadow:inset 0 0 3px rgba(0,0,0,0.25);}'
        + '.vel-zone{position:absolute;top:0;bottom:0;display:flex;'
        +   'align-items:center;justify-content:center;font-size:11px;'
        +   'font-weight:700;color:#fff;text-shadow:0 1px 2px rgba(0,0,0,0.5);}'
        // Terrain palette — cool progression A→D, ice through purple.
        + '.vel-zone[data-layer="0"]{background:linear-gradient(135deg,rgba(199,222,255,0.55),rgba(167,189,255,0.5));}'
        + '.vel-zone[data-layer="1"]{background:linear-gradient(135deg,rgba(190,162,250,0.55),rgba(167,139,250,0.5));}'
        + '.vel-zone[data-layer="2"]{background:linear-gradient(135deg,rgba(139,92,246,0.6),rgba(124,58,237,0.55));}'
        + '.vel-zone[data-layer="3"]{background:linear-gradient(135deg,rgba(99,52,196,0.65),rgba(67,30,138,0.6));}'
        + '.vel-handle{position:absolute;top:0;bottom:0;width:4px;'
        +   'background:var(--text-primary);cursor:ew-resize;transform:translateX(-50%);'
        +   'box-shadow:0 0 4px rgba(0,0,0,0.4);z-index:2;}'
        + '.vel-handle:hover{background:var(--purple-500);}'
        ;
      document.head.appendChild(s);
    }

    function buildTriggerArea () {
      var area = document.getElementById('mix-trigger-area');
      if (! area) return;
      area.innerHTML = ''
        + '<div id="trigger-pills">'
        +   '<button class="trigger-pill" data-val="0">LAYER</button>'
        +   '<button class="trigger-pill" data-val="1">RR</button>'
        +   '<button class="trigger-pill" data-val="2">RANDOM</button>'
        +   '<button class="trigger-pill" data-val="3">KEYTRK</button>'
        +   '<button class="trigger-pill" data-val="4">VEL</button>'
        + '</div>'
        + '<div id="trigger-context" data-mode="0">'
        +   '<div class="trigger-panel" data-panel="layer">'
        +     '<div class="trigger-hint" style="text-align:center;">All populated layers fire together</div>'
        +     '<div class="layer-status-dots" id="layer-status-dots">'
        +       '<span class="layer-status-dot" data-layer="0">A</span>'
        +       '<span class="layer-status-dot" data-layer="1">B</span>'
        +       '<span class="layer-status-dot" data-layer="2">C</span>'
        +       '<span class="layer-status-dot" data-layer="3">D</span>'
        +     '</div>'
        +     '<div class="layer-morph">'
        +       '<div class="morph-track" id="morph-track">'
        +         '<div class="morph-fill"></div>'
        +         '<div class="morph-handle" id="morph-handle" style="left:50%"></div>'
        +       '</div>'
        +     '</div>'
        +   '</div>'
        +   '<div class="trigger-panel" data-panel="rr">'
        +     '<div class="trigger-hint" style="text-align:center;">Cycling A &rarr; B &rarr; C &rarr; D &middot; skips empty</div>'
        +     '<div class="layer-status-dots" id="rr-dots">'
        +       '<span class="layer-status-dot" data-layer="0">A</span>'
        +       '<span class="layer-status-dot" data-layer="1">B</span>'
        +       '<span class="layer-status-dot" data-layer="2">C</span>'
        +       '<span class="layer-status-dot" data-layer="3">D</span>'
        +     '</div>'
        +     '<div class="rr-controls">'
        +       '<button class="trigger-btn" id="rr-reset-btn" style="margin:0;">RESET TO A</button>'
        +       '<label class="trigger-toggle"><input type="checkbox" id="rr-shuffle-cb"> SHUFFLE</label>'
        +       '<label class="trigger-toggle"><input type="checkbox" id="rr-sync-cb"> SYNC</label>'
        +     '</div>'
        +   '</div>'
        +   '<div class="trigger-panel" data-panel="random">'
        +     '<div class="trigger-hint">Weighted random &middot; dbl-click resets</div>'
        +     '<div class="prob-row"><div class="prob-letter">A</div>'
        +       '<div class="prob-slider" data-layer="0"><div class="prob-fill"></div></div>'
        +       '<div class="prob-value" data-layer="0">25%</div></div>'
        +     '<div class="prob-row"><div class="prob-letter">B</div>'
        +       '<div class="prob-slider" data-layer="1"><div class="prob-fill"></div></div>'
        +       '<div class="prob-value" data-layer="1">25%</div></div>'
        +     '<div class="prob-row"><div class="prob-letter">C</div>'
        +       '<div class="prob-slider" data-layer="2"><div class="prob-fill"></div></div>'
        +       '<div class="prob-value" data-layer="2">25%</div></div>'
        +     '<div class="prob-row"><div class="prob-letter">D</div>'
        +       '<div class="prob-slider" data-layer="3"><div class="prob-fill"></div></div>'
        +       '<div class="prob-value" data-layer="3">25%</div></div>'
        +   '</div>'
        +   '<div class="trigger-panel" data-panel="keytrack">'
        +     '<div class="trigger-hint">Key range per layer &middot; dbl-click resets</div>'
        +     '<div id="keytrack-bar">'
        +       '<div class="kt-zone" data-layer="0"><span>A</span><span class="kt-range"></span></div>'
        +       '<div class="kt-zone" data-layer="1"><span>B</span><span class="kt-range"></span></div>'
        +       '<div class="kt-zone" data-layer="2"><span>C</span><span class="kt-range"></span></div>'
        +       '<div class="kt-zone" data-layer="3"><span>D</span><span class="kt-range"></span></div>'
        +       '<div class="kt-handle" data-h="0"></div>'
        +       '<div class="kt-handle" data-h="1"></div>'
        +       '<div class="kt-handle" data-h="2"></div>'
        +     '</div>'
        +   '</div>'
        +   '<div class="trigger-panel" data-panel="velocity">'
        +     '<div class="trigger-hint">Velocity per layer &middot; dbl-click resets</div>'
        +     '<div id="velocity-bar">'
        +       '<div class="vel-zone" data-layer="0">A</div>'
        +       '<div class="vel-zone" data-layer="1">B</div>'
        +       '<div class="vel-zone" data-layer="2">C</div>'
        +       '<div class="vel-zone" data-layer="3">D</div>'
        +       '<div class="vel-handle" data-h="0"></div>'
        +       '<div class="vel-handle" data-h="1"></div>'
        +       '<div class="vel-handle" data-h="2"></div>'
        +     '</div>'
        +   '</div>'
        + '</div>'
        ;
    }

    function setTriggerMode (val) {
      val = Math.max(0, Math.min(4, parseInt(val, 10) || 0));
      var ctx = document.getElementById('trigger-context');
      if (ctx) ctx.setAttribute('data-mode', String(val));
      document.querySelectorAll('.trigger-pill').forEach(function (p) {
        p.classList.toggle('active', parseInt(p.getAttribute('data-val'), 10) === val);
      });
      var fn = getNativeFn('setTriggerMode');
      if (fn) { try { fn(val); } catch (_) {} }
    }

    function pullLayerStatusDots () {
      // Light up dots for layers that have a sample.
      for (var i = 0; i < 4; ++i) {
        (function (idx) {
          var fn = getNativeFn('getLayerHasSample');
          if (! fn) return;
          try {
            var r = fn(idx);
            var apply = function (has) {
              var dot = document.querySelector('.layer-status-dot[data-layer="' + idx + '"]');
              if (dot) dot.classList.toggle('lit', !! has);
            };
            if (r && typeof r.then === 'function') r.then(apply).catch(function () {});
            else apply(r);
          } catch (_) {}
        })(i);
      }
    }

    // LAYER MORPH — full-width blend that travels A..D. Light each stop by
    // proximity so you see the blend move through the layers.
    function paintMorph (p) {
      // Handle travels aligned under the A..D dots above (their centers sit at
      // ~12.5%..87.5%), so the dots double as the morph's labels.
      var handle = document.getElementById('morph-handle');
      if (handle) handle.style.left = (12.5 + p * 75) + '%';
    }

    function wireLayerMorph () {
      var track = document.getElementById('morph-track');
      if (! track) return;
      function fromX (clientX) {
        var rect = track.getBoundingClientRect();
        var p = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
        paintMorph(p);
        var fn = getNativeFn('setLayerMorph');
        if (fn) { try { fn(p); } catch (_) {} }
      }
      track.addEventListener('mousedown', function (ev) {
        ev.preventDefault();
        fromX(ev.clientX);
        function mv (e) { fromX(e.clientX); }
        function up () { document.removeEventListener('mousemove', mv); document.removeEventListener('mouseup', up); }
        document.addEventListener('mousemove', mv);
        document.addEventListener('mouseup', up);
      });
      // Hydrate from C++.
      var gfn = getNativeFn('getLayerMorph');
      var apply = function (v) { paintMorph((typeof v === 'number') ? v : 0.5); };
      if (gfn) {
        try { var r = gfn(); if (r && typeof r.then === 'function') r.then(apply).catch(function () { apply(0.5); }); else apply(r); }
        catch (_) { apply(0.5); }
      } else { apply(0.5); }
    }

    function wireProbSliders () {
      document.querySelectorAll('.prob-slider').forEach(function (sl) {
        var idx  = parseInt(sl.getAttribute('data-layer'), 10);
        var fill = sl.querySelector('.prob-fill');
        var val  = document.querySelector('.prob-value[data-layer="' + idx + '"]');
        function setVisual (p01) {
          fill.style.width = (p01 * 100) + '%';
          if (val) val.textContent = Math.round(p01 * 100) + '%';
        }
        function setFromX (clientX) {
          var rect = sl.getBoundingClientRect();
          var rel = (clientX - rect.left) / rect.width;
          rel = Math.max(0, Math.min(1, rel));
          setVisual(rel);
          var fn = getNativeFn('setLayerProbabilityWeight');
          if (fn) { try { fn(idx, rel); } catch (_) {} }
        }
        sl.addEventListener('mousedown', function (ev) {
          ev.preventDefault();
          setFromX(ev.clientX);
          function onMove (e) { setFromX(e.clientX); }
          function onUp () {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
          }
          document.addEventListener('mousemove', onMove);
          document.addEventListener('mouseup', onUp);
        });
        // Double-click any slider → reset ALL weights to uniform (25% each).
        sl.addEventListener('dblclick', function () {
          for (var i = 0; i < 4; ++i) {
            var fn = getNativeFn('setLayerProbabilityWeight');
            if (fn) { try { fn(i, 0.25); } catch (_) {} }
            var s2 = document.querySelector('.prob-slider[data-layer="' + i + '"]');
            if (s2 && s2._setVisual) s2._setVisual(0.25);
          }
        });
        sl._setVisual = setVisual;   // for restore
      });
    }

    // Keyboard split (KEYTRACK): 4 layers covering MIDI notes 0-127 with 3 handles
    // between them. Drag a handle to resize neighboring zones (non-overlapping,
    // contiguous). Mirrors the VELOCITY bar but on the note axis with note labels.
    function midiToNoteName (n) {
      var names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
      n = Math.max(0, Math.min(127, Math.round(n)));
      return names[n % 12] + (Math.floor(n / 12) - 1);
    }

    function paintKeytrackZones (zones) {
      for (var i = 0; i < 4; ++i) {
        var z = document.querySelector('.kt-zone[data-layer="' + i + '"]');
        if (! z) continue;
        var leftPct  = (zones[i].min / 127) * 100;
        var widthPct = ((zones[i].max - zones[i].min + 1) / 127) * 100;
        z.style.left  = leftPct  + '%';
        z.style.width = widthPct + '%';
        var rng = z.querySelector('.kt-range');
        if (rng) rng.textContent = midiToNoteName(zones[i].min) + '-' + midiToNoteName(zones[i].max);
      }
      for (var h = 0; h < 3; ++h) {
        var handle = document.querySelector('.kt-handle[data-h="' + h + '"]');
        if (! handle) continue;
        var boundaryPct = ((zones[h].max + 1) / 127) * 100;
        handle.style.left = boundaryPct + '%';
      }
    }

    function wireKeytrackBar () {
      var bar = document.getElementById('keytrack-bar');
      if (! bar) return;
      var fn = getNativeFn('getAllKeyZones');
      var zones = [{min: 0, max: 31}, {min: 32, max: 63}, {min: 64, max: 95}, {min: 96, max: 127}];
      function apply (arr) {
        if (arr && arr.length === 4) {
          for (var i = 0; i < 4; ++i) {
            zones[i] = { min: arr[i].min, max: arr[i].max };
          }
        }
        paintKeytrackZones(zones);
      }
      if (fn) {
        try {
          var r = fn();
          if (r && typeof r.then === 'function') r.then(apply).catch(function () { apply(); });
          else apply(r);
        } catch (_) { apply(); }
      } else { apply(); }

      function pushZones () {
        var setFn = getNativeFn('setLayerKeyZone');
        if (! setFn) return;
        for (var i = 0; i < 4; ++i) {
          try { setFn(i, zones[i].min, zones[i].max); } catch (_) {}
        }
      }

      document.querySelectorAll('.kt-handle').forEach(function (handle) {
        var h = parseInt(handle.getAttribute('data-h'), 10);  // 0, 1, or 2
        handle.addEventListener('mousedown', function (ev) {
          ev.preventDefault();
          var rect = bar.getBoundingClientRect();
          function onMove (e) {
            var rel = (e.clientX - rect.left) / rect.width;
            rel = Math.max(0, Math.min(1, rel));
            var v = Math.round(rel * 127);
            if (h === 0) v = Math.max(0, Math.min(zones[1].max,  v));
            if (h === 1) v = Math.max(zones[0].min + 1, Math.min(zones[2].max,  v));
            if (h === 2) v = Math.max(zones[1].min + 1, Math.min(127, v));
            zones[h].max     = v - 1;
            zones[h + 1].min = v;
            paintKeytrackZones(zones);
          }
          function onUp () {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
            pushZones();
          }
          document.addEventListener('mousemove', onMove);
          document.addEventListener('mouseup', onUp);
        });
      });

      // Double-click the bar → reset zones to an even split.
      bar.addEventListener('dblclick', function () {
        zones[0] = {min: 0,  max: 31};
        zones[1] = {min: 32, max: 63};
        zones[2] = {min: 64, max: 95};
        zones[3] = {min: 96, max: 127};
        paintKeytrackZones(zones);
        pushZones();
      });
    }

    // Velocity zones: 4 layers covering 0-127 with 3 handles between them.
    // Drag a handle to resize neighboring zones (non-overlapping, contiguous).
    function paintVelocityZones (zones) {
      // zones = [{min, max}, {min, max}, {min, max}, {min, max}]
      // Each zone's visual: left = min/127 * 100%, width = (max-min+1)/127 * 100%
      for (var i = 0; i < 4; ++i) {
        var z = document.querySelector('.vel-zone[data-layer="' + i + '"]');
        if (! z) continue;
        var leftPct  = (zones[i].min / 127) * 100;
        var widthPct = ((zones[i].max - zones[i].min + 1) / 127) * 100;
        z.style.left  = leftPct  + '%';
        z.style.width = widthPct + '%';
      }
      // Handles at boundaries between zones 0/1, 1/2, 2/3.
      for (var h = 0; h < 3; ++h) {
        var handle = document.querySelector('.vel-handle[data-h="' + h + '"]');
        if (! handle) continue;
        // Boundary at zones[h].max + 1 (= zones[h+1].min)
        var boundaryPct = ((zones[h].max + 1) / 127) * 100;
        handle.style.left = boundaryPct + '%';
      }
    }

    function wireVelocityBar () {
      var bar = document.getElementById('velocity-bar');
      if (! bar) return;
      // Read current zones, paint, then wire drag.
      var fn = getNativeFn('getAllVelocityZones');
      var zones = [{min: 0, max: 31}, {min: 32, max: 63}, {min: 64, max: 95}, {min: 96, max: 127}];
      function apply (arr) {
        if (arr && arr.length === 4) {
          for (var i = 0; i < 4; ++i) {
            zones[i] = { min: arr[i].min, max: arr[i].max };
          }
        }
        paintVelocityZones(zones);
      }
      if (fn) {
        try {
          var r = fn();
          if (r && typeof r.then === 'function') r.then(apply).catch(function () { apply(); });
          else apply(r);
        } catch (_) { apply(); }
      } else { apply(); }

      function pushZones () {
        var setFn = getNativeFn('setLayerVelocityZone');
        if (! setFn) return;
        for (var i = 0; i < 4; ++i) {
          try { setFn(i, zones[i].min, zones[i].max); } catch (_) {}
        }
      }

      document.querySelectorAll('.vel-handle').forEach(function (handle) {
        var h = parseInt(handle.getAttribute('data-h'), 10);  // 0, 1, or 2
        handle.addEventListener('mousedown', function (ev) {
          ev.preventDefault();
          var rect = bar.getBoundingClientRect();
          function onMove (e) {
            var rel = (e.clientX - rect.left) / rect.width;
            rel = Math.max(0, Math.min(1, rel));
            var v = Math.round(rel * 127);
            // Keep boundaries strictly increasing — clamp to neighbors.
            if (h === 0) v = Math.max(0, Math.min(zones[1].max,  v));
            if (h === 1) v = Math.max(zones[0].min + 1, Math.min(zones[2].max,  v));
            if (h === 2) v = Math.max(zones[1].min + 1, Math.min(127, v));
            // Resize neighboring zones around this boundary.
            zones[h].max     = v - 1;
            zones[h + 1].min = v;
            paintVelocityZones(zones);
          }
          function onUp () {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
            pushZones();
          }
          document.addEventListener('mousemove', onMove);
          document.addEventListener('mouseup', onUp);
        });
      });

      // Double-click the bar → reset zones to an even split.
      bar.addEventListener('dblclick', function () {
        zones[0] = {min: 0,  max: 31};
        zones[1] = {min: 32, max: 63};
        zones[2] = {min: 64, max: 95};
        zones[3] = {min: 96, max: 127};
        paintVelocityZones(zones);
        pushZones();
      });
    }

    function wireTriggerArea () {
      // Pills
      document.querySelectorAll('.trigger-pill').forEach(function (p) {
        p.addEventListener('click', function () {
          setTriggerMode(parseInt(p.getAttribute('data-val'), 10));
        });
      });
      // RR controls
      var rrReset = document.getElementById('rr-reset-btn');
      if (rrReset) rrReset.addEventListener('click', function () {
        var fn = getNativeFn('resetRoundRobin');
        if (fn) { try { fn(); } catch (_) {} }
      });
      var rrSync = document.getElementById('rr-sync-cb');
      if (rrSync) rrSync.addEventListener('change', function () {
        var fn = getNativeFn('setRrSyncToBar');
        if (fn) { try { fn(rrSync.checked); } catch (_) {} }
      });
      var rrShuf = document.getElementById('rr-shuffle-cb');
      if (rrShuf) rrShuf.addEventListener('change', function () {
        var fn = getNativeFn('setRrShuffle');
        if (fn) { try { fn(rrShuf.checked); } catch (_) {} }
      });
      // LAYER controls
      wireLayerMorph();
      // RANDOM controls (reset is now double-click on any slider)
      wireProbSliders();
      // KEYTRACK controls
      wireKeytrackBar();
      // VELOCITY controls
      wireVelocityBar();
    }

    function pullTriggerStateFromCpp () {
      // Trigger mode
      var modeFn = getNativeFn('getTriggerMode');
      if (modeFn) {
        try {
          var r = modeFn();
          var apply = function (v) {
            var n = (typeof v === 'number') ? v : (parseInt(v, 10) || 0);
            setTriggerMode(n);
          };
          if (r && typeof r.then === 'function') r.then(apply).catch(function () {});
          else apply(r);
        } catch (_) {}
      }
      // RR sync
      var syncFn = getNativeFn('getRrSyncToBar');
      if (syncFn) {
        try {
          var r = syncFn();
          var apply = function (v) {
            var cb = document.getElementById('rr-sync-cb');
            if (cb) cb.checked = !! v;
          };
          if (r && typeof r.then === 'function') r.then(apply).catch(function () {});
          else apply(r);
        } catch (_) {}
      }
      // RR shuffle
      var shufFn = getNativeFn('getRrShuffle');
      if (shufFn) {
        try {
          var r = shufFn();
          var apply = function (v) {
            var cb = document.getElementById('rr-shuffle-cb');
            if (cb) cb.checked = !! v;
          };
          if (r && typeof r.then === 'function') r.then(apply).catch(function () {});
          else apply(r);
        } catch (_) {}
      }
      // Probability weights
      for (var i = 0; i < 4; ++i) {
        (function (idx) {
          var fn = getNativeFn('getLayerProbabilityWeight');
          if (! fn) return;
          try {
            var r = fn(idx);
            var apply = function (v) {
              var sl = document.querySelector('.prob-slider[data-layer="' + idx + '"]');
              if (sl && sl._setVisual) sl._setVisual(typeof v === 'number' ? v : 0.25);
            };
            if (r && typeof r.then === 'function') r.then(apply).catch(function () {});
            else apply(r);
          } catch (_) {}
        })(i);
      }
      // KEYTRACK + VELOCITY zones — handled inside wireKeytrackBar / wireVelocityBar
      // initial paint (they fetch getAllKeyZones / getAllVelocityZones on open).
      // Layer status dots (LAYER mode)
      pullLayerStatusDots();
    }

    // ── Phase D: stem capture area ─────────────────────────────────────────
    function injectStemStyles () {
      if (document.getElementById('mix-stem-styles')) return;
      var s = document.createElement('style');
      s.id = 'mix-stem-styles';
      s.textContent = ''
        + '#mix-stem-area{display:flex;flex-direction:column;gap:3px;}'
        // Match the "Grain Engine" / "Effects" section-label look: low-opacity,
        // uppercase, wide tracking — a quiet section marker, not a busy caption.
        + '.stem-header{font-size:9px;letter-spacing:2px;text-transform:uppercase;'
        +   'color:var(--text-muted);font-weight:500;}'
        + '.stem-buttons{display:flex;gap:4px;}'
        + '.stem-btn{flex:1;padding:5px 4px;font-size:11px;font-weight:700;'
        +   'background:transparent;color:var(--text-secondary);border:1px solid var(--border-strong);'
        +   'border-radius:9px;cursor:pointer;font-family:inherit;'
        +   'transition:all 0.15s;}'
        + '.stem-btn:hover{border-color:var(--purple-500);color:var(--text-primary);}'
        + '.stem-btn.exporting{background:var(--purple-500);color:#fff;border-color:var(--purple-500);'
        +   'box-shadow:0 0 8px var(--purple-400);}'
        + '.stem-all-row{display:flex;gap:6px;}'
        + '#stem-export-all{flex:2;padding:5px;font-size:10.5px;font-weight:700;letter-spacing:0.5px;'
        +   'background:transparent;color:var(--purple-500);border:1px solid var(--purple-500);'
        +   'border-radius:9px;cursor:pointer;font-family:inherit;}'
        + '#stem-export-all:hover{background:var(--purple-500);color:#fff;}'
        + '#stem-reveal{flex:1;padding:5px;font-size:10px;font-weight:700;'
        +   'background:transparent;color:var(--text-secondary);border:1px solid var(--border-strong);'
        +   'border-radius:9px;cursor:pointer;font-family:inherit;}'
        // Edge-balanced row: DRY/WET group left, live A/B/C/D capture meters right.
        + '.stem-source-row{display:flex;gap:6px;align-items:center;'
        +   'justify-content:space-between;'
        +   'font-size:9.5px;letter-spacing:0.5px;color:var(--text-muted);margin-top:5px;}'
        + '.stem-source-group{display:flex;gap:6px;}'
        + '.stem-source-pill{padding:4px 12px;border:1px solid var(--border-strong);'
        +   'background:transparent;color:var(--text-secondary);border-radius:7px;'
        +   'cursor:pointer;font-weight:700;font-family:inherit;font-size:9.5px;}'
        + '.stem-source-pill.active{background:var(--purple-500);color:#fff;border-color:var(--purple-500);}'
        // CLEAR — action button (no persistent active state); flashes purple on click.
        + '.stem-clear-btn{padding:4px 12px;border:1px solid var(--border-strong);'
        +   'background:transparent;color:var(--text-secondary);border-radius:7px;'
        +   'cursor:pointer;font-weight:700;font-family:inherit;font-size:9.5px;'
        +   'transition:background 0.12s,color 0.12s,border-color 0.12s;}'
        + '.stem-clear-btn:hover{border-color:var(--purple-500);color:var(--text-primary);}'
        + '.stem-clear-btn.flash{background:var(--purple-500);color:#fff;border-color:var(--purple-500);}'
        // Live capture meters — purple on light, white on dark (matches strip meters).
        + '.stem-cap-meters{display:flex;gap:7px;align-items:center;}'
        + '.stem-cap-meter{display:flex;align-items:center;gap:4px;}'
        + '.stem-cap-meter-label{font-size:9px;font-weight:700;color:var(--text-muted);width:8px;text-align:center;}'
        + '.stem-cap-meter-bar{position:relative;width:32px;height:5px;border-radius:3px;'
        +   'background:var(--knob-track);overflow:hidden;}'
        + '.stem-cap-meter-fill{position:absolute;left:0;top:0;bottom:0;border-radius:3px;'
        +   'background:linear-gradient(90deg,var(--purple-400),var(--purple-600));'
        +   'box-shadow:0 0 5px rgba(139,92,246,0.5);transition:width 0.05s linear;}'
        + '[data-theme="dark"] .stem-cap-meter-fill{'
        +   'background:linear-gradient(90deg,rgba(255,255,255,0.5),#fff);'
        +   'box-shadow:0 0 5px rgba(255,255,255,0.55);}'
        + '.stem-status{font-size:9px;color:var(--text-muted);'
        +   'font-style:italic;min-height:11px;}'
        ;
      document.head.appendChild(s);
    }

    function buildStemArea () {
      var area = document.getElementById('mix-stem-area');
      if (! area) return;
      area.innerHTML = ''
        + '<div class="stem-header">STEMS</div>'
        + '<div class="stem-buttons">'
        +   '<button class="stem-btn" data-layer="0">A</button>'
        +   '<button class="stem-btn" data-layer="1">B</button>'
        +   '<button class="stem-btn" data-layer="2">C</button>'
        +   '<button class="stem-btn" data-layer="3">D</button>'
        + '</div>'
        + '<div class="stem-all-row">'
        +   '<button id="stem-export-all">EXPORT ALL 4</button>'
        +   '<button id="stem-reveal">REVEAL FOLDER</button>'
        + '</div>'
        + '<div class="stem-source-row">'
        +   '<div class="stem-source-group">'
        +     '<button class="stem-source-pill active" data-val="0">DRY</button>'
        +     '<button class="stem-source-pill" data-val="1">WET</button>'
        +   '</div>'
        +   '<button class="stem-clear-btn" id="stem-clear" title="Clear the rolling stem buffer for all 4 layers">CLEAR</button>'
        +   '<div class="stem-cap-meters" id="stem-cap-meters">'
        +     '<div class="stem-cap-meter"><span class="stem-cap-meter-label">A</span><div class="stem-cap-meter-bar"><div class="stem-cap-meter-fill" data-layer="0" style="width:0%"></div></div></div>'
        +     '<div class="stem-cap-meter"><span class="stem-cap-meter-label">B</span><div class="stem-cap-meter-bar"><div class="stem-cap-meter-fill" data-layer="1" style="width:0%"></div></div></div>'
        +     '<div class="stem-cap-meter"><span class="stem-cap-meter-label">C</span><div class="stem-cap-meter-bar"><div class="stem-cap-meter-fill" data-layer="2" style="width:0%"></div></div></div>'
        +     '<div class="stem-cap-meter"><span class="stem-cap-meter-label">D</span><div class="stem-cap-meter-bar"><div class="stem-cap-meter-fill" data-layer="3" style="width:0%"></div></div></div>'
        +   '</div>'
        + '</div>'
        + '<div class="stem-status" id="stem-status">&nbsp;</div>'
        ;
    }

    function setStemStatus (msg) {
      var el = document.getElementById('stem-status');
      if (el) el.textContent = msg || '';
    }

    function flashStemBtn (btn) {
      if (! btn) return;
      btn.classList.add('exporting');
      setTimeout(function () { btn.classList.remove('exporting'); }, 800);
    }

    function basename (path) {
      if (! path) return '';
      var s = String(path);
      var i = s.lastIndexOf('/');
      if (i < 0) i = s.lastIndexOf('\\');
      return (i >= 0) ? s.substring(i + 1) : s;
    }

    // Click = export to the Music folder. Drag = drag-out to the DAW (calls the
    // dragStem native fn which fires performExternalDragDropOfFiles). idx -1 = all 4.
    function stemClickExport (idx) {
      if (idx < 0) {
        var fnA = getNativeFn('exportAllStems');
        if (! fnA) return;
        try {
          var r = fnA();
          var ap = function (arr) {
            var n = (arr && arr.length) ? arr.length : 0, ok = 0;
            for (var i = 0; i < n; ++i) if (arr[i] && String(arr[i]).length) ok++;
            setStemStatus('Exported ' + ok + ' / 4 stems to Music folder');
          };
          if (r && typeof r.then === 'function') r.then(ap).catch(function () { setStemStatus('Export error'); });
          else ap(r);
        } catch (_) { setStemStatus('Export error'); }
      } else {
        var fnE = getNativeFn('exportStem');
        if (! fnE) return;
        try {
          var r2 = fnE(idx);
          var ap2 = function (p) {
            if (p && p.length) setStemStatus('Exported ' + basename(p));
            else setStemStatus('Layer ' + 'ABCD'[idx] + ' has no audio yet');
          };
          if (r2 && typeof r2.then === 'function') r2.then(ap2).catch(function () { setStemStatus('Export error'); });
          else ap2(r2);
        } catch (_) { setStemStatus('Export error'); }
      }
    }

    function attachStemDrag (btn, idx, isAll) {
      btn.addEventListener('mousedown', function (ev) {
        ev.preventDefault();
        var sx = ev.clientX, sy = ev.clientY, dragged = false;
        function cleanup () {
          document.removeEventListener('mousemove', onMove);
          document.removeEventListener('mouseup', onUp);
        }
        function onMove (e) {
          if (dragged) return;
          var dx = e.clientX - sx, dy = e.clientY - sy;
          if (dx * dx + dy * dy > 36) {        // ~6px threshold = drag intent
            dragged = true;
            flashStemBtn(btn);
            setStemStatus(isAll ? 'Dragging all stems to DAW...' : 'Dragging stem ' + 'ABCD'[idx] + ' to DAW...');
            var fn = getNativeFn('dragStem');
            if (fn) { try { fn(isAll ? -1 : idx); } catch (_) {} }
            cleanup();
          }
        }
        function onUp () {
          if (! dragged) { flashStemBtn(btn); stemClickExport(isAll ? -1 : idx); }
          cleanup();
        }
        document.addEventListener('mousemove', onMove);
        document.addEventListener('mouseup', onUp);
      });
    }

    function wireStemArea () {
      document.querySelectorAll('.stem-btn').forEach(function (btn) {
        attachStemDrag(btn, parseInt(btn.getAttribute('data-layer'), 10), false);
      });
      var allBtn = document.getElementById('stem-export-all');
      if (allBtn) attachStemDrag(allBtn, -1, true);

      // Reveal folder
      var revealBtn = document.getElementById('stem-reveal');
      if (revealBtn) revealBtn.addEventListener('click', function () {
        var fn = getNativeFn('revealStemsFolder');
        if (fn) { try { fn(); } catch (_) {} }
      });

      // DRY / MIX source pills
      document.querySelectorAll('.stem-source-pill').forEach(function (pill) {
        pill.addEventListener('click', function () {
          var val = parseInt(pill.getAttribute('data-val'), 10);
          document.querySelectorAll('.stem-source-pill').forEach(function (p) {
            p.classList.toggle('active', parseInt(p.getAttribute('data-val'), 10) === val);
          });
          var fn = getNativeFn('setStemSourceMode');
          if (fn) { try { fn(val); } catch (_) {} }
        });
      });
      // CLEAR — wipes all 4 rolling buffers + flashes for feedback.
      var clearBtn = document.getElementById('stem-clear');
      if (clearBtn) clearBtn.addEventListener('click', function () {
        clearBtn.classList.add('flash');
        setTimeout(function () { clearBtn.classList.remove('flash'); }, 200);
        var fn = getNativeFn('clearStemBuffers');
        if (fn) { try { fn(); } catch (_) {} }
      });
    }

    function pullStemStateFromCpp () {
      var fn = getNativeFn('getStemSourceMode');
      if (! fn) return;
      try {
        var r = fn();
        var apply = function (v) {
          var val = (typeof v === 'number') ? v : (parseInt(v, 10) || 0);
          document.querySelectorAll('.stem-source-pill').forEach(function (p) {
            p.classList.toggle('active', parseInt(p.getAttribute('data-val'), 10) === val);
          });
        };
        if (r && typeof r.then === 'function') r.then(apply).catch(function () {});
        else apply(r);
      } catch (_) {}
    }

    // Live capture meters — polled at ~30Hz only while the Mix panel is open.
    function pollStemCaptureMeters () {
      var panel = document.getElementById('mix-panel');
      if (! panel || ! panel.classList.contains('open')) return;
      var fn = getNativeFn('getStemCaptureLevels');
      if (! fn) return;
      try {
        var r = fn();
        var apply = function (arr) {
          if (! arr || ! arr.length) return;
          for (var i = 0; i < 4; ++i) {
            var v = (typeof arr[i] === 'number') ? arr[i] : 0;
            // Perceptual curve so quiet signals still register visibly.
            var w = Math.min(1, Math.sqrt(v) * 1.4) * 100;
            var fill = document.querySelector('.stem-cap-meter-fill[data-layer="' + i + '"]');
            if (fill) fill.style.width = w.toFixed(1) + '%';
          }
        };
        if (r && typeof r.then === 'function') r.then(apply).catch(function () {});
        else apply(r);
      } catch (_) {}
    }

    // Live RR position dots — only polls while the Mix panel is open AND RR mode
    // is selected, so it's idle the rest of the time.
    function pollRrDots () {
      var panel = document.getElementById('mix-panel');
      if (! panel || ! panel.classList.contains('open')) return;
      var ctx = document.getElementById('trigger-context');
      if (! ctx || ctx.getAttribute('data-mode') !== '1') return;
      var cont = document.getElementById('rr-dots');
      if (! cont) return;
      // Only the cursor (next-to-fire) lights up — it travels as the engine cycles.
      var pf = getNativeFn('getRoundRobinPos');
      if (pf) {
        try {
          var r = pf();
          var ap = function (pos) { var p = (typeof pos === 'number') ? pos : 0; for (var i = 0; i < 4; ++i) { var d = cont.children[i]; if (d) d.classList.toggle('next', i === p); } };
          if (r && typeof r.then === 'function') r.then(ap).catch(function(){}); else ap(r);
        } catch (_) {}
      }
    }

    function init () {
      if (! document.getElementById('mix-btn')) {
        // index.html mod-buttons not yet in DOM — retry next frame.
        requestAnimationFrame(init);
        return;
      }
      injectMixPanelStyles();
      injectTriggerStyles();
      injectStemStyles();
      buildMixPanel();
      buildTriggerArea();
      buildStemArea();
      wireStrips();
      wireTriggerArea();
      wireStemArea();
      setupMixPillWiring();
      setInterval(pollMixMeters, 33);            // ~30 Hz strip meters
      setInterval(pollStripsPlayingDots, 100);   // 10 Hz play-dot indicator
      setInterval(pullLayerStatusDots, 250);     // 4 Hz layer-populated dots (LAYER mode)
      setInterval(pollRrDots, 50);               // ~20 Hz RR position dots (RR mode only)
      setInterval(pollStemCaptureMeters, 33);    // ~30 Hz stem capture meters
    }

    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', init);
    } else {
      init();
    }
  })();
})();
</script>
)TIHX");
    html = html.replace ("</body>", heroOverlay + "</body>");

    auto utf8 = html.toUTF8();
    std::vector<std::byte> data(static_cast<size_t>(utf8.sizeInBytes()));
    std::memcpy(data.data(), utf8.getAddress(), data.size());
    return juce::WebBrowserComponent::Resource{ std::move(data), juce::String("text/html; charset=utf-8") };
}

// ════════════════════════════════════════════════════════════════════════════
// File drag-drop (Task 11) + sample loading
// ════════════════════════════════════════════════════════════════════════════

bool TerrainInstrumentAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.isEmpty()) return false;
    const auto ext = juce::File (files[0]).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff"
        || ext == ".flac" || ext == ".mp3"
        || ext == ".terrain" || ext == ".terrainpack";
}

void TerrainInstrumentAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    if (webView != nullptr)
        webView->evaluateJavascript ("if (window.onDragHover) window.onDragHover(true);", nullptr);
}

void TerrainInstrumentAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    if (webView != nullptr)
        webView->evaluateJavascript ("if (window.onDragHover) window.onDragHover(false);", nullptr);
}

void TerrainInstrumentAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (webView != nullptr)
        webView->evaluateJavascript ("if (window.onDragHover) window.onDragHover(false);", nullptr);

    if (files.isEmpty()) return;
    const juce::File f (files[0]);
    const auto ext = f.getFileExtension().toLowerCase();

    if (ext == ".terrain")     { loadPatch (f);          return; }
    if (ext == ".terrainpack") { importTerrainPack (f);  return; }

    // PEROSC-DRAGGUARD — on the synth page, an oscillator waveform drop is handled in JS
    // (the .samp-disp 'drop' listener → loadSampleForOsc → that osc's own buffer). The OS-level
    // drop target must NOT also load the front-panel multi-sampler layer, or one file lands in
    // two places. When the synth page is hidden the front panel still loads via loadSampleAsync.
    if (synthPageActive_) return;

    // Audio file → load as new sample
    loadSampleAsync (f);
}

void TerrainInstrumentAudioProcessorEditor::loadSampleAsync (const juce::File& file)
{
    currentSampleSourcePath = file.getFullPathName();
    // Push to processor so DAW state save captures it (survives project reload).
    audioProcessor.setLoadedSamplePath (currentSampleSourcePath);

    auto& loader = audioProcessor.getSampleLoader();
    auto& target = audioProcessor.getSampleBuffer();

    if (webView != nullptr)
        webView->evaluateJavascript (
            "if (window.onLoadingStarted) window.onLoadingStarted("
            + juce::JSON::toString (juce::var (file.getFileName())) + ");",
            nullptr);

    loader.load (
        file,
        target,
        [this] (float progress)
        {
            if (webView != nullptr)
                webView->evaluateJavascript (
                    "if (window.onLoadingProgress) window.onLoadingProgress("
                    + juce::String (progress, 4) + ");",
                    nullptr);
        },
        [this] (tw::SampleLoader::Result r)
        {
            if (! r.success)
            {
                if (webView != nullptr)
                    webView->evaluateJavascript (
                        "if (window.onLoadError) window.onLoadError("
                        + juce::JSON::toString (juce::var (r.errorMessage)) + ");",
                        nullptr);
                return;
            }

            // Task 8: dispatch sample load into the currently-editing layer so
            // clicking a pad and dragging a file always loads into the right slot.
            // Task 12: also persist the full path in layers[li].sourcePath so
            // V2 preset save can round-trip the file reference per-layer.
            {
                const size_t li = (size_t) audioProcessor.editingLayer.load();
                auto buf = audioProcessor.getSampleBuffer().load();
                audioProcessor.layers[li].sampleBuffer.setSampleRate (r.sampleRate);
                audioProcessor.layers[li].sampleBuffer.store (buf);
                audioProcessor.layers[li].sourceFileName = r.filename;
                audioProcessor.layers[li].sourcePath     = currentSampleSourcePath;
            }

            // Build a JS object literal with peaks + meta and pass to onSampleLoaded.
            juce::Array<juce::var> minArr, maxArr;
            minArr.ensureStorageAllocated ((int) r.peaksMin.size());
            maxArr.ensureStorageAllocated ((int) r.peaksMax.size());
            for (auto v : r.peaksMin) minArr.add (juce::var (v));
            for (auto v : r.peaksMax) maxArr.add (juce::var (v));

            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty ("filename",      r.filename);
            obj->setProperty ("sampleRate",    r.sampleRate);
            obj->setProperty ("lengthSamples", r.lengthSamples);
            obj->setProperty ("numChannels",   r.numChannels);
            obj->setProperty ("peaksMin",      juce::var (minArr));
            obj->setProperty ("peaksMax",      juce::var (maxArr));

            const auto json = juce::JSON::toString (juce::var (obj.get()), true /*allOnOneLine*/);

            // Cache the payload so editor close/reopen restores the waveform
            // display instantly without re-decoding. JS pulls this via the
            // getCachedSamplePayload native fn during hero-overlay init.
            audioProcessor.setCachedSamplePayload (json);

            // Reset pitchModeSlice bounds to cover the full new sample.
            // Preserve user-edited warp/ADSR/scan settings — only the
            // sample-position fields change.
            audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.startSample = 0;
            audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].pitchModeSlice.endSample   = (juce::int64) r.lengthSamples;
            // Register with WarpRenderCache (sliceIndex=-1) — see the
            // setPitchSliceBounds native fn comment for the rationale.
            audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].synth.warpCache.setSliceBounds (-1, 0, (int) r.lengthSamples);

            // Bump the source version counter so WarpRenderCache entries from
            // any previous sample are never matched against the new one. Then
            // push the new source pointers + sample rate into the warp cache
            // so prewarm() calls can start immediately after this load.
            audioProcessor.sourceVersionId_.fetch_add (1, std::memory_order_relaxed);
            {
                auto buf = audioProcessor.getSampleBuffer().load();
                // Mono fallback: duplicate channel 0 into both L/R cache sources so
                // warp engines have something to read. Previous guard demanded
                // numChannels>=2 → mono samples silently bypassed warp cache wiring
                // → warp+scan combo path emitted silent blocks (audit finding #4).
                if (buf && buf->getNumSamples() > 0 && buf->getNumChannels() >= 1)
                {
                    const float* L = buf->getReadPointer (0);
                    const float* R = (buf->getNumChannels() >= 2)
                                       ? buf->getReadPointer (1) : L;
                    audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].synth.warpCache.setSource (L, R, buf->getNumSamples());
                    audioProcessor.layers[(size_t) audioProcessor.editingLayer.load()].synth.warpCache.setSampleRate (r.sampleRate);
                }
            }

            if (webView != nullptr)
                webView->evaluateJavascript (
                    "if (window.onSampleLoaded) window.onSampleLoaded(" + json + ");",
                    nullptr);
        });
}

// ── Task 13: per-layer sample reload ─────────────────────────────────────────
// Identical to loadSampleAsync but targets a FIXED layer index (layerIdx) rather
// than whatever editingLayer is at callback time.  Used by the editor constructor
// to reload all 4 layers after a V2 DAW project restore.
//
// Key difference from loadSampleAsync:
//   - Does NOT call setLoadedSamplePath — that singleton is the V1 "layer 0"
//     path and is already set by setStateInformation / loadV2State.
//   - Does NOT fire the JS onSampleLoaded callback for non-editing layers
//     (only the currently-editing layer drives the visible waveform display).
//   - DOES cache the payload when reloading the currently-editing layer so
//     that subsequent editor reopen (Case 1) still works for that layer.
void TerrainInstrumentAudioProcessorEditor::loadSampleIntoLayer (const juce::File& file,
                                                                  int layerIdx)
{
    if (layerIdx < 0 || layerIdx > 3) return;

    auto& loader = audioProcessor.getSampleLoader();
    auto& target = audioProcessor.layers[(size_t) layerIdx].sampleBuffer;

    const bool isEditingLayer = (layerIdx == audioProcessor.editingLayer.load());

    if (isEditingLayer && webView != nullptr)
        webView->evaluateJavascript (
            "if (window.onLoadingStarted) window.onLoadingStarted("
            + juce::JSON::toString (juce::var (file.getFileName())) + ");",
            nullptr);

    loader.load (
        file,
        target,
        [this, isEditingLayer] (float progress)
        {
            if (isEditingLayer && webView != nullptr)
                webView->evaluateJavascript (
                    "if (window.onLoadingProgress) window.onLoadingProgress("
                    + juce::String (progress, 4) + ");",
                    nullptr);
        },
        [this, layerIdx, isEditingLayer] (tw::SampleLoader::Result r)
        {
            if (! r.success) return;  // Non-editing layer errors: silent skip (no UI to show).

            // Persist filename + path into the layer state.
            {
                auto& L = audioProcessor.layers[(size_t) layerIdx];
                auto buf = L.sampleBuffer.load();
                L.sampleBuffer.setSampleRate (r.sampleRate);
                L.sampleBuffer.store (buf);
                L.sourceFileName = r.filename;
                // sourcePath is already set by setStateInformation — don't overwrite.
            }

            // Wire up the WarpRenderCache for this layer.
            audioProcessor.sourceVersionId_.fetch_add (1, std::memory_order_relaxed);
            {
                auto& L  = audioProcessor.layers[(size_t) layerIdx];
                auto  buf = L.sampleBuffer.load();
                if (buf && buf->getNumSamples() > 0 && buf->getNumChannels() >= 1)
                {
                    const float* Lp = buf->getReadPointer (0);
                    const float* Rp = (buf->getNumChannels() >= 2)
                                        ? buf->getReadPointer (1) : Lp;
                    L.synth.warpCache.setSource (Lp, Rp, buf->getNumSamples());
                    L.synth.warpCache.setSampleRate (r.sampleRate);
                }
                // Restore pitchModeSlice sample bounds now that we know the real length.
                // Only overwrite if the restored slice didn't already have valid bounds
                // (applyPitchSliceJson sets them if serialised; otherwise they're 0/0).
                if (L.pitchModeSlice.endSample <= L.pitchModeSlice.startSample)
                {
                    L.pitchModeSlice.startSample = 0;
                    L.pitchModeSlice.endSample   = (juce::int64) r.lengthSamples;
                }
                L.synth.warpCache.setSliceBounds (-1,
                    (int) L.pitchModeSlice.startSample,
                    (int) L.pitchModeSlice.endSample);
            }

            // Build the JS payload for BOTH the editing-layer callback
            // (drives the visible waveform) and the per-layer mirror callback
            // (populates state.layerStates[layerIdx] so a future pad switch
            // restores this layer's peaks/length without a re-load).
            juce::Array<juce::var> minArr, maxArr;
            minArr.ensureStorageAllocated ((int) r.peaksMin.size());
            maxArr.ensureStorageAllocated ((int) r.peaksMax.size());
            for (auto v : r.peaksMin) minArr.add (juce::var (v));
            for (auto v : r.peaksMax) maxArr.add (juce::var (v));

            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty ("filename",      r.filename);
            obj->setProperty ("sampleRate",    r.sampleRate);
            obj->setProperty ("lengthSamples", r.lengthSamples);
            obj->setProperty ("numChannels",   r.numChannels);
            obj->setProperty ("peaksMin",      juce::var (minArr));
            obj->setProperty ("peaksMax",      juce::var (maxArr));

            const auto json = juce::JSON::toString (juce::var (obj.get()), true);

            // Cache this layer's payload so editor close/reopen restores
            // every populated layer's waveform — not just the editing one.
            // Required for V2 preset migration where all 4 layers can carry samples.
            audioProcessor.setCachedSamplePayload (json, layerIdx);

            // Always populate the per-layer JS mirror — required so V2 preset
            // migration leaves all 4 layers' UIs ready for instant pad-switch.
            if (webView != nullptr)
                webView->evaluateJavascript (
                    "if (window.onLayerSampleMirror) window.onLayerSampleMirror("
                    + juce::String (layerIdx) + ", " + json + ");",
                    nullptr);

            // The editing-layer load also drives the visible UI directly
            // (onSampleLoaded resets peakScale, fires drawWaveform, etc., and
            // its trailing snapshotCurrentLayer overwrites the mirror above
            // with the fully-computed snapshot — which is what we want).
            if (! isEditingLayer) return;

            if (webView != nullptr)
                webView->evaluateJavascript (
                    "if (window.onSampleLoaded) window.onSampleLoaded(" + json + ");",
                    nullptr);
        });
}

// ── PEROSC — per-OSC sample load. Mirrors loadSampleIntoLayer but targets the dedicated
//    synth-side oscSampleBuffers_[idx], caches a per-OSC payload, and fires
//    window.onOscSampleLoaded(letter, json) so the UI draws that oscillator's waveform.
void TerrainInstrumentAudioProcessorEditor::loadOscSampleAsync (int oscIdx, const juce::File& file)
{
    if (oscIdx < 0 || oscIdx > 3) return;
    const char oscLetter = (char) ('a' + oscIdx);
    audioProcessor.oscSourcePath (oscIdx) = file.getFullPathName();

    auto& loader = audioProcessor.getOscSampleLoader (oscIdx);
    auto& target = audioProcessor.getOscSampleBuffer (oscIdx);

    loader.load (
        file,
        target,
        [] (float) {},   // per-OSC drops are short one-shots — no progress UI
        [this, oscIdx, oscLetter] (tw::SampleLoader::Result r)
        {
            if (! r.success) return;   // SampleLoader already stored the buffer (rate set) into target.

            // Build the peaks JSON — identical shape to the front sampler's onSampleLoaded.
            juce::Array<juce::var> minArr, maxArr;
            minArr.ensureStorageAllocated ((int) r.peaksMin.size());
            maxArr.ensureStorageAllocated ((int) r.peaksMax.size());
            for (auto v : r.peaksMin) minArr.add (juce::var (v));
            for (auto v : r.peaksMax) maxArr.add (juce::var (v));

            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty ("filename",      r.filename);
            obj->setProperty ("sampleRate",    r.sampleRate);
            obj->setProperty ("lengthSamples", r.lengthSamples);
            obj->setProperty ("numChannels",   r.numChannels);
            obj->setProperty ("peaksMin",      juce::var (minArr));
            obj->setProperty ("peaksMax",      juce::var (maxArr));
            const auto json = juce::JSON::toString (juce::var (obj.get()), true /*allOnOneLine*/);

            audioProcessor.setCachedOscPayload (json, oscIdx);   // restored on editor reopen

            if (webView != nullptr)
                webView->evaluateJavascript (
                    juce::String ("if (window.onOscSampleLoaded) window.onOscSampleLoaded('")
                    + oscLetter + "', " + json + ");",
                    nullptr);
        });
}

// ── Stubs (real implementations land in Tasks 18 and 22 of the v0a plan) ────

void TerrainInstrumentAudioProcessorEditor::loadPatch (const juce::File&)
{
    // Task 18 (Phase E) — read .terrain JSON, restore APVTS, trigger sample load
    if (webView != nullptr)
        webView->evaluateJavascript (
            "if (window.onLoadError) window.onLoadError('Patch loading lands in v0a Phase E.');",
            nullptr);
}

void TerrainInstrumentAudioProcessorEditor::importTerrainPack (const juce::File&)
{
    // Task 22 (Phase E) — unzip .terrainpack to User/Patches + User/Samples, then loadPatch
    if (webView != nullptr)
        webView->evaluateJavascript (
            "if (window.onLoadError) window.onLoadError('Pack import lands in v0a Phase E.');",
            nullptr);
}
