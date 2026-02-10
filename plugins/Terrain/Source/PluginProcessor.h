#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "GrainEngine.h"
#include "TapeProcessor.h"
#include "ParameterIDs.hpp"
#include <atomic>
#include <array>

//==============================================================================
struct PresetData
{
    juce::String name;
    float grainSize, density, spray, pitch, drift, mix;
    float wowFlutter, saturation, hiss;
    float outputGain;
    // XY pad automation state
    float xyAutoEnabled = 0.f;  // 0 = off, 1 = on
    float xyAutoMode    = 0.f;  // 0 = chaotic, 1 = smooth
    float xyAutoSpeed   = 0.5f; // 0-1 normalized
    // Grain sync state
    float grainSyncEnabled = 0.f; // 0 = free (ms), 1 = synced to BPM
};

//==============================================================================
class TerrainAudioProcessor  : public juce::AudioProcessor
{
public:
    TerrainAudioProcessor();
    ~TerrainAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Preset system
    void loadPreset (int index);
    int getPresetCount() const;
    juce::String getPresetName (int index) const;

    // Preset management (save/rename/overwrite/delete)
    int saveNewPreset (const juce::String& name);
    void renamePreset (int index, const juce::String& newName);
    void overwritePreset (int index);
    void deletePreset (int index);
    int getFactoryPresetCount() const { return numFactoryPresets; }

    // Visualization data (read from editor on timer thread)
    std::atomic<int> activeGrainCount { 0 };
    std::atomic<int> currentPresetIndex { 0 };

    // XY automation state (synced from JS, captured into presets)
    std::atomic<float> xyAutoEnabled { 0.f };
    std::atomic<float> xyAutoMode    { 0.f };
    std::atomic<float> xyAutoSpeed   { 0.5f };

    // Grain BPM sync state (synced from JS, captured into presets)
    std::atomic<float> grainSyncEnabled { 0.f };
    std::atomic<float> currentBPM { 120.f }; // populated from playhead

    static constexpr int SCOPE_SIZE = 256;
    std::array<std::atomic<float>, SCOPE_SIZE> scopeBuffer {};
    std::atomic<int> scopeWritePos { 0 };

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Grain engines (one per channel)
    GrainEngine grainEngineL;
    GrainEngine grainEngineR;

    // Tape processors (one per channel)
    TapeProcessor tapeProcessorL;
    TapeProcessor tapeProcessorR;

    // Smoothed parameters — granular
    juce::SmoothedValue<float> smoothedGrainSize;
    juce::SmoothedValue<float> smoothedDensity;
    juce::SmoothedValue<float> smoothedSpray;
    juce::SmoothedValue<float> smoothedPitch;
    juce::SmoothedValue<float> smoothedDrift;
    juce::SmoothedValue<float> smoothedMix;

    // Smoothed parameters — tape
    juce::SmoothedValue<float> smoothedWowFlutter;
    juce::SmoothedValue<float> smoothedSaturation;
    juce::SmoothedValue<float> smoothedHiss;

    // Smoothed parameters — output
    juce::SmoothedValue<float> smoothedOutputGain;

    // Presets
    void initializePresets();
    PresetData captureCurrentParams() const;
    std::vector<PresetData> presets;
    int numFactoryPresets = 0;

    // User preset file persistence
    juce::File getUserPresetsFile() const;
    void saveUserPresetsToFile();
    void loadUserPresetsFromFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainAudioProcessor)
};
