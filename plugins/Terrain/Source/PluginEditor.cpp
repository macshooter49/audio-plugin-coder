#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
TerrainAudioProcessorEditor::TerrainAudioProcessorEditor (TerrainAudioProcessor& p)
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
            .withOptionsFrom(eqLowFreqRelay)
            .withOptionsFrom(eqLowGainRelay)
            .withOptionsFrom(eqMidFreqRelay)
            .withOptionsFrom(eqMidGainRelay)
            .withOptionsFrom(eqHighFreqRelay)
            .withOptionsFrom(eqHighGainRelay)
            .withNativeFunction("loadPreset", [this](const juce::Array<juce::var>& args,
                                                      juce::WebBrowserComponent::NativeFunctionCompletion complete)
            {
                if (args.size() > 0)
                    audioProcessor.loadPreset(static_cast<int>(args[0]));
                complete({});
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
                int newIdx = audioProcessor.saveNewPreset(name);
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
            .withResourceProvider([this](const auto& url) {
                return getResource(url);
            })
    );

    addAndMakeVisible(*webView);

    // Native capture drag strip below WebView (real mouse events for drag-to-DAW)
    addAndMakeVisible(captureDragStrip);

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

    eqLowFreqAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::EQ_LOW_FREQ), eqLowFreqRelay, nullptr);

    eqLowGainAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::EQ_LOW_GAIN), eqLowGainRelay, nullptr);

    eqMidFreqAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::EQ_MID_FREQ), eqMidFreqRelay, nullptr);

    eqMidGainAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::EQ_MID_GAIN), eqMidGainRelay, nullptr);

    eqHighFreqAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::EQ_HIGH_FREQ), eqHighFreqRelay, nullptr);

    eqHighGainAttachment = std::make_unique<juce::WebSliderParameterAttachment>(
        *audioProcessor.getAPVTS().getParameter(ParameterIDs::EQ_HIGH_GAIN), eqHighGainRelay, nullptr);

    // Load embedded web content
    webView->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    // Set size AFTER webView is created (setSize triggers resized())
    setSize (820, 640 + CAPTURE_STRIP_HEIGHT);

    // Start visualization timer at 30Hz
    startTimerHz(30);
}

TerrainAudioProcessorEditor::~TerrainAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void TerrainAudioProcessorEditor::timerCallback()
{
    if (webView == nullptr) return;

    // Read grain count
    int grainCount = audioProcessor.activeGrainCount.load();

    // Read scope buffer
    juce::String scopeData;
    scopeData.preallocateBytes(2048);
    scopeData << "[";
    for (int i = 0; i < TerrainAudioProcessor::SCOPE_SIZE; ++i)
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

    // ── Mod state lifecycle (phased: restore → settle → save) ──
    // Ticks 1-10:  RESTORE — push saved JSON to JS every tick (10 attempts)
    // Ticks 11-15: SETTLE  — wait for restore to take effect in DOM
    // Ticks 16+:   SAVE    — pull serialized state from JS every 5 ticks (~167ms)
    // CRITICAL: never save during restore/settle or we'd overwrite saved data with empty state
    modStateTickCount++;

    if (modStateTickCount <= 10 && audioProcessor.modStateJson.isNotEmpty())
    {
        auto escaped = audioProcessor.modStateJson.replace("\\", "\\\\").replace("'", "\\'");
        js << "if(typeof restoreModState==='function'){restoreModState('" << escaped << "');}";
    }

    webView->evaluateJavascript(js);

    if (modStateTickCount > 15 && (modStateTickCount % 5 == 0))
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
                        audioProcessor.modStateJson = json;
                        audioProcessor.modulationEngine.updateConfig(
                            ModulationEngine::parseJSON(json));
                    }
                }
            });
    }
}

//==============================================================================
void TerrainAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void TerrainAudioProcessorEditor::resized()
{
    auto b = getLocalBounds();
    captureDragStrip.setBounds(b.removeFromBottom(CAPTURE_STRIP_HEIGHT));
    if (webView != nullptr)
        webView->setBounds(b);
}

//==============================================================================
// CaptureDragStrip implementation
//==============================================================================
void TerrainAudioProcessorEditor::CaptureDragStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.fillAll(juce::Colour(0xFFF8F5FC)); // match WebView --bg-footer

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
            g.setColour(juce::Colour(0x44857399));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("CAPTURE: LISTENING...", b, juce::Justification::centred);
        }
        else
        {
            g.setColour(juce::Colour(0xFF6B5B7B));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("CAPTURE: " + juce::String(mins) + "m " + juce::String(secs) + "s  \u2014  DRAG TO DAW",
                       b, juce::Justification::centred);
        }
    }
}

void TerrainAudioProcessorEditor::CaptureDragStrip::mouseDown (const juce::MouseEvent&)
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

void TerrainAudioProcessorEditor::CaptureDragStrip::mouseDrag (const juce::MouseEvent& e)
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

void TerrainAudioProcessorEditor::CaptureDragStrip::mouseUp (const juce::MouseEvent&)
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
std::optional<juce::WebBrowserComponent::Resource> TerrainAudioProcessorEditor::getResource (const juce::String& url)
{
    // All JS is inlined into index.html — only one resource to serve
    std::vector<std::byte> data(static_cast<size_t>(BinaryData::index_htmlSize));
    std::memcpy(data.data(), BinaryData::index_html, static_cast<size_t>(BinaryData::index_htmlSize));
    return juce::WebBrowserComponent::Resource{ std::move(data), juce::String("text/html") };
}
