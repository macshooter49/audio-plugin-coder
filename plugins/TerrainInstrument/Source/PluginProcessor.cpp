#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParametricEQ.h"
#include <cmath>

//==============================================================================
TerrainInstrumentAudioProcessor::TerrainInstrumentAudioProcessor()
    : AudioProcessor (BusesProperties()
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    initializePresets();

    // === Sampler engine wiring — Task 5: singleton synth removed. Each layer's
    // synth is wired in LayerState's constructor using that layer's own atomics.
    // Label each layer with its 0-3 index for self-identification.
    for (size_t i = 0; i < layers.size(); ++i)
        layers[i].layerIndex = static_cast<int>(i);

    // Mix page Phase A: seed each layer's default VELOCITY zone to even quarters
    // (0-31 / 32-63 / 64-95 / 96-127). Users can drag the boundary handles in
    // the Mix page's VELOCITY-mode UI; this just gives a sensible starting point
    // so the first time a user selects VELOCITY mode each layer already responds
    // to its own slice of the 0-127 range.
    layers[0].velocityZoneMin.store (0);    layers[0].velocityZoneMax.store (31);
    layers[1].velocityZoneMin.store (32);   layers[1].velocityZoneMax.store (63);
    layers[2].velocityZoneMin.store (64);   layers[2].velocityZoneMax.store (95);
    layers[3].velocityZoneMin.store (96);   layers[3].velocityZoneMax.store (127);

    // KEYTRACK trigger mode: seed each layer's default key zone to even quarters
    // of the MIDI note range (0-31 / 32-63 / 64-95 / 96-127) — a sensible keyboard
    // split so KEYTRACK responds immediately. Users drag the handles in the UI.
    layers[0].keyZoneMin.store (0);    layers[0].keyZoneMax.store (31);
    layers[1].keyZoneMin.store (32);   layers[1].keyZoneMax.store (63);
    layers[2].keyZoneMin.store (64);   layers[2].keyZoneMax.store (95);
    layers[3].keyZoneMin.store (96);   layers[3].keyZoneMax.store (127);

    // Phase 1 task 9: wire modulationEngine into ALL layers' voices (was layer 0 only).
    // LayerState constructor passes nullptr for ModulationEngine; we patch it here.
    // Note: SamplerVoice stores a pointer to the engine so this is safe as long as
    // the engine outlives the voices (processor owns both, destruction is LIFO).
    for (auto& layer : layers)
    {
        for (int v = 0; v < layer.synth.getNumVoices(); ++v)
        {
            if (auto* sv = dynamic_cast<tw::SamplerVoice*> (layer.synth.getVoice (v)))
                sv->setModulationEngine (&modulationEngine);
        }
    }

    // Synth engine — Phase 1 MPV (one SynthSound + kSynthVoiceCount voices)
    synthEngine.addSound (new tw::SynthSound());
    for (int i = 0; i < kSynthVoiceCount; ++i)
        synthEngine.addVoice (new tw::SynthVoice());

    // Spectral-morph rebuild runs on the message thread (the rebuild is ~5.6ms,
    // far too heavy for the audio thread). 60Hz polling keeps the morph knob
    // responsive while never touching a buffer the audio thread is reading.
    startTimerHz (60);
}

TerrainInstrumentAudioProcessor::~TerrainInstrumentAudioProcessor()
{
    // Stop the morph rebuild timer BEFORE any members are destroyed — the
    // callback touches apvts / wavetableBank / morph slots.
    stopTimer();

    if (captureExportThread && captureExportThread->joinable())
        captureExportThread->join();
}

//==============================================================================
// Spectral Morph — message-thread rebuild + audio-thread resolve.
//==============================================================================
const tw::Wavetable*
TerrainInstrumentAudioProcessor::resolveMorphTable (MorphSlot& slot, int presetIdx) noexcept
{
    // Audio thread: atomic-load the published morphed table. If a morph is active,
    // report which buffer we're reading so the rebuild never overwrites it.
    const tw::Wavetable* m = slot.live.load (std::memory_order_acquire);
    if (m != nullptr)
    {
        slot.audioReadingIdx.store (m == &slot.buf[1] ? 1 : 0, std::memory_order_release);
        return m;
    }
    slot.audioReadingIdx.store (-1, std::memory_order_release);
    return wavetableBank.getTable (presetIdx);
}

void TerrainInstrumentAudioProcessor::rebuildMorphIfNeeded (MorphSlot& slot,
                                                            const juce::String& presetId,
                                                            const juce::String& modeId,
                                                            const juce::String& amtId)
{
    const int   preset = (int) *apvts.getRawParameterValue (presetId);
    const int   mode   = (int) *apvts.getRawParameterValue (modeId);
    const float amount =       *apvts.getRawParameterValue (amtId);

    // None (or zero amount) → publish nullptr so voices read the plain bank table.
    if (mode <= 0 || amount <= 0.0f)
    {
        slot.live.store (nullptr, std::memory_order_release);
        slot.builtPreset = preset;
        slot.builtMode   = mode;
        slot.builtAmount = amount;
        return;
    }

    // Nothing changed since the last build → skip.
    if (preset == slot.builtPreset && mode == slot.builtMode
        && std::abs (amount - slot.builtAmount) < 1.0e-4f)
        return;

    // Don't rebuild the buffer the audio thread is currently reading.
    const int target = slot.buildIdx;
    if (slot.audioReadingIdx.load (std::memory_order_acquire) == target)
        return;   // try again on the next tick (after the audio thread moves off it)

    slot.buf[target].buildFromSpec (
        tw::SpectralMorph::apply (tw::WavetableBank::specForPreset (preset),
                                  (tw::SpectralMode) mode, amount));

    slot.live.store (&slot.buf[target], std::memory_order_release);
    slot.buildIdx  ^= 1;
    slot.builtPreset = preset;
    slot.builtMode   = mode;
    slot.builtAmount = amount;
}

void TerrainInstrumentAudioProcessor::timerCallback()
{
    rebuildMorphIfNeeded (morphA_, ParameterIDs::SYN_OSC_A_WT_PRESET,
                          ParameterIDs::SYN_OSC_A_SPECTRAL_TYPE,
                          ParameterIDs::SYN_OSC_A_SPECTRAL_AMT);
    rebuildMorphIfNeeded (morphB_, ParameterIDs::SYN_OSC_B_WT_PRESET,
                          ParameterIDs::SYN_OSC_B_SPECTRAL_TYPE,
                          ParameterIDs::SYN_OSC_B_SPECTRAL_AMT);
}

//==============================================================================
void TerrainInstrumentAudioProcessor::initializePresets()
{
    //                                    grSz   dens  spray pitch  drift  frz    mix    wow    sat   hiss  outGn  mMix xyEn xyMd xySpd  gSync  grEn  tpEn
    presets = {
        { "Init",                          80.f,  20.f, 40.f,  0.f,   0.f,   0.f,  50.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Soft Keys",                    332.5f, 29.8f, 7.f,  0.f,  17.9f,  0.f,  82.f, 39.5f, 54.5f, 5.f,  7.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Movements",                     99.3f, 34.f, 43.6f, 0.f,  57.7f,  0.f,  50.f,  0.f,   0.f,  0.f,  7.f, 100.f, 1.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Super Messed Up",               55.4f,  7.1f, 31.9f, 0.f, 24.9f,  0.f,  74.f, 43.f,  12.f,  3.f,  3.5f, 100.f, 1.f, 1.f, 0.472f, 0.f, 1.f, 1.f },
        { "Tape Master",                   60.3f, 80.1f, 11.9f, 0.f,  0.f,   0.f,  39.f, 46.f,  70.f,  3.f,  0.f, 100.f, 0.f, 1.f, 0.472f, 0.f, 1.f, 1.f },
        { "Delay The Pluck",              500.f,   3.3f,  3.5f, 12.f,  0.f,   0.f,  40.f, 26.5f, 22.5f, 0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Synced Grains",                268.7f,  5.8f,  9.5f, 0.f,   0.f,   0.f,  67.5f, 17.5f, 31.5f, 1.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Clown Delay",                  114.3f, 16.8f,  0.f,  0.f,   0.f,   0.f,  80.5f, 17.5f,  7.f,  1.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Instant Pad",                  152.1f, 26.8f,  5.f,  0.f,   3.f,   0.f,  50.f, 41.f,  13.f,  0.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Slow Changes",                 346.5f, 43.8f, 83.1f, 0.f,  65.8f,  0.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 1.f, 0.f, 0.f,   1.f, 1.f, 1.f },
        { "Mood",                          156.f,  7.8f,  9.f,  0.f,  23.1f,  0.f,  50.f, 40.f,  32.5f, 1.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Soundscape",                   220.2f, 54.9f, 81.5f, 0.f,   0.f,   0.f, 100.f, 44.f,  20.5f, 0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Use A Key",                    103.3f, 28.3f,  1.f, -12.f,  0.f,   0.f,  50.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 1.f, 0.f,   1.f, 1.f, 1.f },
        { "Blank Forms",                   17.5f, 100.f, 25.f,  0.f,   0.f,   0.f, 100.f,  9.5f, 29.f,  5.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Habits",                       261.1f, 55.2f,  0.f,  0.f,  71.f,   0.f,  20.5f, 44.f,   9.5f, 8.f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Pitch Drifter",                146.3f, 24.2f,  2.5f,-12.f, 100.f,  0.f, 100.f, 28.f,  30.5f, 0.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Mandalorian",                  240.1f, 13.5f, 68.9f,-12.f,  35.6f, 0.f, 100.f, 28.f,  30.5f, 0.5f, 0.f, 100.f, 1.f, 1.f, 0.056f, 0.f, 1.f, 1.f },
        { "See The Light",                280.7f, 53.f,  32.6f, 12.f,  31.9f, 0.f, 100.f, 28.f,  30.5f, 0.5f, 0.f, 100.f, 0.f, 1.f, 0.056f, 0.f, 1.f, 1.f },
        { "Back To The Future",           325.8f,  1.f, 100.f,  0.f,  39.f,   0.f,  50.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Saturation",                   268.9f, 10.5f,  1.5f, 0.f,   0.f,   0.f,  50.f, 19.f,  83.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        { "Chop Shop",                     49.3f, 100.f, 40.f,  0.f,   0.f,   0.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Depressed",                    171.2f, 81.2f, 40.f, -12.f,  0.f,   0.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Deep Rest",                    168.3f, 81.2f, 40.f, -12.f, 25.f,   0.f, 100.f, 48.5f, 54.5f, 3.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Final Tape",                   183.3f, 71.2f,  3.f,  0.f,   0.f,   0.f,  40.f, 33.f,  53.f, 15.5f, 0.f, 100.f, 0.f, 0.f, 0.5f,  1.f, 1.f, 1.f },
        // ── Wander presets ──
        { "Gentle Wander",                150.f,  25.f, 20.f,  0.f,  15.f,   0.f,  55.f, 10.f,  15.f,  2.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Scattered",                     80.f,  65.f, 45.f,  0.f,  55.f,   0.f,  70.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Disintegration",               120.f,  12.f, 30.f, 12.f,  85.f,   0.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Breathing Texture",            350.f,  60.f, 15.f,  0.f,  25.f,   0.f,  60.f,  5.f,   8.f,  1.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Glitch Cloud",                  20.f,  15.f, 50.f,  0.f,  70.f,   0.f,  80.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        // ── Freeze presets ──
        { "Frozen Cathedral",             200.f,  60.f, 30.f,  0.f,  20.f, 100.f,  80.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Ghost Layer",                  120.f,  30.f, 20.f,  0.f,  10.f,  60.f,  65.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Living Pad",                   250.f,  50.f, 25.f, 12.f,  40.f, 100.f,  90.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Instant Eno",                  400.f,  10.f, 80.f,  0.f,  15.f,  85.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Sub Freeze",                   300.f,  40.f, 15.f,-12.f,   5.f, 100.f,  85.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Thaw",                         150.f,  35.f, 40.f,  0.f,  60.f,  40.f,  70.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
        { "Time Crystal",                 180.f,  45.f,100.f,  0.f,   0.f, 100.f, 100.f,  0.f,   0.f,  0.f,  0.f, 100.f, 0.f, 0.f, 0.5f,  0.f, 1.f, 1.f },
    };

    // ── Harmonic Sculptor presets (Studio v2.0) ──
    auto makeSculptorPreset = [](const char* name, float sculpt, float weave, float tilt) {
        PresetData p;
        p.name        = name;
        p.grainSize   = 80.f;   p.density   = 20.f;   p.spray  = 40.f;
        p.pitch       = 0.f;    p.drift     = 0.f;    p.freeze = 0.f;
        p.mix         = 50.f;
        p.wowFlutter  = 0.f;    p.saturation = 0.f;   p.hiss   = 0.f;  // unused on Studio (sculptor mode)
        p.outputGain  = 0.f;
        // masterMix, loopLength/Feedback/Degrade/Speed, xyAuto*, grainSync*, grainEngineEnabled, tapeEnabled,
        // tapeMachine (defaults to 0 = Studio), wanderLinked, grainFilter, space*, eq* — all take defaults
        p.studioSculpt = sculpt;
        p.studioWeave  = weave;
        p.studioTilt   = tilt;
        p.tag          = "TAPE";
        return p;
    };

    presets.push_back(makeSculptorPreset("Crystal Console",   15.f,  40.f,  20.f));
    presets.push_back(makeSculptorPreset("Warm Web",          35.f,  70.f, -30.f));
    presets.push_back(makeSculptorPreset("Flat Crunch",       55.f,  20.f,   0.f));
    presets.push_back(makeSculptorPreset("Shimmer Density",   40.f,  85.f,  40.f));
    presets.push_back(makeSculptorPreset("Harmonic Blanket",  70.f,  95.f, -10.f));
    presets.push_back(makeSculptorPreset("Sculptor Extreme", 100.f, 100.f,   0.f));

    numFactoryPresets = static_cast<int>(presets.size());  // All presets above are factory

    // Load any additional user presets from disk
    loadUserPresetsFromFile();
}

void TerrainInstrumentAudioProcessor::loadPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;

    currentPresetIndex.store(index);
    const auto& p = presets[static_cast<size_t>(index)];

    auto setParam = [this](const char* id, float value)
    {
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(param->convertTo0to1(value));
    };

    setParam(ParameterIDs::GRAIN_SIZE,   p.grainSize);
    setParam(ParameterIDs::DENSITY,      p.density);
    setParam(ParameterIDs::SPRAY,        p.spray);
    setParam(ParameterIDs::PITCH,        p.pitch);
    setParam(ParameterIDs::WANDER,       p.drift);
    setParam(ParameterIDs::FREEZE,       p.freeze);
    setParam(ParameterIDs::GRAIN_FILTER, p.grainFilter);
    setParam(ParameterIDs::MIX,          p.mix);
    setParam(ParameterIDs::WOW_FLUTTER,  p.wowFlutter);
    setParam(ParameterIDs::SATURATION,   p.saturation);
    setParam(ParameterIDs::HISS,         p.hiss);
    setParam(ParameterIDs::WIRE_WOW,        p.wireWow);
    setParam(ParameterIDs::WIRE_SATURATION, p.wireSaturation);
    setParam(ParameterIDs::WIRE_HISS,       p.wireHiss);
    setParam (ParameterIDs::STUDIO_SCULPT, p.studioSculpt);
    setParam (ParameterIDs::STUDIO_WEAVE,  p.studioWeave);
    setParam (ParameterIDs::STUDIO_TILT,   p.studioTilt);
    setParam(ParameterIDs::OUTPUT_GAIN,  p.outputGain);
    setParam(ParameterIDs::MASTER_MIX,   p.masterMix);
    setParam(ParameterIDs::LOOP_LENGTH,   p.loopLength);
    setParam(ParameterIDs::LOOP_FEEDBACK, p.loopFeedback);
    setParam(ParameterIDs::LOOP_DEGRADE,  p.loopDegrade);
    setParam(ParameterIDs::LOOP_SPEED,    p.loopSpeed);
    setParam(ParameterIDs::SPACE_SIZE,   p.spaceSize);
    setParam(ParameterIDs::SPACE_DECAY,  p.spaceDecay);
    setParam(ParameterIDs::SPACE_TONE,   p.spaceTone);
    setParam(ParameterIDs::SPACE_MIX,    p.spaceMix);

    // Parametric EQ — 35 APVTS params + 2 UI-only filter-mode flags.
    setParam (ParameterIDs::EQ_MASTER_BYPASS, p.eqMasterBypass);
    setParam (ParameterIDs::EQ_HP_FREQ,   p.eqHpFreq);
    setParam (ParameterIDs::EQ_HP_SLOPE,  p.eqHpSlope);
    setParam (ParameterIDs::EQ_HP_BYPASS, p.eqHpBypass);
    setParam (ParameterIDs::EQ_LP_FREQ,   p.eqLpFreq);
    setParam (ParameterIDs::EQ_LP_SLOPE,  p.eqLpSlope);
    setParam (ParameterIDs::EQ_LP_BYPASS, p.eqLpBypass);
    setParam (ParameterIDs::EQ_B1_FREQ, p.eqB1Freq); setParam (ParameterIDs::EQ_B1_GAIN, p.eqB1Gain); setParam (ParameterIDs::EQ_B1_Q, p.eqB1Q); setParam (ParameterIDs::EQ_B1_BYPASS, p.eqB1Bypass);
    setParam (ParameterIDs::EQ_B2_FREQ, p.eqB2Freq); setParam (ParameterIDs::EQ_B2_GAIN, p.eqB2Gain); setParam (ParameterIDs::EQ_B2_Q, p.eqB2Q); setParam (ParameterIDs::EQ_B2_BYPASS, p.eqB2Bypass);
    setParam (ParameterIDs::EQ_B3_FREQ, p.eqB3Freq); setParam (ParameterIDs::EQ_B3_GAIN, p.eqB3Gain); setParam (ParameterIDs::EQ_B3_Q, p.eqB3Q); setParam (ParameterIDs::EQ_B3_BYPASS, p.eqB3Bypass);
    setParam (ParameterIDs::EQ_B4_FREQ, p.eqB4Freq); setParam (ParameterIDs::EQ_B4_GAIN, p.eqB4Gain); setParam (ParameterIDs::EQ_B4_Q, p.eqB4Q); setParam (ParameterIDs::EQ_B4_BYPASS, p.eqB4Bypass);
    setParam (ParameterIDs::EQ_B5_FREQ, p.eqB5Freq); setParam (ParameterIDs::EQ_B5_GAIN, p.eqB5Gain); setParam (ParameterIDs::EQ_B5_Q, p.eqB5Q); setParam (ParameterIDs::EQ_B5_BYPASS, p.eqB5Bypass);
    setParam (ParameterIDs::EQ_B6_FREQ, p.eqB6Freq); setParam (ParameterIDs::EQ_B6_GAIN, p.eqB6Gain); setParam (ParameterIDs::EQ_B6_Q, p.eqB6Q); setParam (ParameterIDs::EQ_B6_BYPASS, p.eqB6Bypass);
    setParam (ParameterIDs::EQ_B7_FREQ, p.eqB7Freq); setParam (ParameterIDs::EQ_B7_GAIN, p.eqB7Gain); setParam (ParameterIDs::EQ_B7_Q, p.eqB7Q); setParam (ParameterIDs::EQ_B7_BYPASS, p.eqB7Bypass);
    setParam (ParameterIDs::EQ_B1_HP_MODE, p.eqB1HpMode);
    setParam (ParameterIDs::EQ_B7_LP_MODE, p.eqB7LpMode);

    setParam (ParameterIDs::DLY_TIME,      p.dlyTime);
    setParam (ParameterIDs::DLY_FEEDBACK,  p.dlyFeedback);
    setParam (ParameterIDs::DLY_TONE,      p.dlyTone);
    setParam (ParameterIDs::DLY_CHARACTER, p.dlyCharacter);
    setParam (ParameterIDs::DLY_MOD,       p.dlyMod);
    setParam (ParameterIDs::DLY_MOD_RATE,  p.dlyModRate);
    setParam (ParameterIDs::DLY_MIX,       p.dlyMix);
    setParam (ParameterIDs::DLY_DUCK,      p.dlyDuck);
    setParam (ParameterIDs::DLY_MOD_WAVE,  p.dlyModWave);
    setParam (ParameterIDs::DLY_SYNC,      p.dlySync);
    setParam (ParameterIDs::DLY_SYNC_DIV,  p.dlySyncDiv);
    setParam (ParameterIDs::DLY_PITCH,     p.dlyPitch);
    setParam (ParameterIDs::DLY_WIDTH,     p.dlyWidth);
    setParam (ParameterIDs::CHORUS_AMOUNT,    p.chorusAmount);
    setParam (ParameterIDs::CHORUS_WIDTH,     p.chorusWidth);
    setParam (ParameterIDs::CHORUS_CHARACTER, p.chorusCharacter);

    // Restore XY automation state for this preset
    xyAutoEnabled.store(p.xyAutoEnabled);
    xyAutoMode.store(p.xyAutoMode);
    xyAutoSpeed.store(p.xyAutoSpeed);

    // Restore grain sync state
    grainSyncEnabled.store(p.grainSyncEnabled);

    // Restore grain engine on/off, tape on/off, and drift link states
    grainEngineEnabled.store(p.grainEngineEnabled);
    tapeEnabled.store(p.tapeEnabled);
    tapeLoopEnabled.store(p.tapeLoopEnabled);
    setParam(ParameterIDs::TAPE_MACHINE, p.tapeMachine);
    wanderLinked.store(p.wanderLinked);
    tapeLinkEnabled.store(p.tapeLinkEnabled);

    // Restore modulation state (LFOs + assignments) for this preset
    modStateJson = p.modState;
    if (modStateJson.isNotEmpty())
        modulationEngine.updateConfig(ModulationEngine::parseJSON(modStateJson));
    else
        modulationEngine.updateConfig(ModulationEngine::Config{});  // Clear all LFOs
}

int TerrainInstrumentAudioProcessor::getPresetCount() const
{
    return static_cast<int>(presets.size());
}

juce::String TerrainInstrumentAudioProcessor::getPresetName(int index) const
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return {};
    return presets[static_cast<size_t>(index)].name;
}

//==============================================================================
PresetData TerrainInstrumentAudioProcessor::captureCurrentParams() const
{
    PresetData p;
    p.grainSize  = apvts.getRawParameterValue(ParameterIDs::GRAIN_SIZE)->load();
    p.density    = apvts.getRawParameterValue(ParameterIDs::DENSITY)->load();
    p.spray      = apvts.getRawParameterValue(ParameterIDs::SPRAY)->load();
    p.pitch      = apvts.getRawParameterValue(ParameterIDs::PITCH)->load();
    p.drift      = apvts.getRawParameterValue(ParameterIDs::WANDER)->load();
    p.freeze     = apvts.getRawParameterValue(ParameterIDs::FREEZE)->load();
    p.grainFilter = apvts.getRawParameterValue(ParameterIDs::GRAIN_FILTER)->load();
    p.mix        = apvts.getRawParameterValue(ParameterIDs::MIX)->load();
    p.wowFlutter = apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load();
    p.saturation = apvts.getRawParameterValue(ParameterIDs::SATURATION)->load();
    p.hiss       = apvts.getRawParameterValue(ParameterIDs::HISS)->load();
    p.wireWow        = apvts.getRawParameterValue (ParameterIDs::WIRE_WOW)       ->load();
    p.wireSaturation = apvts.getRawParameterValue (ParameterIDs::WIRE_SATURATION)->load();
    p.wireHiss       = apvts.getRawParameterValue (ParameterIDs::WIRE_HISS)      ->load();
    p.studioSculpt = apvts.getRawParameterValue (ParameterIDs::STUDIO_SCULPT)->load();
    p.studioWeave  = apvts.getRawParameterValue (ParameterIDs::STUDIO_WEAVE) ->load();
    p.studioTilt   = apvts.getRawParameterValue (ParameterIDs::STUDIO_TILT)  ->load();
    p.outputGain = apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load();
    p.masterMix  = apvts.getRawParameterValue(ParameterIDs::MASTER_MIX)->load();
    p.loopLength   = apvts.getRawParameterValue(ParameterIDs::LOOP_LENGTH)->load();
    p.loopFeedback = apvts.getRawParameterValue(ParameterIDs::LOOP_FEEDBACK)->load();
    p.loopDegrade  = apvts.getRawParameterValue(ParameterIDs::LOOP_DEGRADE)->load();
    p.loopSpeed    = apvts.getRawParameterValue(ParameterIDs::LOOP_SPEED)->load();
    p.spaceSize    = apvts.getRawParameterValue(ParameterIDs::SPACE_SIZE)->load();
    p.spaceDecay   = apvts.getRawParameterValue(ParameterIDs::SPACE_DECAY)->load();
    p.spaceTone    = apvts.getRawParameterValue(ParameterIDs::SPACE_TONE)->load();
    p.spaceMix     = apvts.getRawParameterValue(ParameterIDs::SPACE_MIX)->load();
    // Parametric EQ — 35 APVTS params + 2 UI-only filter-mode flags.
    p.eqMasterBypass = apvts.getRawParameterValue (ParameterIDs::EQ_MASTER_BYPASS)->load();
    p.eqHpFreq    = apvts.getRawParameterValue (ParameterIDs::EQ_HP_FREQ)  ->load();
    p.eqHpSlope   = apvts.getRawParameterValue (ParameterIDs::EQ_HP_SLOPE) ->load();
    p.eqHpBypass  = apvts.getRawParameterValue (ParameterIDs::EQ_HP_BYPASS)->load();
    p.eqLpFreq    = apvts.getRawParameterValue (ParameterIDs::EQ_LP_FREQ)  ->load();
    p.eqLpSlope   = apvts.getRawParameterValue (ParameterIDs::EQ_LP_SLOPE) ->load();
    p.eqLpBypass  = apvts.getRawParameterValue (ParameterIDs::EQ_LP_BYPASS)->load();
    p.eqB1Freq = apvts.getRawParameterValue (ParameterIDs::EQ_B1_FREQ)->load(); p.eqB1Gain = apvts.getRawParameterValue (ParameterIDs::EQ_B1_GAIN)->load(); p.eqB1Q = apvts.getRawParameterValue (ParameterIDs::EQ_B1_Q)->load(); p.eqB1Bypass = apvts.getRawParameterValue (ParameterIDs::EQ_B1_BYPASS)->load();
    p.eqB2Freq = apvts.getRawParameterValue (ParameterIDs::EQ_B2_FREQ)->load(); p.eqB2Gain = apvts.getRawParameterValue (ParameterIDs::EQ_B2_GAIN)->load(); p.eqB2Q = apvts.getRawParameterValue (ParameterIDs::EQ_B2_Q)->load(); p.eqB2Bypass = apvts.getRawParameterValue (ParameterIDs::EQ_B2_BYPASS)->load();
    p.eqB3Freq = apvts.getRawParameterValue (ParameterIDs::EQ_B3_FREQ)->load(); p.eqB3Gain = apvts.getRawParameterValue (ParameterIDs::EQ_B3_GAIN)->load(); p.eqB3Q = apvts.getRawParameterValue (ParameterIDs::EQ_B3_Q)->load(); p.eqB3Bypass = apvts.getRawParameterValue (ParameterIDs::EQ_B3_BYPASS)->load();
    p.eqB4Freq = apvts.getRawParameterValue (ParameterIDs::EQ_B4_FREQ)->load(); p.eqB4Gain = apvts.getRawParameterValue (ParameterIDs::EQ_B4_GAIN)->load(); p.eqB4Q = apvts.getRawParameterValue (ParameterIDs::EQ_B4_Q)->load(); p.eqB4Bypass = apvts.getRawParameterValue (ParameterIDs::EQ_B4_BYPASS)->load();
    p.eqB5Freq = apvts.getRawParameterValue (ParameterIDs::EQ_B5_FREQ)->load(); p.eqB5Gain = apvts.getRawParameterValue (ParameterIDs::EQ_B5_GAIN)->load(); p.eqB5Q = apvts.getRawParameterValue (ParameterIDs::EQ_B5_Q)->load(); p.eqB5Bypass = apvts.getRawParameterValue (ParameterIDs::EQ_B5_BYPASS)->load();
    p.eqB6Freq = apvts.getRawParameterValue (ParameterIDs::EQ_B6_FREQ)->load(); p.eqB6Gain = apvts.getRawParameterValue (ParameterIDs::EQ_B6_GAIN)->load(); p.eqB6Q = apvts.getRawParameterValue (ParameterIDs::EQ_B6_Q)->load(); p.eqB6Bypass = apvts.getRawParameterValue (ParameterIDs::EQ_B6_BYPASS)->load();
    p.eqB7Freq = apvts.getRawParameterValue (ParameterIDs::EQ_B7_FREQ)->load(); p.eqB7Gain = apvts.getRawParameterValue (ParameterIDs::EQ_B7_GAIN)->load(); p.eqB7Q = apvts.getRawParameterValue (ParameterIDs::EQ_B7_Q)->load(); p.eqB7Bypass = apvts.getRawParameterValue (ParameterIDs::EQ_B7_BYPASS)->load();
    p.eqB1HpMode = apvts.getRawParameterValue (ParameterIDs::EQ_B1_HP_MODE)->load();
    p.eqB7LpMode = apvts.getRawParameterValue (ParameterIDs::EQ_B7_LP_MODE)->load();
    p.xyAutoEnabled    = xyAutoEnabled.load();
    p.xyAutoMode       = xyAutoMode.load();
    p.xyAutoSpeed      = xyAutoSpeed.load();
    p.grainSyncEnabled = grainSyncEnabled.load();
    p.grainEngineEnabled = grainEngineEnabled.load();
    p.tapeEnabled        = tapeEnabled.load();
    p.tapeLoopEnabled    = tapeLoopEnabled.load();
    p.tapeMachine        = apvts.getRawParameterValue(ParameterIDs::TAPE_MACHINE)->load();
    p.wanderLinked        = wanderLinked.load();
    p.tapeLinkEnabled    = tapeLinkEnabled.load();
    p.modState            = modStateJson;  // Capture current LFO/mod config
    p.dlyTime      = apvts.getRawParameterValue (ParameterIDs::DLY_TIME)->load();
    p.dlyFeedback  = apvts.getRawParameterValue (ParameterIDs::DLY_FEEDBACK)->load();
    p.dlyTone      = apvts.getRawParameterValue (ParameterIDs::DLY_TONE)->load();
    p.dlyCharacter = apvts.getRawParameterValue (ParameterIDs::DLY_CHARACTER)->load();
    p.dlyMod       = apvts.getRawParameterValue (ParameterIDs::DLY_MOD)->load();
    p.dlyModRate   = apvts.getRawParameterValue (ParameterIDs::DLY_MOD_RATE)->load();
    p.dlyMix       = apvts.getRawParameterValue (ParameterIDs::DLY_MIX)->load();
    p.dlyDuck      = apvts.getRawParameterValue (ParameterIDs::DLY_DUCK)->load();
    p.dlyModWave   = apvts.getRawParameterValue (ParameterIDs::DLY_MOD_WAVE)->load();
    p.dlySync      = apvts.getRawParameterValue (ParameterIDs::DLY_SYNC)->load();
    p.dlySyncDiv   = apvts.getRawParameterValue (ParameterIDs::DLY_SYNC_DIV)->load();
    p.dlyPitch     = apvts.getRawParameterValue (ParameterIDs::DLY_PITCH)->load();
    p.dlyWidth     = apvts.getRawParameterValue (ParameterIDs::DLY_WIDTH)->load();
    p.chorusAmount    = apvts.getRawParameterValue (ParameterIDs::CHORUS_AMOUNT)   ->load();
    p.chorusWidth     = apvts.getRawParameterValue (ParameterIDs::CHORUS_WIDTH)    ->load();
    p.chorusCharacter = apvts.getRawParameterValue (ParameterIDs::CHORUS_CHARACTER)->load();
    return p;
}

int TerrainInstrumentAudioProcessor::saveNewPreset(const juce::String& name, const juce::String& tag)
{
    PresetData p = captureCurrentParams();
    p.name = name;
    p.tag = tag;
    presets.push_back(p);
    int newIdx = static_cast<int>(presets.size()) - 1;
    currentPresetIndex.store(newIdx);
    saveUserPresetsToFile();
    return newIdx;
}

void TerrainInstrumentAudioProcessor::renamePreset(int index, const juce::String& newName)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    presets[static_cast<size_t>(index)].name = newName;
    saveUserPresetsToFile();
}

void TerrainInstrumentAudioProcessor::overwritePreset(int index)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    juce::String name = presets[static_cast<size_t>(index)].name;
    juce::String tag  = presets[static_cast<size_t>(index)].tag;
    presets[static_cast<size_t>(index)] = captureCurrentParams();
    presets[static_cast<size_t>(index)].name = name;
    presets[static_cast<size_t>(index)].tag  = tag;
    saveUserPresetsToFile();
}

