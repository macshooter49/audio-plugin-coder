#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
TerrainAudioProcessor::TerrainAudioProcessor()
    : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    initializePresets();
}

TerrainAudioProcessor::~TerrainAudioProcessor()
{
}

//==============================================================================
void TerrainAudioProcessor::initializePresets()
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
    numFactoryPresets = static_cast<int>(presets.size());  // All presets above are factory

    // Load any additional user presets from disk
    loadUserPresetsFromFile();
}

void TerrainAudioProcessor::loadPreset(int index)
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
    setParam(ParameterIDs::MIX,          p.mix);
    setParam(ParameterIDs::WOW_FLUTTER,  p.wowFlutter);
    setParam(ParameterIDs::SATURATION,   p.saturation);
    setParam(ParameterIDs::HISS,         p.hiss);
    setParam(ParameterIDs::OUTPUT_GAIN,  p.outputGain);
    setParam(ParameterIDs::MASTER_MIX,   p.masterMix);
    setParam(ParameterIDs::LOOP_LENGTH,   p.loopLength);
    setParam(ParameterIDs::LOOP_FEEDBACK, p.loopFeedback);
    setParam(ParameterIDs::LOOP_DEGRADE,  p.loopDegrade);
    setParam(ParameterIDs::LOOP_SPEED,    p.loopSpeed);

    // Restore XY automation state for this preset
    xyAutoEnabled.store(p.xyAutoEnabled);
    xyAutoMode.store(p.xyAutoMode);
    xyAutoSpeed.store(p.xyAutoSpeed);

    // Restore grain sync state
    grainSyncEnabled.store(p.grainSyncEnabled);

    // Restore grain engine on/off, tape on/off, and drift link states
    grainEngineEnabled.store(p.grainEngineEnabled);
    tapeEnabled.store(p.tapeEnabled);
    wanderLinked.store(p.wanderLinked);
}

int TerrainAudioProcessor::getPresetCount() const
{
    return static_cast<int>(presets.size());
}

juce::String TerrainAudioProcessor::getPresetName(int index) const
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return {};
    return presets[static_cast<size_t>(index)].name;
}

//==============================================================================
PresetData TerrainAudioProcessor::captureCurrentParams() const
{
    PresetData p;
    p.grainSize  = apvts.getRawParameterValue(ParameterIDs::GRAIN_SIZE)->load();
    p.density    = apvts.getRawParameterValue(ParameterIDs::DENSITY)->load();
    p.spray      = apvts.getRawParameterValue(ParameterIDs::SPRAY)->load();
    p.pitch      = apvts.getRawParameterValue(ParameterIDs::PITCH)->load();
    p.drift      = apvts.getRawParameterValue(ParameterIDs::WANDER)->load();
    p.freeze     = apvts.getRawParameterValue(ParameterIDs::FREEZE)->load();
    p.mix        = apvts.getRawParameterValue(ParameterIDs::MIX)->load();
    p.wowFlutter = apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load();
    p.saturation = apvts.getRawParameterValue(ParameterIDs::SATURATION)->load();
    p.hiss       = apvts.getRawParameterValue(ParameterIDs::HISS)->load();
    p.outputGain = apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load();
    p.masterMix  = apvts.getRawParameterValue(ParameterIDs::MASTER_MIX)->load();
    p.loopLength   = apvts.getRawParameterValue(ParameterIDs::LOOP_LENGTH)->load();
    p.loopFeedback = apvts.getRawParameterValue(ParameterIDs::LOOP_FEEDBACK)->load();
    p.loopDegrade  = apvts.getRawParameterValue(ParameterIDs::LOOP_DEGRADE)->load();
    p.loopSpeed    = apvts.getRawParameterValue(ParameterIDs::LOOP_SPEED)->load();
    p.xyAutoEnabled    = xyAutoEnabled.load();
    p.xyAutoMode       = xyAutoMode.load();
    p.xyAutoSpeed      = xyAutoSpeed.load();
    p.grainSyncEnabled = grainSyncEnabled.load();
    p.grainEngineEnabled = grainEngineEnabled.load();
    p.tapeEnabled        = tapeEnabled.load();
    p.wanderLinked        = wanderLinked.load();
    return p;
}

