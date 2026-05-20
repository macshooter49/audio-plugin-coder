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

    // === Sampler engine wiring (Terrain Instrument v0a) ======================
    synth.addSound (new tw::SamplerSound());
    for (int i = 0; i < kNumVoices; ++i)
        synth.addVoice (new tw::SamplerVoice (sampleBuffer, rootNoteMidi,
                                              attackMsAtomic, releaseMsAtomic,
                                              sampleLoopMode));
    // Sample buffer starts empty. User drags a file in, or the editor opens a
    // file picker. SampleLoader (async) populates the shared buffer when a load
    // completes. Voices read it via the SampleBuffer atomic shared_ptr.
}

TerrainInstrumentAudioProcessor::~TerrainInstrumentAudioProcessor()
{
    if (captureExportThread && captureExportThread->joinable())
        captureExportThread->join();
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
        juce::StringArray { "CHOP", "CHROMATIC" }, 0));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { ParameterIDs::SAMPLE_LOOP_MODE, 1 },
        "Sample Loop",
        juce::StringArray { "ONE-SHOT", "LOOP" }, 0));

    return layout;
}

//==============================================================================
void TerrainInstrumentAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);

    grainEngineL.prepare(sampleRate, samplesPerBlock);
    grainEngineR.prepare(sampleRate, samplesPerBlock);
    tapeProcessorL.prepare(sampleRate, samplesPerBlock);
    tapeProcessorR.prepare(sampleRate, samplesPerBlock);
    tapeLoop.prepare(sampleRate, samplesPerBlock);
    spaceReverb.prepare(sampleRate, samplesPerBlock);
    moogDelay.prepare(sampleRate, samplesPerBlock);
    terrainChorus.prepare (sampleRate, samplesPerBlock);

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
    rootNoteMidi.store    ((int) *apvts.getRawParameterValue (ParameterIDs::ROOT_NOTE));
    attackMsAtomic.store  (*apvts.getRawParameterValue (ParameterIDs::ATTACK_MS));
    releaseMsAtomic.store (*apvts.getRawParameterValue (ParameterIDs::RELEASE_MS));
    sampleLoopMode.store  ((int) *apvts.getRawParameterValue (ParameterIDs::SAMPLE_LOOP_MODE));

    // Build the SliceContext for the synth dispatcher. The slice list is an
    // immutable shared_ptr snapshot so the audio thread reads without locks.
    {
        const int sliceModeIdx    = (int) *apvts.getRawParameterValue (ParameterIDs::SLICE_MODE);
        const int sliceSubModeIdx = (int) *apvts.getRawParameterValue (ParameterIDs::SLICE_SUB_MODE);
        tw::SliceContext ctx;
        ctx.rootMidiNote     = rootNoteMidi.load();
        ctx.activeSliceIndex = activeSliceIndex.load();
        ctx.slices           = std::atomic_load (&slicesPtr);

        if (sliceModeIdx == 0)
            ctx.mode = tw::SliceContext::Mode::Whole;
        else if (sliceSubModeIdx == 1)
            ctx.mode = tw::SliceContext::Mode::ChromaticOneSlice;
        else
            ctx.mode = tw::SliceContext::Mode::ChopChromaticLayout;
        synth.setSliceContext (ctx);
    }

    // Drain audition queue — UI-clicked previews of individual slices.
    // Triggered before renderNextBlock so the audition voice starts within
    // this block.
    {
        std::vector<int> drained;
        {
            const juce::SpinLock::ScopedLockType sl (auditionLock);
            drained.swap (auditionQueue);
        }
        if (! drained.empty())
        {
            auto sl = std::atomic_load (&slicesPtr);
            if (sl && ! sl->empty())
            {
                for (int idx : drained)
                {
                    if (idx >= 0 && idx < (int) sl->size())
                        synth.auditionSlice ((*sl)[(size_t) idx], idx);
                }
            }
        }
    }

    // Voices replace plugin input as the source of audio for the FX chain.
    buffer.clear();
    synth.renderNextBlock (buffer, midiMessages, 0, numSamples);

    // ── Update slice play-glow array ──────────────────────────────────
    // For each glow slot: take the max envelope across voices that are
    // currently playing that slice; then apply a slow visual decay so
    // short one-shots fade out instead of snapping to 0 when the voice
    // dies. Block-rate update (~5-22 ms at 256-1024 samples / 48 kHz)
    // outpaces the ~16 ms UI poll, so motion stays smooth.
    {
        std::array<float, kMaxGlowSlots> blockMax {};
        for (int v = 0; v < synth.getNumVoices(); ++v)
        {
            if (auto* sv = dynamic_cast<tw::SamplerVoice*> (synth.getVoice (v)))
            {
                if (! sv->isPlaying()) continue;
                const int idx = sv->getSliceIndex();
                if (idx < 0 || idx >= kMaxGlowSlots) continue;
                const float lvl = sv->getEnvelopeLevel();
                if (lvl > blockMax[(size_t) idx]) blockMax[(size_t) idx] = lvl;
            }
        }

        // Visual decay: full-bright → ~5% in ~200 ms. Per-block coefficient
        // is exp(-blockSec / tau), tau ≈ 65 ms.
        const double blockSec = (double) numSamples / juce::jmax (1.0, getSampleRate());
        const float  decay    = (float) std::exp (-blockSec / 0.065);
        for (int i = 0; i < kMaxGlowSlots; ++i)
        {
            const float live = blockMax[(size_t) i];
            const float prev = sliceGlowLevel[(size_t) i].load (std::memory_order_relaxed);
            const float next = juce::jmax (live, prev * decay);
            sliceGlowLevel[(size_t) i].store (next, std::memory_order_relaxed);
        }
    }

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