void TerrainInstrumentAudioProcessor::deletePreset(int index)
{
    if (index < numFactoryPresets || index >= static_cast<int>(presets.size()))
        return;
    presets.erase(presets.begin() + index);
    if (currentPresetIndex.load() >= static_cast<int>(presets.size()))
        currentPresetIndex.store(static_cast<int>(presets.size()) - 1);
    saveUserPresetsToFile();
}

juce::String TerrainInstrumentAudioProcessor::getPresetTag(int index) const
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return {};
    return presets[static_cast<size_t>(index)].tag;
}

void TerrainInstrumentAudioProcessor::setPresetTag(int index, const juce::String& tag)
{
    if (index < numFactoryPresets || index >= static_cast<int>(presets.size()))
        return;
    presets[static_cast<size_t>(index)].tag = tag;
    saveUserPresetsToFile();
}

juce::String TerrainInstrumentAudioProcessor::getCustomTags() const
{
    return customTags.joinIntoString(",");
}

void TerrainInstrumentAudioProcessor::setCustomTags(const juce::String& commaSeparated)
{
    customTags.clear();
    customTags.addTokens(commaSeparated, ",", "");
    customTags.removeEmptyStrings();
    saveUserPresetsToFile();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TerrainInstrumentAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::GRAIN_SIZE, 1 },
        "Grain Size",
        juce::NormalisableRange<float>(5.0f, 500.0f, 0.1f, 0.5f),
        80.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DENSITY, 1 },
        "Density",
        juce::NormalisableRange<float>(1.0f, 100.0f, 0.1f, 0.5f),
        20.0f,
        juce::AudioParameterFloatAttributes().withLabel("grains/s")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPRAY, 1 },
        "Spray",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        40.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::PITCH, 1 },
        "Pitch",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("st")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WANDER, 1 },
        "Wander",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::FREEZE, 1 },
        "Freeze",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::MIX, 1 },
        "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Grain filter: 0 = HP, 50 = bypass, 100 = LP
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::GRAIN_FILTER, 1 },
        "Grain Filter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("")));

    // Tape parameters
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WOW_FLUTTER, 1 },
        "Wow/Flutter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SATURATION, 1 },
        "Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::HISS, 1 },
        "Hiss",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Wire-specific wow/sat/hiss — independent of Cassette so each machine
    // can be tuned separately when LINK is on.
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WIRE_WOW, 1 },
        "Wire Wow/Flutter",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WIRE_SATURATION, 1 },
        "Wire Saturation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::WIRE_HISS, 1 },
        "Wire Hiss",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Tape machine selection
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::TAPE_MACHINE, 1 },
        "Tape Machine",
        juce::StringArray { "Studio", "Cassette", "Wire" },
        0));

    // Harmonic Sculptor (Studio v2.0)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::STUDIO_SCULPT, 1 },
        "Studio Sculpt",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::STUDIO_WEAVE, 1 },
        "Studio Weave",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::STUDIO_TILT, 1 },
        "Studio Tilt",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Output gain: -12 dB to +12 dB, default 0 dB
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::OUTPUT_GAIN, 1 },
        "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Master dry/wet mix: 0-100%, default 100% (fully wet)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::MASTER_MIX, 1 },
        "Master Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Tape loop params
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::LOOP_LENGTH, 1 },
        "Loop Length",
        juce::NormalisableRange<float>(0.0f, 6.0f, 1.0f),
        3.0f,
        juce::AudioParameterFloatAttributes().withLabel("")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::LOOP_FEEDBACK, 1 },
        "Loop Feedback",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        85.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::LOOP_DEGRADE, 1 },
        "Loop Degrade",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        30.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::LOOP_SPEED, 1 },
        "Loop Speed",
        juce::NormalisableRange<float>(0.0f, 9.0f, 0.01f),
        6.0f,
        juce::AudioParameterFloatAttributes().withLabel("")));

    // Space reverb params (0-100%)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPACE_SIZE, 1 },
        "Space Size",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPACE_DECAY, 1 },
        "Space Decay",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPACE_TONE, 1 },
        "Space Tone",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::SPACE_MIX, 1 },
        "Space Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    // Parametric EQ — 7 bands + HP/LP edge cuts
    {
        using NRange = juce::NormalisableRange<float>;
        // Logarithmic frequency range, like a Pro-Q.
        auto freqRange = []() { NRange r (20.0f, 20000.0f, 0.1f); r.setSkewForCentre (1000.0f); return r; }();
        auto qRange    = []() { NRange r (0.1f, 30.0f, 0.001f); r.setSkewForCentre (2.0f); return r; }();
        // Symmetrically log-spaced defaults across [20Hz, 20kHz] — bands at 1/8..7/8 of log range.
        constexpr float defaultFreqs[ParametricEQ::NUM_BANDS] = { 50.f, 110.f, 270.f, 630.f, 1500.f, 3500.f, 8500.f };

        layout.add (std::make_unique<juce::AudioParameterBool>  (ParameterIDs::EQ_MASTER_BYPASS, "EQ Bypass", false));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterIDs::EQ_HP_FREQ,  "EQ HP Freq",  freqRange, 35.f));
        layout.add (std::make_unique<juce::AudioParameterChoice>(ParameterIDs::EQ_HP_SLOPE, "EQ HP Slope", juce::StringArray { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1));
        layout.add (std::make_unique<juce::AudioParameterBool>  (ParameterIDs::EQ_HP_BYPASS, "EQ HP Bypass", true));
        layout.add (std::make_unique<juce::AudioParameterFloat> (ParameterIDs::EQ_LP_FREQ,  "EQ LP Freq",  freqRange, 16000.f));
        layout.add (std::make_unique<juce::AudioParameterChoice>(ParameterIDs::EQ_LP_SLOPE, "EQ LP Slope", juce::StringArray { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 0));
        layout.add (std::make_unique<juce::AudioParameterBool>  (ParameterIDs::EQ_LP_BYPASS, "EQ LP Bypass", true));

        const char* freqIds [7] = { ParameterIDs::EQ_B1_FREQ,   ParameterIDs::EQ_B2_FREQ,   ParameterIDs::EQ_B3_FREQ,   ParameterIDs::EQ_B4_FREQ,   ParameterIDs::EQ_B5_FREQ,   ParameterIDs::EQ_B6_FREQ,   ParameterIDs::EQ_B7_FREQ };
        const char* gainIds [7] = { ParameterIDs::EQ_B1_GAIN,   ParameterIDs::EQ_B2_GAIN,   ParameterIDs::EQ_B3_GAIN,   ParameterIDs::EQ_B4_GAIN,   ParameterIDs::EQ_B5_GAIN,   ParameterIDs::EQ_B6_GAIN,   ParameterIDs::EQ_B7_GAIN };
        const char* qIds    [7] = { ParameterIDs::EQ_B1_Q,      ParameterIDs::EQ_B2_Q,      ParameterIDs::EQ_B3_Q,      ParameterIDs::EQ_B4_Q,      ParameterIDs::EQ_B5_Q,      ParameterIDs::EQ_B6_Q,      ParameterIDs::EQ_B7_Q };
        const char* bypIds  [7] = { ParameterIDs::EQ_B1_BYPASS, ParameterIDs::EQ_B2_BYPASS, ParameterIDs::EQ_B3_BYPASS, ParameterIDs::EQ_B4_BYPASS, ParameterIDs::EQ_B5_BYPASS, ParameterIDs::EQ_B6_BYPASS, ParameterIDs::EQ_B7_BYPASS };
        for (int b = 0; b < 7; ++b)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (freqIds[b], juce::String ("EQ B") + juce::String (b + 1) + " Freq", freqRange, defaultFreqs[b]));
            layout.add (std::make_unique<juce::AudioParameterFloat> (gainIds[b], juce::String ("EQ B") + juce::String (b + 1) + " Gain", juce::NormalisableRange<float> (-24.f, 24.f, 0.01f), 0.0f));
            layout.add (std::make_unique<juce::AudioParameterFloat> (qIds[b],    juce::String ("EQ B") + juce::String (b + 1) + " Q",    qRange, 0.707f));
            layout.add (std::make_unique<juce::AudioParameterBool>  (bypIds[b],  juce::String ("EQ B") + juce::String (b + 1) + " Bypass", false));
        }

        // Filter-mode flags for the corner bands (UI-only — restores _asHighPass /
        // _asLowPass visual state. Audio behaviour is fully captured by the bell +
        // HP/LP bypass/freq/slope params above.)
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::EQ_B1_HP_MODE, "EQ B1 HP Mode", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (ParameterIDs::EQ_B7_LP_MODE, "EQ B7 LP Mode", false));
    }

    // MF-104S delay (v6)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_TIME, 1 },
        "Delay Time",
        juce::NormalisableRange<float>(5.0f, 1500.0f, 0.1f, 0.4f),
        350.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_FEEDBACK, 1 },
        "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 1.10f, 0.001f),
        0.45f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_TONE, 1 },
        "Delay Tone",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_CHARACTER, 1 },
        "Delay Character",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,                                   // Instrument default 0 (no Moog drift on first load)
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_MOD, 1 },
        "Delay Mod Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,                                   // Instrument default 0
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_MOD_RATE, 1 },
        "Delay Mod Rate",
        juce::NormalisableRange<float>(0.05f, 8.0f, 0.001f, 0.3f),
        0.05f,                                  // Instrument default at the bottom of the range
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_MIX, 1 },
        "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.30f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_DUCK, 1 },
        "Delay Duck",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::DLY_FREEZE, 1 },
        "Delay Freeze",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.0f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_MOD_WAVE, 1 },
        "Delay Mod Wave",
        juce::StringArray { "Sine", "Tri", "Square", "Ramp", "Saw", "S&H", "Chaos" },
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_SYNC, 1 },
        "Delay Sync",
        juce::StringArray { "Free", "Sync" },
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_SYNC_DIV, 1 },
        "Delay Sync Division",
        juce::StringArray { "1/32", "1/16T", "1/16", "1/8T", "1/8.", "1/8", "1/4T", "1/4", "1/2" },
        5));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_PITCH, 1 },
        "Delay Pitch",
        juce::StringArray { "OFF", "-12", "-7", "+7", "+12" },
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::DLY_WIDTH, 1 },
        "Delay Width",
        juce::StringArray { "Mono", "Stereo", "Ping" },
        1));

    // Chorus (v6)
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::CHORUS_AMOUNT, 1 },
        "June Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.0f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::CHORUS_WIDTH, 1 },
        "June Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.7f,
        juce::AudioParameterFloatAttributes()));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::CHORUS_CHARACTER, 1 },
        "June Character",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        0.3f,
        juce::AudioParameterFloatAttributes()));

    // Sampler params (Terrain Instrument v0a)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::ATTACK_MS, 1 },
        "Attack",
        juce::NormalisableRange<float>(0.0f, 2000.0f, 0.0f, 0.4f),
        5.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::RELEASE_MS, 1 },
        "Release",
        juce::NormalisableRange<float>(1.0f, 5000.0f, 0.0f, 0.4f),
        800.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { ParameterIDs::ROOT_NOTE, 1 },
        "Root Note", 0, 127, 60));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SLICE_MODE, 1 },
        "Mode",
        juce::StringArray { "PITCH", "SLICE" }, 0));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SLICE_SUB_MODE, 1 },
        "Slice Sub-Mode",
        juce::StringArray { "CHOP", "CHROMATIC", "RANDOM", "LAYER" }, 0));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SAMPLE_LOOP_MODE, 1 },
        "Sample Loop",
        juce::StringArray { "ONE-SHOT", "LOOP" }, 0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::CHOP_FADE_MS, 1 },
        "Chop Fade",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.1f),
        5.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // ── Synth section (Phase 1 MPV — see Design/v1-syn-spec.md) ──────────
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_ENGINE, 1 },
        "Synth OSC A Engine",
        juce::StringArray { "WT", "SAMP", "GRAN", "SPEC", "FM", "NOISE" },
        0));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_OCT, 1 },
        "Synth OSC A Octave", -3, 3, 0));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SEMI, 1 },
        "Synth OSC A Semitone", -12, 12, 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_CENT, 1 },
        "Synth OSC A Cents",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_LEVEL, 1 },
        "Synth OSC A Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.7f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_PAN, 1 },
        "Synth OSC A Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_CUT, 1 },
        "Synth Filter 1 Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f),
        20000.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_RES, 1 },
        "Synth Filter 1 Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    // ── Batch 1 Filter — TYPE (27 choices, NONE last in enum but first in UI),
    //                     DRV (0..1 → 0..+24 dB drive), ENV (bipolar -1..+1).
    {
        juce::StringArray filterTypeChoices;
        filterTypeChoices.add ("LADDER LP 24");
        filterTypeChoices.add ("LADDER LP 12");
        filterTypeChoices.add ("LADDER HP 24");
        filterTypeChoices.add ("DIODE LP");
        filterTypeChoices.add ("ACID 303");
        filterTypeChoices.add ("SVF LP");
        filterTypeChoices.add ("SVF HP");
        filterTypeChoices.add ("SVF BP");
        filterTypeChoices.add ("SVF NOTCH");
        filterTypeChoices.add ("OB-X SVF");
        filterTypeChoices.add ("COMB +");
        filterTypeChoices.add ("COMB -");
        filterTypeChoices.add ("COMB SHIMMER");
        filterTypeChoices.add ("KARPLUS-STRONG");
        filterTypeChoices.add ("FORMANT A");
        filterTypeChoices.add ("FORMANT E");
        filterTypeChoices.add ("FORMANT I");
        filterTypeChoices.add ("FORMANT MORPH");
        filterTypeChoices.add ("REVERB FILTER");
        filterTypeChoices.add ("PHASER 4P");
        filterTypeChoices.add ("PHASER 8P");
        filterTypeChoices.add ("RING MOD");
        filterTypeChoices.add ("BODE SHIFTER");
        filterTypeChoices.add ("BIT-CRUSH");
        filterTypeChoices.add ("WAVESHAPER");
        filterTypeChoices.add ("GRAIN MASK");
        filterTypeChoices.add ("REVERB FILTER 2");
        filterTypeChoices.add ("NONE");
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_FILTER1_TYPE, 1 },
            "Synth Filter 1 Type", filterTypeChoices, 0));   // default = LADDER LP 24 (was the hardwired one)
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIDs::SYN_FILTER2_TYPE, 1 },
            "Synth Filter 2 Type", filterTypeChoices, 27));  // default = NONE (slot 2 inert this batch)
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_DRV, 1 },
        "Synth Filter 1 Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_ENV, 1 },
        "Synth Filter 1 Env Amount",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_FILTER_SLOT, 1 },
        "Synth Filter Edit Slot", 0, 1, 0));

    // Slot 2 reserved (inert this batch — no DSP wiring, just preset persistence)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_CUT, 1 },
        "Synth Filter 2 Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f), 20000.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_RES, 1 },
        "Synth Filter 2 Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_DRV, 1 },
        "Synth Filter 2 Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_ENV, 1 },
        "Synth Filter 2 Env Amount",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

    // Per-filter wet/dry mix (default fully filtered) + series/parallel routing.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER1_MIX, 1 },
        "Synth Filter 1 Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_FILTER2_MIX, 1 },
        "Synth Filter 2 Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_FILTER_ROUTING, 1 },
        "Synth Filter Routing", juce::StringArray { "SERIES", "PARALLEL" }, 0));

    // Filter ADSR (independent from AMP env — drives the cutoff via the bipolar ENV knob).
    // Defaults: classic "filter sweep down" shape (instant attack, mid decay, no sustain, short release).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_A, 1 },
        "Synth Filter Env Attack",
        juce::NormalisableRange<float> (1.0f, 5000.0f, 0.0f, 0.3f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_D, 1 },
        "Synth Filter Env Decay",
        juce::NormalisableRange<float> (1.0f, 5000.0f, 0.0f, 0.3f), 200.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_S, 1 },
        "Synth Filter Env Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_FLT_R, 1 },
        "Synth Filter Env Release",
        juce::NormalisableRange<float> (1.0f, 5000.0f, 0.0f, 0.3f), 300.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_A, 1 },
        "Synth Amp Attack",
        juce::NormalisableRange<float> (1.0f, 5000.0f, 0.0f, 0.3f),
        5.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_D, 1 },
        "Synth Amp Decay",
        juce::NormalisableRange<float> (1.0f, 5000.0f, 0.0f, 0.3f),
        100.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_S, 1 },
        "Synth Amp Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.7f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_ENV_AMP_R, 1 },
        "Synth Amp Release",
        juce::NormalisableRange<float> (1.0f, 10000.0f, 0.0f, 0.3f),
        200.0f));

    // ── Synth section — Phase 2A (Wavetable foundation) ──────────────────
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WT_PRESET, 1 },
        "Synth OSC A WT Preset",
        juce::StringArray { "Sine", "Triangle", "Square", "Pulse",
                            "Prophet Saw", "Jupiter PWM", "Moog Sqr",
                            "OB-X Saw", "CS-80 Brass", "Juno Str",
                            "PPG Wave", "DX7 EP", "D-50 Bell", "M1 Piano",
                            "Choir A->O", "Whisper", "Vowel Morph",
                            "Bowed Metal", "Glass Harmonics", "Railroad",
                            "Dustbowl", "Static Evolve", "Spectral Drift", "Serum HD",
                            // Morph (Phase 11h)
                            "Rise", "Even", "Drift", "Sweep", "Formant", "Stack" },
        0));  // default = Sine

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WT_FRAME, 1 },
        "Synth OSC A WT Frame",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    // ── Synth section — Phase 2C (Warp modes) ────────────────────────────
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WARP_MODE, 1 },
        "Synth OSC A Warp Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" },
        0));

    // PHASE mode (back panel pill 1 — replaces the redundant WARP-mode selector).
    // 0=RETRIG (all voices to phase 0, tight/punchy) · 1=FREE (never reset, analog) ·
    // 2=RANDOM (fresh decorrelated phase each note, default) · 3=SPREAD (even fan, wide+repeatable)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_PHASE_MODE, 1 },
        "Synth OSC A Phase Mode",
        juce::StringArray { "Retrig", "Free", "Random", "Spread" },
        2));

    // WAVER (back panel pill 2 — replaces the redundant SPECTRAL-mode selector).
    // Per-OSC analog pitch-drift depth (Ornstein–Uhlenbeck, per unison sine).
    // Supersedes the old SYN_EROSION pitch drift; SYN_EROSION now drives FILTER drift only.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WAVER, 1 },
        "Synth OSC A Waver",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f));

    // KEYTRACK (back panel pill 3 — replaces the redundant FOLD-shape selector).
    // First note->destination modulation route: depth 0..100 %, destination selectable
    // (FRAME=WT POS, WARP, FOLD). Per-voice, latched at note-on. Architected toward the
    // future mod-matrix. (SPECTRAL is off-thread; sample START has no per-OSC param yet.)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_KEYTRACK, 1 },
        "Synth OSC A Keytrack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_KEYTRACK_DEST, 1 },
        "Synth OSC A Keytrack Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_WARP_AMOUNT, 1 },
        "Synth OSC A Warp Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));

    // ── Phase 11a — OSC A wavetable rework foundation ────────────────────
    // SPECTRAL MORPH mode (Phase 11c rework — frequency-domain morph applied to the
    // wavetable's spectrum off the audio thread). v1 exposes the implemented modes;
    // order MUST match tw::SpectralMode (None=0, HarmonicStretch=1, InharmonicStretch=2).
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SPECTRAL_TYPE, 1 },
        "OSC A Spectral Type",
        juce::StringArray { "None", "Harmonic Stretch", "Inharmonic Stretch",
                            "Vocode", "Smear", "Random Amps", "Data Compress", "Spectral Phaser" },
        0));
    // SPECTRAL AMT — morph amount (0 = base table, 1 = full morph).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_SPECTRAL_AMT, 1 },
        "OSC A Spectral Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    // FOLD SHAPE choice (Phase 11d: 3 shapes — Linear / Sine / Triangle)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_FOLD_SHAPE, 1 },
        "OSC A Fold Shape",
        juce::StringArray { "Linear", "Sine", "Triangle" },
        0));
    // FOLD AMT (placeholder param)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_FOLD_AMT, 1 },
        "OSC A Fold Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    // BLUR (real DSP — frame-blend width; repurposes the old FRAME_SPREAD param ID)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_FRAME_SPREAD, 1 },
        "OSC A Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    // INTERP MODE choice (Phase 11g: 2 modes — Linear / Stepped)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_A_INTERP_MODE, 1 },
        "OSC A Interp Mode",
        juce::StringArray { "Linear", "Stepped" },
        0));

    // ── Synth section — Phase 9 (OSC B chassis) ──────────────────────────
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_ENGINE, 1 },
        "Synth OSC B Engine",
        juce::StringArray { "WT", "SAMP", "GRAN", "SPEC", "FM", "NOISE" },
        0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_OCT, 1 },
        "Synth OSC B Octave", -3, 3, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SEMI, 1 },
        "Synth OSC B Semitone", -12, 12, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_CENT, 1 },
        "Synth OSC B Cents",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_LEVEL, 1 },
        "Synth OSC B Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_PAN, 1 },
        "Synth OSC B Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WT_PRESET, 1 },
        "Synth OSC B WT Preset",
        juce::StringArray { "Sine", "Triangle", "Square", "Pulse",
                            "Prophet Saw", "Jupiter PWM", "Moog Sqr",
                            "OB-X Saw", "CS-80 Brass", "Juno Str",
                            "PPG Wave", "DX7 EP", "D-50 Bell", "M1 Piano",
                            "Choir A->O", "Whisper", "Vowel Morph",
                            "Bowed Metal", "Glass Harmonics", "Railroad",
                            "Dustbowl", "Static Evolve", "Spectral Drift", "Serum HD",
                            // Morph (Phase 11h)
                            "Rise", "Even", "Drift", "Sweep", "Formant", "Stack" },
        0));  // default = Sine
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WT_FRAME, 1 },
        "Synth OSC B WT Frame",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WARP_MODE, 1 },
        "Synth OSC B Warp Mode",
        juce::StringArray { "NONE", "Bend", "Sync", "Formant", "PWM", "Skew", "Mirror", "Fractalize", "P-Quantize", "Rectify", "Sine Shaper" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_PHASE_MODE, 1 },
        "Synth OSC B Phase Mode",
        juce::StringArray { "Retrig", "Free", "Random", "Spread" },
        2));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WAVER, 1 },
        "Synth OSC B Waver",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_KEYTRACK, 1 },
        "Synth OSC B Keytrack",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_KEYTRACK_DEST, 1 },
        "Synth OSC B Keytrack Dest",
        juce::StringArray { "Frame", "Warp", "Fold" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_WARP_AMOUNT, 1 },
        "Synth OSC B Warp Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // ── Phase 11a — OSC B wavetable rework foundation (SPECTRAL MORPH — Phase 11c) ─
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SPECTRAL_TYPE, 1 },
        "OSC B Spectral Type",
        juce::StringArray { "None", "Harmonic Stretch", "Inharmonic Stretch",
                            "Vocode", "Smear", "Random Amps", "Data Compress", "Spectral Phaser" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_SPECTRAL_AMT, 1 },
        "OSC B Spectral Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_FOLD_SHAPE, 1 },
        "OSC B Fold Shape",
        juce::StringArray { "Linear", "Sine", "Triangle" },
        0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_FOLD_AMT, 1 },
        "OSC B Fold Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_FRAME_SPREAD, 1 },
        "OSC B Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParameterIDs::SYN_OSC_B_INTERP_MODE, 1 },
        "OSC B Interp Mode",
        juce::StringArray { "Linear", "Stepped" },
        0));

    // ── Synth section — Phase 8a (Voice settings + flagship features) ────
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_VOICES, 1 },
        "Synth Voices", 1, 32, 8));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParameterIDs::SYN_UNISON, 1 },
        "Synth Unison", 1, 8, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_SPREAD, 1 },
        "Synth Spread",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_EROSION, 1 },
        "Synth Erosion",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParameterIDs::SYN_HORIZON, 1 },
        "Synth Horizon",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));

    return layout;
}