int TerrainAudioProcessor::saveNewPreset(const juce::String& name)
{
    PresetData p = captureCurrentParams();
    p.name = name;
    presets.push_back(p);
    int newIdx = static_cast<int>(presets.size()) - 1;
    currentPresetIndex.store(newIdx);
    saveUserPresetsToFile();
    return newIdx;
}

void TerrainAudioProcessor::renamePreset(int index, const juce::String& newName)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    presets[static_cast<size_t>(index)].name = newName;
    saveUserPresetsToFile();
}

void TerrainAudioProcessor::overwritePreset(int index)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    juce::String name = presets[static_cast<size_t>(index)].name;
    presets[static_cast<size_t>(index)] = captureCurrentParams();
    presets[static_cast<size_t>(index)].name = name;
    saveUserPresetsToFile();
}

void TerrainAudioProcessor::deletePreset(int index)
{
    if (index < numFactoryPresets || index >= static_cast<int>(presets.size()))
        return;
    presets.erase(presets.begin() + index);
    if (currentPresetIndex.load() >= static_cast<int>(presets.size()))
        currentPresetIndex.store(static_cast<int>(presets.size()) - 1);
    saveUserPresetsToFile();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TerrainAudioProcessor::createParameterLayout()
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

    return layout;
}

//==============================================================================
void TerrainAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    grainEngineL.prepare(sampleRate, samplesPerBlock);
    grainEngineR.prepare(sampleRate, samplesPerBlock);
    tapeProcessorL.prepare(sampleRate, samplesPerBlock);
    tapeProcessorR.prepare(sampleRate, samplesPerBlock);
    tapeLoop.prepare(sampleRate, samplesPerBlock);

    // 20ms ramp for all smoothed parameters
    smoothedGrainSize.reset(sampleRate, 0.02);
    smoothedDensity.reset(sampleRate, 0.02);
    smoothedSpray.reset(sampleRate, 0.02);
    smoothedPitch.reset(sampleRate, 0.02);
    smoothedWander.reset(sampleRate, 0.02);
    smoothedFreeze.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    smoothedWowFlutter.reset(sampleRate, 0.02);
    smoothedSaturation.reset(sampleRate, 0.02);
    smoothedHiss.reset(sampleRate, 0.02);
    smoothedOutputGain.reset(sampleRate, 0.02);
    smoothedMasterMix.reset(sampleRate, 0.02);
    smoothedLoopFeedback.reset(sampleRate, 0.02);
    smoothedLoopDegrade.reset(sampleRate, 0.02);

    // Set initial values
    smoothedGrainSize.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::GRAIN_SIZE)->load());
    smoothedDensity.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DENSITY)->load());
    smoothedSpray.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SPRAY)->load());
    smoothedPitch.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::PITCH)->load());
    smoothedWander.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::WANDER)->load());
    smoothedFreeze.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::FREEZE)->load());
    smoothedMix.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::MIX)->load());
    smoothedWowFlutter.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load());
    smoothedSaturation.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SATURATION)->load());
    smoothedHiss.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::HISS)->load());
    smoothedOutputGain.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load());
    smoothedMasterMix.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::MASTER_MIX)->load());
    smoothedLoopFeedback.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::LOOP_FEEDBACK)->load());
    smoothedLoopDegrade.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::LOOP_DEGRADE)->load());
}

void TerrainAudioProcessor::releaseResources()
{
    grainEngineL.reset();
    grainEngineR.reset();
    tapeProcessorL.reset();
    tapeProcessorR.reset();
    tapeLoop.reset();
}

