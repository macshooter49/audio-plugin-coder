#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
EarcandyAudioProcessor::EarcandyAudioProcessor()
    : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

EarcandyAudioProcessor::~EarcandyAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EarcandyAudioProcessor::createParameterLayout()
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
        juce::ParameterID { ParameterIDs::FEEDBACK, 1 },
        "Feedback",
        juce::NormalisableRange<float>(0.0f, 95.0f, 0.1f),
        15.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { ParameterIDs::MIX, 1 },
        "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    return layout;
}

//==============================================================================
void EarcandyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    grainEngineL.prepare(sampleRate, samplesPerBlock);
    grainEngineR.prepare(sampleRate, samplesPerBlock);

    // 20ms ramp for all smoothed parameters
    smoothedGrainSize.reset(sampleRate, 0.02);
    smoothedDensity.reset(sampleRate, 0.02);
    smoothedSpray.reset(sampleRate, 0.02);
    smoothedPitch.reset(sampleRate, 0.02);
    smoothedFeedback.reset(sampleRate, 0.02);
    smoothedMix.reset(sampleRate, 0.02);

    // Set initial values
    smoothedGrainSize.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::GRAIN_SIZE)->load());
    smoothedDensity.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::DENSITY)->load());
    smoothedSpray.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::SPRAY)->load());
    smoothedPitch.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::PITCH)->load());
    smoothedFeedback.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::FEEDBACK)->load());
    smoothedMix.setCurrentAndTargetValue(apvts.getRawParameterValue(ParameterIDs::MIX)->load());
}

void EarcandyAudioProcessor::releaseResources()
{
    grainEngineL.reset();
    grainEngineR.reset();
}

bool EarcandyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void EarcandyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
    smoothedFeedback.setTargetValue(apvts.getRawParameterValue(ParameterIDs::FEEDBACK)->load());
    smoothedMix.setTargetValue(apvts.getRawParameterValue(ParameterIDs::MIX)->load());

    // Per-sample processing
    auto* leftChannel  = buffer.getWritePointer(0);
    auto* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float grainSize = smoothedGrainSize.getNextValue();
        const float density   = smoothedDensity.getNextValue();
        const float spray     = smoothedSpray.getNextValue();
        const float pitch     = smoothedPitch.getNextValue();
        const float feedback  = smoothedFeedback.getNextValue() * 0.01f; // Convert % to 0-0.95
        const float mix       = smoothedMix.getNextValue();

        leftChannel[i] = grainEngineL.processSample(leftChannel[i],
                                                     grainSize, density, spray,
                                                     pitch, feedback, mix);

        if (rightChannel != nullptr)
        {
            rightChannel[i] = grainEngineR.processSample(rightChannel[i],
                                                          grainSize, density, spray,
                                                          pitch, feedback, mix);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* EarcandyAudioProcessor::createEditor()
{
    return new EarcandyAudioProcessorEditor(*this);
}

bool EarcandyAudioProcessor::hasEditor() const { return true; }

const juce::String EarcandyAudioProcessor::getName() const { return JucePlugin_Name; }
bool EarcandyAudioProcessor::acceptsMidi() const { return false; }
bool EarcandyAudioProcessor::producesMidi() const { return false; }
bool EarcandyAudioProcessor::isMidiEffect() const { return false; }
double EarcandyAudioProcessor::getTailLengthSeconds() const { return 5.0; }

int EarcandyAudioProcessor::getNumPrograms() { return 1; }
int EarcandyAudioProcessor::getCurrentProgram() { return 0; }
void EarcandyAudioProcessor::setCurrentProgram (int) {}
const juce::String EarcandyAudioProcessor::getProgramName (int) { return {}; }
void EarcandyAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void EarcandyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void EarcandyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EarcandyAudioProcessor();
}