void TerrainInstrumentAudioProcessor::setCachedSamplePayload (const juce::String& jsonPayload)
{
    juce::ScopedLock sl (samplePayloadLock);
    cachedSamplePayloadJson = jsonPayload;
}

juce::String TerrainInstrumentAudioProcessor::getCachedSamplePayload() const
{
    juce::ScopedLock sl (samplePayloadLock);
    return cachedSamplePayloadJson;
}

//==============================================================================
// Slicer state management
//==============================================================================
void TerrainInstrumentAudioProcessor::replaceSlices (tw::SliceList newSlices)
{
    auto snapshot = std::make_shared<const tw::SliceList> (std::move (newSlices));
    std::atomic_store (&slicesPtr, tw::SliceListPtr (snapshot));

    // Clamp activeSliceIndex to new range.
    const int n = (int) snapshot->size();
    int idx = activeSliceIndex.load();
    if (n == 0) idx = 0;
    else if (idx >= n) idx = n - 1;
    else if (idx < 0) idx = 0;
    activeSliceIndex.store (idx);
}

tw::SliceListPtr TerrainInstrumentAudioProcessor::loadSlices() const
{
    return std::atomic_load (&slicesPtr);
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

juce::var TerrainInstrumentAudioProcessor::snapshotSliceGlowLevels() const
{
    const int n = juce::jmin (getNumSlices(), kMaxGlowSlots);
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated (n);
    for (int i = 0; i < n; ++i)
        arr.add ((double) sliceGlowLevel[(size_t) i].load (std::memory_order_relaxed));
    return juce::var (arr);
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

    // Persist the loaded sample's source path so DAW project save/restore
    // can re-load the same file when the editor re-opens. Empty string if
    // nothing was loaded yet.
    {
        juce::ScopedLock sl (sampleSourcePathLock);
        if (loadedSamplePath.isNotEmpty())
            state.setProperty ("sampleSourcePath", loadedSamplePath, nullptr);
    }

    // Slice list (JSON) + active slice index — sub-mode rides in APVTS.
    {
        const auto sliceJson = getSlicesJson();
        if (sliceJson.isNotEmpty() && sliceJson != "{\"slices\":[]}")
            state.setProperty ("slicesJson", sliceJson, nullptr);
        state.setProperty ("activeSliceIndex", activeSliceIndex.load(), nullptr);
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

            // Restore the loaded sample's path. Editor's constructor reads
            // this and kicks off async reload via loadSampleAsync — audio
            // thread plays silence until reload completes.
            {
                auto path = newState.getProperty ("sampleSourcePath", "").toString();
                juce::ScopedLock sl (sampleSourcePathLock);
                loadedSamplePath = path;
            }

            // Restore slice list + active index. Sub-mode comes from APVTS
            // automatically. Empty list is fine — slicer just behaves like
            // there are no chops yet.
            {
                auto sliceJson = newState.getProperty ("slicesJson", "").toString();
                if (sliceJson.isNotEmpty())
                    setSlicesFromJson (sliceJson);
                else
                    replaceSlices ({});
                activeSliceIndex.store ((int) newState.getProperty ("activeSliceIndex", 0));
            }

            // Reload presets from disk (the single source of truth)
            while (static_cast<int>(presets.size()) > numFactoryPresets)
                presets.pop_back();
            loadUserPresetsFromFile();

            // Clamp preset index to valid range
            if (presetIdx >= static_cast<int>(presets.size()))
                presetIdx = 0;
            currentPresetIndex.store(presetIdx);

            apvts.replaceState (newState);
        }
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
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TerrainInstrumentAudioProcessor();
}