bool TerrainAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void TerrainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples == 0) return;

    // Clear any extra output channels
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, numSamples);

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
    smoothedWowFlutter.setTargetValue(apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load());
    smoothedSaturation.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SATURATION)->load());
    smoothedHiss.setTargetValue(apvts.getRawParameterValue(ParameterIDs::HISS)->load());
    smoothedOutputGain.setTargetValue(apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load());
    smoothedMasterMix.setTargetValue(apvts.getRawParameterValue(ParameterIDs::MASTER_MIX)->load());

    smoothedLoopFeedback.setTargetValue(apvts.getRawParameterValue(ParameterIDs::LOOP_FEEDBACK)->load());
    smoothedLoopDegrade.setTargetValue(apvts.getRawParameterValue(ParameterIDs::LOOP_DEGRADE)->load());

    // Tape loop discrete params (no smoothing needed)
    const float loopLengthParam = apvts.getRawParameterValue(ParameterIDs::LOOP_LENGTH)->load();
    const float loopSpeedParam  = apvts.getRawParameterValue(ParameterIDs::LOOP_SPEED)->load();

    // Tape loop transport state (may be modified by auto-stop)
    bool wantRecord = tapeLoopRecording.load() > 0.5f;
    bool wantPlay   = tapeLoopPlaying.load() > 0.5f;
    const float bpm = currentBPM.load();
    const bool isFreeform = speedFreeform.load() > 0.5f;

    // Per-sample processing
    auto* leftChannel  = buffer.getWritePointer(0);
    auto* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    int scopePos = scopeWritePos.load();

    const bool grainOn = grainEngineEnabled.load() > 0.5f;
    const bool tapeOn = tapeEnabled.load() > 0.5f;
    const bool feedToGrain = tapeLoopFeedToGrain.load() > 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float grainSize    = smoothedGrainSize.getNextValue();
        const float density      = smoothedDensity.getNextValue();
        const float spray        = smoothedSpray.getNextValue();
        const float pitch        = smoothedPitch.getNextValue();
        const float wander       = smoothedWander.getNextValue() * 0.01f;
        const float freezeRaw    = smoothedFreeze.getNextValue() * 0.01f;
        const float freeze       = std::pow(freezeRaw, 1.5f);
        const float mix          = smoothedMix.getNextValue();
        const float wowFlutter   = smoothedWowFlutter.getNextValue() * 0.01f;
        const float saturationAmt = smoothedSaturation.getNextValue() * 0.01f;
        const float hissAmt      = smoothedHiss.getNextValue() * 0.01f;
        const float outputGainDb = smoothedOutputGain.getNextValue();
        const float outputGain   = std::pow(10.0f, outputGainDb / 20.0f);
        const float masterMixAmt = smoothedMasterMix.getNextValue() * 0.01f;
        const float loopFeedback = smoothedLoopFeedback.getNextValue() * 0.01f;
        const float loopDegrade  = smoothedLoopDegrade.getNextValue() * 0.01f;

        // Capture dry input before processing
        const float dryL = leftChannel[i];
        const float dryR = rightChannel != nullptr ? rightChannel[i] : 0.0f;

        // Choose grain engine input: live audio OR tape loop feedback (one-sample delay)
        const float grainInputL = (feedToGrain && wantPlay && tapeLoop.hasContent()) ? feedDelayL : leftChannel[i];
        const float grainInputR = (feedToGrain && wantPlay && tapeLoop.hasContent())
            ? feedDelayR
            : (rightChannel != nullptr ? rightChannel[i] : leftChannel[i]);

        // Signal chain: Input → GrainEngine → TapeProcessor → TapeLoop → MasterMix → OutputGain
        float wetL = grainOn
            ? grainEngineL.processSample(grainInputL, grainSize, density, spray, pitch, wander, freeze, mix)
            : grainInputL;
        if (tapeOn)
            wetL = tapeProcessorL.processSample(wetL, wowFlutter, saturationAmt, hissAmt);

        float wetR;
        if (rightChannel != nullptr)
        {
            wetR = grainOn
                ? grainEngineR.processSample(grainInputR, grainSize, density, spray, pitch, wander, freeze, mix)
                : grainInputR;
            if (tapeOn)
                wetR = tapeProcessorR.processSample(wetR, wowFlutter, saturationAmt, hissAmt);
        }
        else
        {
            wetR = wetL;
        }

        // Tape loop (stereo, modifies wetL/wetR in-place, may auto-stop recording)
        const float preLoopL = wetL;
        const float preLoopR = wetR;
        tapeLoop.processStereo(wetL, wetR, wantRecord, wantPlay,
                               loopLengthParam, loopFeedback, loopDegrade,
                               loopSpeedParam, bpm, isFreeform);

        if (feedToGrain)
        {
            // Extract loop-only contribution for the feed delay buffer
            feedDelayL = wetL - preLoopL;
            feedDelayR = wetR - preLoopR;
            // Remove loop from direct output (user hears it only through grain)
            wetL = preLoopL;
            wetR = preLoopR;
        }
        else
        {
            feedDelayL = wetL;
            feedDelayR = wetR;
        }

        // Master mix + output gain
        float outL = (dryL * (1.0f - masterMixAmt) + wetL * masterMixAmt) * outputGain;
        leftChannel[i] = outL;

        if (rightChannel != nullptr)
        {
            float outR = (dryR * (1.0f - masterMixAmt) + wetR * masterMixAmt) * outputGain;
            rightChannel[i] = outR;
        }

        // Write to scope buffer (mono mix for visualization)
        float scopeSample = rightChannel != nullptr ? (leftChannel[i] + rightChannel[i]) * 0.5f : leftChannel[i];
        scopeBuffer[static_cast<size_t>(scopePos)].store(scopeSample, std::memory_order_relaxed);
        scopePos = (scopePos + 1) % SCOPE_SIZE;
    }

    // Sync transport state back (auto-stop may have changed wantRecord/wantPlay)
    tapeLoopRecording.store(wantRecord ? 1.f : 0.f);
    tapeLoopPlaying.store(wantPlay ? 1.f : 0.f);

    scopeWritePos.store(scopePos);

    // Update grain count for visualization
    activeGrainCount.store(grainEngineL.getActiveGrainCount() + grainEngineR.getActiveGrainCount());
}