//==============================================================================
void TerrainInstrumentAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Task 5: singleton synth removed. Prep all layer synths instead.
    // Mark 2 task 4: prep per-layer scratch buffers and per-layer synth rates.
    // Each layer synth renders into its own scratch buffer in processBlock,
    // then the results are summed (with mixer math) into the master `buffer`.
    for (auto& buf : layerScratch)
        buf.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);

    for (auto& layer : layers)
        layer.synth.setCurrentPlaybackSampleRate (sampleRate);

    // Synth scratch buffer + per-voice DSP prep.
    synthScratch.setSize (2, samplesPerBlock, false, true, true);
    synthEngine.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synthEngine.getNumVoices(); ++i)
    {
        if (auto* sv = dynamic_cast<tw::SynthVoice*> (synthEngine.getVoice (i)))
            sv->prepareToPlay (sampleRate, samplesPerBlock, 2);
    }

    grainEngineL.prepare(sampleRate, samplesPerBlock);
    grainEngineR.prepare(sampleRate, samplesPerBlock);
    tapeProcessorL.prepare(sampleRate, samplesPerBlock);
    tapeProcessorR.prepare(sampleRate, samplesPerBlock);
    tapeLoop.prepare(sampleRate, samplesPerBlock);
    spaceReverb.prepare(sampleRate, samplesPerBlock);
    moogDelay.prepare(sampleRate, samplesPerBlock);
    terrainChorus.prepare (sampleRate, samplesPerBlock);

    // Per-chop FX independence capture bus — stereo, sized to the host block.
    // Voices write here when their chop has fxIndependent=true; the bus is
    // added directly to master output, bypassing the entire global FX chain.
    indyCaptureBus.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);

    // Per-chop FX-independence (option 1): prepare the shared indy chain
    // and allocate its output sum buffer. The chain processes
    // indyCaptureBus into indySumBuffer once per block; the per-sample
    // master loop reads indySumBuffer and mixes it in.
    indyChain.prepare (sampleRate, samplesPerBlock);
    indySumBuffer.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    indySumBuffer.clear();

    // Mix page Phase D: allocate per-layer rolling stem buffers (~92 MB at 48k).
    allocateStemBuffers (sampleRate);
    indyCaptureBus.clear();

    smoothedChorusAmount.reset    (sampleRate, 0.02);
    smoothedChorusWidth.reset     (sampleRate, 0.02);
    smoothedChorusCharacter.reset (sampleRate, 0.05);  // CHARACTER changes more dramatically
    smoothedChorusAmount.setCurrentAndTargetValue    (apvts.getRawParameterValue (ParameterIDs::CHORUS_AMOUNT)->load());
    smoothedChorusWidth.setCurrentAndTargetValue     (apvts.getRawParameterValue (ParameterIDs::CHORUS_WIDTH)->load());
    smoothedChorusCharacter.setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::CHORUS_CHARACTER)->load());

    eqL.prepare(sampleRate, samplesPerBlock);
    eqR.prepare(sampleRate, samplesPerBlock);
    analyzerPre.prepare (sampleRate);
    analyzerPost.prepare (sampleRate);
    // Initialize EQ smoothers to APVTS defaults so the first audio block sees
    // valid values (not 0, which would make ParametricEQ's internal Q smoother
    // briefly cross zero and produce NaN coefficients).
    {
        const char* freqIds [7] = { ParameterIDs::EQ_B1_FREQ, ParameterIDs::EQ_B2_FREQ, ParameterIDs::EQ_B3_FREQ, ParameterIDs::EQ_B4_FREQ, ParameterIDs::EQ_B5_FREQ, ParameterIDs::EQ_B6_FREQ, ParameterIDs::EQ_B7_FREQ };
        const char* gainIds [7] = { ParameterIDs::EQ_B1_GAIN, ParameterIDs::EQ_B2_GAIN, ParameterIDs::EQ_B3_GAIN, ParameterIDs::EQ_B4_GAIN, ParameterIDs::EQ_B5_GAIN, ParameterIDs::EQ_B6_GAIN, ParameterIDs::EQ_B7_GAIN };
        const char* qIds    [7] = { ParameterIDs::EQ_B1_Q,    ParameterIDs::EQ_B2_Q,    ParameterIDs::EQ_B3_Q,    ParameterIDs::EQ_B4_Q,    ParameterIDs::EQ_B5_Q,    ParameterIDs::EQ_B6_Q,    ParameterIDs::EQ_B7_Q };
        for (int b = 0; b < 7; ++b)
        {
            smoothedEqBandFreq[b].reset (sampleRate, 0.005);
            smoothedEqBandGain[b].reset (sampleRate, 0.005);
            smoothedEqBandQ[b]   .reset (sampleRate, 0.005);
            smoothedEqBandFreq[b].setCurrentAndTargetValue (apvts.getRawParameterValue (freqIds[b])->load());
            smoothedEqBandGain[b].setCurrentAndTargetValue (apvts.getRawParameterValue (gainIds[b])->load());
            smoothedEqBandQ[b]   .setCurrentAndTargetValue (apvts.getRawParameterValue (qIds[b])->load());
        }
        smoothedEqHpFreq.reset (sampleRate, 0.005);
        smoothedEqLpFreq.reset (sampleRate, 0.005);
        smoothedEqHpFreq.setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::EQ_HP_FREQ)->load());
        smoothedEqLpFreq.setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::EQ_LP_FREQ)->load());
    }
    captureBuffer.prepare(sampleRate, samplesPerBlock);

    // Prepare modulation engine
    modulationEngine.prepare(sampleRate);
    if (modStateJson.isNotEmpty())
        modulationEngine.updateConfig(ModulationEngine::parseJSON(modStateJson));

    // 20ms ramp for all smoothed parameters
    smoothedGrainSize.reset(sampleRate, 0.02);
    smoothedDensity.reset(sampleRate, 0.02);
    smoothedSpray.reset(sampleRate, 0.02);
    smoothedPitch.reset(sampleRate, 0.02);
    smoothedWander.reset(sampleRate, 0.02);
    smoothedFreeze.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    smoothedGrainFilter.reset(sampleRate, 0.02);
    smoothedWowFlutter.reset(sampleRate, 0.02);
    smoothedSaturation.reset(sampleRate, 0.02);
    smoothedHiss.reset(sampleRate, 0.02);
    smoothedStudioSculpt.reset (sampleRate, 0.02);
    smoothedStudioWeave .reset (sampleRate, 0.02);
    smoothedStudioTilt  .reset (sampleRate, 0.02);
    smoothedWireWow .reset (sampleRate, 0.02);
    smoothedWireSat .reset (sampleRate, 0.02);
    smoothedWireHiss.reset (sampleRate, 0.02);
    smoothedOutputGain.reset(sampleRate, 0.02);
    smoothedMasterMix.reset(sampleRate, 0.02);
    smoothedLoopFeedback.reset(sampleRate, 0.02);
    smoothedLoopDegrade.reset(sampleRate, 0.02);

    // Space reverb smoothing
    smoothedSpaceSize.reset(sampleRate, 0.02);
    smoothedSpaceDecay.reset(sampleRate, 0.02);
    smoothedSpaceTone.reset(sampleRate, 0.02);
    smoothedSpaceMix.reset(sampleRate, 0.02);

    // Delay smoothing (20 ms ramp like the rest)
    smoothedDlyTime.reset(sampleRate, 0.02);
    smoothedDlyFeedback.reset(sampleRate, 0.02);
    smoothedDlyTone.reset(sampleRate, 0.02);
    smoothedDlyCharacter.reset(sampleRate, 0.02);
    smoothedDlyMod.reset(sampleRate, 0.02);
    smoothedDlyModRate.reset(sampleRate, 0.02);
    smoothedDlyMix.reset(sampleRate, 0.02);
    smoothedDlyDuck.reset(sampleRate, 0.02);
    smoothedDelayFreeze.reset(sampleRate, 0.005); // 5ms — fast since freeze is binary-ish

    // Set initial values
    smoothedGrainSize.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::GRAIN_SIZE)->load());
    smoothedDensity.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DENSITY)->load());
    smoothedSpray.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SPRAY)->load());
    smoothedPitch.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::PITCH)->load());
    smoothedWander.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::WANDER)->load());
    smoothedFreeze.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::FREEZE)->load());
    smoothedMix.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::MIX)->load());
    smoothedGrainFilter.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::GRAIN_FILTER)->load());
    smoothedWowFlutter.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load());
    smoothedSaturation.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SATURATION)->load());
    smoothedHiss.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::HISS)->load());
    smoothedStudioSculpt.setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::STUDIO_SCULPT)->load());
    smoothedStudioWeave .setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::STUDIO_WEAVE) ->load());
    smoothedStudioTilt  .setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::STUDIO_TILT)  ->load());
    smoothedWireWow .setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::WIRE_WOW)       ->load());
    smoothedWireSat .setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::WIRE_SATURATION)->load());
    smoothedWireHiss.setCurrentAndTargetValue (apvts.getRawParameterValue (ParameterIDs::WIRE_HISS)      ->load());
    smoothedOutputGain.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load());
    smoothedMasterMix.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::MASTER_MIX)->load());
    smoothedLoopFeedback.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::LOOP_FEEDBACK)->load());
    smoothedLoopDegrade.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::LOOP_DEGRADE)->load());

    // Space reverb initial values
    smoothedSpaceSize.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SPACE_SIZE)->load());
    smoothedSpaceDecay.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SPACE_DECAY)->load());
    smoothedSpaceTone.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SPACE_TONE)->load());
    smoothedSpaceMix.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SPACE_MIX)->load());

    // Delay initial values
    smoothedDlyTime.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_TIME)->load());
    smoothedDlyFeedback.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_FEEDBACK)->load());
    smoothedDlyTone.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_TONE)->load());
    smoothedDlyCharacter.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_CHARACTER)->load());
    smoothedDlyMod.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_MOD)->load());
    smoothedDlyModRate.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_MOD_RATE)->load());
    smoothedDlyMix.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_MIX)->load());
    smoothedDlyDuck.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_DUCK)->load());
    smoothedDelayFreeze.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_FREEZE)->load());

}

void TerrainInstrumentAudioProcessor::releaseResources()
{
    grainEngineL.reset();
    grainEngineR.reset();
    tapeProcessorL.reset();
    tapeProcessorR.reset();
    tapeLoop.reset();
    spaceReverb.reset();
    moogDelay.reset();
    terrainChorus.reset();
    eqL.reset();
    eqR.reset();
}

