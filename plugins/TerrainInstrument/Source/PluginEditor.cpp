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
            .withOptionsFrom(eqRelays[0])  .withOptionsFrom(eqRelays[1])  .withOptionsFrom(eqRelays[2])  .withOptionsFrom(eqRelays[3])  .withOptionsFrom(eqRelays[4])
            .withOptionsFrom(eqRelays[5])  .withOptionsFrom(eqRelays[6])  .withOptionsFrom(eqRelays[7])  .withOptionsFrom(eqRelays[8])  .withOptionsFrom(eqRelays[9])
            .withOptionsFrom(eqRelays[10]) .withOptionsFrom(eqRelays[11]) .withOptionsFrom(eqRelays[12]) .withOptionsFrom(eqRelays[13]) .withOptionsFrom(eqRelays[14])
            .withOptionsFrom(eqRelays[15]) .withOptionsFrom(eqRelays[16]) .withOptionsFrom(eqRelays[17]) .withOptionsFrom(eqRelays[18]) .withOptionsFrom(eqRelays[19])
            .withOptionsFrom(eqRelays[20]) .withOptionsFrom(eqRelays[21]) .withOptionsFrom(eqRelays[22]) .withOptionsFrom(eqRelays[23]) .withOptionsFrom(eqRelays[24])
            .withOptionsFrom(eqRelays[25]) .withOptionsFrom(eqRelays[26]) .withOptionsFrom(eqRelays[27]) .withOptionsFrom(eqRelays[28]) .withOptionsFrom(eqRelays[29])
            .withOptionsFrom(eqRelays[30]) .withOptionsFrom(eqRelays[31]) .withOptionsFrom(eqRelays[32]) .withOptionsFrom(eqRelays[33]) .withOptionsFrom(eqRelays[34])
            .withNativeFunction("loadPreset", [this](const juce::Array<juce::var>& args,
                                                      juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.loadPreset(static_cast<int>(args[0]));
                complete({});
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
                    audioProcessor.rootNoteMidi.store (midi);
                }
                complete ({});
            })
            .withNativeFunction("getCachedSamplePayload", [this](const juce::Array<juce::var>&,
                                                                  juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                // JS pulls this on hero-overlay init. Returns the cached
                // sample payload JSON (filename + peaks + meta) so editor
                // close/reopen restores the waveform display without a
                // re-decode. Empty string if no sample loaded yet.
                complete (audioProcessor.getCachedSamplePayload());
            })
            .withNativeFunction("setSampleLoopMode", [this](const juce::Array<juce::var>& args,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                {
                    const int mode = juce::jlimit (0, 1, (int) args[0]);
                    if (auto* p = audioProcessor.getAPVTS().getParameter (ParameterIDs::SAMPLE_LOOP_MODE))
                        p->setValueNotifyingHost (static_cast<float> (mode));  // Choice: 0=ONE-SHOT, 1=LOOP
                    audioProcessor.sampleLoopMode.store (mode);
                }
                complete ({});
            })
            .withNativeFunction("getSampleLoopMode", [this](const juce::Array<juce::var>&,
                                                              juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.sampleLoopMode.load());
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
                    if (auto* p = audioProcessor.getAPVTS().getParameter (ParameterIDs::SLICE_MODE))
                        p->setValueNotifyingHost (static_cast<float> (mode));
                }
                complete ({});
            })
            .withNativeFunction("getSliceMode", [this](const juce::Array<juce::var>&,
                                                         juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete ((int) *audioProcessor.getAPVTS().getRawParameterValue (ParameterIDs::SLICE_MODE));
            })
            .withNativeFunction("getSlicesJson", [this](const juce::Array<juce::var>&,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.getSlicesJson());
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
                    const int sub = juce::jlimit (0, 2, (int) args[0]);  // 0=CHOP, 1=CHROMATIC, 2=RANDOM
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
                    audioProcessor.activeSliceIndex.store (idx);
                }
                complete ({});
            })
            .withNativeFunction("getActiveSliceIndex", [this](const juce::Array<juce::var>&,
                                                                juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                complete (audioProcessor.activeSliceIndex.load());
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
                // args[0] = sliceIndex (int), args[1] = reverse (bool)
                if (args.size() < 2) { complete ({}); return; }
                const int idx = (int) args[0];
                const bool rev = (bool) args[1];
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
                // args[0] = sliceIndex, args[1] = semitones (-24..+24)
                if (args.size() < 2) { complete ({}); return; }
                const int   idx = (int) args[0];
                const float st  = juce::jlimit (-24.0f, 24.0f, (float) (double) args[1]);
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].pitchOffsetSemis = st;
                audioProcessor.replaceSlices (std::move (copy));
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

    // Load embedded web content
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    // Set size AFTER webView is created (setSize triggers resized())
    setSize (820, 640 + CAPTURE_STRIP_HEIGHT);

    // Start visualization timer at 60Hz for smooth LFO/mod display
    startTimerHz(60);

    // Auto-reload the previously-loaded sample. Two cases:
    //   1) Cache hit (editor close+reopen, same processor instance): the JS
    //      side pulls the cached payload via getCachedSamplePayload and
    //      restores the waveform display instantly. No decode needed —
    //      audio buffer was never lost. Skip loadSampleAsync here.
    //   2) Cache miss + path set (DAW project reload, fresh processor): the
    //      audio buffer is empty, so we must re-decode from disk to repopulate
    //      it. This also re-pushes the JS payload via the load completion.
    juce::Component::SafePointer<TerrainInstrumentAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr) return;
        // Case 1: cache hit — JS will pull on its own. Nothing for C++ to do.
        if (safeThis->audioProcessor.getCachedSamplePayload().isNotEmpty()) return;
        // Case 2: cache empty but path stored — full decode.
        const auto storedPath = safeThis->audioProcessor.getLoadedSamplePath();
        if (storedPath.isEmpty()) return;
        const juce::File f (storedPath);
        if (f.existsAsFile()) safeThis->loadSampleAsync (f);
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

    g.fillAll(isDarkMode ? juce::Colour(0xFF232340) : juce::Colour(0xFFE8E4EF));

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
            g.setColour(isDarkMode ? juce::Colour(0x44606080) : juce::Colour(0x44857399));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("CAPTURE: LISTENING...", b, juce::Justification::centred);
        }
        else
        {
            g.setColour(isDarkMode ? juce::Colour(0xFF9B93B0) : juce::Colour(0xFF6B5B7B));
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
    const juce::String heroOverlay = R"(
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

  /* Slice markers + bodies — drawn on top of the waveform canvas */
  #ti-slice-overlays {
    position: absolute; left: 0; right: 0;
    pointer-events: none;  /* parent doesn't intercept, children re-enable */
    z-index: 4;
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
      ellipse 60% 78% at 50% 50%,
      rgba(139,92,246, calc(var(--glow-alpha) * 0.55)) 0%,
      rgba(139,92,246, calc(var(--glow-alpha) * 0.20)) 55%,
      rgba(139,92,246, 0) 100%);
    filter: blur(2px);
    z-index: -1;
  }
  .ti-slice-body.dragging { background: rgba(139,92,246,0.22); }
  .ti-slice-badge {
    position: absolute; top: -22px; left: 50%; transform: translateX(-50%);
    background: rgba(20,18,32,0.85); backdrop-filter: blur(6px);
    padding: 3px 7px; border-radius: 3px;
    font: 600 9px/1 sans-serif; letter-spacing: 0.1em;
    color: rgba(245,243,255,0.85); white-space: nowrap;
    pointer-events: none;
  }
  .ti-slice-badge .pitch { color: #A78BFA; }
  .ti-slice-badge .rev   { color: #FFB066; margin-right: 4px; }

  /* Right-click context menu */
  #ti-slice-ctx {
    position: absolute; z-index: 20;
    background: rgba(20,18,32,0.96); backdrop-filter: blur(12px);
    border-radius: 5px; padding: 4px;
    min-width: 140px;
    box-shadow: 0 4px 16px rgba(0,0,0,0.4);
    display: none;
  }
  #ti-slice-ctx.open { display: block; }
  #ti-slice-ctx .item {
    padding: 7px 14px;
    font: 600 10px/1 sans-serif; letter-spacing: 0.10em;
    color: rgba(245,243,255,0.85); cursor: pointer; border-radius: 3px;
    user-select: none;
  }
  #ti-slice-ctx .item:hover { background: rgba(139,92,246,0.25); color: white; }
  #ti-slice-ctx .item.danger:hover { background: rgba(255,80,80,0.22); }
  #ti-slice-ctx .sep { height: 1px; background: rgba(255,255,255,0.06); margin: 3px 6px; }

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
      '<span class="ti-root-label">ROOT</span>' +
      '<span class="ti-root-value" id="ti-root-value">C4</span>';
    hero.appendChild(rootWrap);

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
        '</div>' +
      '</div>';
    bottomPills.appendChild(slicesWrap);

    // Slice marker overlay container (positioned over the waveform).
    var sliceOverlays = document.createElement('div');
    sliceOverlays.id = 'ti-slice-overlays';
    hero.appendChild(sliceOverlays);

    // Right-click context menu (single instance, repositioned on demand).
    var ctx = document.createElement('div');
    ctx.id = 'ti-slice-ctx';
    ctx.innerHTML =
      '<div class="item" data-act="rev">Reverse</div>' +
      '<div class="item" data-act="resetPitch">Reset Pitch</div>' +
      '<div class="sep"></div>' +
      '<div class="item danger" data-act="del">Delete Chop</div>';
    hero.appendChild(ctx);

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

  // ── State ─────────────────────────────────────────────────────────────────
  var state = {
    peaksMin: null,
    peaksMax: null,
    progress: 0,    // 0..1 during loading; 1 when fully loaded
    loading: false,
    rootNote: 60,
    // Slicer state
    sliceMode: 0,           // 0 = PITCH (whole sample), 1 = SLICE
    sliceSubMode: 0,        // 0 = CHOP, 1 = CHROMATIC
    slices: [],             // [{start, end, reverse, pitch}, ...]
    activeSliceIndex: 0,    // active in CHROMATIC sub-mode
    gridN: 16,              // last grid count used
    sampleLengthSamples: 0, // total length of loaded sample (for marker positioning)
    sliceGlow: new Float32Array(256)  // per-slice glow [0..1], polled from C++ at ~60Hz
  };

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

    // Filled body (mirrored around centerline)
    ctx.fillStyle = 'rgba(245, 243, 255, 0.10)';
    ctx.beginPath();
    ctx.moveTo(0, cy);
    var i;
    for (i = 0; i < visibleN; i++) {
      var x = (i / Math.max(1, n - 1)) * w;
      var yMax = cy - state.peaksMax[i] * cy * 0.95;
      ctx.lineTo(x, yMax);
    }
    for (i = visibleN - 1; i >= 0; i--) {
      var x2 = (i / Math.max(1, n - 1)) * w;
      var yMin = cy - state.peaksMin[i] * cy * 0.95;
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
      var ym = cy - state.peaksMax[i] * cy * 0.95;
      if (i === 0) ctx.moveTo(xx, ym); else ctx.lineTo(xx, ym);
    }
    ctx.stroke();

    // Bottom edge stroke
    ctx.strokeStyle = 'rgba(245, 243, 255, 0.85)';
    ctx.beginPath();
    for (i = 0; i < visibleN; i++) {
      var xx2 = (i / Math.max(1, n - 1)) * w;
      var ym2 = cy - state.peaksMin[i] * cy * 0.95;
      if (i === 0) ctx.moveTo(xx2, ym2); else ctx.lineTo(xx2, ym2);
    }
    ctx.stroke();
  }

  // ── Slicer helpers ────────────────────────────────────────────────────────
  function setSliceModeUI (modeIdx) {
    state.sliceMode = modeIdx;
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
      state.slices = arr.map(function (s) {
        return {
          start:   parseInt(s.start, 10) || 0,
          end:     parseInt(s.end,   10) || 0,
          reverse: !!s.reverse,
          pitch:   parseFloat(s.pitch) || 0
        };
      });
      // Clamp activeSliceIndex.
      if (state.activeSliceIndex >= state.slices.length)
        state.activeSliceIndex = Math.max(0, state.slices.length - 1);
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

    overlays.innerHTML = '';

    if (state.sliceMode !== 1 || state.slices.length === 0 || state.sampleLengthSamples <= 0) {
      return;
    }

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
    var totalSamples = state.sampleLengthSamples;
    var isChromatic  = state.sliceSubMode === 1;
    var activeIdx    = state.activeSliceIndex;

    state.slices.forEach(function (s, i) {
      var leftFrac  = s.start / totalSamples;
      var widthFrac = (s.end - s.start) / totalSamples;
      var leftPx    = leftFrac  * W;
      var widthPx   = widthFrac * W;
      if (widthPx < 1) return;

      // Body (clickable / draggable region)
      var body = document.createElement('div');
      body.className = 'ti-slice-body';
      body.style.left  = leftPx  + 'px';
      body.style.width = widthPx + 'px';
      body.dataset.idx = i;
      attachSliceGestures(body, i);
      overlays.appendChild(body);

      // Pitch / reverse badge — only shown when the slice has an offset.
      // No persistent armed-slice indicator in CHROMATIC; the play-glow
      // is the only purple you ever see.
      var hasOffset = (s.pitch !== 0) || s.reverse;
      if (hasOffset) {
        var badge = document.createElement('div');
        badge.className = 'ti-slice-badge';
        badge.style.left = (leftPx + widthPx / 2) + 'px';
        var pitchTxt = (s.pitch > 0 ? '+' : '') + (s.pitch || 0).toFixed(0) + 'st';
        badge.innerHTML = (s.reverse ? '<span class="rev">REV</span>' : '') +
                          '<span class="pitch">' + pitchTxt + '</span>';
        body.appendChild(badge);
      }

      // Marker (left edge — index label sits at top)
      if (i > 0) {  // first slice always starts at 0; no marker line needed
        var marker = document.createElement('div');
        marker.className = 'ti-slice-marker';
        marker.style.left = leftPx + 'px';
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

  // Drag a marker line horizontally to move the boundary between
  // slice[i-1] and slice[i]. Live visual updates during drag (no DOM
  // thrash); on release, send the final position to C++ which snaps to
  // the nearest zero crossing and clamps to keep neither slice below
  // 64 samples. C++ returns the authoritative slice list which we apply.
  function attachMarkerDrag (marker, i) {
    marker.addEventListener('mousedown', function (ev) {
      if (ev.button !== 0) return;
      ev.preventDefault();
      ev.stopPropagation();
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

      function clientXToSample (x) {
        var frac = (x - waveRect.left) / W;
        frac = Math.max(0, Math.min(1, frac));
        var s = Math.round(frac * total);
        return Math.max(minSample, Math.min(maxSample, s));
      }
      function onMove (mev) {
        var s = clientXToSample(mev.clientX);
        marker.style.left = ((s / total) * W) + 'px';
      }
      function onUp (mev) {
        document.removeEventListener('mousemove', onMove, true);
        document.removeEventListener('mouseup',   onUp,   true);
        document.body.style.cursor = '';
        marker.classList.remove('dragging');
        var finalSample = clientXToSample(mev.clientX);
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
      // The second click of a double-click suppresses the audition / pitch-drag
      // path — the dblclick handler below adds a marker instead. Without this
      // the user would audition twice on every "add chop" gesture.
      if (ev.detail >= 2) { ev.preventDefault(); return; }
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
      var frac = (ev.clientX - waveRect.left) / waveRect.width;
      frac = Math.max(0, Math.min(1, frac));
      var samplePos = Math.round(frac * state.sampleLengthSamples);
      var fn = getNativeFn('addMarkerAt');
      if (fn) fn(samplePos).then(applySlicesJson).catch(function(){});
    });

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
      var newPitch = Math.max(-24, Math.min(24, dragState.startPitch + deltaSemi));
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

  function openSliceContextMenu (ev, idx) {
    var ctx = document.getElementById('ti-slice-ctx');
    var hero = document.getElementById('hero');
    if (!ctx || !hero) return;
    var heroRect = hero.getBoundingClientRect();
    ctx.style.left = (ev.clientX - heroRect.left) + 'px';
    ctx.style.top  = (ev.clientY - heroRect.top)  + 'px';
    ctx.classList.add('open');
    ctx.dataset.targetIdx = idx;
  }

  function closeSliceContextMenu () {
    var ctx = document.getElementById('ti-slice-ctx');
    if (ctx) ctx.classList.remove('open');
  }

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

    // Sub-mode pills (CHOP / CHROMATIC) — selector unchanged from v0b
    // since #ti-submode-toggle moved into the drawer with the same id.
    document.querySelectorAll('#ti-submode-toggle .ti-submode-pill').forEach(function (p) {
      p.addEventListener('click', function (ev) {
        ev.stopPropagation();
        var sub = parseInt(p.dataset.sub, 10) || 0;
        setSubModeUI(sub);
        var fn = getNativeFn('setSliceSubMode');
        if (fn) { try { fn(sub); } catch (_) {} }
      });
    });

    // Context menu actions
    var ctxMenu = document.getElementById('ti-slice-ctx');
    if (ctxMenu) {
      ctxMenu.addEventListener('click', function (ev) {
        var item = ev.target.closest('.item');
        if (!item) return;
        var idx = parseInt(ctxMenu.dataset.targetIdx, 10);
        if (isNaN(idx)) return;
        var act = item.dataset.act;
        if (act === 'rev') {
          if (state.slices[idx]) {
            state.slices[idx].reverse = !state.slices[idx].reverse;
            var fnR = getNativeFn('setSliceReverse');
            if (fnR) { try { fnR(idx, state.slices[idx].reverse); } catch (_) {} }
            redrawSliceOverlay();
          }
        } else if (act === 'resetPitch') {
          if (state.slices[idx]) {
            state.slices[idx].pitch = 0;
            var fnP = getNativeFn('setSlicePitch');
            if (fnP) { try { fnP(idx, 0); } catch (_) {} }
            redrawSliceOverlay();
          }
        } else if (act === 'del') {
          var fnD = getNativeFn('deleteSlice');
          if (fnD) {
            try {
              var r = fnD(idx);
              if (r && typeof r.then === 'function') r.then(applySlicesJson);
              else applySlicesJson(r);
            } catch (_) {}
          }
        }
        closeSliceContextMenu();
      });
    }
    // Click anywhere else closes the context menu.
    document.addEventListener('click', closeSliceContextMenu);
    // Click outside the SLICES drawer closes it. Drawer interactions
    // already stopPropagation so they don't reach this listener.
    document.addEventListener('click', function (ev) {
      var dr = document.getElementById('ti-slicer-drawer');
      if (!dr || !dr.classList.contains('open')) return;
      var wrap = document.getElementById('ti-slices-wrap');
      if (wrap && !wrap.contains(ev.target)) closeSlicerDrawer();
    });


    // Play-mode toggle (1-SHOT / LOOP) — writes APVTS via setSampleLoopMode.
    document.querySelectorAll('#ti-play-mode-toggle .ti-play-pill').forEach(function (pill) {
      pill.addEventListener('click', function () {
        var mode = parseInt(pill.dataset.play, 10) || 0;
        document.querySelectorAll('#ti-play-mode-toggle .ti-play-pill').forEach(function (p) {
          p.classList.toggle('active', parseInt(p.dataset.play, 10) === mode);
        });
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
            document.querySelectorAll('#ti-play-mode-toggle .ti-play-pill').forEach(function (q) {
              q.classList.toggle('active', parseInt(q.dataset.play, 10) === m);
            });
          });
        }
      } catch (_) {}
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

    // ── Restore cached sample on editor reopen ────────────────────────────────
    // If the processor instance still has a sample loaded (audio buffer hot,
    // user just closed and reopened the editor window), pull the cached
    // payload and dispatch it through onSampleLoaded so the waveform display
    // returns immediately without a re-decode. Empty result = nothing to
    // restore (fresh processor / no sample loaded yet).
    (function () {
      var fn = getNativeFn('getCachedSamplePayload');
      if (!fn) return;
      try {
        var p = fn();
        if (p && typeof p.then === 'function') {
          p.then(function (json) {
            if (!json || typeof json !== 'string' || json.length === 0) return;
            try {
              var payload = JSON.parse(json);
              if (window.onSampleLoaded) window.onSampleLoaded(payload);
            } catch (_) {}
          });
        }
      } catch (_) {}
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

  window.onSampleLoaded = function (info) {
    if (!info) return;
    state.peaksMin = info.peaksMin || null;
    state.peaksMax = info.peaksMax || null;
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

  // Initial render kick after DOM ready (handles mid-page-load injection too).
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', injectHeroOverlays);
  } else {
    injectHeroOverlays();
  }

  // Helper accessible to other scripts (status pill from drag-drop bridge).
  window.tiSetRootNote = function (m) {
    state.rootNote = Math.max(0, Math.min(127, m));
    updateRootDisplay();
  };
})();
</script>
)";
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

            if (webView != nullptr)
                webView->evaluateJavascript (
                    "if (window.onSampleLoaded) window.onSampleLoaded(" + json + ");",
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