//==============================================================================
juce::AudioProcessorEditor* TerrainAudioProcessor::createEditor()
{
    return new TerrainAudioProcessorEditor(*this);
}

bool TerrainAudioProcessor::hasEditor() const { return true; }

const juce::String TerrainAudioProcessor::getName() const { return JucePlugin_Name; }
bool TerrainAudioProcessor::acceptsMidi() const { return false; }
bool TerrainAudioProcessor::producesMidi() const { return false; }
bool TerrainAudioProcessor::isMidiEffect() const { return false; }
double TerrainAudioProcessor::getTailLengthSeconds() const { return 5.0; }

int TerrainAudioProcessor::getNumPrograms() { return static_cast<int>(presets.size()); }
int TerrainAudioProcessor::getCurrentProgram() { return currentPresetIndex.load(); }
void TerrainAudioProcessor::setCurrentProgram (int index) { loadPreset(index); }
const juce::String TerrainAudioProcessor::getProgramName (int index) { return getPresetName(index); }
void TerrainAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void TerrainAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
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
    state.setProperty("wanderLinked",        wanderLinked.load(),        nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TerrainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
            wanderLinked.store(static_cast<float>(newState.getProperty("wanderLinked", 1.f)));

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
juce::File TerrainAudioProcessor::getUserPresetsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Noizefield")
        .getChildFile("Terrain")
        .getChildFile("UserPresets.xml");
}

