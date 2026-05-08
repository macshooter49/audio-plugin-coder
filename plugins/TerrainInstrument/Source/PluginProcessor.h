#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "GrainEngine.h"
#include "TapeProcessor.h"
#include "TapeLoopProcessor.h"
#include "SpaceReverb.h"
#include "SimpleEQ.h"
#include "RollingCaptureBuffer.h"
#include "ModulationEngine.h"
#include "ParameterIDs.hpp"
#include "SamplerVoice.h"
#include "SampleBuffer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <thread>

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
    float tapeMachine = 0.f; // 0=Studio, 1=Cassette, 2=Wire
    // Drift link to XY pad
    float wanderLinked = 1.f; // 1 = linked (default), 0 = unlinked from XY pad
    float grainFilter = 50.f; // 0 = HP, 50 = bypass, 100 = LP
    // Space reverb
    float spaceSize = 50.f, spaceDecay = 50.f, spaceTone = 50.f, spaceMix = 0.f;
    // 3-band EQ
    float eqLowFreq = 200.f, eqLowGain = 0.f;
    float eqMidFreq = 1000.f, eqMidGain = 0.f;
    float eqHighFreq = 6000.f, eqHighGain = 0.f;
    // Category tag (at end so initializer lists work without specifying it)
    juce::String tag;  // e.g. "GRAIN", "TAPE", "AMBIENT", custom
    // Modulation state JSON (LFO configs + assignments, saved per-preset)
    juce::String modState;
};

//==============================================================================
class TerrainInstrumentAudioProcessor  : public juce::AudioProcessor
{
public:
    TerrainInstrumentAudioProcessor();
    ~TerrainInstrumentAudioProcessor() override;

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
    tw::SampleBuffer& getSampleBuffer() noexcept { return sampleBuffer; }

    // Preset system
    void loadPreset (int index);
    int getPresetCount() const;
    juce::String getPresetName (int index) const;

    // Preset management (save/rename/overwrite/delete)
    int saveNewPreset (const juce::String& name, const juce::String& tag = {});
    void renamePreset (int index, const juce::String& newName);
    void overwritePreset (int index);
    void deletePreset (int index);
    int getFactoryPresetCount() const { return numFactoryPresets; }

    // Tag management
    juce::String getPresetTag (int index) const;
    void setPresetTag (int index, const juce::String& tag);
    juce::String getCustomTags() const;
    void setCustomTags (const juce::String& commaSeparated);

    // Visualization data (read from editor on timer thread)
    std::atomic<int> activeGrainCount { 0 };
    std::atomic<int> currentPresetIndex { 0 };

    // Public so SamplerVoice can take a reference (atomic, lock-free reads on audio thread).
    std::atomic<int>   rootNoteMidi   { 60 };    // default C4
    std::atomic<float> attackMsAtomic { 5.0f };
    std::atomic<float> releaseMsAtomic { 800.0f };

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

    // Pitch locked to semitone steps 1-12 (synced from JS)
    std::atomic<float> pitchLocked { 0.f }; // 0 = free, 1 = locked

    // Feed tape loop back into granular engine (synced from JS)
    std::atomic<float> tapeLoopFeedToGrain { 0.f }; // 0 = off, 1 = on

    // Wire-only mode toggles (synced from JS, persisted in DAW state)
    std::atomic<float> wireSpaceNoiseEnabled { 0.f }; // 0 = standard hiss, 1 = space noise
    std::atomic<float> wireTubeSatEnabled { 0.f };    // 0 = standard sat, 1 = tube

    // Modulation engine (runs in processBlock, independent of editor window)
    ModulationEngine modulationEngine;

    // XY pad values (UI writes, audio reads)
    std::atomic<float> xyPadX { 0.5f };
    std::atomic<float> xyPadY { 0.5f };

    // Modulation state JSON (persisted from JS, survives editor close/reopen + DAW session)
    juce::String modStateJson;

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

    // Rolling capture buffer state
    // 0 = idle, 1 = exporting, 2 = ready (file saved), 3 = error
    std::atomic<int> captureExportState { 0 };
    void exportCapture(int durationSeconds);
    juce::String getLastCaptureFilePath() const;
    float getCaptureAvailableSeconds() const;

    static constexpr int SCOPE_SIZE = 256;
    std::array<std::atomic<float>, SCOPE_SIZE> scopeBuffer {};
    std::atomic<int> scopeWritePos { 0 };

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::Synthesiser synth;
    static constexpr int kNumVoices = 16;

    tw::SampleBuffer sampleBuffer;

    // Grain engines (one per channel)
    GrainEngine grainEngineL;
    GrainEngine grainEngineR;

    // Tape processors (one per channel)
    TapeProcessor tapeProcessorL;
    TapeProcessor tapeProcessorR;

    // Tape loop (stereo — single instance)
    TapeLoopProcessor tapeLoop;

    // Space reverb (stereo — single instance handles both channels)
    SpaceReverb spaceReverb;

    // 3-band EQ (one per channel)
    SimpleEQ eqL;
    SimpleEQ eqR;

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

    // Smoothed parameters — space reverb
    juce::SmoothedValue<float> smoothedSpaceSize;
    juce::SmoothedValue<float> smoothedSpaceDecay;
    juce::SmoothedValue<float> smoothedSpaceTone;
    juce::SmoothedValue<float> smoothedSpaceMix;

    // Smoothed parameters — EQ
    juce::SmoothedValue<float> smoothedEqLowFreq;
    juce::SmoothedValue<float> smoothedEqLowGain;
    juce::SmoothedValue<float> smoothedEqMidFreq;
    juce::SmoothedValue<float> smoothedEqMidGain;
    juce::SmoothedValue<float> smoothedEqHighFreq;
    juce::SmoothedValue<float> smoothedEqHighGain;

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

    // Rolling capture buffer
    RollingCaptureBuffer captureBuffer;
    std::unique_ptr<std::thread> captureExportThread;
    juce::String lastCaptureFilePath;

    // Presets
    void initializePresets();
    PresetData captureCurrentParams() const;
    std::vector<PresetData> presets;
    int numFactoryPresets = 0;
    juce::StringArray customTags;  // User-created tags beyond built-in ones

    // User preset file persistence
    juce::File getUserPresetsFile() const;
    void saveUserPresetsToFile();
    void loadUserPresetsFromFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainInstrumentAudioProcessor)
};
