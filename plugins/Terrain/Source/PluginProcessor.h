#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "GrainEngine.h"
#include "TapeProcessor.h"
#include "TapeLoopProcessor.h"
#include "ParameterIDs.hpp"
#include <atomic>
#include <array>

//==============================================================================
struct PresetData
{
    juce::String name;
    float grainSize, density, spray, pitch, drift, freeze = 0.f, mix;
    float wowFlutter, saturation, hiss;
    float outputGain;
    float masterMix = 100.f;
    // Tape loop params (loopSpeed: 0-9 stepped, 6 = 1x normal)
    float loopLength = 3.f, loopFeedback = 85.f, loopDegrade = 30.f, loopSpeed = 6.f;
    // XY pad automation state
    float xyAutoEnabled = 0.f;  // 0 = off, 1 = on
    float xyAutoMode    = 0.f;  // 0 = chaotic, 1 = smooth
    float xyAutoSpeed   = 0.5f; // 0-1 normalized
    // Grain sync state
    float grainSyncEnabled = 0.f; // 0 = free (ms), 1 = synced to BPM
    // Grain engine on/off
    float grainEngineEnabled = 1.f; // 1 = on (default), 0 = bypass grain processing
    // Tape engine on/off
    float tapeEnabled = 1.f; // 1 = on (default), 0 = bypass tape processing
    // Drift link to XY pad
    float wanderLinked = 1.f; // 1 = linked (default), 0 = unlinked from XY pad
    float grainFilter = 50.f; // 0 = HP, 50 = bypass, 100 = LP
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

    // Grain engine master on/off (synced from JS, captured into presets)
    std::atomic<float> grainEngineEnabled { 1.f }; // 1 = on, 0 = bypass

    // Tape engine master on/off (synced from JS, captured into presets)
    std::atomic<float> tapeEnabled { 1.f }; // 1 = on, 0 = bypass

    // Drift link to XY pad (synced from JS, captured into presets)
    std::atomic<float> wanderLinked { 1.f }; // 1 = linked, 0 = unlinked

    // Tape loop transport state (synced from JS, may be modified by auto-stop in processBlock)
    std::atomic<float> tapeLoopRecording { 0.f };
    std::atomic<float> tapeLoopPlaying { 0.f };

    // Speed freeform mode (synced from JS)
    std::atomic<float> speedFreeform { 0.f }; // 0 = stepped, 1 = freeform

    // Feed tape loop back into granular engine (synced from JS)
    std::atomic<float> tapeLoopFeedToGrain { 0.f }; // 0 = off, 1 = on

    // Tape loop read-only state (set by processBlock for UI)
    bool getTapeLoopHasContent() const { return tapeLoop.hasContent(); }
    float getTapeLoopProgress() const { return tapeLoop.getProgress(); }
    bool getTapeLoopHasUndo() const { return tapeLoop.hasUndo(); }
    int getTapeLoopCountInBeat() const { return tapeLoop.getCountInBeat(); }

    // Tape loop actions (called from editor native functions)
    void clearTapeLoop()
    {
        tapeLoop.clear();
        tapeLoopRecording.store(0.f);
        tapeLoopPlaying.store(0.f);
    }

    void undoTapeLoop()
    {
        tapeLoop.restoreFromUndo();
        tapeLoopRecording.store(0.f);
    }

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

    // Tape loop (stereo — single instance)
    TapeLoopProcessor tapeLoop;

    // Smoothed parameters — granular
    juce::SmoothedValue<float> smoothedGrainSize;
    juce::SmoothedValue<float> smoothedDensity;
    juce::SmoothedValue<float> smoothedSpray;
    juce::SmoothedValue<float> smoothedPitch;
    juce::SmoothedValue<float> smoothedWander;
    juce::SmoothedValue<float> smoothedFreeze;
    juce::SmoothedValue<float> smoothedMix;

    // Smoothed parameters — tape
    juce::SmoothedValue<float> smoothedWowFlutter;
    juce::SmoothedValue<float> smoothedSaturation;
    juce::SmoothedValue<float> smoothedHiss;

    // Smoothed parameters — grain filter
    juce::SmoothedValue<float> smoothedGrainFilter;

    // Smoothed parameters — tape loop (continuous params only)
    juce::SmoothedValue<float> smoothedLoopFeedback;
    juce::SmoothedValue<float> smoothedLoopDegrade;

    // Smoothed parameters — output
    juce::SmoothedValue<float> smoothedOutputGain;
    juce::SmoothedValue<float> smoothedMasterMix;

    // Grain filter state (one-pole)
    float grainFilterStateL = 0.0f;
    float grainFilterStateR = 0.0f;

    // Feed-to-grain: one-sample delay buffer (previous tape loop output)
    float feedDelayL = 0.0f;
    float feedDelayR = 0.0f;
    bool prevProcessBlockRecording = false; // Track recording transitions for auto-disabling feed
    bool prevFeedActive = false; // Track feed mode transitions for grain buffer clearing

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