bool TerrainInstrumentAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Instrument: only require mono or stereo output. No input bus check —
    // instruments don't have an audio input bus (host disables it).
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void TerrainInstrumentAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples == 0) return;

    // === Sampler engine (Terrain Instrument v0a + slicer v0b) ================
    // Pull sampler params from APVTS into the lock-free atomics that voices read.
    // Phase 1 task 9: APVTS is global — broadcast every block to ALL layers so
    // their voices have live values. Per-layer APVTS is a future Phase refactor.
    //
    // Mark 2 Phase 1 audio-fix (LOOP per-layer): sampleLoopMode REMOVED from
    // this broadcast — it's now per-layer (atomic written by setSampleLoopMode
    // native fn directly, V1 preset compat handled in setStateInformation).
    // Without this, switching LOOP on layer A also forced B/C/D into LOOP.
    {
        const int   rootMidi   = (int) *apvts.getRawParameterValue (ParameterIDs::ROOT_NOTE);
        const float attackMs   = *apvts.getRawParameterValue (ParameterIDs::ATTACK_MS);
        const float releaseMs  = *apvts.getRawParameterValue (ParameterIDs::RELEASE_MS);
        const float chopFadeMs = *apvts.getRawParameterValue (ParameterIDs::CHOP_FADE_MS);

        for (auto& layer : layers)
        {
            layer.rootMidiNote.store   (rootMidi);
            layer.attackMsAtomic.store (attackMs);
            layer.releaseMsAtomic.store(releaseMs);
            layer.chopFadeMs.store     (chopFadeMs);
        }
    }

    // Mark 2 Phase 1 audio-fix: sliceMode is now per-layer (read from
    // layer.sliceMode inside the layer loop below). sliceSubMode is still
    // global for Phase 1 — only matters when 2+ layers are simultaneously
    // in SLICE mode with different sub-modes (edge case to address later).
    const int sliceSubModeIdx_blk = (int) *apvts.getRawParameterValue (ParameterIDs::SLICE_SUB_MODE);
    const size_t elIdx = (size_t) editingLayer.load();

    // Helper: derive the SliceContext::Mode enum from a per-layer slice mode +
    // the (still-global) slice sub-mode. PITCH (layerSliceMode==0) always
    // resolves to Whole regardless of sub-mode; SLICE (==1) picks the variant
    // based on the global sub-mode selector.
    auto modeFromLayer = [sliceSubModeIdx_blk] (int layerSliceMode) -> tw::SliceContext::Mode
    {
        if (layerSliceMode == 0)               return tw::SliceContext::Mode::Whole;
        if (sliceSubModeIdx_blk == 3)          return tw::SliceContext::Mode::Layer;
        if (sliceSubModeIdx_blk == 2)          return tw::SliceContext::Mode::ChromaticRandom;
        if (sliceSubModeIdx_blk == 1)          return tw::SliceContext::Mode::ChromaticOneSlice;
        return tw::SliceContext::Mode::ChopChromaticLayout;
    };

    // Drain audition queue — UI-clicked previews of individual slices.
    // Triggered before renderNextBlock so the audition voice starts within
    // this block. Dispatches to the currently-editing layer's synth.
    {
        std::vector<int> drained;
        {
            const juce::SpinLock::ScopedLockType sl (auditionLock);
            drained.swap (auditionQueue);
        }
        if (! drained.empty())
        {
            auto sl = std::atomic_load (&layers[elIdx].currentSlices);
            if (sl && ! sl->empty())
            {
                for (int idx : drained)
                {
                    if (idx >= 0 && idx < (int) sl->size())
                    {
                        layers[elIdx].synth.auditionSlice ((*sl)[(size_t) idx], idx, getSourceVersionId());
                    }
                }
            }
        }
    }

    // ── Per-chop FX independence routing ────────────────────────────────
    // Voices whose chop has fxIndependent=true redirect their output to
    // indyCaptureBus (bypasses the global FX chain entirely). All others
    // write into `buffer` as before and flow through the chain. We add
    // the indy bus back at the END of processBlock as a clean signal.
    if (indyCaptureBus.getNumSamples() < numSamples)
        indyCaptureBus.setSize (2, numSamples, false, true, true);
    indyCaptureBus.clear (0, numSamples);

    // ── Mix page Phase 2: MIDI dispatch filter (trigger modes) ───────────
    // Walks midiMessages once and builds 4 per-layer MIDI buffers based on
    // the active triggerMode. Non-note-on events (note-off, CC, pitch-bend,
    // aftertouch) go to ALL layers so voices already in flight can be
    // turned off / modulated correctly.
    //
    // For note-ons, the picker decides which layer(s) receive the event:
    //   LAYER     → all populated layers
    //   RR        → one layer at roundRobinPos (skip empties, advance)
    //   RANDOM    → one layer picked via weighted random over probabilityWeight
    //   KEYTRACK  → one layer whose [keyZoneMin..Max] contains the note number
    //   VELOCITY  → one layer whose [velocityZoneMin..Max] contains note vel
    //
    // After this block, per-layer renderNextBlock uses perLayerMidi[li]
    // instead of the shared midiMessages.
    std::array<juce::MidiBuffer, 4> perLayerMidi;
    {
        const int  tmode = triggerMode.load();
        const auto isPopulated = [this] (int li) {
            return li >= 0 && li < 4 && layers[(size_t) li].hasSample();
        };

        for (const auto event : midiMessages)
        {
            const auto msg = event.getMessage();
            const int  pos = event.samplePosition;

            if (! msg.isNoteOn())
            {
                // Non-note-on events broadcast to all layers (note-offs, CC, pitch-bend,
                // aftertouch). Layers that didn't receive the corresponding note-on
                // simply have nothing to turn off — JUCE Synthesiser handles gracefully.
                for (auto& buf : perLayerMidi)
                    buf.addEvent (msg, pos);
                continue;
            }

            // ── Note-on routing per trigger mode ──
            const int vel = msg.getVelocity();

            if (tmode == 0)
            {
                // LAYER — all populated layers fire (current Phase 1 behavior).
                for (int li = 0; li < 4; ++li)
                    if (isPopulated (li))
                        perLayerMidi[(size_t) li].addEvent (msg, pos);
            }
            else if (tmode == 1)
            {
                // ROUND-ROBIN — one layer per note. SHUFFLE = random non-repeating
                // populated layer; otherwise sequential A→B→C→D (skip empties).
                int picked = -1;
                if (rrShuffle.load())
                {
                    int pops[4]; int n = 0;
                    for (int li = 0; li < 4; ++li) if (isPopulated (li)) pops[n++] = li;
                    if (n == 1) picked = pops[0];
                    else if (n > 1)
                    {
                        for (int attempts = 0; attempts < 8 && picked < 0; ++attempts)
                        {
                            int cand = pops[juce::jlimit (0, n - 1, (int) (triggerRandom.nextFloat() * (float) n))];
                            if (cand != lastRrLayer) picked = cand;
                        }
                        if (picked < 0) picked = pops[0];
                    }
                    if (picked >= 0)
                    {
                        perLayerMidi[(size_t) picked].addEvent (msg, pos);
                        lastRrLayer = picked;
                        roundRobinPos.store (picked);            // dot shows last-played
                    }
                }
                else
                {
                    int cur = roundRobinPos.load();
                    for (int attempts = 0; attempts < 4; ++attempts)
                    {
                        if (isPopulated (cur)) { picked = cur; break; }
                        cur = (cur + 1) % 4;
                    }
                    if (picked >= 0)
                    {
                        perLayerMidi[(size_t) picked].addEvent (msg, pos);
                        lastRrLayer = picked;
                        roundRobinPos.store ((picked + 1) % 4);  // cursor + dot show next
                    }
                }
            }
            else if (tmode == 2)
            {
                // RANDOM — weighted pick over populated layers' probabilityWeight.
                // weights renormalized over the populated set so empty layers can't win.
                float total = 0.0f;
                for (int li = 0; li < 4; ++li)
                    if (isPopulated (li))
                        total += juce::jmax (0.0f, layers[(size_t) li].probabilityWeight.load());

                if (total > 1.0e-6f)
                {
                    float r = triggerRandom.nextFloat() * total;
                    int picked = -1;
                    for (int li = 0; li < 4; ++li)
                    {
                        if (! isPopulated (li)) continue;
                        const float w = juce::jmax (0.0f, layers[(size_t) li].probabilityWeight.load());
                        if (r < w) { picked = li; break; }
                        r -= w;
                    }
                    if (picked >= 0)
                        perLayerMidi[(size_t) picked].addEvent (msg, pos);
                }
                // total == 0 → no eligible layers, drop the note (predictable silence).
            }
            else if (tmode == 3)
            {
                // KEYTRACK — keyboard split. Fire the layer whose key zone contains
                // this note number. First match wins (zones enforced contiguous +
                // non-overlapping by UI). No match → drop the note.
                const int note = msg.getNoteNumber();
                for (int li = 0; li < 4; ++li)
                {
                    if (! isPopulated (li)) continue;
                    const int lo = layers[(size_t) li].keyZoneMin.load();
                    const int hi = layers[(size_t) li].keyZoneMax.load();
                    if (note >= lo && note <= hi)
                    {
                        perLayerMidi[(size_t) li].addEvent (msg, pos);
                        break;
                    }
                }
            }
            else if (tmode == 4)
            {
                // VELOCITY — fire the layer whose zone contains this velocity.
                // First match wins (zones are enforced contiguous + non-overlapping by UI).
                for (int li = 0; li < 4; ++li)
                {
                    if (! isPopulated (li)) continue;
                    const int lo = layers[(size_t) li].velocityZoneMin.load();
                    const int hi = layers[(size_t) li].velocityZoneMax.load();
                    if (vel >= lo && vel <= hi)
                    {
                        perLayerMidi[(size_t) li].addEvent (msg, pos);
                        break;
                    }
                }
            }
        }
    }

    // ── Mark 2 task 4 / task 9: render each populated layer into its own scratch
    // buffer, then sum (with vol/mute/solo) into master `buffer`.
    // Task 9 complete: each layer now receives its own per-layer SliceContext.
    buffer.clear();   // master starts silent; we sum each layer into it below

    {
        const bool anySolo = std::any_of (layers.begin(), layers.end(),
                                           [](const tw::LayerState& l){ return l.solo.load(); });

        const double blockSec = (double) numSamples / juce::jmax (1.0, getSampleRate());
        const float  decay    = (float) std::exp (-blockSec / 0.065);

        // LAYER-mode MORPH precompute: map the morph focus across POPULATED layers
        // only, so a single loaded layer stays full and the blend travels just over
        // what's actually loaded. morphPos is in populated-index units (0..popCount-1).
        const bool layerMode = (triggerMode.load() == 0);
        int popCount = 0; int popIdx[4] = { -1, -1, -1, -1 };
        for (int i = 0; i < 4; ++i) if (layers[(size_t) i].hasSample()) popIdx[i] = popCount++;
        const float morphPos = layerMorph.load() * (float) juce::jmax (1, popCount - 1);

        for (size_t li = 0; li < layers.size(); ++li)
        {
            auto& layer = layers[li];

            // Skip layers that have no sample loaded (no voices possible).
            if (! layer.hasSample())               continue;

            // Mix page audio-fix: mute/solo gate the SUMMING, NOT the render.
            // We must still call renderNextBlock on every populated layer so its
            // voices process note-offs even when the layer is silenced. Without
            // this, a voice playing on a layer that gets muted or solo-excluded
            // mid-note never receives its note-off and sticks forever — this was
            // the "infinite scan" bug (solo layer A on the mixer while layer C
            // has a scan voice in flight → C is skipped → its scan never ends).
            const bool audible = ! layer.mute.load()
                               && (! anySolo || layer.solo.load());

            // Build per-layer SliceContext. ctx.mode is now sourced from each
            // layer's own sliceMode atomic (Mark 2 Phase 1 audio-fix) so layers
            // can independently choose PITCH vs SLICE. sliceSubMode is still
            // global — when 2+ layers are simultaneously in SLICE the most
            // recent sub-mode click wins for all of them.
            {
                tw::SliceContext ctx;
                ctx.mode             = modeFromLayer (layer.sliceMode.load());
                ctx.rootMidiNote     = layer.rootMidiNote.load();
                ctx.activeSliceIndex = layer.activeSliceIndex.load();
                ctx.sourceVersionId  = getSourceVersionId();
                ctx.slices           = std::atomic_load (&layer.currentSlices);
                ctx.pitchModeSlice   = layer.pitchModeSlice;   // copy-by-value snapshot
                ctx.holdMode         = holdMode.load (std::memory_order_relaxed);
                layer.synth.setSliceContext (ctx);
            }

            // Indy bus routing — Phase 1: only layers[0] feeds the shared indy bus.
            // Other layers would get nullptr (skip routing) if they ever reached here.
            if (li == 0)
                layer.synth.setIndyTargetBufferForVoices (&indyCaptureBus);

            // Render this layer's voices into the per-layer scratch buffer.
            // Mix page Phase 2: each layer now sees a FILTERED midi buffer
            // (perLayerMidi[li] built above by the trigger-mode dispatcher).
            // In LAYER mode this is identical to the old broadcast behavior;
            // in RR/RANDOM/SOLO/VELOCITY modes only the picked layer(s) see
            // each note-on, while non-note-on events still broadcast to all.
            auto& scratch = layerScratch[li];
            scratch.clear();
            layer.synth.renderNextBlock (scratch, perLayerMidi[li], 0, numSamples);

            // Detach indy pointer immediately after render.
            if (li == 0)
                layer.synth.setIndyTargetBufferForVoices (nullptr);

            // Per-layer glow update: walk this layer's voices, write into this
            // layer's sliceGlowLevel. snapshotSliceGlowLevels() reads directly
            // from layers[editingLayer].sliceGlowLevel (Task 5 — singleton removed).
            {
                std::array<float, tw::kMaxGlowSlots> blockMax {};
                for (int v = 0; v < layer.synth.getNumVoices(); ++v)
                {
                    if (auto* sv = dynamic_cast<tw::SamplerVoice*> (layer.synth.getVoice (v)))
                    {
                        if (! sv->isPlaying()) continue;
                        const int idx = sv->getSliceIndex();
                        if (idx < 0 || idx >= tw::kMaxGlowSlots) continue;
                        const float lvl = sv->getEnvelopeLevel();
                        if (lvl > blockMax[(size_t) idx]) blockMax[(size_t) idx] = lvl;
                    }
                }
                for (int i = 0; i < tw::kMaxGlowSlots; ++i)
                {
                    float prev = layer.sliceGlowLevel[(size_t) i].load (std::memory_order_relaxed);
                    float next = juce::jmax (blockMax[(size_t) i], prev * decay);
                    layer.sliceGlowLevel[(size_t) i].store (next, std::memory_order_relaxed);
                }
            }

            // Mix page Phase D: write to this layer's rolling stem buffer.
            // DRY = raw layerScratch (just synth output before mix). MIX = post
            // volume/pan applied (computed below). We capture DRY now so it's
            // available regardless of which mode the user picks at export time;
            // the MIX path snapshots after the mixer math.
            // (For v1 we just capture DRY always; the stemSourceMode toggle
            // gates which version the EXPORT writes. Two-buffer-per-layer
            // would double RAM — instead we apply mix at export when needed.)
            writeToStemBuffer ((int) li, scratch.getReadPointer (0), scratch.getReadPointer (1), numSamples);

            // Sum this layer into the master buffer with per-layer gain + pan.
            // Equal-power pan: panAngle = (panNorm + 1) * π/4 → leftGain = cos,
            // rightGain = sin. Center (panNorm=0) gives 0.707/0.707 (constant
            // perceived power across the pan range). pre-clamped to [-1, +1].
            float morphGain = 1.0f;
            if (layerMode && popCount > 1 && popIdx[(int) li] >= 0)
                morphGain = juce::jlimit (0.0f, 1.0f,
                                          1.0f - std::abs ((float) popIdx[(int) li] - morphPos) / 2.0f);
            const float g    = layer.volume.load() * morphGain;
            const float pn   = juce::jlimit (-1.0f, 1.0f, layer.pan.load());
            const float ang  = (pn + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
            const float gL   = g * std::cos (ang);
            const float gR   = g * std::sin (ang);
            // Only AUDIBLE layers contribute to the master mix. Muted / solo-
            // excluded layers still rendered above (so their voices release),
            // they just don't sum here.
            if (audible)
            {
                buffer.addFrom (0, 0, scratch, 0, 0, numSamples, gL);
                buffer.addFrom (1, 0, scratch, 1, 0, numSamples, gR);
            }

            // Per-layer peak meter — feeds the channel-strip meter widget.
            // Visual gain: the raw magnitude reads low for typical samples, so
            // the strip meters barely moved. Apply a perceptual sqrt curve +
            // ~6 dB makeup so the LED bars are lively and responsive (user
            // wanted "4K 60fps interactive" levels) while still clipping at 1.0.
            // Muted layers report 0 (meter goes dark, matching what you hear).
            {
                const float rawL = audible ? scratch.getMagnitude (0, 0, numSamples) * std::abs (gL) : 0.0f;
                const float rawR = audible ? scratch.getMagnitude (1, 0, numSamples) * std::abs (gR) : 0.0f;
                // Perceptual curve: sqrt expands low levels, ×1.6 makeup, clamp.
                const float visL = juce::jmin (1.0f, std::sqrt (rawL) * 1.6f);
                const float visR = juce::jmin (1.0f, std::sqrt (rawR) * 1.6f);
                // Fast attack (instant), slower decay for readable peaks.
                const float prevL = layer.peakLevelL.load (std::memory_order_relaxed);
                const float prevR = layer.peakLevelR.load (std::memory_order_relaxed);
                layer.peakLevelL.store (juce::jmax (visL, prevL * decay), std::memory_order_relaxed);
                layer.peakLevelR.store (juce::jmax (visR, prevR * decay), std::memory_order_relaxed);
            }
        }
    }

    // ── Synth section render (Phase 1 MPV) ──────────────────────────────
    // Push current SYN_* APVTS values onto every SynthVoice each block
    // — same pattern as the per-layer atomic broadcast above.
    {
        const int   oct     = (int)   *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_OCT);
        const int   semi    = (int)   *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_SEMI);
        const float cent    =         *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_CENT);
        const float lvl     =         *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_LEVEL);
        const float pan     =         *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_PAN);
        const float cut     =         *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER1_CUT);
        const float res     =         *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER1_RES);
        // Batch 1 Filter — TYPE, DRV, bipolar ENV, and the dedicated FLT ADSR.
        const int   filtType= (int)   *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER1_TYPE);
        const float filtDrv =         *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER1_DRV);
        const float filtEnv =         *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER1_ENV);
        // Filter 2 (independent) + per-filter mix + routing.
        const float cut2     =        *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER2_CUT);
        const float res2     =        *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER2_RES);
        const int   filtType2= (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER2_TYPE);
        const float filtDrv2 =        *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER2_DRV);
        const float filtEnv2 =        *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER2_ENV);
        const float filtMix1 =        *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER1_MIX);
        const float filtMix2 =        *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER2_MIX);
        const int   filtRoute= (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_FILTER_ROUTING);
        const float fltEnvA =         *apvts.getRawParameterValue (ParameterIDs::SYN_ENV_FLT_A);
        const float fltEnvD =         *apvts.getRawParameterValue (ParameterIDs::SYN_ENV_FLT_D);
        const float fltEnvS =         *apvts.getRawParameterValue (ParameterIDs::SYN_ENV_FLT_S);
        const float fltEnvR =         *apvts.getRawParameterValue (ParameterIDs::SYN_ENV_FLT_R);
        const float ampA    =         *apvts.getRawParameterValue (ParameterIDs::SYN_ENV_AMP_A);
        const float ampD    =         *apvts.getRawParameterValue (ParameterIDs::SYN_ENV_AMP_D);
        const float ampS    =         *apvts.getRawParameterValue (ParameterIDs::SYN_ENV_AMP_S);
        const float ampR    =         *apvts.getRawParameterValue (ParameterIDs::SYN_ENV_AMP_R);
        // Phase 2A wavetable selection — resolve preset enum to const Wavetable*.
        const int            wtPreset = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_WT_PRESET);
        const float          wtFrame  =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_WT_FRAME);
        const tw::Wavetable* wt       = resolveMorphTable (morphA_, wtPreset);
        // Phase 2C — warp mode + amount
        const int   warpMode   = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_WARP_MODE);
        const int   phaseModeA = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_PHASE_MODE);
        const float warpAmount =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_WARP_AMOUNT);
        // Phase 3 — OSC A engine choice
        const int engineIdx = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_ENGINE);

        // Phase 9 — OSC B params
        const int   octB       = (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_OCT);
        const int   semiB      = (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_SEMI);
        const float centB      =        *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_CENT);
        const float lvlB       =        *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_LEVEL);
        const float panB       =        *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_PAN);
        const int   wtPresetB  = (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_WT_PRESET);
        const float wtFrameB   =        *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_WT_FRAME);
        const tw::Wavetable* wtB = resolveMorphTable (morphB_, wtPresetB);
        const int   warpModeB  = (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_WARP_MODE);
        const int   phaseModeB = (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_PHASE_MODE);
        const float warpAmountB =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_WARP_AMOUNT);
        const int   engineIdxB = (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_ENGINE);
        // WAVER — per-OSC analog pitch-drift depth (0..100 %). Pushed per voice below.
        const float waverA      =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_WAVER);
        const float waverB      =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_WAVER);
        // KEYTRACK — per-OSC note->destination depth (0..100 %) + destination choice.
        const float ktDepthA    =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_KEYTRACK);
        const int   ktDestA     = (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_KEYTRACK_DEST);
        const float ktDepthB    =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_KEYTRACK);
        const int   ktDestB     = (int)  *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_KEYTRACK_DEST);

        for (int i = 0; i < synthEngine.getNumVoices(); ++i)
        {
            if (auto* sv = dynamic_cast<tw::SynthVoice*> (synthEngine.getVoice (i)))
            {
                sv->setTuning                 (oct, semi, cent);
                sv->setLevel                  (lvl);
                sv->setPan                    (pan);
                sv->setFilterParameters       (cut, res);
                sv->setFilterType             (filtType);
                sv->setFilterDrive            (filtDrv);
                sv->setFilterEnvAmount        (filtEnv);
                sv->setFilterEnvParameters    (fltEnvA, fltEnvD, fltEnvS, fltEnvR);
                sv->setFilterParameters2      (cut2, res2);
                sv->setFilterType2            (filtType2);
                sv->setFilterDrive2           (filtDrv2);
                sv->setFilterEnvAmount2       (filtEnv2);
                sv->setFilterMix1             (filtMix1);
                sv->setFilterMix2             (filtMix2);
                sv->setFilterRouting          (filtRoute);
                sv->setAmpEnvelopeParameters  (ampA, ampD, ampS, ampR);
                sv->setWavetable              (wt);
                sv->setWavetableFrame         (wtFrame);
                sv->setWarp                   (warpMode, warpAmount);
                sv->setEngine                 (engineIdx);
                // Phase 9 — OSC B setters
                sv->setTuningB                (octB, semiB, centB);
                sv->setLevelB                 (lvlB);
                sv->setPanB                   (panB);
                sv->setWavetableB             (wtB);
                sv->setWavetableFrameB        (wtFrameB);
                sv->setWarpB                  (warpModeB, warpAmountB);
                sv->setPhaseMode              (phaseModeA, phaseModeB);
                sv->setWaver                  (waverA / 100.0f, waverB / 100.0f);   // WAVER — analog pitch drift (OU)
                sv->setKeytrack               (ktDepthA / 100.0f, ktDestA, ktDepthB / 100.0f, ktDestB);  // KEYTRACK
                sv->setEngineB                (engineIdxB);
            }
        }

        // Phase 8b — Voice settings: UNISON+SPREAD pushed per-voice (in-voice unison).
        // The voice computes per-sine detune+pan internally and renders all sines as one note.
        const int   unisonCount = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_UNISON);
        const float spreadPct   =       *apvts.getRawParameterValue (ParameterIDs::SYN_SPREAD);
        const float erosionPct  =       *apvts.getRawParameterValue (ParameterIDs::SYN_EROSION);
        const float horizonPct  =       *apvts.getRawParameterValue (ParameterIDs::SYN_HORIZON);
        const float unisonSpread01 = spreadPct / 100.0f;

        // Phase 11a — per-OSC FRAME SPREAD (real DSP). Other 4 new params per OSC
        // (SPECTRAL_TYPE/AMT, FOLD_SHAPE/AMT, INTERP_MODE) persist via APVTS but
        // have no audio-thread effect yet — render path will start reading them
        // in Phase 11c (SPECTRAL) and 11d (FOLD).
        const float blurA = *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_FRAME_SPREAD);
        const float blurB = *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_FRAME_SPREAD);

        // Phase 11d — FOLD per OSC.
        const int   foldShapeA  = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_FOLD_SHAPE);
        const float foldAmtA    =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_FOLD_AMT);
        const int   foldShapeB  = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_FOLD_SHAPE);
        const float foldAmtB    =       *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_FOLD_AMT);

        // Phase 11c — SPECTRAL MORPH per OSC is now applied to the wavetable spectrum
        // off the audio thread (see timerCallback / resolveMorphTable). The TYPE/AMT
        // params are read on the message thread; nothing to push per-voice here.

        // Phase 11g — INTERP per OSC.
        const int interpModeA = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_A_INTERP_MODE);
        const int interpModeB = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_OSC_B_INTERP_MODE);

        // Phase 8b polish-3 — push VOICES knob into UnisonSynth as polyphony cap.
        // VOICES=8 → exactly 8 simultaneous, new notes steal oldest (Serum 2 behavior).
        const int voiceCap = (int) *apvts.getRawParameterValue (ParameterIDs::SYN_VOICES);
        synthEngine.setVoiceCap (voiceCap);
        for (int v = 0; v < synthEngine.getNumVoices(); ++v)
        {
            if (auto* tv = dynamic_cast<tw::SynthVoice*> (synthEngine.getVoice (v)))
            {
                tv->setUnison (unisonCount, unisonSpread01);
                tv->setBlur (blurA, blurB);   // WT BLUR (frame blend)
                tv->setFold (foldShapeA, foldAmtA, foldShapeB, foldAmtB);   // Phase 11d
                tv->setInterpMode (interpModeA, interpModeB);   // Phase 11g
            }
        }

        for (int i = 0; i < synthEngine.getNumVoices(); ++i)
        {
            if (auto* sv = dynamic_cast<tw::SynthVoice*> (synthEngine.getVoice (i)))
            {
                sv->setHorizonAmount (horizonPct  / 100.0f);
                // SYN_EROSION now drives the FILTER cutoff drift only — the per-voice
                // PITCH drift it used to add is superseded by per-OSC WAVER (setWaver above).
                sv->setErosionAmount_filter (erosionPct / 100.0f);
            }
        }
    }

    // Synth renders into its own scratch (broadcast midiMessages unfiltered —
    // the trigger-mode dispatcher above gates the LAYERS only, the synth
    // is a parallel pipeline that always receives the host's MIDI).
    if (synthScratch.getNumSamples() < numSamples)
        synthScratch.setSize (2, numSamples, false, true, true);
    synthScratch.clear();
    synthEngine.renderNextBlock (synthScratch, midiMessages, 0, numSamples);

    // Sum synth into master buffer (flows through the master FX chain below).
    for (int ch = 0; ch < buffer.getNumChannels() && ch < 2; ++ch)
        buffer.addFrom (ch, 0, synthScratch, ch, 0, numSamples);

    // -6 dB pad on the voice mix before the FX chain. The FX modules
    // (TapeProcessor saturation, SpaceReverb feedback paths, etc.) were
    // tuned for plugin-input levels around -12 to -6 dBFS — typical track
    // signal coming from a DAW. Voice output at velocity=1 + envLevel=1
    // is a unity-gain (0 dBFS) signal, ~6 dB hotter than the FX expects,
    // which causes audible distortion at "subtle" tape settings. This pad
    // brings the gain staging into the FX's design window.
    constexpr float kVoiceToFxPad = 0.5f; // -6 dB
    buffer.applyGain (kVoiceToFxPad);

    // Read BPM from DAW playhead
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                currentBPM.store(static_cast<float>(*bpm));
        }
    }

    // Update smoothing targets from APVTS
    float grainSizeMs = apvts.getRawParameterValue(ParameterIDs::GRAIN_SIZE)->load();

    // BPM sync: override grain size with note-division-based ms
    if (grainSyncEnabled.load() > 0.5f)
    {
        static constexpr float divisionMultipliers[10] = {
            0.125f, 0.1667f, 0.25f, 0.3333f, 0.5f,
            0.6667f, 1.0f, 1.3333f, 2.0f, 4.0f
        };

        // Map the knob's normalized 0-1 value to one of 10 divisions
        auto* param = apvts.getParameter(ParameterIDs::GRAIN_SIZE);
        float norm = param->convertTo0to1(grainSizeMs);
        int idx = static_cast<int>(norm * 10.0f);
        if (idx > 9) idx = 9;
        if (idx < 0) idx = 0;

        float bpm = currentBPM.load();
        if (bpm < 20.f) bpm = 20.f;
        grainSizeMs = (60000.f / bpm) * divisionMultipliers[idx];
    }

    smoothedGrainSize.setTargetValue(grainSizeMs);
    smoothedDensity.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DENSITY)->load());
    smoothedSpray.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SPRAY)->load());
    smoothedPitch.setTargetValue(apvts.getRawParameterValue(ParameterIDs::PITCH)->load());
    smoothedWander.setTargetValue(apvts.getRawParameterValue(ParameterIDs::WANDER)->load());
    smoothedFreeze.setTargetValue(apvts.getRawParameterValue(ParameterIDs::FREEZE)->load());
    smoothedMix.setTargetValue(apvts.getRawParameterValue(ParameterIDs::MIX)->load());
    smoothedGrainFilter.setTargetValue(apvts.getRawParameterValue(ParameterIDs::GRAIN_FILTER)->load());
    smoothedWowFlutter.setTargetValue(apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load());
    smoothedSaturation.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SATURATION)->load());
    smoothedHiss.setTargetValue(apvts.getRawParameterValue(ParameterIDs::HISS)->load());
    smoothedStudioSculpt.setTargetValue (apvts.getRawParameterValue (ParameterIDs::STUDIO_SCULPT)->load());
    smoothedStudioWeave .setTargetValue (apvts.getRawParameterValue (ParameterIDs::STUDIO_WEAVE) ->load());
    smoothedStudioTilt  .setTargetValue (apvts.getRawParameterValue (ParameterIDs::STUDIO_TILT)  ->load());
    smoothedWireWow .setTargetValue (apvts.getRawParameterValue (ParameterIDs::WIRE_WOW)       ->load());
    smoothedWireSat .setTargetValue (apvts.getRawParameterValue (ParameterIDs::WIRE_SATURATION)->load());
    smoothedWireHiss.setTargetValue (apvts.getRawParameterValue (ParameterIDs::WIRE_HISS)      ->load());
    smoothedOutputGain.setTargetValue(apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load());
    smoothedMasterMix.setTargetValue(apvts.getRawParameterValue(ParameterIDs::MASTER_MIX)->load());

    smoothedLoopFeedback.setTargetValue(apvts.getRawParameterValue(ParameterIDs::LOOP_FEEDBACK)->load());
    smoothedLoopDegrade.setTargetValue(apvts.getRawParameterValue(ParameterIDs::LOOP_DEGRADE)->load());

    // Space reverb targets
    smoothedSpaceSize.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SPACE_SIZE)->load());
    smoothedSpaceDecay.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SPACE_DECAY)->load());
    smoothedSpaceTone.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SPACE_TONE)->load());
    smoothedSpaceMix.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SPACE_MIX)->load());

    // Delay targets (MF-104S)
    smoothedDlyTime.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_TIME)->load());
    smoothedDlyFeedback.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_FEEDBACK)->load());
    smoothedDlyTone.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_TONE)->load());
    smoothedDlyCharacter.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_CHARACTER)->load());
    smoothedDlyMod.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_MOD)->load());
    smoothedDlyModRate.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_MOD_RATE)->load());
    smoothedDlyMix.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_MIX)->load());
    smoothedDlyDuck.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_DUCK)->load());
    smoothedDelayFreeze.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DLY_FREEZE)->load());

    // Chorus targets
    smoothedChorusAmount.setTargetValue    (apvts.getRawParameterValue (ParameterIDs::CHORUS_AMOUNT)   ->load());
    smoothedChorusWidth.setTargetValue     (apvts.getRawParameterValue (ParameterIDs::CHORUS_WIDTH)    ->load());
    smoothedChorusCharacter.setTargetValue (apvts.getRawParameterValue (ParameterIDs::CHORUS_CHARACTER)->load());

    // Parametric EQ targets
    {
        const char* freqIds [7] = { ParameterIDs::EQ_B1_FREQ, ParameterIDs::EQ_B2_FREQ, ParameterIDs::EQ_B3_FREQ, ParameterIDs::EQ_B4_FREQ, ParameterIDs::EQ_B5_FREQ, ParameterIDs::EQ_B6_FREQ, ParameterIDs::EQ_B7_FREQ };
        const char* gainIds [7] = { ParameterIDs::EQ_B1_GAIN, ParameterIDs::EQ_B2_GAIN, ParameterIDs::EQ_B3_GAIN, ParameterIDs::EQ_B4_GAIN, ParameterIDs::EQ_B5_GAIN, ParameterIDs::EQ_B6_GAIN, ParameterIDs::EQ_B7_GAIN };
        const char* qIds    [7] = { ParameterIDs::EQ_B1_Q,    ParameterIDs::EQ_B2_Q,    ParameterIDs::EQ_B3_Q,    ParameterIDs::EQ_B4_Q,    ParameterIDs::EQ_B5_Q,    ParameterIDs::EQ_B6_Q,    ParameterIDs::EQ_B7_Q };
        for (int b = 0; b < 7; ++b)
        {
            smoothedEqBandFreq[b].setTargetValue (apvts.getRawParameterValue (freqIds[b])->load());
            smoothedEqBandGain[b].setTargetValue (apvts.getRawParameterValue (gainIds[b])->load());
            smoothedEqBandQ[b]   .setTargetValue (apvts.getRawParameterValue (qIds[b])->load());
        }
        smoothedEqHpFreq.setTargetValue (apvts.getRawParameterValue (ParameterIDs::EQ_HP_FREQ)->load());
        smoothedEqLpFreq.setTargetValue (apvts.getRawParameterValue (ParameterIDs::EQ_LP_FREQ)->load());
    }

    // Tape loop discrete params (no smoothing needed)
    const float loopLengthParam = apvts.getRawParameterValue(ParameterIDs::LOOP_LENGTH)->load();
    const float loopSpeedBase   = apvts.getRawParameterValue(ParameterIDs::LOOP_SPEED)->load();

    // Tape loop transport state (may be modified by auto-stop)
    bool wantRecord = tapeLoopRecording.load() > 0.5f;
    bool wantPlay   = tapeLoopPlaying.load() > 0.5f;

    // Auto-start playback when recording with existing content (overdub while paused).
    // Without this, feed-to-grain doesn't activate and playback section doesn't run.
    if (wantRecord && tapeLoop.hasContent() && !wantPlay)
        wantPlay = true;
    const float bpm = currentBPM.load();

    // Modulation engine: pick up pending config, set XY, precompute rates
    modulationEngine.setXY(xyPadX.load(std::memory_order_relaxed),
                           xyPadY.load(std::memory_order_relaxed));
    modulationEngine.beginBlock(bpm);
    const bool isFreeform = speedFreeform.load() > 0.5f;

    // Dynamic loop length resizing (once per block, not per sample)
    tapeLoop.updateLength(loopLengthParam, bpm);

    // Per-sample processing
    auto* leftChannel  = buffer.getWritePointer(0);
    auto* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    // ── Per-chop FX-independence (option 1): aggregate active indy masks ─
    // Walk all voices across all 4 layers. For each voice that's currently
    // playing AND has fxIndependent latched at startNote, OR its packed
    // chip mask into activeIndyMask. The indy chain runs once below with
    // this aggregate mask driving per-module bypass.
    std::uint8_t activeIndyMask = 0;
    for (auto& layer : layers)
    {
        auto& synth = layer.synth;
        for (int v = 0; v < synth.getNumVoices(); ++v)
        {
            if (auto* sv = dynamic_cast<tw::SamplerVoice*> (synth.getVoice (v)))
            {
                if (sv->isPlaying() && sv->isFxIndependent())
                    activeIndyMask |= sv->packFxMask();
            }
        }
    }

    // ── Run the indy chain into indySumBuffer ───────────────────────────
    // Grow + clear the sum buffer for this block; snapshot APVTS into
    // ParamTargets; run the chain on indyCaptureBus (which holds whatever
    // the indy voices wrote during layer.synth.renderNextBlock above).
    if (indySumBuffer.getNumSamples() < numSamples)
        indySumBuffer.setSize (2, numSamples, false, true, true);
    indySumBuffer.clear (0, numSamples);

    indyChain.setMask (activeIndyMask);
    indyChain.setParamTargets (snapshotFxParamTargets());
    indyChain.processInto (indyCaptureBus, indySumBuffer, numSamples);

    int scopePos = scopeWritePos.load();

    const bool grainOn = grainEngineEnabled.load() > 0.5f;
    const bool tapeOn = tapeEnabled.load() > 0.5f;
    // Tape loop transport is independent of tape FX (gated separately so
    // disabling the FX section keeps the loop running and vice versa).
    const bool tapeLoopOn = tapeLoopEnabled.load() > 0.5f;
    const int tapeMachineIdx = [&]() {
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ParameterIDs::TAPE_MACHINE)))
            return cp->getIndex();
        return 0;
    }();
    tapeProcessorL.setMachine(tapeMachineIdx);
    tapeProcessorR.setMachine(tapeMachineIdx);

    // Wire-only mode toggles
    const bool wireSpace = wireSpaceNoiseEnabled.load() > 0.5f;
    const bool wireTube = wireTubeSatEnabled.load() > 0.5f;
    tapeProcessorL.setWireModes(wireSpace, wireTube);
    tapeProcessorR.setWireModes(wireSpace, wireTube);

    // Tape Link: when on, route input through ALL THREE tape machines in
    // series (Studio → Cassette → Wire) instead of just the active machine.
    const bool tapeLinkOn = tapeLinkEnabled.load() > 0.5f;

    const bool feedToGrain = tapeLoopFeedToGrain.load() > 0.5f;

    // When feed mode activates, clear the grain engine's circular buffer.
    // Without this, stale live-input data persists (up to 5s) and grains with
    // spray read old piano/input instead of loop content → dry signal leaks
    // into overdub recordings.
    const bool feedActiveNow = feedToGrain && wantPlay && tapeLoop.hasContent();
    if (feedActiveNow && !prevFeedActive)
    {
        grainEngineL.clearBuffer();
        grainEngineR.clearBuffer();
    }
    prevFeedActive = feedActiveNow;

    // Tape section OFF→ON: clear stale filter/reverb state so the first
    // sample after re-enable doesn't slam the biquads / reverb tail with
    // input that's been bypassing them.
    if (tapeOn && !prevTapeOn)
    {
        spaceReverb.reset();
        eqL.reset();
        eqR.reset();
    }
    prevTapeOn = tapeOn;

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance LFOs and compute per-param offsets
        modulationEngine.processSample();

        // Read smoothed base values, apply modulation offsets, then scale
        const float grainSize    = modulationEngine.getModulatedValue(ModulationEngine::pGrainSize,  smoothedGrainSize.getNextValue());
        const float density      = modulationEngine.getModulatedValue(ModulationEngine::pDensity,    smoothedDensity.getNextValue());
        const float spray        = modulationEngine.getModulatedValue(ModulationEngine::pSpray,      smoothedSpray.getNextValue());
        const float pitch        = modulationEngine.getModulatedValue(ModulationEngine::pPitch,      smoothedPitch.getNextValue());
        const float wanderRaw    = modulationEngine.getModulatedValue(ModulationEngine::pWander,     smoothedWander.getNextValue()) * 0.01f;
        const float freezeRaw    = modulationEngine.getModulatedValue(ModulationEngine::pFreeze,     smoothedFreeze.getNextValue()) * 0.01f;
        const float freeze       = std::pow(freezeRaw, 1.5f);
        const float mix          = modulationEngine.getModulatedValue(ModulationEngine::pMix,        smoothedMix.getNextValue());
        const float grainFilterVal = modulationEngine.getModulatedValue(ModulationEngine::pGrainFilter, smoothedGrainFilter.getNextValue());
        const float wowFlutter   = modulationEngine.getModulatedValue(ModulationEngine::pWowFlutter, smoothedWowFlutter.getNextValue()) * 0.01f;
        const float saturationAmt = modulationEngine.getModulatedValue(ModulationEngine::pSaturation, smoothedSaturation.getNextValue()) * 0.01f;
        const float hissAmt      = modulationEngine.getModulatedValue(ModulationEngine::pHiss,       smoothedHiss.getNextValue()) * 0.01f;
        // Wire-specific wow/sat/hiss — modulation-aware (mirrors cassette pattern).
        const float wireWowAmt   = modulationEngine.getModulatedValue (ModulationEngine::pWireWow,        smoothedWireWow .getNextValue()) * 0.01f;
        const float wireSatAmt   = modulationEngine.getModulatedValue (ModulationEngine::pWireSaturation, smoothedWireSat .getNextValue()) * 0.01f;
        const float wireHissAmt  = modulationEngine.getModulatedValue (ModulationEngine::pWireHiss,       smoothedWireHiss.getNextValue()) * 0.01f;
        const float sculptAmt = modulationEngine.getModulatedValue (ModulationEngine::pStudioSculpt, smoothedStudioSculpt.getNextValue()) * 0.01f;
        const float weaveAmt  = modulationEngine.getModulatedValue (ModulationEngine::pStudioWeave,  smoothedStudioWeave .getNextValue()) * 0.01f;
        const float tiltAmt   = modulationEngine.getModulatedValue (ModulationEngine::pStudioTilt,   smoothedStudioTilt  .getNextValue()) * 0.01f;
        const float outputGainDb = smoothedOutputGain.getNextValue();
        const float outputGain   = std::pow(10.0f, outputGainDb / 20.0f);
        const float masterMixAmt = smoothedMasterMix.getNextValue() * 0.01f;
        const float loopFeedback = modulationEngine.getModulatedValue(ModulationEngine::pLoopFeedback, smoothedLoopFeedback.getNextValue()) * 0.01f;
        const float loopDegrade  = modulationEngine.getModulatedValue(ModulationEngine::pLoopDegrade,  smoothedLoopDegrade.getNextValue()) * 0.01f;
        const float loopSpeedParam = modulationEngine.getModulatedValue(ModulationEngine::pLoopSpeed, loopSpeedBase);

        // MF-104S delay params (read once per sample so smoothers advance)
        const float dlyTimeRaw   = modulationEngine.getModulatedValue (ModulationEngine::pDlyTime,      smoothedDlyTime.getNextValue());
        const float dlyFeedback  = modulationEngine.getModulatedValue (ModulationEngine::pDlyFeedback,  smoothedDlyFeedback.getNextValue());
        const float dlyTone      = modulationEngine.getModulatedValue (ModulationEngine::pDlyTone,      smoothedDlyTone.getNextValue());
        const float dlyCharacter = modulationEngine.getModulatedValue (ModulationEngine::pDlyCharacter, smoothedDlyCharacter.getNextValue());
        const float dlyMod       = modulationEngine.getModulatedValue (ModulationEngine::pDlyMod,       smoothedDlyMod.getNextValue());
        const float dlyModRate   = modulationEngine.getModulatedValue (ModulationEngine::pDlyModRate,   smoothedDlyModRate.getNextValue());
        const float dlyMix       = modulationEngine.getModulatedValue (ModulationEngine::pDlyMix,       smoothedDlyMix.getNextValue());
        const float dlyDuck      = modulationEngine.getModulatedValue (ModulationEngine::pDlyDuck,      smoothedDlyDuck.getNextValue());
        // dlyFreeze is a binary gate (manual button or LFO-driven). Using the
        // standard `getModulatedValue > 0.5` threshold meant an assignment at
        // default depth (50%) topped out at 0.25 — never crossing the gate.
        // Treat dlyFreeze as a gate target: engage if manual pressed OR if any
        // assignment's offset is currently positive (LFO above center). Sine
        // bipolar @ depth 50% → 50% duty cycle gate; square → on/off.
        const float dlyFreezeBase   = smoothedDelayFreeze.getNextValue();
        const float dlyFreezeOffset = modulationEngine.getOffset (ModulationEngine::pDlyFreeze);
        const bool  dlyFreezeEngaged = (dlyFreezeBase > 0.5f) || (dlyFreezeOffset > 1.0e-6f);
        const float dlyFreezeRaw    = dlyFreezeEngaged ? 1.0f : 0.0f;

        // Choice params: read raw indices.
        const int dlyModWaveIdx = static_cast<int> (apvts.getRawParameterValue (ParameterIDs::DLY_MOD_WAVE)->load());
        const int dlySyncIdx    = static_cast<int> (apvts.getRawParameterValue (ParameterIDs::DLY_SYNC)->load());
        const int dlySyncDivIdx = static_cast<int> (apvts.getRawParameterValue (ParameterIDs::DLY_SYNC_DIV)->load());
        const int dlyPitchIdx   = static_cast<int> (apvts.getRawParameterValue (ParameterIDs::DLY_PITCH)->load());
        const int dlyWidthIdx   = static_cast<int> (apvts.getRawParameterValue (ParameterIDs::DLY_WIDTH)->load());

        // If sync mode is on, replace TIME with the BPM-derived ms.
        float dlyTimeMs = dlyTimeRaw;
        if (dlySyncIdx == 1)
        {
            // beats per division (matches DLY_SYNC_DIV order):
            //   1/32, 1/16T, 1/16, 1/8T, 1/8., 1/8, 1/4T, 1/4, 1/2
            static constexpr float beatsPerDiv[9] = {
                0.125f,         // 1/32
                0.16667f,       // 1/16T
                0.25f,          // 1/16
                0.33333f,       // 1/8T
                0.75f,          // 1/8.
                0.5f,           // 1/8
                0.66667f,       // 1/4T
                1.0f,           // 1/4
                2.0f            // 1/2
            };
            float bpm = currentBPM.load();
            if (bpm < 20.0f) bpm = 20.0f;
            const int divIdx = (dlySyncDivIdx < 0 || dlySyncDivIdx > 8) ? 5 : dlySyncDivIdx;
            dlyTimeMs = (60000.0f / bpm) * beatsPerDiv[divIdx];
        }

        // Space reverb params (read early so smoothers advance every sample)
        const float spSize  = modulationEngine.getModulatedValue(ModulationEngine::pSpaceSize,  smoothedSpaceSize.getNextValue()) * 0.01f;
        const float spDecay = modulationEngine.getModulatedValue(ModulationEngine::pSpaceDecay, smoothedSpaceDecay.getNextValue()) * 0.01f;
        const float spTone  = modulationEngine.getModulatedValue(ModulationEngine::pSpaceTone,  smoothedSpaceTone.getNextValue()) * 0.01f;
        const float spMix   = modulationEngine.getModulatedValue(ModulationEngine::pSpaceMix,   smoothedSpaceMix.getNextValue()) * 0.01f;

        // Capture dry input before processing
        const float dryL = leftChannel[i];
        const float dryR = rightChannel != nullptr ? rightChannel[i] : 0.0f;

        // Choose grain engine input: live audio OR tape loop feedback (one-sample delay)
        const bool feedActive = feedToGrain && wantPlay && tapeLoop.hasContent();
        const float grainInputL = feedActive ? feedDelayL : leftChannel[i];
        const float grainInputR = feedActive
            ? feedDelayR
            : (rightChannel != nullptr ? rightChannel[i] : leftChannel[i]);

        // When feed-to-grain is active, reduce wander to prevent click compounding
        // (already-wandered signal gets re-wandered → clicks multiply)
        const float wander = feedActive ? wanderRaw * 0.7f : wanderRaw;

        // Signal chain: Input → GrainEngine → GrainFilter → TapeProcessor → TapeLoop → MasterMix → OutputGain
        float wetL = grainOn
            ? grainEngineL.processSample(grainInputL, grainSize, density, spray, pitch, wander, freeze, mix)
            : grainInputL;

        float wetR;
        if (rightChannel != nullptr)
        {
            wetR = grainOn
                ? grainEngineR.processSample(grainInputR, grainSize, density, spray, pitch, wander, freeze, mix)
                : grainInputR;
        }
        else
        {
            wetR = wetL;
        }

        // (Previously a "dry-strip" subtracted grainInput * (1 - mix/100) from
        // wet here to prevent loop content from feeding back through the grain
        // engine's dry path during overdub. That strip killed the legitimate
        // "feed loop through FX without grain" workflow: at grain mix=0 the
        // strip would zero out the entire feed signal so reverb/EQ/tape never
        // received the loop content. Strip removed; loop content now reaches
        // the FX chain regardless of grain mix. Doubling with the tape loop's
        // internal self-feedback (TapeLoopProcessor at slow speeds) is avoided
        // by disabling that internal path whenever externalFeedActive is true.)

        // Grain filter: bipolar one-pole (0=HP, 50=bypass, 100=LP)
        if (grainOn && std::abs(grainFilterVal - 50.f) > 0.5f)
        {
            float cutoff;
            bool isHP;
            if (grainFilterVal < 50.f)
            {
                const float hpNorm = (50.f - grainFilterVal) / 50.f;
                cutoff = 20.f * std::pow(400.f, hpNorm);
                isHP = true;
            }
            else
            {
                const float lpNorm = (grainFilterVal - 50.f) / 50.f;
                cutoff = 20000.f * std::pow(0.01f, lpNorm);
                isHP = false;
            }
            const float alpha = 1.f - std::exp(-6.283185f * cutoff / static_cast<float>(getSampleRate()));
            grainFilterStateL += alpha * (wetL - grainFilterStateL);
            grainFilterStateR += alpha * (wetR - grainFilterStateR);
            if (isHP)
            {
                wetL -= grainFilterStateL;
                wetR -= grainFilterStateR;
            }
            else
            {
                wetL = grainFilterStateL;
                wetR = grainFilterStateR;
            }
        }

        // Signal chain: Grain → Space → EQ → TapeProc → TapeLoop
        // Space + EQ are applied BEFORE the tape loop so all recordings
        // capture the full FX chain (reverb, EQ, and tape character).
        // The whole chain is gated on tapeOn — toggling the tape section
        // off bypasses Space, EQ, the tape processor, and the tape loop.

        // MF-104S delay: lives between grain output and SpaceReverb so reverb
        // tails wash over the repeats. Gated by tapeOn (FX chain master switch).
        if (tapeOn && dlyMix > 0.001f)
        {
            MoogDelay::Params dp;
            dp.timeMs    = dlyTimeMs;
            dp.feedback  = dlyFeedback;
            dp.tone      = dlyTone;
            dp.character = dlyCharacter;
            dp.modDepth  = dlyMod;
            dp.modRateHz = dlyModRate;
            dp.modWave   = dlyModWaveIdx;
            dp.mix       = dlyMix;
            dp.duck      = dlyDuck;
            dp.pitch     = dlyPitchIdx;
            dp.width     = dlyWidthIdx;
            dp.freezeHeld = (dlyFreezeRaw > 0.5f);
            if (delayEnabled.load() > 0.5f)
                moogDelay.processStereo (wetL, wetR, dp);
            // Bypass: wetL/wetR pass through unchanged when delayEnabled is false
        }

        // Space reverb (before tape loop so recordings include reverb)
        if (tapeOn && spMix > 0.001f)
            spaceReverb.processStereo(wetL, wetR, spSize, spDecay, spTone, spMix);

        // Capture pre-tape signal (after Space, before tape effects)
        // Overdub records this — avoids tape compounding
        const float preTapeL = wetL;
        const float preTapeR = wetR;

        if (tapeOn)
        {
            if (tapeLinkOn)
            {
                wetL = tapeProcessorL.processSampleLinked (wetL,
                                                            wowFlutter, saturationAmt, hissAmt,
                                                            wireWowAmt, wireSatAmt, wireHissAmt,
                                                            sculptAmt, weaveAmt, tiltAmt);
                if (rightChannel != nullptr)
                    wetR = tapeProcessorR.processSampleLinked (wetR,
                                                                wowFlutter, saturationAmt, hissAmt,
                                                                wireWowAmt, wireSatAmt, wireHissAmt,
                                                                sculptAmt, weaveAmt, tiltAmt);
                else
                    wetR = wetL;
            }
            else
            {
                wetL = tapeProcessorL.processSample (wetL,
                                                      wowFlutter, saturationAmt, hissAmt,
                                                      wireWowAmt, wireSatAmt, wireHissAmt,
                                                      sculptAmt, weaveAmt, tiltAmt);
                if (rightChannel != nullptr)
                    wetR = tapeProcessorR.processSample (wetR,
                                                          wowFlutter, saturationAmt, hissAmt,
                                                          wireWowAmt, wireSatAmt, wireHissAmt,
                                                          sculptAmt, weaveAmt, tiltAmt);
                else
                    wetR = wetL;
            }
        }

        // Chorus — post-tape, pre-loop
        {
            const float chAmount    = modulationEngine.getModulatedValue (
                ModulationEngine::pChorusAmount,    smoothedChorusAmount.getNextValue());
            const float chWidth     = modulationEngine.getModulatedValue (
                ModulationEngine::pChorusWidth,     smoothedChorusWidth.getNextValue());
            const float chCharacter = modulationEngine.getModulatedValue (
                ModulationEngine::pChorusCharacter, smoothedChorusCharacter.getNextValue());

            const bool chorusOn = chorusEnabled.load() > 0.5f;
            if (chorusOn)
            {
                terrainChorus.setParams (chAmount, chWidth, chCharacter);
                terrainChorus.processStereo (wetL, wetR);
            }
            else
            {
                // Bypass — still advance smoothing so re-engagement isn't a jump
                terrainChorus.setParams (chAmount, chWidth, chCharacter);
            }
        }

        // Tape loop (records fully processed signal on first pass,
        // preTapeL/R on overdub to avoid tape effect compounding).
        // Gated on tapeLoopOn (its own independent flag) — tape FX bypass
        // no longer freezes the loop transport. Resumes from same position
        // on re-enable since tapeLoop preserves its internal write head.
        const float preLoopL = wetL;
        const float preLoopR = wetR;
        if (tapeLoopOn)
            tapeLoop.processStereo(wetL, wetR, wantRecord, wantPlay,
                                   loopLengthParam, loopFeedback, loopDegrade,
                                   loopSpeedParam, bpm, isFreeform,
                                   preTapeL, preTapeR, feedActive);

        if (feedToGrain)
        {
            // Extract loop-only contribution for the feed delay buffer.
            // The feed path runs the loop output back through grain → reverb
            // → EQ → tape on the NEXT sample, accumulating into the overdub
            // buffer if recording is active.
            feedDelayL = wetL - preLoopL;
            feedDelayR = wetR - preLoopR;

            // (Previously stripped wetL = preLoopL here so the user "heard the
            // loop only through grain". That muted direct playback and made
            // the loop inaudible whenever the feed path produced silence — at
            // grain mix=0 with feed first activating, before feedDelay ramps
            // up, or during count-in when tapeLoop returns early. Removed:
            // direct loop playback now always audible. With grain mix high
            // the user will hear loop + grain layered, which is the natural
            // "send" semantics they asked for.)
        }
        else
        {
            feedDelayL = wetL;
            feedDelayR = wetR;
        }

        // Parametric EQ — post-tape, pre-master mix.
        // Independent of tapeOn so the EQ panel works even when the tape
        // section is toggled off. Master gate is EQ_MASTER_BYPASS only.
        // The per-band/per-cut params and analyzer feeds tap around this.
        {
            const float hpF    = smoothedEqHpFreq.getNextValue();
            const float lpF    = smoothedEqLpFreq.getNextValue();
            const auto* hpSlope = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParameterIDs::EQ_HP_SLOPE));
            const auto* lpSlope = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParameterIDs::EQ_LP_SLOPE));
            const bool hpByp  = apvts.getRawParameterValue (ParameterIDs::EQ_HP_BYPASS)->load() > 0.5f;
            const bool lpByp  = apvts.getRawParameterValue (ParameterIDs::EQ_LP_BYPASS)->load() > 0.5f;
            const bool eqByp  = apvts.getRawParameterValue (ParameterIDs::EQ_MASTER_BYPASS)->load() > 0.5f;
            eqL.setMasterBypass (eqByp); eqR.setMasterBypass (eqByp);
            eqL.setHp (hpF, hpSlope ? hpSlope->getIndex() : 1, hpByp);
            eqR.setHp (hpF, hpSlope ? hpSlope->getIndex() : 1, hpByp);
            eqL.setLp (lpF, lpSlope ? lpSlope->getIndex() : 0, lpByp);
            eqR.setLp (lpF, lpSlope ? lpSlope->getIndex() : 0, lpByp);

            for (int b = 0; b < 7; ++b)
            {
                const float bf = modulationEngine.getModulatedValue (ModulationEngine::pEqB1Freq + b * 3, smoothedEqBandFreq[b].getNextValue());
                const float bg = modulationEngine.getModulatedValue (ModulationEngine::pEqB1Gain + b * 3, smoothedEqBandGain[b].getNextValue());
                const float bq = modulationEngine.getModulatedValue (ModulationEngine::pEqB1Q    + b * 3, smoothedEqBandQ[b].getNextValue());
                const bool bByp = apvts.getRawParameterValue (
                                       b == 0 ? ParameterIDs::EQ_B1_BYPASS : b == 1 ? ParameterIDs::EQ_B2_BYPASS :
                                       b == 2 ? ParameterIDs::EQ_B3_BYPASS : b == 3 ? ParameterIDs::EQ_B4_BYPASS :
                                       b == 4 ? ParameterIDs::EQ_B5_BYPASS : b == 5 ? ParameterIDs::EQ_B6_BYPASS :
                                                                                       ParameterIDs::EQ_B7_BYPASS)->load() > 0.5f;
                eqL.setBandParams (b, bf, bg, bq, bByp);
                eqR.setBandParams (b, bf, bg, bq, bByp);
            }

            analyzerPre.pushSample (0.5f * (wetL + wetR));
            wetL = eqL.processSample (wetL);
            if (rightChannel != nullptr) wetR = eqR.processSample (wetR);
            else wetR = wetL;
            analyzerPost.pushSample (0.5f * (wetL + wetR));
        }

        // Master mix + output gain
        float outL = (dryL * (1.0f - masterMixAmt) + wetL * masterMixAmt) * outputGain;
        leftChannel[i] = outL;

        if (rightChannel != nullptr)
        {
            float outR = (dryR * (1.0f - masterMixAmt) + wetR * masterMixAmt) * outputGain;
            rightChannel[i] = outR;
        }

        // Per-chop FX-independence (option 1): read the indy chain's output
        // (which already processed indyCaptureBus through the enabled FX per
        // activeIndyMask above) and mix into master. Same spot as the old
        // indy add-back so master volume + soft-clipper still apply.
        leftChannel[i] += indySumBuffer.getSample (0, i) * outputGain;
        if (rightChannel != nullptr)
            rightChannel[i] += indySumBuffer.getSample (1, i) * outputGain;

        // Master soft-clipper — DAC protection net at -0.3 dBFS.
        // Symmetric tanh: transparent below ~0.4, smooth roll-off, output bounded to (-c, c).
        constexpr float kMasterCeiling = 0.96605f; // 10^(-0.3 / 20)
        leftChannel[i]  = kMasterCeiling * std::tanh (leftChannel[i]  / kMasterCeiling);
        if (rightChannel != nullptr)
            rightChannel[i] = kMasterCeiling * std::tanh (rightChannel[i] / kMasterCeiling);

        // Write to scope buffer (mono mix for visualization)
        float scopeSample = rightChannel != nullptr ? (leftChannel[i] + rightChannel[i]) * 0.5f : leftChannel[i];
        scopeBuffer[static_cast<size_t>(scopePos)].store(scopeSample, std::memory_order_relaxed);
        scopePos = (scopePos + 1) % SCOPE_SIZE;
    }

    // Write final output to rolling capture buffer
    captureBuffer.writeBlock(leftChannel,
        numChannels > 1 ? rightChannel : nullptr, numSamples);

    // Capture the post-FX master into the masterFx ring (in lockstep with the
    // per-layer DRY rings). WET stem export uses energy-ratio attribution
    // against this ring so each layer's WET file carries its proportional
    // share of the shared FX processing.
    writeToMasterFxRing (leftChannel,
                         numChannels > 1 ? rightChannel : leftChannel,
                         numSamples);

    // Sync transport state back (auto-stop may have changed wantRecord/wantPlay)
    tapeLoopRecording.store(wantRecord ? 1.f : 0.f);
    tapeLoopPlaying.store(wantPlay ? 1.f : 0.f);

    // Auto-disable feed-to-grain when recording stops (prevents accidental feedback loops)
    if (prevProcessBlockRecording && !wantRecord)
    {
        tapeLoopFeedToGrain.store(0.f);
        // Flush grain engine circular buffers to purge stale loop content
        // that would otherwise persist for up to 5 seconds after overdub
        grainEngineL.clearBuffer();
        grainEngineR.clearBuffer();
    }
    prevProcessBlockRecording = wantRecord;

    scopeWritePos.store(scopePos);

    // Update grain count for visualization
    activeGrainCount.store(grainEngineL.getActiveGrainCount() + grainEngineR.getActiveGrainCount());
}

