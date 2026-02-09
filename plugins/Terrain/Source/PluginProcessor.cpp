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
    presets = {
        //                 name               grSz  dens  spray pitch drift  mix   wow   sat   hiss  outGn
        { "Init",                              80.f, 20.f, 40.f,  0.f,  0.f, 50.f,  0.f,  0.f,  0.f, 0.f },
        { "Pristine Cloud",                   120.f, 60.f, 25.f,  0.f,  0.f, 70.f,  0.f,  0.f,  0.f, 0.f },
        { "Cassette Haze",                     80.f, 25.f, 50.f,  0.f, 15.f, 55.f, 45.f, 40.f, 50.f, 0.f },
        { "Warped Vinyl",                      60.f, 10.f, 30.f, -2.f, 20.f, 65.f, 85.f, 25.f, 20.f, 0.f },
        { "Octave Chaos",                     100.f, 40.f, 60.f,  0.f, 70.f, 60.f, 15.f, 30.f, 10.f, 0.f },
        { "Lo-Fi Dream",                       90.f, 30.f, 70.f,  0.f, 25.f, 50.f, 30.f, 45.f, 65.f, 0.f },
        { "Shimmer",                          150.f, 40.f, 20.f, 12.f, 40.f, 55.f, 10.f, 20.f,  0.f, 0.f },
        { "Ghost Machine",                     40.f,  5.f, 95.f,  7.f, 60.f, 80.f, 50.f, 15.f, 80.f, 0.f },
        { "Frozen Texture",                   400.f, 50.f,  0.f,  0.f,  5.f, 75.f, 20.f, 30.f,  0.f, 0.f },
        { "Cosmic Wash",                      250.f, 80.f, 80.f,  5.f, 50.f, 85.f, 70.f, 60.f, 40.f, 0.f },
    };
    numFactoryPresets = static_cast<int>(presets.size());
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
    setParam(ParameterIDs::DRIFT,        p.drift);
    setParam(ParameterIDs::MIX,          p.mix);
    setParam(ParameterIDs::WOW_FLUTTER,  p.wowFlutter);
    setParam(ParameterIDs::SATURATION,   p.saturation);
    setParam(ParameterIDs::HISS,         p.hiss);
    setParam(ParameterIDs::OUTPUT_GAIN,  p.outputGain);
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
    p.drift      = apvts.getRawParameterValue(ParameterIDs::DRIFT)->load();
    p.mix        = apvts.getRawParameterValue(ParameterIDs::MIX)->load();
    p.wowFlutter = apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load();
    p.saturation = apvts.getRawParameterValue(ParameterIDs::SATURATION)->load();
    p.hiss       = apvts.getRawParameterValue(ParameterIDs::HISS)->load();
    p.outputGain = apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load();
    return p;
}

int TerrainAudioProcessor::saveNewPreset(const juce::String& name)
{
    PresetData p = captureCurrentParams();
    p.name = name;
    presets.push_back(p);
    int newIdx = static_cast<int>(presets.size()) - 1;
    currentPresetIndex.store(newIdx);
    return newIdx;
}

void TerrainAudioProcessor::renamePreset(int index, const juce::String& newName)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    presets[static_cast<size_t>(index)].name = newName;
}

void TerrainAudioProcessor::overwritePreset(int index)
{
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;
    juce::String name = presets[static_cast<size_t>(index)].name;
    presets[static_cast<size_t>(index)] = captureCurrentParams();
    presets[static_cast<size_t>(index)].name = name;
}

void TerrainAudioProcessor::deletePreset(int index)
{
    if (index < numFactoryPresets || index >= static_cast<int>(presets.size()))
        return;
    presets.erase(presets.begin() + index);
    if (currentPresetIndex.load() >= static_cast<int>(presets.size()))
        currentPresetIndex.store(static_cast<int>(presets.size()) - 1);
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
        juce::ParameterID { ParameterIDs::DRIFT, 1 },
        "Drift",
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

    return layout;
}

//==============================================================================
void TerrainAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    grainEngineL.prepare(sampleRate, samplesPerBlock);
    grainEngineR.prepare(sampleRate, samplesPerBlock);
    tapeProcessorL.prepare(sampleRate, samplesPerBlock);
    tapeProcessorR.prepare(sampleRate, samplesPerBlock);

    // 20ms ramp for all smoothed parameters
    smoothedGrainSize.reset(sampleRate, 0.02);
    smoothedDensity.reset(sampleRate, 0.02);
    smoothedSpray.reset(sampleRate, 0.02);
    smoothedPitch.reset(sampleRate, 0.02);
    smoothedDrift.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);
    smoothedWowFlutter.reset(sampleRate, 0.02);
    smoothedSaturation.reset(sampleRate, 0.02);
    smoothedHiss.reset(sampleRate, 0.02);
    smoothedOutputGain.reset(sampleRate, 0.02);

    // Set initial values
    smoothedGrainSize.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::GRAIN_SIZE)->load());
    smoothedDensity.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DENSITY)->load());
    smoothedSpray.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SPRAY)->load());
    smoothedPitch.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::PITCH)->load());
    smoothedDrift.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DRIFT)->load());
    smoothedMix.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::MIX)->load());
    smoothedWowFlutter.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load());
    smoothedSaturation.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SATURATION)->load());
    smoothedHiss.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::HISS)->load());
    smoothedOutputGain.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load());
}

