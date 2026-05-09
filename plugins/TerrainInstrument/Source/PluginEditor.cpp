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
                    audioProcessor.xyPadX.store(static_cast<float>(args[0]), std::memory_order_relaxed);
                    audioProcessor.xyPadY.store(static_cast<float>(args[1]), std::memory_order_relaxed);
                }
                complete({});
            })
            .withNativeFunction("getSettings", [this](const juce::Array<juce::var>&,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                           .getChildFile("Waves Crate").getChildFile("Terrain").getChildFile("PluginSettings.json");
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
                    dir.getChildFile("PluginSettings.json").replaceWithText(args[0].toString());

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
                    .getChildFile("Waves Crate").getChildFile("Terrain").getChildFile("PluginSettings.json");
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
                              .getChildFile("Waves Crate").getChildFile("Terrain").getChildFile("PluginSettings.json");
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
                          .getChildFile("Waves Crate").getChildFile("Terrain").getChildFile("PluginSettings.json");
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

  /* ─── Mode toggle (PITCH ↔ SLICE), top-center ─── */
  #ti-mode-toggle {
    position: absolute; top: 12px; left: 50%;
    transform: translateX(-50%);
    z-index: 5;
    display: flex; gap: 4px;
    background: rgba(0, 0, 0, 0.42);
    border: 1px solid rgba(245, 243, 255, 0.28);
    padding: 4px; border-radius: 6px;
    backdrop-filter: blur(6px);
    -webkit-backdrop-filter: blur(6px);
  }
  .ti-mode-pill {
    padding: 4px 12px;
    font: 700 10px/1 -apple-system, BlinkMacSystemFont, sans-serif;
    letter-spacing: 0.18em;
    border-radius: 3px;
    color: rgba(245, 243, 255, 0.45);
    cursor: pointer;
    transition: all 150ms ease;
    user-select: none;
  }
  .ti-mode-pill.active {
    background: linear-gradient(135deg, #8B5CF6, #7C3AED);
    color: white;
    box-shadow: 0 0 10px rgba(139, 92, 246, 0.5);
  }
  .ti-mode-pill.disabled {
    opacity: 0.35; cursor: not-allowed;
  }

  /* ─── Root-note picker, lower-left — click-hold-vertical-drag ─── */
  #ti-root-picker {
    position: absolute; bottom: 12px; left: 12px;
    z-index: 5;
    display: inline-flex; gap: 7px; align-items: center;
    background: rgba(0, 0, 0, 0.42);
    border: 1px solid rgba(245, 243, 255, 0.28);
    padding: 5px 11px; border-radius: 6px;
    backdrop-filter: blur(6px);
    -webkit-backdrop-filter: blur(6px);
    cursor: ns-resize;
    user-select: none;
    transition: border-color 150ms ease, background-color 150ms ease;
  }
  #ti-root-picker:hover { border-color: rgba(167, 139, 250, 0.65); }
  #ti-root-picker.dragging {
    border-color: rgba(167, 139, 250, 0.95);
    background: rgba(139, 92, 246, 0.18);
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

  /* ─── XY readout — relocate to bottom-right (mirror of root picker) ─── */
  #hero .xy-readout {
    top: auto !important;
    bottom: 12px !important;
    left: auto !important;
    right: 12px !important;
    background: rgba(0, 0, 0, 0.42) !important;
    border: 1px solid rgba(245, 243, 255, 0.28) !important;
    padding: 5px 11px !important;
    border-radius: 6px !important;
    backdrop-filter: blur(6px) !important;
    -webkit-backdrop-filter: blur(6px) !important;
    font: 600 10px/1 -apple-system, BlinkMacSystemFont, sans-serif !important;
    letter-spacing: 0.12em !important;
    color: rgba(245, 243, 255, 0.55) !important;
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

    // Mode toggle (PITCH active, SLICE disabled in v0a)
    var modeWrap = document.createElement('div');
    modeWrap.id = 'ti-mode-toggle';
    modeWrap.innerHTML =
      '<div class="ti-mode-pill active" data-mode="PITCH">PITCH</div>' +
      '<div class="ti-mode-pill disabled" data-mode="SLICE" title="Slicer coming in v0b">SLICE</div>';
    hero.appendChild(modeWrap);

    // Root note picker
    var rootWrap = document.createElement('div');
    rootWrap.id = 'ti-root-picker';
    rootWrap.title = 'Root note — click to cycle, shift-click to go down';
    rootWrap.innerHTML =
      '<span class="ti-root-label">ROOT</span>' +
      '<span class="ti-root-value" id="ti-root-value">C4</span>';
    hero.appendChild(rootWrap);

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
    rootNote: 60
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

  // ── Interactions ──────────────────────────────────────────────────────────
  function wireInteractions () {
    // Mode toggle: SLICE disabled (toast on click)
    document.querySelectorAll('#ti-mode-toggle .ti-mode-pill').forEach(function (pill) {
      pill.addEventListener('click', function () {
        if (pill.dataset.mode === 'SLICE') {
          if (typeof showStatus === 'function') showStatus('Slicer coming in v0b', 3000);
        }
        // PITCH stays active in v0a; nothing to toggle.
      });
    });

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
    window.addEventListener('resize', drawWaveform);
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
    var hero = document.getElementById('hero');
    if (hero) {
      hero.classList.remove('empty-state');
      hero.classList.remove('has-sample-missing');
      hero.classList.add('has-sample');
    }
    drawWaveform();
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
    return juce::WebBrowserComponent::Resource{ std::move(data), juce::String("text/html") };
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