//==============================================================================
juce::AudioProcessorEditor* TerrainInstrumentAudioProcessor::createEditor()
{
    return new TerrainInstrumentAudioProcessorEditor(*this);
}

bool TerrainInstrumentAudioProcessor::hasEditor() const { return true; }

const juce::String TerrainInstrumentAudioProcessor::getName() const { return JucePlugin_Name; }
bool TerrainInstrumentAudioProcessor::acceptsMidi() const { return true; }
bool TerrainInstrumentAudioProcessor::producesMidi() const { return false; }
bool TerrainInstrumentAudioProcessor::isMidiEffect() const { return false; }
double TerrainInstrumentAudioProcessor::getTailLengthSeconds() const { return 5.0; }

int TerrainInstrumentAudioProcessor::getNumPrograms() { return static_cast<int>(presets.size()); }
int TerrainInstrumentAudioProcessor::getCurrentProgram() { return currentPresetIndex.load(); }
void TerrainInstrumentAudioProcessor::setCurrentProgram (int index) { loadPreset(index); }
const juce::String TerrainInstrumentAudioProcessor::getProgramName (int index) { return getPresetName(index); }
void TerrainInstrumentAudioProcessor::changeProgramName (int, const juce::String&) {}

void TerrainInstrumentAudioProcessor::setLoadedSamplePath (const juce::String& path)
{
    juce::ScopedLock sl (sampleSourcePathLock);
    loadedSamplePath = path;
}