void TerrainAudioProcessor::releaseResources()
{
    grainEngineL.reset();
    grainEngineR.reset();
    tapeProcessorL.reset();
    tapeProcessorR.reset();
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

    // Update smoothing targets from APVTS
    smoothedGrainSize.setTargetValue(apvts.getRawParameterValue(ParameterIDs::GRAIN_SIZE)->load());
    smoothedDensity.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DENSITY)->load());
    smoothedSpray.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SPRAY)->load());
    smoothedPitch.setTargetValue(apvts.getRawParameterValue(ParameterIDs::PITCH)->load());
    smoothedDrift.setTargetValue(apvts.getRawParameterValue(ParameterIDs::DRIFT)->load());
    smoothedMix.setTargetValue(apvts.getRawParameterValue(ParameterIDs::MIX)->load());
    smoothedWowFlutter.setTargetValue(apvts.getRawParameterValue(ParameterIDs::WOW_FLUTTER)->load());
    smoothedSaturation.setTargetValue(apvts.getRawParameterValue(ParameterIDs::SATURATION)->load());
    smoothedHiss.setTargetValue(apvts.getRawParameterValue(ParameterIDs::HISS)->load());
    smoothedOutputGain.setTargetValue(apvts.getRawParameterValue(ParameterIDs::OUTPUT_GAIN)->load());

    // Per-sample processing
    auto* leftChannel  = buffer.getWritePointer(0);
    auto* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    int scopePos = scopeWritePos.load();

    for (int i = 0; i < numSamples; ++i)
    {
        const float grainSize    = smoothedGrainSize.getNextValue();
        const float density      = smoothedDensity.getNextValue();
        const float spray        = smoothedSpray.getNextValue();
        const float pitch        = smoothedPitch.getNextValue();
        const float drift        = smoothedDrift.getNextValue() * 0.01f;
        const float mix          = smoothedMix.getNextValue();
        const float wowFlutter   = smoothedWowFlutter.getNextValue() * 0.01f;
        const float saturationAmt = smoothedSaturation.getNextValue() * 0.01f;
        const float hissAmt      = smoothedHiss.getNextValue() * 0.01f;
        const float outputGainDb = smoothedOutputGain.getNextValue();
        const float outputGain   = std::pow(10.0f, outputGainDb / 20.0f); // dB to linear

        // Signal chain: Input → GrainEngine → TapeProcessor → Output Gain
        float outL = grainEngineL.processSample(leftChannel[i],
                                                 grainSize, density, spray,
                                                 pitch, drift, mix);
        outL = tapeProcessorL.processSample(outL, wowFlutter, saturationAmt, hissAmt);
        outL *= outputGain;
        leftChannel[i] = outL;

        if (rightChannel != nullptr)
        {
            float outR = grainEngineR.processSample(rightChannel[i],
                                                     grainSize, density, spray,
                                                     pitch, drift, mix);
            outR = tapeProcessorR.processSample(outR, wowFlutter, saturationAmt, hissAmt);
            outR *= outputGain;
            rightChannel[i] = outR;
        }

        // Write to scope buffer (mono mix for visualization)
        float scopeSample = rightChannel != nullptr ? (outL + rightChannel[i]) * 0.5f : outL;
        scopeBuffer[static_cast<size_t>(scopePos)].store(scopeSample, std::memory_order_relaxed);
        scopePos = (scopePos + 1) % SCOPE_SIZE;
    }

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
    auto state = apvts.copyState();
    state.setProperty("presetIndex", currentPresetIndex.load(), nullptr);

    // Serialize user presets (beyond factory)
    for (int i = numFactoryPresets; i < static_cast<int>(presets.size()); ++i)
    {
        const auto& p = presets[static_cast<size_t>(i)];
        juce::ValueTree presetNode ("UserPreset");
        presetNode.setProperty("name",       p.name,       nullptr);
        presetNode.setProperty("grainSize",  p.grainSize,  nullptr);
        presetNode.setProperty("density",    p.density,    nullptr);
        presetNode.setProperty("spray",      p.spray,      nullptr);
        presetNode.setProperty("pitch",      p.pitch,      nullptr);
        presetNode.setProperty("drift",      p.drift,      nullptr);
        presetNode.setProperty("mix",        p.mix,        nullptr);
        presetNode.setProperty("wowFlutter", p.wowFlutter, nullptr);
        presetNode.setProperty("saturation", p.saturation, nullptr);
        presetNode.setProperty("hiss",       p.hiss,       nullptr);
        presetNode.setProperty("outputGain", p.outputGain, nullptr);
        state.addChild(presetNode, -1, nullptr);
    }

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
            int presetIdx = newState.getProperty("presetIndex", 0);
            currentPresetIndex.store(presetIdx);

            // Restore user presets
            // Remove any existing user presets first
            while (static_cast<int>(presets.size()) > numFactoryPresets)
                presets.pop_back();

            for (int i = 0; i < newState.getNumChildren(); ++i)
            {
                auto child = newState.getChild(i);
                if (child.hasType("UserPreset"))
                {
                    PresetData p;
                    p.name       = child.getProperty("name").toString();
                    p.grainSize  = static_cast<float>(child.getProperty("grainSize",  80.f));
                    p.density    = static_cast<float>(child.getProperty("density",    20.f));
                    p.spray      = static_cast<float>(child.getProperty("spray",      40.f));
                    p.pitch      = static_cast<float>(child.getProperty("pitch",       0.f));
                    p.drift      = static_cast<float>(child.getProperty("drift",       0.f));
                    p.mix        = static_cast<float>(child.getProperty("mix",        50.f));
                    p.wowFlutter = static_cast<float>(child.getProperty("wowFlutter",  0.f));
                    p.saturation = static_cast<float>(child.getProperty("saturation",  0.f));
                    p.hiss       = static_cast<float>(child.getProperty("hiss",        0.f));
                    p.outputGain = static_cast<float>(child.getProperty("outputGain",  0.f));
                    presets.push_back(p);
                }
            }

            apvts.replaceState (newState);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TerrainAudioProcessor();
}
