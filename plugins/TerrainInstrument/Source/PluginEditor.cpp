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
                // args[0] = sliceIndex, args[1] = semitones (-12..+12)
                if (args.size() < 2) { complete ({}); return; }
                const int   idx = (int) args[0];
                const float st  = juce::jlimit (-12.0f, 12.0f, (float) (double) args[1]);
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
                // args[0] = sliceIndex, args[1] = stretch ratio (0.25..4.0)
                if (args.size() < 2) { complete ({}); return; }
                const int   idx   = (int) args[0];
                const float ratio = juce::jlimit (0.1f, 15.0f, (float) (double) args[1]);
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
                auto cur = audioProcessor.loadSlices();
                if (! cur || idx < 0 || idx >= (int) cur->size()) { complete ({}); return; }
                tw::SliceList copy = *cur;
                copy[(size_t) idx].volume = vol;
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

  /* Action buttons inside the drawer — RANDOM OCTAVE etc. Ghost-glass
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
    text-shadow: 0 1px 2px rgba(0,0,0,0.55);
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
  /* REV badge — tiny tag in the top-left corner when slice plays backwards. */
  .ti-slice-rev-tag {
    position: absolute; top: 4px; left: 4px;
    padding: 1px 4px; border-radius: 2px;
    font: 700 8px/1 sans-serif; letter-spacing: 0.12em;
    color: #FFB066;
    background: rgba(20,18,32,0.7);
    pointer-events: none;
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
  /* Stretch ratio label — visible only when ratio != 1.0; sits above the
     pitch meter (which lives at the bottom-center). Low-contrast monospace. */
  .ti-slice-stretch-label {
    position: absolute; left: 4px; bottom: 22px;
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
        '<div class="ti-drawer-row" id="ti-action-row">' +
          '<div class="ti-action-btn" id="ti-random-oct" title="Random octave per chop — picks -12, 0, or +12 for each">RANDOM OCTAVE</div>' +
        '</div>' +
      '</div>';
    bottomPills.appendChild(slicesWrap);

    // Slice marker overlay container (positioned over the waveform).
    var sliceOverlays = document.createElement('div');
    sliceOverlays.id = 'ti-slice-overlays';
    hero.appendChild(sliceOverlays);

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
          volume:       isFinite(vl) ? Math.max(0,  Math.min(2,    vl)) : 1
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
      document.body.classList.remove('ti-slicer-active');
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
      // REV tag — separate from pitch meter, top-left corner.
      if (s.reverse && !hideOverlays) {
        var rev = document.createElement('div');
        rev.className = 'ti-slice-rev-tag';
        rev.textContent = 'REV';
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

    // Filled body (mirrored around centerline).
    ctx.fillStyle = 'rgba(245, 243, 255, 0.10)';
    ctx.beginPath();
    ctx.moveTo(0, cy);
    for (i = 0; i < pCount; i++) {
      var x   = (i / Math.max(1, pCount - 1)) * w;
      var yMax = cy - peaksMax[pStart + i] * cy * 0.95;
      ctx.lineTo(x, yMax);
    }
    for (i = pCount - 1; i >= 0; i--) {
      var x2  = (i / Math.max(1, pCount - 1)) * w;
      var yMin = cy - peaksMin[pStart + i] * cy * 0.95;
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
      var ym = cy - peaksMax[pStart + i] * cy * 0.95;
      if (i === 0) ctx.moveTo(xx, ym); else ctx.lineTo(xx, ym);
    }
    ctx.stroke();

    // Bottom edge stroke.
    ctx.strokeStyle = 'rgba(245, 243, 255, 0.85)';
    ctx.beginPath();
    for (i = 0; i < pCount; i++) {
      var xx2 = (i / Math.max(1, pCount - 1)) * w;
      var ym2 = cy - peaksMin[pStart + i] * cy * 0.95;
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

      function cursorToSample (x) {
        var s = clientXToSourceSample(x, waveRect.left, W);
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
  function ovValueFromState (idx, key) {
    var s = state.slices[idx];
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
    var s = state.slices[idx];
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

    // Mode pill clicks.
    panel.querySelectorAll('.ov-mode').forEach(function (el) {
      el.addEventListener('click', function () {
        if (el.classList.contains('soon')) return;
        var idx = parseInt(panel.dataset.targetIdx, 10);
        if (isNaN(idx) || !state.slices[idx]) return;
        var mode = parseInt(el.dataset.mode, 10) || 0;
        state.slices[idx].warpMode = mode;
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
          if (isNaN(idx) || !state.slices[idx]) return;
          var rect = svg.getBoundingClientRect();
          // convert pointer to SVG-viewBox space
          var vx = (ev.clientX - rect.left) / Math.max(1, rect.width)  * ENV_VB_W;
          var vy = (ev.clientY - rect.top ) / Math.max(1, rect.height) * ENV_VB_H;
          if (which === 'A') {
            var t = Math.max(0, Math.min(1, vx / ENV_ATTACK_W));
            var ms = denormSkew(t, OV_RANGES.attack.min, OV_RANGES.attack.max, OV_RANGES.attack.skew);
            state.slices[idx].attackMs = ms;
            var fn = getNativeFn('setSliceAttackMs'); if (fn) { try { fn(idx, ms); } catch (_) {} }
          } else if (which === 'D') {
            // D's x = A.x + decayPx → decayPx = vx - aPx
            var aT = normSkew(state.slices[idx].attackMs >= 0 ? state.slices[idx].attackMs : GLOBAL_ATTACK_DEFAULT,
                              OV_RANGES.attack.min, OV_RANGES.attack.max, OV_RANGES.attack.skew);
            var aPx = aT * ENV_ATTACK_W;
            var dPx = Math.max(0, Math.min(ENV_DECAY_W, vx - aPx));
            var dT = dPx / ENV_DECAY_W;
            var ms = denormSkew(dT, OV_RANGES.decay.min, OV_RANGES.decay.max, OV_RANGES.decay.skew);
            state.slices[idx].decayMs = ms;
            var fn = getNativeFn('setSliceDecayMs'); if (fn) { try { fn(idx, ms); } catch (_) {} }
          } else if (which === 'S') {
            // map vy [PEAK..BASE] → level [1..0]
            var lvl = 1.0 - (vy - ENV_PEAK_Y) / (ENV_BASE_Y - ENV_PEAK_Y);
            lvl = Math.max(0, Math.min(1, lvl));
            state.slices[idx].sustainLevel = lvl;
            var fn = getNativeFn('setSliceSustain'); if (fn) { try { fn(idx, lvl); } catch (_) {} }
          } else if (which === 'R') {
            // R's x = plateauEnd + releasePx → releasePx = vx - plateauEnd
            var aT2 = normSkew(state.slices[idx].attackMs >= 0 ? state.slices[idx].attackMs : GLOBAL_ATTACK_DEFAULT,
                               OV_RANGES.attack.min, OV_RANGES.attack.max, OV_RANGES.attack.skew);
            var dT2 = normSkew(state.slices[idx].decayMs || 0,
                               OV_RANGES.decay.min, OV_RANGES.decay.max, OV_RANGES.decay.skew);
            var plateauEnd = aT2 * ENV_ATTACK_W + dT2 * ENV_DECAY_W + ENV_PLATEAU_W;
            var rPx = Math.max(0, Math.min(ENV_RELEASE_W, vx - plateauEnd));
            var rT = rPx / ENV_RELEASE_W;
            var ms = denormSkew(rT, OV_RANGES.release.min, OV_RANGES.release.max, OV_RANGES.release.skew);
            state.slices[idx].releaseMs = ms;
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
          if (isNaN(idx) || !state.slices[idx]) return;
          if (which === 'A') {
            state.slices[idx].attackMs = -1;
            var fn = getNativeFn('setSliceAttackMs'); if (fn) { try { fn(idx, -1); } catch (_) {} }
          } else if (which === 'D') {
            state.slices[idx].decayMs = 0;
            var fn = getNativeFn('setSliceDecayMs');  if (fn) { try { fn(idx, 0); } catch (_) {} }
          } else if (which === 'S') {
            state.slices[idx].sustainLevel = 1.0;
            var fn = getNativeFn('setSliceSustain');  if (fn) { try { fn(idx, 1.0); } catch (_) {} }
          } else if (which === 'R') {
            state.slices[idx].releaseMs = -1;
            var fn = getNativeFn('setSliceReleaseMs');if (fn) { try { fn(idx, -1); } catch (_) {} }
          }
          requestAnimationFrame(function () {
            try { if (state.slices[idx]) ovRedrawEnvelope(idx); } catch (_) {}
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
          if (isNaN(idx) || !state.slices[idx]) return;
          var deltaY = startY - ev.clientY;     // up = positive
          var deltaT = deltaY / 200.0;           // 200 px = full range
          var t = Math.max(0, Math.min(1, startT + deltaT));
          var v = denormSkew(t, range.min, range.max, range.skew);
          if (key === 'pitch') v = Math.round(v);
          if (key === 'volume') {
            state.slices[idx].volume = v;
            var fn = getNativeFn('setSliceVolume'); if (fn) { try { fn(idx, v); } catch (_) {} }
          } else if (key === 'pitch') {
            state.slices[idx].pitch = v;
            var fn = getNativeFn('setSlicePitch'); if (fn) { try { fn(idx, v); } catch (_) {} }
            redrawSliceOverlay();
          } else if (key === 'stretch') {
            state.slices[idx].stretchRatio = v;
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
          if (isNaN(idx) || !state.slices[idx]) return;
          if (key === 'volume') {
            state.slices[idx].volume = 1.0;
            var fn = getNativeFn('setSliceVolume'); if (fn) { try { fn(idx, 1.0); } catch (_) {} }
          } else if (key === 'pitch') {
            state.slices[idx].pitch = 0;
            var fn = getNativeFn('setSlicePitch');  if (fn) { try { fn(idx, 0); } catch (_) {} }
          } else if (key === 'stretch') {
            state.slices[idx].stretchRatio = 1.0;
            var fn = getNativeFn('setSliceStretchRatio'); if (fn) { try { fn(idx, 1.0); } catch (_) {} }
          }
          requestAnimationFrame(function () {
            try {
              if (!state.slices[idx]) return;
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
          if (isNaN(idx) || !state.slices[idx]) return;
          var act = el.dataset.act;

          if (act === 'rev') {
            var nextRev = !state.slices[idx].reverse;
            state.slices[idx].reverse = nextRev;
            var fnR = getNativeFn('setSliceReverse');
            if (fnR) { try { fnR(idx, nextRev); } catch (_) {} }
            requestAnimationFrame(function () { try { redrawSliceOverlay(); } catch (_) {} });
          }
          else if (act === 'resetPitch') {
            state.slices[idx].pitch = 0;
            var fnP = getNativeFn('setSlicePitch');
            if (fnP) { try { fnP(idx, 0); } catch (_) {} }
            requestAnimationFrame(function () {
              try {
                if (!state.slices[idx]) return;
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
      if (!isNaN(idx) && state.slices[idx]) ovRedrawEnvelope(idx);
    });
  }

  function ovApplyState (idx) {
    var panel = document.getElementById('ti-chop-panel');
    if (!panel) return;
    var s = state.slices[idx];
    if (!s) return;
    panel.dataset.targetIdx = idx;
    var numEl = document.getElementById('ti-chop-num');
    if (numEl) numEl.textContent = (idx + 1 < 10 ? '0' : '') + (idx + 1);

    // Mode pill highlight + warp-only visibility.
    var mode = Number(s.warpMode || 0);
    var modeName = ['none','beats','tones','texture'][mode] || 'none';
    panel.setAttribute('data-warp', modeName);
    panel.querySelectorAll('.ov-mode').forEach(function (el) {
      el.classList.toggle('active', parseInt(el.dataset.mode, 10) === mode);
    });

    ovRedrawEnvelope(idx);
    ovRedrawEmblems(idx);
  }

  function openChopOverlay (idx) {
    if (!state.slices[idx]) return;
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

    // Sub-mode pills (CHOP / CHROMATIC / RANDOM) — selector unchanged
    // from v0b since #ti-submode-toggle moved into the drawer with the same id.
    document.querySelectorAll('#ti-submode-toggle .ti-submode-pill').forEach(function (p) {
      p.addEventListener('click', function (ev) {
        ev.stopPropagation();
        var sub = parseInt(p.dataset.sub, 10) || 0;
        setSubModeUI(sub);
        var fn = getNativeFn('setSliceSubMode');
        if (fn) { try { fn(sub); } catch (_) {} }
      });
    });

    // RANDOM OCTAVE — assign each chop a random pitch from {-12, 0, +12}.
    // Stacks beautifully with RANDOM sub-mode: random chop selection per
    // note + random per-chop octave = textural chaos. Sends the whole
    // updated list via setSlicesJson so it's a single round-trip rather
    // than N setSlicePitch calls.
    //
    // CRITICAL: spread the existing slice with Object.assign so every field
    // survives the round-trip (warpMode, stretchRatio, attackMs, releaseMs,
    // decayMs, sustainLevel, volume, …). Earlier the handler explicitly
    // copied only {start, end, reverse, pitch} and the C++ side rebuilt the
    // slice with defaults for everything else — a single Random Octave
    // click wiped Tones/Beats markers and reset stretchRatio to 1.0 on
    // every chop. Same family of bug as the existing "applySlicesJson must
    // preserve ALL Slice fields" gotcha but in the JS→C++ direction.
    var randomOctBtn = document.getElementById('ti-random-oct');
    if (randomOctBtn) {
      randomOctBtn.addEventListener('click', function (ev) {
        ev.stopPropagation();
        if (state.slices.length === 0) return;
        var CHOICES = [-12, 0, 12];
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
      });
    }

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