juce::String TerrainInstrumentAudioProcessor::getLoadedSamplePath() const
{
    juce::ScopedLock sl (sampleSourcePathLock);
    return loadedSamplePath;
}

// ── Mix page Phase D: rolling stem buffers ───────────────────────────────────

void TerrainInstrumentAudioProcessor::allocateStemBuffers (double sampleRate)
{
    const int totalSamples = juce::jmax (1, (int) (sampleRate * (double) kStemSeconds));
    for (auto& s : stemBuffers)
    {
        s.ring.setSize (2, totalSamples, false, true, true);
        s.ring.clear();
        s.totalSize = totalSamples;
        s.writeIndex.store     (0, std::memory_order_relaxed);
        s.samplesWritten.store (0, std::memory_order_relaxed);
    }
    masterFxBuffer.ring.setSize (2, totalSamples, false, true, true);
    masterFxBuffer.ring.clear();
    masterFxBuffer.totalSize = totalSamples;
    masterFxBuffer.writeIndex.store     (0, std::memory_order_relaxed);
    masterFxBuffer.samplesWritten.store (0, std::memory_order_relaxed);
}

void TerrainInstrumentAudioProcessor::writeToMasterFxRing (const float* L,
                                                            const float* R,
                                                            int numSamples)
{
    if (numSamples <= 0) return;
    auto& s = masterFxBuffer;
    if (s.totalSize <= 0) return;

    int w = s.writeIndex.load (std::memory_order_relaxed);
    auto* destL = s.ring.getWritePointer (0);
    auto* destR = s.ring.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        destL[w] = L[i];
        destR[w] = R[i];
        if (++w >= s.totalSize) w = 0;
    }
    s.writeIndex.store (w, std::memory_order_relaxed);
    const int prev = s.samplesWritten.load (std::memory_order_relaxed);
    s.samplesWritten.store (juce::jmin (s.totalSize, prev + numSamples),
                             std::memory_order_relaxed);
}

void TerrainInstrumentAudioProcessor::writeToStemBuffer (int layerIdx,
                                                          const float* L,
                                                          const float* R,
                                                          int numSamples)
{
    if (layerIdx < 0 || layerIdx > 3 || numSamples <= 0) return;
    auto& s = stemBuffers[(size_t) layerIdx];
    if (s.totalSize <= 0) return;

    // Live capture-level meter: decaying peak of what's being written. Read by
    // the UI at ~30Hz to drive the 4 mini-meters on the stem row.
    {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
        const float prev = stemCaptureLevel[(size_t) layerIdx].load (std::memory_order_relaxed);
        stemCaptureLevel[(size_t) layerIdx].store (juce::jmax (peak, prev * 0.92f),
                                                    std::memory_order_relaxed);
    }

    int w = s.writeIndex.load (std::memory_order_relaxed);
    auto* destL = s.ring.getWritePointer (0);
    auto* destR = s.ring.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        destL[w] = L[i];
        destR[w] = R[i];
        if (++w >= s.totalSize) w = 0;
    }
    s.writeIndex.store (w, std::memory_order_relaxed);
    // Saturate samplesWritten at totalSize so we know when the ring is fully populated.
    const int prev = s.samplesWritten.load (std::memory_order_relaxed);
    s.samplesWritten.store (juce::jmin (s.totalSize, prev + numSamples),
                             std::memory_order_relaxed);
}

void TerrainInstrumentAudioProcessor::clearStemBuffers()
{
    for (int i = 0; i < 4; ++i)
    {
        auto& s = stemBuffers[(size_t) i];
        if (s.totalSize > 0) s.ring.clear();
        s.writeIndex.store     (0, std::memory_order_relaxed);
        s.samplesWritten.store (0, std::memory_order_relaxed);
        stemCaptureLevel[(size_t) i].store (0.0f, std::memory_order_relaxed);
    }
    if (masterFxBuffer.totalSize > 0) masterFxBuffer.ring.clear();
    masterFxBuffer.writeIndex.store     (0, std::memory_order_relaxed);
    masterFxBuffer.samplesWritten.store (0, std::memory_order_relaxed);
}

juce::File TerrainInstrumentAudioProcessor::exportStemToFile (int layerIdx, const juce::File& dest)
{
    if (layerIdx < 0 || layerIdx > 3) return {};
    auto& s = stemBuffers[(size_t) layerIdx];
    if (s.totalSize <= 0) return {};

    // Only export what's actually been captured. Below totalSize, the ring
    // hasn't wrapped yet — audio sits chronologically at [0..captured-1] and
    // we skip the silent tail. Once captured == totalSize, full rolling unwrap.
    const int captured = s.samplesWritten.load (std::memory_order_relaxed);
    if (captured <= 0) return {};                       // nothing recorded yet

    // Snapshot current write position. Audio thread may continue writing during
    // the unwrap loop below — at most one sample tear at the wrap boundary,
    // inaudible in practice.
    const int writeIdx = s.writeIndex.load (std::memory_order_relaxed);

    const int  mode     = stemSourceMode.load();
    const bool ringFull = (captured >= s.totalSize);
    const int  outLen   = ringFull ? s.totalSize : captured;

    juce::AudioBuffer<float> out (2, outLen);
    auto* dstL = out.getWritePointer (0);
    auto* dstR = out.getWritePointer (1);

    if (mode == 0)   // DRY — raw per-layer ring, no gain.
    {
        const auto* srcL = s.ring.getReadPointer (0);
        const auto* srcR = s.ring.getReadPointer (1);
        if (ringFull)
        {
            int srcIdx = writeIdx;
            for (int i = 0; i < outLen; ++i)
            {
                dstL[i] = srcL[srcIdx];
                dstR[i] = srcR[srcIdx];
                if (++srcIdx >= s.totalSize) srcIdx = 0;
            }
        }
        else
        {
            for (int i = 0; i < outLen; ++i) { dstL[i] = srcL[i]; dstR[i] = srcR[i]; }
        }
    }
    else             // WET — attribute the post-FX master ring back to this layer
                     // by per-sample energy ratio (computed from all 4 dry rings
                     // weighted by each layer's volume). Sum of the 4 layers'
                     // WET stems == the master FX output.
    {
        const auto* mL = masterFxBuffer.ring.getReadPointer (0);
        const auto* mR = masterFxBuffer.ring.getReadPointer (1);
        const float* dryL[4]; const float* dryR[4];
        float        vol[4];
        for (int j = 0; j < 4; ++j)
        {
            dryL[j] = stemBuffers[(size_t) j].ring.getReadPointer (0);
            dryR[j] = stemBuffers[(size_t) j].ring.getReadPointer (1);
            vol[j]  = juce::jmax (0.0f, layers[(size_t) j].volume.load());
        }
        constexpr float kEps = 1.0e-7f;
        for (int i = 0; i < outLen; ++i)
        {
            const int srcIdx = ringFull ? ((writeIdx + i) % s.totalSize) : i;
            float total = kEps;
            float strengths[4];
            for (int j = 0; j < 4; ++j)
            {
                strengths[j] = (std::abs (dryL[j][srcIdx]) + std::abs (dryR[j][srcIdx])) * vol[j];
                total       += strengths[j];
            }
            const float ratio = strengths[layerIdx] / total;
            dstL[i] = mL[srcIdx] * ratio;
            dstR[i] = mR[srcIdx] * ratio;
        }
    }

    // Filename: Stem-<A/B/C/D>-YYYYMMDD-HHMMSS-{DRY|MIX}.wav
    const auto letter    = juce::String::charToString ((juce::juce_wchar)('A' + layerIdx));
    const auto timestamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    const auto suffix    = (mode == 1) ? juce::String("-WET") : juce::String("-DRY");
    const auto file      = dest.getChildFile ("Stem-" + letter + "-" + timestamp + suffix + ".wav");

    if (! dest.exists()) dest.createDirectory();

    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr || ! stream->openedOk()) return {};
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
        stream.get(), sr, 2 /*channels*/, 24 /*bit depth*/, {}, 0));
    if (writer == nullptr) return {};
    stream.release();  // writer takes ownership
    writer->writeFromAudioSampleBuffer (out, 0, outLen);
    // writer destructor finalizes the WAV chunks on scope exit
    return file;
}

void TerrainInstrumentAudioProcessor::setCachedSamplePayload (const juce::String& jsonPayload, int layerIdx)
{
    if (layerIdx < 0)         layerIdx = editingLayer.load();
    if (layerIdx < 0 || layerIdx > 3) return;
    juce::ScopedLock sl (samplePayloadLock);
    cachedLayerPayloads[(size_t) layerIdx] = jsonPayload;
}

juce::String TerrainInstrumentAudioProcessor::getCachedSamplePayload (int layerIdx) const
{
    if (layerIdx < 0)         layerIdx = editingLayer.load();
    if (layerIdx < 0 || layerIdx > 3) return {};
    juce::ScopedLock sl (samplePayloadLock);
    return cachedLayerPayloads[(size_t) layerIdx];
}

std::array<juce::String, 4> TerrainInstrumentAudioProcessor::getAllLayerPayloads() const
{
    juce::ScopedLock sl (samplePayloadLock);
    return cachedLayerPayloads;   // returns a copy under the lock
}

//==============================================================================
// Slicer state management
//==============================================================================
void TerrainInstrumentAudioProcessor::replaceSlices (tw::SliceList newSlices)
{
    // Task 5: routes through layers[editingLayer] instead of singleton slicesPtr/activeSliceIndex/synth.
    const size_t el = (size_t) editingLayer.load();
    auto snapshot = std::make_shared<const tw::SliceList> (std::move (newSlices));
    std::atomic_store (&layers[el].currentSlices, tw::SliceListPtr (snapshot));

    // Clamp activeSliceIndex to new range.
    const int n = (int) snapshot->size();
    int idx = layers[el].activeSliceIndex.load();
    if (n == 0) idx = 0;
    else if (idx >= n) idx = n - 1;
    else if (idx < 0) idx = 0;
    layers[el].activeSliceIndex.store (idx);

    // Push fresh slice bounds into the warp cache so prewarm() callers
    // that fire shortly after (e.g. setSliceScanEnabled native fn) have
    // valid bounds for the new layout.
    //
    // NOTE: do NOT call invalidateSlice() here. setSliceBounds() already
    // drops cache entries whose bounds changed (the only mutation that
    // makes a cached render stale). Calling invalidateSlice()
    // unconditionally was discarding renders-in-flight triggered by the
    // immediately-preceding prewarm() call in setSliceScanEnabled — the
    // race caused a cache-miss-every-note loop on Warp+Scan chops.
    for (int i = 0; i < n; ++i)
    {
        const auto& s = (*snapshot)[(size_t) i];
        layers[el].synth.warpCache.setSliceBounds (i, s.startSample, s.endSample);
    }
}

tw::SliceListPtr TerrainInstrumentAudioProcessor::loadSlices() const
{
    // Task 5: routes through layers[editingLayer].currentSlices.
    const size_t el = (size_t) editingLayer.load();
    return std::atomic_load (&layers[el].currentSlices);
}

int TerrainInstrumentAudioProcessor::getNumSlices() const
{
    auto p = loadSlices();
    return p ? (int) p->size() : 0;
}

juce::String TerrainInstrumentAudioProcessor::getSlicesJson() const
{
    auto p = loadSlices();
    if (! p) return "{\"slices\":[]}";
    return tw::slicesToJson (*p);
}

void TerrainInstrumentAudioProcessor::setSlicesFromJson (const juce::String& json)
{
    replaceSlices (tw::slicesFromJson (json));
}

juce::String TerrainInstrumentAudioProcessor::getPitchSliceJson() const
{
    // Serialise pitchModeSlice as a single-element object so JS can use
    // a consistent schema with no special-casing on the network boundary.
    // Task 5: routes through layers[editingLayer].pitchModeSlice.
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    const size_t el = (size_t) editingLayer.load();
    const auto& s = layers[el].pitchModeSlice;
    obj->setProperty ("startSample",  (juce::int64) s.startSample);
    obj->setProperty ("endSample",    (juce::int64) s.endSample);
    obj->setProperty ("reverse",      s.reverse);
    obj->setProperty ("pitch",        (double) s.pitchOffsetSemis);
    obj->setProperty ("warpMode",     (int) s.warpMode);
    obj->setProperty ("stretchRatio", (double) s.stretchRatio);
    obj->setProperty ("attackMs",     (double) s.attackMs);
    obj->setProperty ("releaseMs",    (double) s.releaseMs);
    obj->setProperty ("decayMs",      (double) s.decayMs);
    obj->setProperty ("sustainLevel", (double) s.sustainLevel);
    obj->setProperty ("volume",       (double) s.volume);
    obj->setProperty ("scanEnabled",  s.scanEnabled);
    obj->setProperty ("scanRate",     (double) s.scanRate);
    obj->setProperty ("scanWindow",   (double) s.scanWindow);
    return juce::JSON::toString (juce::var (obj.get()), true);
}

juce::var TerrainInstrumentAudioProcessor::snapshotSliceGlowLevels() const
{
    // Task 5: reads from layers[editingLayer].sliceGlowLevel (singleton removed).
    const size_t el = (size_t) editingLayer.load();
    const int n = juce::jmin (getNumSlices(), tw::kMaxGlowSlots);
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated (n);
    for (int i = 0; i < n; ++i)
        arr.add ((double) layers[el].sliceGlowLevel[(size_t) i].load (std::memory_order_relaxed));
    return juce::var (arr);
}

float TerrainInstrumentAudioProcessor::getScanPosition (int sliceIndex) const noexcept
{
    // Task 5: routes through layers[editingLayer].synth.
    const size_t el = (size_t) editingLayer.load();
    for (int i = 0; i < layers[el].synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<const tw::SamplerVoice*> (layers[el].synth.getVoice (i)))
        {
            if (v->isPlaying()
                && v->getSliceIndex() == sliceIndex
                && v->isScanActive())
            {
                return v->getScanPositionNormalized();
            }
        }
    }
    return -1.0f;
}

TerrainInstrumentAudioProcessor::ScanWindowBounds
TerrainInstrumentAudioProcessor::getScanWindowBounds (int sliceIndex) const noexcept
{
    // Task 5: routes through layers[editingLayer].synth.
    const size_t el = (size_t) editingLayer.load();
    for (int i = 0; i < layers[el].synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<const tw::SamplerVoice*> (layers[el].synth.getVoice (i)))
        {
            if (v->isPlaying()
                && v->getSliceIndex() == sliceIndex
                && v->isScanActive())
            {
                const float window = v->getScanWindowLive();
                const float margin = (1.0f - window) * 0.5f;
                return { margin, 1.0f - margin };
            }
        }
    }
    return { 0.0f, 1.0f };
}

void TerrainInstrumentAudioProcessor::auditionSlice (int sliceIndex)
{
    const int n = getNumSlices();
    if (sliceIndex < 0 || sliceIndex >= n) return;

    const juce::SpinLock::ScopedLockType sl (auditionLock);
    auditionQueue.push_back (sliceIndex);
}