void TerrainAudioProcessor::saveUserPresetsToFile()
{
    auto file = getUserPresetsFile();

    // Ensure the directory tree exists
    auto dir = file.getParentDirectory();
    if (!dir.exists())
        dir.createDirectory();

    juce::ValueTree root ("TerrainUserPresets");

    // Save everything except Init (index 0)
    for (int i = numFactoryPresets; i < static_cast<int>(presets.size()); ++i)
    {
        const auto& p = presets[static_cast<size_t>(i)];
        juce::ValueTree node ("Preset");
        node.setProperty("name",          p.name,          nullptr);
        node.setProperty("grainSize",     p.grainSize,     nullptr);
        node.setProperty("density",       p.density,       nullptr);
        node.setProperty("spray",         p.spray,         nullptr);
        node.setProperty("pitch",         p.pitch,         nullptr);
        node.setProperty("drift",         p.drift,         nullptr);
        node.setProperty("freeze",        p.freeze,        nullptr);
        node.setProperty("mix",           p.mix,           nullptr);
        node.setProperty("wowFlutter",    p.wowFlutter,    nullptr);
        node.setProperty("saturation",    p.saturation,    nullptr);
        node.setProperty("hiss",          p.hiss,          nullptr);
        node.setProperty("outputGain",    p.outputGain,    nullptr);
        node.setProperty("masterMix",     p.masterMix,     nullptr);
        node.setProperty("xyAutoEnabled",    p.xyAutoEnabled,    nullptr);
        node.setProperty("xyAutoMode",       p.xyAutoMode,       nullptr);
        node.setProperty("xyAutoSpeed",      p.xyAutoSpeed,      nullptr);
        node.setProperty("grainSyncEnabled",  p.grainSyncEnabled,  nullptr);
        node.setProperty("grainEngineEnabled", p.grainEngineEnabled, nullptr);
        node.setProperty("tapeEnabled",        p.tapeEnabled,        nullptr);
        node.setProperty("wanderLinked",        p.wanderLinked,        nullptr);
        node.setProperty("loopLength",      p.loopLength,      nullptr);
        node.setProperty("loopFeedback",    p.loopFeedback,    nullptr);
        node.setProperty("loopDegrade",     p.loopDegrade,     nullptr);
        node.setProperty("loopSpeed",       p.loopSpeed,       nullptr);
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

void TerrainAudioProcessor::loadUserPresetsFromFile()
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

    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        auto child = root.getChild(i);
        if (!child.hasType("Preset"))
            continue;

        PresetData p;
        p.name          = child.getProperty("name").toString();
        p.grainSize     = static_cast<float>(child.getProperty("grainSize",     80.f));
        p.density       = static_cast<float>(child.getProperty("density",       20.f));
        p.spray         = static_cast<float>(child.getProperty("spray",         40.f));
        p.pitch         = static_cast<float>(child.getProperty("pitch",          0.f));
        p.drift         = static_cast<float>(child.getProperty("drift",          0.f));
        p.freeze        = static_cast<float>(child.getProperty("freeze",         0.f));
        p.mix           = static_cast<float>(child.getProperty("mix",           50.f));
        p.wowFlutter    = static_cast<float>(child.getProperty("wowFlutter",     0.f));
        p.saturation    = static_cast<float>(child.getProperty("saturation",     0.f));
        p.hiss          = static_cast<float>(child.getProperty("hiss",           0.f));
        p.outputGain    = static_cast<float>(child.getProperty("outputGain",     0.f));
        p.masterMix     = static_cast<float>(child.getProperty("masterMix",    100.f));
        p.xyAutoEnabled    = static_cast<float>(child.getProperty("xyAutoEnabled",    0.f));
        p.xyAutoMode       = static_cast<float>(child.getProperty("xyAutoMode",       0.f));
        p.xyAutoSpeed      = static_cast<float>(child.getProperty("xyAutoSpeed",     0.5f));
        p.grainSyncEnabled  = static_cast<float>(child.getProperty("grainSyncEnabled",  0.f));
        p.grainEngineEnabled = static_cast<float>(child.getProperty("grainEngineEnabled", 1.f));
        p.tapeEnabled        = static_cast<float>(child.getProperty("tapeEnabled",        1.f));
        p.wanderLinked        = static_cast<float>(child.getProperty("wanderLinked",        1.f));
        p.loopLength      = static_cast<float>(child.getProperty("loopLength",      3.f));
        p.loopFeedback    = static_cast<float>(child.getProperty("loopFeedback",   85.f));
        p.loopDegrade     = static_cast<float>(child.getProperty("loopDegrade",    30.f));
        p.loopSpeed       = static_cast<float>(child.getProperty("loopSpeed",       6.f));

        presets.push_back(p);
        loaded++;
    }

    DBG("Terrain: loaded " + juce::String(loaded) + " user presets from disk, total=" + juce::String(presets.size()));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TerrainAudioProcessor();
}