//==============================================================================
void TerrainInstrumentAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // DAW state: parameter values + preset index + XY auto state
    // Presets themselves live on disk only (getUserPresetsFile)
    auto state = apvts.copyState();
    state.setProperty("presetIndex",      currentPresetIndex.load(),  nullptr);
    state.setProperty("xyAutoEnabled",    xyAutoEnabled.load(),     nullptr);
    state.setProperty("xyAutoMode",       xyAutoMode.load(),        nullptr);
    state.setProperty("xyAutoSpeed",      xyAutoSpeed.load(),       nullptr);
    state.setProperty("grainSyncEnabled",  grainSyncEnabled.load(),  nullptr);
    state.setProperty("grainEngineEnabled", grainEngineEnabled.load(), nullptr);
    state.setProperty("tapeEnabled",        tapeEnabled.load(),        nullptr);
    state.setProperty("tapeLoopEnabled",    tapeLoopEnabled.load(),    nullptr);
    state.setProperty("wanderLinked",        wanderLinked.load(),        nullptr);
    state.setProperty("wireSpaceNoise",     wireSpaceNoiseEnabled.load(), nullptr);
    state.setProperty("wireTubeSat",        wireTubeSatEnabled.load(),    nullptr);
    state.setProperty("tapeLinkEnabled",    tapeLinkEnabled.load(),       nullptr);
    state.setProperty("chorusEnabled",      chorusEnabled.load(),         nullptr);
    state.setProperty("delayEnabled",       delayEnabled.load(),          nullptr);
    if (modStateJson.isNotEmpty())
        state.setProperty("modStateJson", modStateJson, nullptr);

    // ── V2 format marker ─────────────────────────────────────────────────────
    // Task 12: introduce version=2 and editingLayer so Task 13 (setStateInformation)
    // can distinguish V1 blobs (no "version" property) from V2 blobs.
    state.setProperty ("version",      2,                   nullptr);
    state.setProperty ("editingLayer", editingLayer.load(), nullptr);
    // Mix page Phase 2: global trigger-mode state at the root level.
    state.setProperty ("triggerMode",     triggerMode.load(),     nullptr);
    state.setProperty ("rrSyncToBar",     (bool) rrSyncToBar.load(), nullptr);
    state.setProperty ("rrShuffle",       (bool) rrShuffle.load(), nullptr);
    state.setProperty ("layerMorph",      (double) layerMorph.load(), nullptr);
    state.setProperty ("stemSourceMode",  stemSourceMode.load(),  nullptr);

    // ── V2: per-layer state node array ───────────────────────────────────────
    // Helper lambda — serialises a pitchModeSlice into the same JSON string
    // format that V1 used, so Task 13 can reuse the identical parse path
    // when restoring per-layer pitchModeSlice from V2.
    auto pitchSliceToJson = [] (const tw::Slice& s) -> juce::String
    {
        juce::DynamicObject::Ptr pObj = new juce::DynamicObject();
        pObj->setProperty ("startSample",  (juce::int64) s.startSample);
        pObj->setProperty ("endSample",    (juce::int64) s.endSample);
        pObj->setProperty ("reverse",      s.reverse);
        pObj->setProperty ("pitch",        (double) s.pitchOffsetSemis);
        pObj->setProperty ("warpMode",     (int) s.warpMode);
        pObj->setProperty ("stretchRatio", (double) s.stretchRatio);
        pObj->setProperty ("attackMs",     (double) s.attackMs);
        pObj->setProperty ("releaseMs",    (double) s.releaseMs);
        pObj->setProperty ("decayMs",      (double) s.decayMs);
        pObj->setProperty ("sustainLevel", (double) s.sustainLevel);
        pObj->setProperty ("volume",       (double) s.volume);
        pObj->setProperty ("scanEnabled",  s.scanEnabled);
        pObj->setProperty ("scanRate",     (double) s.scanRate);
        pObj->setProperty ("scanWindow",   (double) s.scanWindow);
        return juce::JSON::toString (juce::var (pObj.get()), true);
    };

    juce::ValueTree layersTree ("layers");
    for (size_t li = 0; li < layers.size(); ++li)
    {
        const auto& L = layers[li];

        juce::ValueTree layerNode ("layer");
        layerNode.setProperty ("index",            (int) li,                    nullptr);
        layerNode.setProperty ("sourcePath",       L.sourcePath,                nullptr);
        layerNode.setProperty ("sourceFileName",   L.sourceFileName,            nullptr);
        layerNode.setProperty ("activeSliceIndex", L.activeSliceIndex.load(),   nullptr);
        layerNode.setProperty ("rootMidiNote",     L.rootMidiNote.load(),       nullptr);
        layerNode.setProperty ("sliceMode",        L.sliceMode.load(),          nullptr);
        layerNode.setProperty ("playMode",         L.playMode.load(),           nullptr);
        layerNode.setProperty ("sliceCount",       L.sliceCount.load(),         nullptr);
        layerNode.setProperty ("chopFadeMs",       L.chopFadeMs.load(),         nullptr);
        layerNode.setProperty ("volume",           L.volume.load(),             nullptr);
        layerNode.setProperty ("mute",             (bool) L.mute.load(),        nullptr);
        layerNode.setProperty ("solo",             (bool) L.solo.load(),        nullptr);
        // Mix page Phase 2: per-layer creative-routing + mixer fields.
        layerNode.setProperty ("pan",              (double) L.pan.load(),               nullptr);
        layerNode.setProperty ("pitchJitterCents", (double) L.pitchJitterCents.load(),  nullptr);
        layerNode.setProperty ("probabilityWeight",(double) L.probabilityWeight.load(), nullptr);
        layerNode.setProperty ("keyZoneMin",       L.keyZoneMin.load(),                 nullptr);
        layerNode.setProperty ("keyZoneMax",       L.keyZoneMax.load(),                 nullptr);
        layerNode.setProperty ("velocityZoneMin",  L.velocityZoneMin.load(),            nullptr);
        layerNode.setProperty ("velocityZoneMax",  L.velocityZoneMax.load(),            nullptr);

        // slicesJson — same JSON format as V1 (slicesToJson produces
        // {"slices":[...]}), so Task 13's parse path is identical.
        const auto layerSlices = std::atomic_load (&L.currentSlices);
        const auto sliceJson = layerSlices ? tw::slicesToJson (*layerSlices)
                                           : juce::String ("{\"slices\":[]}");
        layerNode.setProperty ("slicesJson", sliceJson, nullptr);

        // pitchSliceJson — same JSON format as V1.
        layerNode.setProperty ("pitchSliceJson", pitchSliceToJson (L.pitchModeSlice), nullptr);

        layersTree.addChild (layerNode, -1, nullptr);
    }
    state.addChild (layersTree, -1, nullptr);

    // ── V1 root-level properties — KEPT for backward compat ──────────────────
    // These duplicate the layer-A data so that:
    //   (a) An older build that loads a V2 blob via the pre-Task-13 setStateInformation
    //       still finds sampleSourcePath / slicesJson / pitchSliceJson / holdMode at
    //       the root and restores layer A correctly (no crash, partial restore).
    //   (b) Task 13 migration code can detect V1 vs V2 by checking "version".
    //
    // When Task 13 is complete, the V1 root-level read path will be replaced by
    // the V2 layers[] parse path. The V1 properties can be removed from saves
    // once there are no V1 users left.
    {
        // Full path for the V1 sample-path restore path (layers[0] only).
        if (layers[0].sourcePath.isNotEmpty())
            state.setProperty ("sampleSourcePath", layers[0].sourcePath, nullptr);
        else
        {
            // Fallback: processor-level singleton (unchanged for pure-V1 instances
            // that never went through Task 12's editor path).
            juce::ScopedLock sl (sampleSourcePathLock);
            if (loadedSamplePath.isNotEmpty())
                state.setProperty ("sampleSourcePath", loadedSamplePath, nullptr);
        }

        // V1 slicesJson / activeSliceIndex from layer A (same data as layersTree[0]).
        const auto layerASlices = std::atomic_load (&layers[0].currentSlices);
        const auto sliceJson = layerASlices ? tw::slicesToJson (*layerASlices)
                                            : juce::String ("{\"slices\":[]}");
        if (sliceJson.isNotEmpty() && sliceJson != "{\"slices\":[]}")
            state.setProperty ("slicesJson", sliceJson, nullptr);
        state.setProperty ("activeSliceIndex", layers[0].activeSliceIndex.load(), nullptr);

        // V1 pitchSliceJson from layer A.
        state.setProperty ("pitchSliceJson", pitchSliceToJson (layers[0].pitchModeSlice), nullptr);

        // HOLD mode — global (not yet per-layer).
        state.setProperty ("holdMode", (bool) holdMode.load(), nullptr);
    }

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TerrainInstrumentAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            auto newState = juce::ValueTree::fromXml (*xmlState);

            // Restore preset index (clamped to valid range after disk presets load)
            int presetIdx = newState.getProperty("presetIndex", 0);

            // Restore XY auto state
            xyAutoEnabled.store(static_cast<float>(newState.getProperty("xyAutoEnabled", 0.f)));
            xyAutoMode.store(static_cast<float>(newState.getProperty("xyAutoMode", 0.f)));
            xyAutoSpeed.store(static_cast<float>(newState.getProperty("xyAutoSpeed", 0.5f)));

            // Restore grain sync state
            grainSyncEnabled.store(static_cast<float>(newState.getProperty("grainSyncEnabled", 0.f)));

            // Restore grain engine on/off, tape on/off, and drift link states
            grainEngineEnabled.store(static_cast<float>(newState.getProperty("grainEngineEnabled", 1.f)));
            tapeEnabled.store(static_cast<float>(newState.getProperty("tapeEnabled", 1.f)));
            tapeLoopEnabled.store(static_cast<float>(newState.getProperty("tapeLoopEnabled", 1.f)));
            wanderLinked.store(static_cast<float>(newState.getProperty("wanderLinked", 1.f)));
            wireSpaceNoiseEnabled.store(static_cast<float>(newState.getProperty("wireSpaceNoise", 0.f)));
            wireTubeSatEnabled.store(static_cast<float>(newState.getProperty("wireTubeSat", 0.f)));
            tapeLinkEnabled.store(static_cast<float>(newState.getProperty("tapeLinkEnabled", 0.f)));
            chorusEnabled.store(static_cast<float>(newState.getProperty("chorusEnabled", 1.f)));
            delayEnabled.store(static_cast<float>(newState.getProperty("delayEnabled", 1.f)));
            modStateJson = newState.getProperty("modStateJson", "").toString();
            if (modStateJson.isNotEmpty())
                modulationEngine.updateConfig(ModulationEngine::parseJSON(modStateJson));

            // ── Task 13: V1 / V2 branching ────────────────────────────────────
            // V2 blobs (saved by Task 12) carry version=2 and a "layers" child tree.
            // V1 blobs (saved by pre-Task-12 builds) have no "version" property and
            // carry the sample state at the root level (sampleSourcePath, slicesJson,
            // pitchSliceJson, activeSliceIndex).
            const int version = (int) newState.getProperty ("version", 1);

            if (version >= 2)
            {
                loadV2State (newState);
                // Restore editingLayer from V2 blob (clamped to 0..3).
                editingLayer.store (juce::jlimit (0, 3, (int) newState.getProperty ("editingLayer", 0)));
                // Mix page Phase 2: global trigger-mode state. Defaults to LAYER mode
                // (and DRY stems / RR-not-bar-synced) if absent so V2 blobs from
                // pre-Phase-2 builds open cleanly into the new feature defaults.
                triggerMode.store    (juce::jlimit (0, 4, (int) newState.getProperty ("triggerMode",    0)));
                rrSyncToBar.store    ((bool) newState.getProperty ("rrSyncToBar",  false));
                rrShuffle.store      ((bool) newState.getProperty ("rrShuffle",    false));
                layerMorph.store     (juce::jlimit (0.0f, 1.0f, (float)(double) newState.getProperty ("layerMorph", 0.5)));
                stemSourceMode.store (juce::jlimit (0, 1, (int) newState.getProperty ("stemSourceMode", 0)));
            }
            else
            {
                loadV1State (newState);
                editingLayer.store (0);
            }

            // HOLD mode — global (not yet per-layer). Applies to both V1 and V2.
            holdMode.store ((bool) newState.getProperty ("holdMode", false),
                            std::memory_order_relaxed);

            // Reload presets from disk (the single source of truth)
            while (static_cast<int>(presets.size()) > numFactoryPresets)
                presets.pop_back();
            loadUserPresetsFromFile();

            // Clamp preset index to valid range
            if (presetIdx >= static_cast<int>(presets.size()))
                presetIdx = 0;
            currentPresetIndex.store(presetIdx);

            apvts.replaceState (newState);

            // Mark 2 Phase 1 audio-fix: V1 backward-compat. V1 presets carry
            // sliceMode and sampleLoopMode only in APVTS (global). Engine now
            // reads from per-layer atomics. Seed layers[0] from the freshly
            // loaded APVTS values so V1 presets keep their SLICE/PITCH and
            // 1-SHOT/LOOP selections. V2 presets already populated per-layer
            // in loadV2State.
            if (version < 2)
            {
                const int v1SliceMode = (int) *apvts.getRawParameterValue (ParameterIDs::SLICE_MODE);
                const int v1LoopMode  = (int) *apvts.getRawParameterValue (ParameterIDs::SAMPLE_LOOP_MODE);
                layers[0].sliceMode.store      (v1SliceMode);
                layers[0].sampleLoopMode.store (v1LoopMode);
            }
        }
    }
}

// ── Task 13: pitchSliceJson → LayerState helper ───────────────────────────────
// Shared by loadV1State and loadV2State.  Reads the JSON string produced by the
// pitchSliceToJson lambda in getStateInformation and writes the fields into the
// provided LayerState's pitchModeSlice.  No-op if the string is empty or invalid.
/*static*/ void TerrainInstrumentAudioProcessor::applyPitchSliceJson (
        const juce::String& psJson, tw::LayerState& layer)
{
    if (psJson.isEmpty()) return;

    auto pv = juce::JSON::parse (psJson);
    if (! pv.isObject()) return;

    auto& ps = layer.pitchModeSlice;
    ps.reverse          = (bool) pv.getProperty ("reverse", false);
    ps.pitchOffsetSemis = juce::jlimit (-12.0f, 12.0f,
                              (float)(double) pv.getProperty ("pitch", 0.0));
    const int wmRaw     = (int) pv.getProperty ("warpMode", 0);
    ps.warpMode         = (wmRaw >= 0 && wmRaw <= 3)
                              ? static_cast<tw::WarpMode> (wmRaw)
                              : tw::WarpMode::None;
    ps.stretchRatio     = juce::jlimit (0.1f, 15.0f,
                              (float)(double) pv.getProperty ("stretchRatio", 1.0));
    const double atkRaw = (double) pv.getProperty ("attackMs",  -1.0);
    const double relRaw = (double) pv.getProperty ("releaseMs", -1.0);
    ps.attackMs         = atkRaw < 0.0 ? -1.0f : juce::jlimit (0.0f, 2000.0f, (float) atkRaw);
    ps.releaseMs        = relRaw < 0.0 ? -1.0f : juce::jlimit (1.0f, 5000.0f, (float) relRaw);
    ps.decayMs          = juce::jlimit (0.0f, 2000.0f,
                              (float)(double) pv.getProperty ("decayMs",      0.0));
    ps.sustainLevel     = juce::jlimit (0.0f, 1.0f,
                              (float)(double) pv.getProperty ("sustainLevel", 1.0));
    ps.volume           = juce::jlimit (0.0f, 2.0f,
                              (float)(double) pv.getProperty ("volume",       1.0));
    ps.scanEnabled      = (bool) pv.getProperty ("scanEnabled", false);
    float scRt = (float)(double) pv.getProperty ("scanRate",   0.0);
    float scWn = (float)(double) pv.getProperty ("scanWindow", 0.0);
    ps.scanRate         = (scRt < 0.05f) ? 1.0f : juce::jlimit (0.1f, 8.0f,  scRt);
    ps.scanWindow       = (scWn < 0.04f) ? 1.0f : juce::jlimit (0.05f, 1.0f, scWn);

    // Restore pitch-mode in/out bounds if serialised.  If absent (old state),
    // leave as-is — a subsequent sample file load will overwrite with [0, N].
    const juce::int64 savedStart = (juce::int64)(double) pv.getProperty ("startSample", (juce::int64)-1);
    const juce::int64 savedEnd   = (juce::int64)(double) pv.getProperty ("endSample",   (juce::int64)-1);
    if (savedStart >= 0 && savedEnd > savedStart)
    {
        ps.startSample = savedStart;
        ps.endSample   = savedEnd;
        // Register restored bounds with the WarpRenderCache under sliceIndex=-1
        // (pitch-mode sentinel) so warp+scan in pitch mode has valid bounds
        // immediately after state reload without waiting for a file load.
        layer.synth.warpCache.setSliceBounds (-1, (int) savedStart, (int) savedEnd);
    }
}

// ── Task 13: V1 state loader ──────────────────────────────────────────────────
// Reads the pre-Mark-2 blob layout: single sample at the root level.
// Populates layers[0]; clears layers[1..3] so they show as empty.
void TerrainInstrumentAudioProcessor::loadV1State (const juce::ValueTree& loaded)
{
    // Clear all 4 layers first so layers[1..3] show as empty after a V1 load.
    for (auto& L : layers)
    {
        L.sourceFileName = juce::String();
        L.sourcePath     = juce::String();
        // Drop the sample buffer (atomic store of nullptr).
        L.sampleBuffer.store (tw::SampleBuffer::BufferPtr{});
        // Drop slice list.
        std::atomic_store (&L.currentSlices, tw::SliceListPtr{});
        // Reset pitchModeSlice to default-constructed state.
        L.pitchModeSlice = tw::Slice{};
        L.activeSliceIndex.store (0);
        L.volume.store (1.0f);
        L.mute.store   (false);
        L.solo.store   (false);
    }

    // ── Populate layer A (index 0) from root-level V1 properties ─────────────
    auto& A = layers[0];

    // V1 stored the path under "sampleSourcePath" at the root.
    A.sourcePath     = loaded.getProperty ("sampleSourcePath", "").toString();
    A.sourceFileName = loaded.getProperty ("sourceFileName",   "").toString();

    // Persist the path via the legacy singleton so the editor constructor's
    // V1 reload path (getLoadedSamplePath → loadSampleAsync for layer 0) fires.
    {
        juce::ScopedLock sl (sampleSourcePathLock);
        loadedSamplePath = A.sourcePath;
    }

    // Slices.
    const juce::String sliceJson = loaded.getProperty ("slicesJson", "").toString();
    if (sliceJson.isNotEmpty())
    {
        auto sl = std::make_shared<const tw::SliceList> (tw::slicesFromJson (sliceJson));
        std::atomic_store (&A.currentSlices, tw::SliceListPtr (sl));

        // Push slice bounds into the warp cache (same as replaceSlices does,
        // but writing directly to layer[0] instead of routing through editingLayer).
        for (int i = 0; i < (int) sl->size(); ++i)
        {
            const auto& s = (*sl)[(size_t) i];
            A.synth.warpCache.setSliceBounds (i, (int) s.startSample, (int) s.endSample);
        }
    }

    A.activeSliceIndex.store ((int) loaded.getProperty ("activeSliceIndex", 0));

    // pitchModeSlice — uses the shared helper.
    applyPitchSliceJson (loaded.getProperty ("pitchSliceJson", "").toString(), A);

    // Mode/play/count come via APVTS (already replaced by the caller).
}

// ── Task 13: V2 state loader ──────────────────────────────────────────────────
// Reads the Task-12 blob layout: 4-layer "layers" child tree.
// Populates all 4 layers.  Falls back to loadV1State if the tree is absent.
void TerrainInstrumentAudioProcessor::loadV2State (const juce::ValueTree& loaded)
{
    auto layersTree = loaded.getChildWithName ("layers");
    if (! layersTree.isValid())
    {
        // Defensive: V2 marker set but no layers tree (shouldn't happen with
        // Task-12-saved blobs, but protect against partial writes).
        loadV1State (loaded);
        return;
    }

    // Clear all 4 layers first so empty layer nodes leave those layers visually
    // empty (no ghost sample state from a previous load).
    for (auto& L : layers)
    {
        L.sourceFileName = juce::String();
        L.sourcePath     = juce::String();
        L.sampleBuffer.store (nullptr);
        std::atomic_store (&L.currentSlices, tw::SliceListPtr{});
        L.pitchModeSlice = tw::Slice{};
        L.activeSliceIndex.store (0);
        L.volume.store (1.0f);
        L.mute.store   (false);
        L.solo.store   (false);
    }

    for (int i = 0; i < layersTree.getNumChildren(); ++i)
    {
        auto layerNode = layersTree.getChild (i);
        const int idx  = (int) layerNode.getProperty ("index", i);
        if (idx < 0 || idx > 3) continue;

        auto& L = layers[(size_t) idx];

        L.sourceFileName = layerNode.getProperty ("sourceFileName", "").toString();
        L.sourcePath     = layerNode.getProperty ("sourcePath",     "").toString();
        L.activeSliceIndex.store ((int) layerNode.getProperty ("activeSliceIndex", 0));
        L.rootMidiNote.store     ((int) layerNode.getProperty ("rootMidiNote", 60));
        L.sliceMode.store        ((int) layerNode.getProperty ("sliceMode",    0));
        L.playMode.store         ((int) layerNode.getProperty ("playMode",     0));
        L.sliceCount.store       ((int) layerNode.getProperty ("sliceCount",   4));
        L.chopFadeMs.store ((float)(double) layerNode.getProperty ("chopFadeMs", 5.0));
        L.volume.store     ((float)(double) layerNode.getProperty ("volume",     1.0));
        L.mute.store  ((bool) layerNode.getProperty ("mute",  false));
        L.solo.store  ((bool) layerNode.getProperty ("solo",  false));
        // Mix page Phase 2: per-layer creative-routing + mixer fields.
        // Defaults match LayerState defaults so pre-Phase-2 V2 blobs open clean.
        // Velocity + key zone defaults per-layer index: even quarters (matches processor seed).
        const int dvzMin = (idx == 0 ? 0  : idx == 1 ? 32 : idx == 2 ? 64 : 96);
        const int dvzMax = (idx == 0 ? 31 : idx == 1 ? 63 : idx == 2 ? 95 : 127);
        L.pan.store               ((float)(double) layerNode.getProperty ("pan",               0.0));
        L.pitchJitterCents.store  ((float)(double) layerNode.getProperty ("pitchJitterCents",  0.0));
        L.probabilityWeight.store ((float)(double) layerNode.getProperty ("probabilityWeight", 0.25));
        L.keyZoneMin.store        ((int)           layerNode.getProperty ("keyZoneMin",        dvzMin));
        L.keyZoneMax.store        ((int)           layerNode.getProperty ("keyZoneMax",        dvzMax));
        L.velocityZoneMin.store   ((int)           layerNode.getProperty ("velocityZoneMin",   dvzMin));
        L.velocityZoneMax.store   ((int)           layerNode.getProperty ("velocityZoneMax",   dvzMax));

        // Slices — same JSON format as V1 (slicesToJson/slicesFromJson).
        const juce::String sliceJson = layerNode.getProperty ("slicesJson", "").toString();
        if (sliceJson.isNotEmpty())
        {
            auto sl = std::make_shared<const tw::SliceList> (tw::slicesFromJson (sliceJson));
            std::atomic_store (&L.currentSlices, tw::SliceListPtr (sl));

            // Push slice bounds into this layer's warp cache.
            for (int si = 0; si < (int) sl->size(); ++si)
            {
                const auto& s = (*sl)[(size_t) si];
                L.synth.warpCache.setSliceBounds (si, (int) s.startSample, (int) s.endSample);
            }
        }

        // pitchModeSlice — shared helper.
        applyPitchSliceJson (layerNode.getProperty ("pitchSliceJson", "").toString(), L);
    }

    // Keep the legacy loadedSamplePath in sync with layer 0's path so the
    // editor constructor's existing V1 reload path fires for layer 0.  The
    // editor constructor's V2 path (added in the same task) handles layers 1-3.
    {
        juce::ScopedLock sl (sampleSourcePathLock);
        loadedSamplePath = layers[0].sourcePath;
    }
}

//==============================================================================
// User Preset File Persistence
//==============================================================================
juce::File TerrainInstrumentAudioProcessor::getUserPresetsFile() const
{
    // Separate folder from FX (which writes to Noizefield/Terrain/UserPresets.xml).
    // Schemas differ — instrument has SAMPLE_LOOP_MODE / ATTACK_MS / RELEASE_MS /
    // ROOT_NOTE etc. that the FX doesn't know about. If both wrote to the same
    // file, save-from-one would clobber/merge incompatibly with the other.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Noizefield")
        .getChildFile("Terrain Instrument")
        .getChildFile("UserPresets.xml");
}

void TerrainInstrumentAudioProcessor::saveUserPresetsToFile()
{
    auto file = getUserPresetsFile();

    // Ensure the directory tree exists
    auto dir = file.getParentDirectory();
    if (!dir.exists())
        dir.createDirectory();

    juce::ValueTree root ("TerrainUserPresets");

    // Save custom tags list
    if (customTags.size() > 0)
        root.setProperty("customTags", customTags.joinIntoString(","), nullptr);

    // Save everything except Init (index 0)
    for (int i = numFactoryPresets; i < static_cast<int>(presets.size()); ++i)
    {
        const auto& p = presets[static_cast<size_t>(i)];
        juce::ValueTree node ("Preset");
        node.setProperty("name",          p.name,          nullptr);
        if (p.tag.isNotEmpty())
            node.setProperty("tag",       p.tag,           nullptr);
        node.setProperty("grainSize",     p.grainSize,     nullptr);
        node.setProperty("density",       p.density,       nullptr);
        node.setProperty("spray",         p.spray,         nullptr);
        node.setProperty("pitch",         p.pitch,         nullptr);
        node.setProperty("drift",         p.drift,         nullptr);
        node.setProperty("freeze",        p.freeze,        nullptr);
        node.setProperty("grainFilter",   p.grainFilter,   nullptr);
        node.setProperty("mix",           p.mix,           nullptr);
        node.setProperty("wowFlutter",    p.wowFlutter,    nullptr);
        node.setProperty("saturation",    p.saturation,    nullptr);
        node.setProperty("hiss",          p.hiss,          nullptr);
        node.setProperty("wireWow",        p.wireWow,        nullptr);
        node.setProperty("wireSaturation", p.wireSaturation, nullptr);
        node.setProperty("wireHiss",       p.wireHiss,       nullptr);
        node.setProperty ("studioSculpt", p.studioSculpt, nullptr);
        node.setProperty ("studioWeave",  p.studioWeave,  nullptr);
        node.setProperty ("studioTilt",   p.studioTilt,   nullptr);
        node.setProperty("outputGain",    p.outputGain,    nullptr);
        node.setProperty("masterMix",     p.masterMix,     nullptr);
        node.setProperty("xyAutoEnabled",    p.xyAutoEnabled,    nullptr);
        node.setProperty("xyAutoMode",       p.xyAutoMode,       nullptr);
        node.setProperty("xyAutoSpeed",      p.xyAutoSpeed,      nullptr);
        node.setProperty("grainSyncEnabled",  p.grainSyncEnabled,  nullptr);
        node.setProperty("grainEngineEnabled", p.grainEngineEnabled, nullptr);
        node.setProperty("tapeEnabled",        p.tapeEnabled,        nullptr);
        node.setProperty("tapeLoopEnabled",    p.tapeLoopEnabled,    nullptr);
        node.setProperty("tapeMachine",        p.tapeMachine,        nullptr);
        node.setProperty("wanderLinked",        p.wanderLinked,        nullptr);
        node.setProperty("tapeLinkEnabled",    p.tapeLinkEnabled,    nullptr);
        node.setProperty("loopLength",      p.loopLength,      nullptr);
        node.setProperty("loopFeedback",    p.loopFeedback,    nullptr);
        node.setProperty("loopDegrade",     p.loopDegrade,     nullptr);
        node.setProperty("loopSpeed",       p.loopSpeed,       nullptr);
        node.setProperty("spaceSize",      p.spaceSize,       nullptr);
        node.setProperty("spaceDecay",     p.spaceDecay,      nullptr);
        node.setProperty("spaceTone",      p.spaceTone,       nullptr);
        node.setProperty("spaceMix",       p.spaceMix,        nullptr);
        // Parametric EQ (v6) — 35 APVTS values + 2 UI-only filter-mode flags.
        node.setProperty ("eqMasterBypass", p.eqMasterBypass, nullptr);
        node.setProperty ("eqHpFreq",   p.eqHpFreq,   nullptr);
        node.setProperty ("eqHpSlope",  p.eqHpSlope,  nullptr);
        node.setProperty ("eqHpBypass", p.eqHpBypass, nullptr);
        node.setProperty ("eqLpFreq",   p.eqLpFreq,   nullptr);
        node.setProperty ("eqLpSlope",  p.eqLpSlope,  nullptr);
        node.setProperty ("eqLpBypass", p.eqLpBypass, nullptr);
        node.setProperty ("eqB1Freq", p.eqB1Freq, nullptr); node.setProperty ("eqB1Gain", p.eqB1Gain, nullptr); node.setProperty ("eqB1Q", p.eqB1Q, nullptr); node.setProperty ("eqB1Bypass", p.eqB1Bypass, nullptr);
        node.setProperty ("eqB2Freq", p.eqB2Freq, nullptr); node.setProperty ("eqB2Gain", p.eqB2Gain, nullptr); node.setProperty ("eqB2Q", p.eqB2Q, nullptr); node.setProperty ("eqB2Bypass", p.eqB2Bypass, nullptr);
        node.setProperty ("eqB3Freq", p.eqB3Freq, nullptr); node.setProperty ("eqB3Gain", p.eqB3Gain, nullptr); node.setProperty ("eqB3Q", p.eqB3Q, nullptr); node.setProperty ("eqB3Bypass", p.eqB3Bypass, nullptr);
        node.setProperty ("eqB4Freq", p.eqB4Freq, nullptr); node.setProperty ("eqB4Gain", p.eqB4Gain, nullptr); node.setProperty ("eqB4Q", p.eqB4Q, nullptr); node.setProperty ("eqB4Bypass", p.eqB4Bypass, nullptr);
        node.setProperty ("eqB5Freq", p.eqB5Freq, nullptr); node.setProperty ("eqB5Gain", p.eqB5Gain, nullptr); node.setProperty ("eqB5Q", p.eqB5Q, nullptr); node.setProperty ("eqB5Bypass", p.eqB5Bypass, nullptr);
        node.setProperty ("eqB6Freq", p.eqB6Freq, nullptr); node.setProperty ("eqB6Gain", p.eqB6Gain, nullptr); node.setProperty ("eqB6Q", p.eqB6Q, nullptr); node.setProperty ("eqB6Bypass", p.eqB6Bypass, nullptr);
        node.setProperty ("eqB7Freq", p.eqB7Freq, nullptr); node.setProperty ("eqB7Gain", p.eqB7Gain, nullptr); node.setProperty ("eqB7Q", p.eqB7Q, nullptr); node.setProperty ("eqB7Bypass", p.eqB7Bypass, nullptr);
        node.setProperty ("eqB1HpMode", p.eqB1HpMode, nullptr);
        node.setProperty ("eqB7LpMode", p.eqB7LpMode, nullptr);
        node.setProperty ("chorusAmount",    p.chorusAmount,    nullptr);
        node.setProperty ("chorusWidth",     p.chorusWidth,     nullptr);
        node.setProperty ("chorusCharacter", p.chorusCharacter, nullptr);
        if (p.modState.isNotEmpty())
            node.setProperty("modState",  p.modState,       nullptr);
        root.addChild(node, -1, nullptr);
    }

    if (auto xml = root.createXml())
    {
        auto result = xml->writeTo(file);
        DBG("Terrain: saveUserPresetsToFile -> " + file.getFullPathName()
            + " (" + juce::String(presets.size() - numFactoryPresets) + " presets, "
            + (result ? "OK" : "FAILED") + ")");
    }
}

void TerrainInstrumentAudioProcessor::loadUserPresetsFromFile()
{
    auto file = getUserPresetsFile();

    DBG("Terrain: loadUserPresetsFromFile -> " + file.getFullPathName()
        + " exists=" + (file.existsAsFile() ? "YES" : "NO"));

    if (!file.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("TerrainUserPresets"))
    {
        DBG("Terrain: XML parse failed or wrong tag");
        return;
    }

    auto root = juce::ValueTree::fromXml(*xml);
    int loaded = 0;

    // Load custom tags
    juce::String tagsStr = root.getProperty("customTags", "").toString();
    if (tagsStr.isNotEmpty())
    {
        customTags.clear();
        customTags.addTokens(tagsStr, ",", "");
        customTags.removeEmptyStrings();
    }

    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        auto child = root.getChild(i);
        if (!child.hasType("Preset"))
            continue;

        PresetData p;
        p.name          = child.getProperty("name").toString();
        p.tag           = child.getProperty("tag", "").toString();
        p.grainSize     = static_cast<float>(child.getProperty("grainSize",     80.f));
        p.density       = static_cast<float>(child.getProperty("density",       20.f));
        p.spray         = static_cast<float>(child.getProperty("spray",         40.f));
        p.pitch         = static_cast<float>(child.getProperty("pitch",          0.f));
        p.drift         = static_cast<float>(child.getProperty("drift",          0.f));
        p.freeze        = static_cast<float>(child.getProperty("freeze",         0.f));
        p.grainFilter   = static_cast<float>(child.getProperty("grainFilter",  50.f));
        p.mix           = static_cast<float>(child.getProperty("mix",           50.f));
        p.wowFlutter    = static_cast<float>(child.getProperty("wowFlutter",     0.f));
        p.saturation    = static_cast<float>(child.getProperty("saturation",     0.f));
        p.hiss          = static_cast<float>(child.getProperty("hiss",           0.f));
        p.wireWow        = static_cast<float>(child.getProperty("wireWow",        0.f));
        p.wireSaturation = static_cast<float>(child.getProperty("wireSaturation", 0.f));
        p.wireHiss       = static_cast<float>(child.getProperty("wireHiss",       0.f));
        p.studioSculpt = static_cast<float> (child.getProperty ("studioSculpt", 0.f));
        p.studioWeave  = static_cast<float> (child.getProperty ("studioWeave",  0.f));
        p.studioTilt   = static_cast<float> (child.getProperty ("studioTilt",   0.f));
        p.outputGain    = static_cast<float>(child.getProperty("outputGain",     0.f));
        p.masterMix     = static_cast<float>(child.getProperty("masterMix",    100.f));
        p.xyAutoEnabled    = static_cast<float>(child.getProperty("xyAutoEnabled",    0.f));
        p.xyAutoMode       = static_cast<float>(child.getProperty("xyAutoMode",       0.f));
        p.xyAutoSpeed      = static_cast<float>(child.getProperty("xyAutoSpeed",     0.5f));
        p.grainSyncEnabled  = static_cast<float>(child.getProperty("grainSyncEnabled",  0.f));
        p.grainEngineEnabled = static_cast<float>(child.getProperty("grainEngineEnabled", 1.f));
        p.tapeEnabled        = static_cast<float>(child.getProperty("tapeEnabled",        1.f));
        p.tapeLoopEnabled    = static_cast<float>(child.getProperty("tapeLoopEnabled",    1.f));
        p.tapeMachine        = static_cast<float>(child.getProperty("tapeMachine",        0.f));
        p.wanderLinked        = static_cast<float>(child.getProperty("wanderLinked",        1.f));
        p.tapeLinkEnabled    = static_cast<float>(child.getProperty("tapeLinkEnabled",    0.f));
        p.loopLength      = static_cast<float>(child.getProperty("loopLength",      3.f));
        p.loopFeedback    = static_cast<float>(child.getProperty("loopFeedback",   85.f));
        p.loopDegrade     = static_cast<float>(child.getProperty("loopDegrade",    30.f));
        p.loopSpeed       = static_cast<float>(child.getProperty("loopSpeed",       6.f));
        p.spaceSize       = static_cast<float>(child.getProperty("spaceSize",      50.f));
        p.spaceDecay      = static_cast<float>(child.getProperty("spaceDecay",     50.f));
        p.spaceTone       = static_cast<float>(child.getProperty("spaceTone",      50.f));
        p.spaceMix        = static_cast<float>(child.getProperty("spaceMix",        0.f));
        // Parametric EQ (v6) — fall back to APVTS defaults for old presets that
        // pre-date the EQ persistence work (so loading them leaves EQ at sane defaults).
        p.eqMasterBypass = static_cast<float> (child.getProperty ("eqMasterBypass", 0.f));
        p.eqHpFreq    = static_cast<float> (child.getProperty ("eqHpFreq",    35.f));
        p.eqHpSlope   = static_cast<float> (child.getProperty ("eqHpSlope",    1.f));
        p.eqHpBypass  = static_cast<float> (child.getProperty ("eqHpBypass",   1.f));
        p.eqLpFreq    = static_cast<float> (child.getProperty ("eqLpFreq", 16000.f));
        p.eqLpSlope   = static_cast<float> (child.getProperty ("eqLpSlope",    0.f));
        p.eqLpBypass  = static_cast<float> (child.getProperty ("eqLpBypass",   1.f));
        p.eqB1Freq = static_cast<float> (child.getProperty ("eqB1Freq",   50.f)); p.eqB1Gain = static_cast<float> (child.getProperty ("eqB1Gain", 0.f)); p.eqB1Q = static_cast<float> (child.getProperty ("eqB1Q", 0.707f)); p.eqB1Bypass = static_cast<float> (child.getProperty ("eqB1Bypass", 0.f));
        p.eqB2Freq = static_cast<float> (child.getProperty ("eqB2Freq",  110.f)); p.eqB2Gain = static_cast<float> (child.getProperty ("eqB2Gain", 0.f)); p.eqB2Q = static_cast<float> (child.getProperty ("eqB2Q", 0.707f)); p.eqB2Bypass = static_cast<float> (child.getProperty ("eqB2Bypass", 0.f));
        p.eqB3Freq = static_cast<float> (child.getProperty ("eqB3Freq",  270.f)); p.eqB3Gain = static_cast<float> (child.getProperty ("eqB3Gain", 0.f)); p.eqB3Q = static_cast<float> (child.getProperty ("eqB3Q", 0.707f)); p.eqB3Bypass = static_cast<float> (child.getProperty ("eqB3Bypass", 0.f));
        p.eqB4Freq = static_cast<float> (child.getProperty ("eqB4Freq",  630.f)); p.eqB4Gain = static_cast<float> (child.getProperty ("eqB4Gain", 0.f)); p.eqB4Q = static_cast<float> (child.getProperty ("eqB4Q", 0.707f)); p.eqB4Bypass = static_cast<float> (child.getProperty ("eqB4Bypass", 0.f));
        p.eqB5Freq = static_cast<float> (child.getProperty ("eqB5Freq", 1500.f)); p.eqB5Gain = static_cast<float> (child.getProperty ("eqB5Gain", 0.f)); p.eqB5Q = static_cast<float> (child.getProperty ("eqB5Q", 0.707f)); p.eqB5Bypass = static_cast<float> (child.getProperty ("eqB5Bypass", 0.f));
        p.eqB6Freq = static_cast<float> (child.getProperty ("eqB6Freq", 3500.f)); p.eqB6Gain = static_cast<float> (child.getProperty ("eqB6Gain", 0.f)); p.eqB6Q = static_cast<float> (child.getProperty ("eqB6Q", 0.707f)); p.eqB6Bypass = static_cast<float> (child.getProperty ("eqB6Bypass", 0.f));
        p.eqB7Freq = static_cast<float> (child.getProperty ("eqB7Freq", 8500.f)); p.eqB7Gain = static_cast<float> (child.getProperty ("eqB7Gain", 0.f)); p.eqB7Q = static_cast<float> (child.getProperty ("eqB7Q", 0.707f)); p.eqB7Bypass = static_cast<float> (child.getProperty ("eqB7Bypass", 0.f));
        p.eqB1HpMode = static_cast<float> (child.getProperty ("eqB1HpMode", 0.f));
        p.eqB7LpMode = static_cast<float> (child.getProperty ("eqB7LpMode", 0.f));
        p.chorusAmount    = static_cast<float> (child.getProperty ("chorusAmount",    0.0f));
        p.chorusWidth     = static_cast<float> (child.getProperty ("chorusWidth",     0.7f));
        p.chorusCharacter = static_cast<float> (child.getProperty ("chorusCharacter", 0.3f));
        p.modState       = child.getProperty("modState", "").toString();

        presets.push_back(p);
        loaded++;
    }

    DBG("Terrain: loaded " + juce::String(loaded) + " user presets from disk, total=" + juce::String(presets.size()));
}

//==============================================================================
// Rolling Capture Buffer
//==============================================================================
float TerrainInstrumentAudioProcessor::getCaptureAvailableSeconds() const
{
    return captureBuffer.getAvailableSeconds();
}

juce::String TerrainInstrumentAudioProcessor::getLastCaptureFilePath() const
{
    return lastCaptureFilePath;
}

void TerrainInstrumentAudioProcessor::exportCapture(int durationSeconds)
{
    // CAS: only start if idle (0)
    int expected = 0;
    if (!captureExportState.compare_exchange_strong(expected, 1))
        return; // already exporting or ready

    // Join any previous thread
    if (captureExportThread && captureExportThread->joinable())
        captureExportThread->join();

    const double sr = captureBuffer.getSampleRate();
    if (sr <= 0.0)
    {
        captureExportState.store(3); // error
        return;
    }

    const int maxSamples = static_cast<int>(sr * durationSeconds);
    auto tempL = std::make_shared<std::vector<float>>(static_cast<size_t>(maxSamples), 0.0f);
    auto tempR = std::make_shared<std::vector<float>>(static_cast<size_t>(maxSamples), 0.0f);

    int copied = captureBuffer.copyForExport(tempL->data(), tempR->data(),
                                              static_cast<double>(durationSeconds));
    if (copied <= 0)
    {
        captureExportState.store(3); // error
        return;
    }

    // Build output path: ~/Music/Waves Crate/Terrain Instrument/Capture_YYYY-MM-DD_HH-MM-SS.wav
    // Separate folder from FX (~/Music/Waves Crate/Terrain/) so the same
    // user can run both products without their captures landing in the same
    // bucket — instrument captures are typically MIDI-recorded sample takes,
    // FX captures are processed track recordings; users want them separated.
    auto now = juce::Time::getCurrentTime();
    auto timestamp = now.formatted("%Y-%m-%d_%H-%M-%S");
    auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                   .getChildFile("Waves Crate")
                   .getChildFile("Terrain Instrument");

    if (!dir.exists())
        dir.createDirectory();

    auto filePath = dir.getChildFile("Terrain_Instrument_Capture_" + timestamp + ".wav");

    captureExportThread = std::make_unique<std::thread>(
        [this, tempL, tempR, copied, sr, filePath]()
        {
            juce::WavAudioFormat wav;
            auto outFile = filePath;
            outFile.deleteFile(); // remove if exists

            auto stream = outFile.createOutputStream();
            if (stream == nullptr)
            {
                captureExportState.store(3);
                return;
            }

            JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
            auto* writer = wav.createWriterFor(stream.release(), sr, 2, 16, {}, 0);
            JUCE_END_IGNORE_WARNINGS_GCC_LIKE
            if (writer == nullptr)
            {
                captureExportState.store(3);
                return;
            }
            std::unique_ptr<juce::AudioFormatWriter> writerPtr(writer);

            // Write in chunks
            constexpr int chunkSize = 8192;
            juce::AudioBuffer<float> chunk(2, chunkSize);

            for (int pos = 0; pos < copied; pos += chunkSize)
            {
                int samplesThisChunk = std::min(chunkSize, copied - pos);
                chunk.copyFrom(0, 0, tempL->data() + pos, samplesThisChunk);
                chunk.copyFrom(1, 0, tempR->data() + pos, samplesThisChunk);
                writerPtr->writeFromAudioSampleBuffer(chunk, 0, samplesThisChunk);
            }

            writerPtr.reset(); // flush and close

            lastCaptureFilePath = outFile.getFullPathName();
            captureExportState.store(2); // ready
        });
}

//==============================================================================
// Per-chop FX-independence (option 1): snapshots APVTS parameters into a
// IndyFxChain::ParamTargets struct so the shared indy chain uses the same
// values as the global chain. Scaling conventions match the global chain's
// per-sample loop exactly (see baked-in lessons in the implementation plan).
tw::IndyFxChain::ParamTargets
TerrainInstrumentAudioProcessor::snapshotFxParamTargets() const noexcept
{
    tw::IndyFxChain::ParamTargets t;

    // Grain — density / spray are RAW (matching global chain's per-sample loop)
    t.grainSize = apvts.getRawParameterValue (ParameterIDs::GRAIN_SIZE)->load();
    t.density   = apvts.getRawParameterValue (ParameterIDs::DENSITY)->load();
    t.spray     = apvts.getRawParameterValue (ParameterIDs::SPRAY)->load();
    t.pitch     = apvts.getRawParameterValue (ParameterIDs::PITCH)->load();
    t.wander01  = apvts.getRawParameterValue (ParameterIDs::WANDER)->load() * 0.01f;
    // Freeze uses the global chain's concave curve: pow(raw * 0.01f, 1.5f).
    {
        const float raw = apvts.getRawParameterValue (ParameterIDs::FREEZE)->load() * 0.01f;
        t.freeze01 = std::pow (raw, 1.5f);
    }
    t.mix = apvts.getRawParameterValue (ParameterIDs::MIX)->load();

    // Tape (cassette params *0.01f to match global chain per-sample scaling)
    t.wowFlutter01 = apvts.getRawParameterValue (ParameterIDs::WOW_FLUTTER)->load() * 0.01f;
    t.saturation01 = apvts.getRawParameterValue (ParameterIDs::SATURATION)->load() * 0.01f;
    t.hiss01       = apvts.getRawParameterValue (ParameterIDs::HISS)->load() * 0.01f;
    // Studio sculpt/weave/tilt: global chain computes sculptAmt = raw*0.01f,
    // then passes sculptAmt to tapeProcessor. IndyFxChain passes these directly
    // to tapeL.processSample in the same argument position — must match.
    t.studioSculpt = apvts.getRawParameterValue (ParameterIDs::STUDIO_SCULPT)->load() * 0.01f;
    t.studioWeave  = apvts.getRawParameterValue (ParameterIDs::STUDIO_WEAVE)->load() * 0.01f;
    t.studioTilt   = apvts.getRawParameterValue (ParameterIDs::STUDIO_TILT)->load() * 0.01f;
    // Wire wow/sat/hiss: global chain uses wireWowAmt = raw*0.01f.
    // IndyFxChain passes these in the same wire argument position.
    t.wireWow  = apvts.getRawParameterValue (ParameterIDs::WIRE_WOW)->load() * 0.01f;
    t.wireSat  = apvts.getRawParameterValue (ParameterIDs::WIRE_SATURATION)->load() * 0.01f;
    t.wireHiss = apvts.getRawParameterValue (ParameterIDs::WIRE_HISS)->load() * 0.01f;
    t.wireSpaceNoise = wireSpaceNoiseEnabled.load() > 0.5f;
    t.wireTubeSat    = wireTubeSatEnabled.load() > 0.5f;

    // Space — global chain scales ALL FOUR by * 0.01f (see PluginProcessor.cpp
    // lines 1663-1666). Passing raw 0..100 values causes SpaceReverb to silence
    // / NaN out at typical user knob settings.
    t.spaceSize  = apvts.getRawParameterValue (ParameterIDs::SPACE_SIZE)->load() * 0.01f;
    t.spaceDecay = apvts.getRawParameterValue (ParameterIDs::SPACE_DECAY)->load() * 0.01f;
    t.spaceTone  = apvts.getRawParameterValue (ParameterIDs::SPACE_TONE)->load() * 0.01f;
    t.spaceMix   = apvts.getRawParameterValue (ParameterIDs::SPACE_MIX)->load() * 0.01f;

    // Delay (MoogDelay::Params field names verified from MoogDelay.h)
    t.dlyTime       = apvts.getRawParameterValue (ParameterIDs::DLY_TIME)->load();
    t.dlyFeedback   = apvts.getRawParameterValue (ParameterIDs::DLY_FEEDBACK)->load();
    t.dlyTone       = apvts.getRawParameterValue (ParameterIDs::DLY_TONE)->load();
    t.dlyCharacter  = apvts.getRawParameterValue (ParameterIDs::DLY_CHARACTER)->load();
    t.dlyMod        = apvts.getRawParameterValue (ParameterIDs::DLY_MOD)->load();
    t.dlyModRate    = apvts.getRawParameterValue (ParameterIDs::DLY_MOD_RATE)->load();
    t.dlyModWave    = (int) apvts.getRawParameterValue (ParameterIDs::DLY_MOD_WAVE)->load();
    t.dlyMix        = apvts.getRawParameterValue (ParameterIDs::DLY_MIX)->load();
    t.dlyDuck       = apvts.getRawParameterValue (ParameterIDs::DLY_DUCK)->load();
    t.dlyPitch      = (int) apvts.getRawParameterValue (ParameterIDs::DLY_PITCH)->load();
    t.dlyWidth      = (int) apvts.getRawParameterValue (ParameterIDs::DLY_WIDTH)->load();
    t.dlyFreezeHeld = apvts.getRawParameterValue (ParameterIDs::DLY_FREEZE)->load() > 0.5f;

    // June (chorus)
    t.chAmount    = apvts.getRawParameterValue (ParameterIDs::CHORUS_AMOUNT)->load();
    t.chWidth     = apvts.getRawParameterValue (ParameterIDs::CHORUS_WIDTH)->load();
    t.chCharacter = apvts.getRawParameterValue (ParameterIDs::CHORUS_CHARACTER)->load();

    // EQ — bypass args are bool
    t.eqMasterBypass = apvts.getRawParameterValue (ParameterIDs::EQ_MASTER_BYPASS)->load() > 0.5f;
    t.eqHpFreq   = apvts.getRawParameterValue (ParameterIDs::EQ_HP_FREQ)->load();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParameterIDs::EQ_HP_SLOPE)))
        t.eqHpSlope = p->getIndex();
    t.eqHpBypass = apvts.getRawParameterValue (ParameterIDs::EQ_HP_BYPASS)->load() > 0.5f;
    t.eqLpFreq   = apvts.getRawParameterValue (ParameterIDs::EQ_LP_FREQ)->load();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParameterIDs::EQ_LP_SLOPE)))
        t.eqLpSlope = p->getIndex();
    t.eqLpBypass = apvts.getRawParameterValue (ParameterIDs::EQ_LP_BYPASS)->load() > 0.5f;

    static const char* freqIds[7] { ParameterIDs::EQ_B1_FREQ, ParameterIDs::EQ_B2_FREQ, ParameterIDs::EQ_B3_FREQ, ParameterIDs::EQ_B4_FREQ, ParameterIDs::EQ_B5_FREQ, ParameterIDs::EQ_B6_FREQ, ParameterIDs::EQ_B7_FREQ };
    static const char* gainIds[7] { ParameterIDs::EQ_B1_GAIN, ParameterIDs::EQ_B2_GAIN, ParameterIDs::EQ_B3_GAIN, ParameterIDs::EQ_B4_GAIN, ParameterIDs::EQ_B5_GAIN, ParameterIDs::EQ_B6_GAIN, ParameterIDs::EQ_B7_GAIN };
    static const char* qIds[7]    { ParameterIDs::EQ_B1_Q,    ParameterIDs::EQ_B2_Q,    ParameterIDs::EQ_B3_Q,    ParameterIDs::EQ_B4_Q,    ParameterIDs::EQ_B5_Q,    ParameterIDs::EQ_B6_Q,    ParameterIDs::EQ_B7_Q };
    static const char* bypIds[7]  { ParameterIDs::EQ_B1_BYPASS, ParameterIDs::EQ_B2_BYPASS, ParameterIDs::EQ_B3_BYPASS, ParameterIDs::EQ_B4_BYPASS, ParameterIDs::EQ_B5_BYPASS, ParameterIDs::EQ_B6_BYPASS, ParameterIDs::EQ_B7_BYPASS };
    for (int b = 0; b < 7; ++b)
    {
        t.bandFreq[b]   = apvts.getRawParameterValue (freqIds[b])->load();
        t.bandGain[b]   = apvts.getRawParameterValue (gainIds[b])->load();
        t.bandQ[b]      = apvts.getRawParameterValue (qIds[b])->load();
        t.bandBypass[b] = apvts.getRawParameterValue (bypIds[b])->load() > 0.5f;
    }

    return t;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TerrainInstrumentAudioProcessor();
}
