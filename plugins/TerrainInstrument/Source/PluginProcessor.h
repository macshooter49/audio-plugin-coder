#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "GrainEngine.h"
#include "TapeProcessor.h"
#include "TapeLoopProcessor.h"
#include "SpaceReverb.h"
#include "MoogDelay.h"
#include "TerrainChorus.h"
#include "ParametricEQ.h"
#include "SpectrumAnalyzer.h"
#include "RollingCaptureBuffer.h"
#include "ModulationEngine.h"
#include "ParameterIDs.hpp"
#include "SamplerVoice.h"
#include "SampleBuffer.h"
#include "SampleLoader.h"
#include "Slice.h"
#include "TerrainSynth.h"
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
    // Harmonic Sculptor (Studio v2.0) — placed AFTER all positionally-initialized
    // factory preset slots so the existing initializer literals don't shift values
    // into these fields. Defaults match APVTS defaults.
    float studioSculpt = 0.f;
    float studioWeave  = 0.f;
    float studioTilt   = 0.f;
    // Tape Link: when 1, all 3 tape machines run in series.
    float tapeLinkEnabled = 0.f;
    // Wire-specific wow/sat/hiss — independent of Cassette.
    float wireWow        = 0.f;
    float wireSaturation = 0.f;
    float wireHiss       = 0.f;
    // Parametric EQ (v6) — 35 APVTS params + 2 JS-side filter mode flags.
    // Defaults mirror the APVTS layout in createParameterLayout().
    float eqMasterBypass = 0.f;
    float eqHpFreq = 35.f,    eqHpSlope = 1.f, eqHpBypass = 1.f;
    float eqLpFreq = 16000.f, eqLpSlope = 0.f, eqLpBypass = 1.f;
    float eqB1Freq = 50.f,   eqB1Gain = 0.f, eqB1Q = 0.707f, eqB1Bypass = 0.f;
    float eqB2Freq = 110.f,  eqB2Gain = 0.f, eqB2Q = 0.707f, eqB2Bypass = 0.f;
    float eqB3Freq = 270.f,  eqB3Gain = 0.f, eqB3Q = 0.707f, eqB3Bypass = 0.f;
    float eqB4Freq = 630.f,  eqB4Gain = 0.f, eqB4Q = 0.707f, eqB4Bypass = 0.f;
    float eqB5Freq = 1500.f, eqB5Gain = 0.f, eqB5Q = 0.707f, eqB5Bypass = 0.f;
    float eqB6Freq = 3500.f, eqB6Gain = 0.f, eqB6Q = 0.707f, eqB6Bypass = 0.f;
    float eqB7Freq = 8500.f, eqB7Gain = 0.f, eqB7Q = 0.707f, eqB7Bypass = 0.f;
    float eqB1HpMode = 0.f;  // band 1 in HP filter mode (replaces bell + engages HP)
    float eqB7LpMode = 0.f;  // band 7 in LP filter mode (replaces bell + engages LP)
    // MF-104S delay (v6)
    float dlyTime      = 350.0f;
    float dlyFeedback  = 0.45f;
    float dlyTone      = 0.0f;
    float dlyCharacter = 0.5f;
    float dlyMod       = 0.25f;
    float dlyModRate   = 0.5f;
    float dlyMix       = 0.0f;   // Default 0.0 so old presets sound the same.
    float dlyDuck      = 0.0f;
    float dlyModWave   = 0.0f;   // 0=Sine
    float dlySync      = 0.0f;   // 0=Free
    float dlySyncDiv   = 5.0f;   // 5=1/8
    float dlyPitch     = 0.0f;   // 0=OFF
    float dlyWidth     = 1.0f;   // 1=Stereo
    // Chorus (v6)
    float chorusAmount    = 0.0f;
    float chorusWidth     = 0.7f;
    float chorusCharacter = 0.3f;
    // Tape LOOP transport on/off — independent of tapeEnabled (which gates the
    // tape FX chain: Delay + Space + Tape Processor). Splitting these lets the
    // user disable tape effects while keeping the loop running, and vice versa.
    // Placed at end so existing factory preset literal positions don't shift.
    float tapeLoopEnabled = 1.f;
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
    tw::SampleLoader& getSampleLoader() noexcept { return sampleLoader; }

    // Sampler atomics — public so SamplerVoice can take a reference (audio-thread-safe).
    std::atomic<int>   rootNoteMidi      { 60 };    // default C4
    std::atomic<float> attackMsAtomic    { 5.0f };
    std::atomic<float> releaseMsAtomic   { 800.0f };
    std::atomic<float> chopFadeMsAtomic  { 5.0f };  // CHOP_FADE_MS: anti-click ramp at slice boundaries
    // Sample playback loop mode: 0 = one-shot (envelope releases at end-of-buffer),
    // 1 = forward loop (playhead wraps to 0 and keeps playing until note-off).
    std::atomic<int>   sampleLoopMode  { 0 };

    // ── Slicer state ──────────────────────────────────────────────────────
    // Slice list — atomic snapshot pointer. UI thread writes via
    // replaceSlices(); audio thread reads via loadSlices() / readSlices().
    // The shared_ptr is treated as immutable — never modified after store.
    void              replaceSlices (tw::SliceList newSlices);
    tw::SliceListPtr  loadSlices() const;
    int               getNumSlices() const;
    juce::String      getSlicesJson() const;
    void              setSlicesFromJson (const juce::String& json);

    // ── Scan-viz polling API (UI thread only) ─────────────────────────────
    /** Returns normalized [0,1] scan-playhead position for the given slice index.
     *  Returns -1.0f if no active scan-on voice is playing on this slice.
     *  Safe to call from the UI thread (read-only walk of voice list). */
    float getScanPosition (int sliceIndex) const noexcept;

    struct ScanWindowBounds { float startNorm = 0.0f; float endNorm = 1.0f; };

    /** Returns the live scan-window bounds for the given slice.
     *  If scanWindow == 1.0, returns {0.0, 1.0}.
     *  Returns {0.0, 1.0} if no scan-on voice is sounding on this slice. */
    ScanWindowBounds getScanWindowBounds (int sliceIndex) const noexcept;

    // Active slice index — used in CHROMATIC sub-mode. Atomic so JS push
    // and audio thread read are race-free.
    std::atomic<int>  activeSliceIndex { 0 };

    // Audition: trigger a slice once at unity pitch via the synth dispatcher,
    // bypassing the regular MIDI key mapping. Used for click-to-preview in
    // the editor. Implemented by injecting a synthetic MIDI note-on/off
    // pair into the next processBlock.
    void              auditionSlice (int sliceIndex);

    // Path of the most-recently-loaded sample. Editor pushes after each
    // successful load; processor saves it in getStateInformation so DAW
    // project save/restore re-loads the same file when the editor opens.
    // Guarded by a CriticalSection because the editor (message thread) and
    // the audio thread (during state restore) can both touch it.
    void setLoadedSamplePath (const juce::String& path);
    juce::String getLoadedSamplePath() const;

    /** Monotonic version counter — increments each time a new sample is loaded
     *  into sampleBuffer. Used as the sourceVersionId field of WarpRenderCache::Key
     *  so that cache entries from a previous sample are never served for a new one.
     *  Default = 0 (no sample loaded). Bump happens in loadSampleAsync completion
     *  callback (message thread only), so no atomic needed — but atomic<int> keeps
     *  it trivially safe if the audio thread ever reads it for a future key build.
     */
    int getSourceVersionId() const noexcept { return sourceVersionId_.load (std::memory_order_relaxed); }

    // Cached JSON payload for the loaded sample (filename + peaks + meta).
    // Populated by the editor after each successful load. Survives editor
    // close/reopen WITHIN the same plugin instance — JS pulls via the
    // getCachedSamplePayload native fn on hero injection, restoring the
    // waveform display instantly without re-decoding the file. On DAW
    // project reload the processor is fresh (cache empty) — falls back to
    // file-path reload so the audio buffer also re-populates.
    void setCachedSamplePayload (const juce::String& jsonPayload);
    juce::String getCachedSamplePayload() const;

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

    // Tape LOOP transport on/off — independent of tapeEnabled. Toggling tape
    // FX off no longer freezes the loop transport; users wanted these split
    // so they can bypass tape effects while the loop keeps playing/recording.
    std::atomic<float> tapeLoopEnabled { 1.f }; // 1 = on, 0 = bypass loop transport

    // EQ panel open/closed UI state (editor-side only, persists via PluginSettings.json)
    std::atomic<float> eqPanelOpen { 0.f };  // editor UI state, persists via PluginSettings.json

    // Spectrum analyzers (public so editor's timerCallback can readLatest() for WebView push)
    SpectrumAnalyzer analyzerPre, analyzerPost;

    // Parametric EQ (one per channel) — public so editor's setEqSolo native fn can call setSolo()
    ParametricEQ eqL, eqR;

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

    // Tape Link: when 1, all 3 tape machines run in series (Studio →
    // Cassette → Wire) instead of just the active machine. Each machine
    // reads its own knob values from APVTS. (synced from JS, persisted
    // in DAW state and presets)
    std::atomic<float> tapeLinkEnabled { 0.f };

    // Delay freeze (now an APVTS param DLY_FREEZE; atomics replaced by APVTS + smoother)

    // Chorus enable/disable (bridged via setChorusEnabled native function)
    std::atomic<float> chorusEnabled { 1.0f };

    // Delay enable/disable (bridged via setDelayEnabled native function)
    std::atomic<float> delayEnabled { 1.0f };

    // Modulation engine (runs in processBlock, independent of editor window)
    ModulationEngine modulationEngine;

    // XY pad values (UI writes, audio reads)
    std::atomic<float> xyPadX { 0.5f };
    std::atomic<float> xyPadY { 0.5f };

    // XY pad master enable. When 0, the pad ignores clicks/drags (JS gate),
    // the cross-hair viz hides, the auto-play loop pauses, and XY-as-mod-source
    // contributions go neutral (we force xyPadX/Y to 0.5 so the mod engine sees
    // (xy-0.5)*2 == 0 for both axes, regardless of mod polarity). Persisted
    // via InstrumentSettings.json from the JS side; default ON.
    std::atomic<float> xyEnabled { 1.0f };

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

    // ── Slice play-glow ───────────────────────────────────────────────────
    // Per-slice envelope level for the WebView glow render. Audio thread
    // writes each block (max envelope across voices firing the same slice,
    // plus a slow visual decay so short one-shots leave a natural tail).
    // UI thread polls via snapshotSliceGlowLevels() at ~60 Hz.
    // Fixed cap — the slicer UI tops out at 32 grid chops; 256 is a safe
    // upper bound. Indices ≥ kMaxGlowSlots are silently dropped on write.
    static constexpr int kMaxGlowSlots = 256;
    std::array<std::atomic<float>, kMaxGlowSlots> sliceGlowLevel {};

    /** Snapshot the current glow levels for the first getNumSlices() slots
     *  into a juce::var array suitable for returning from a native fn. */
    juce::var snapshotSliceGlowLevels() const;

    // Sampler engine — promoted to public so PluginEditor can reach
    // synth.warpCache (warp-cache prewarm + setSource from sample-load
    // path). Keeping the rest of the processor state encapsulated.
    tw::TerrainSynth synth;

    // Source version counter — public so the editor can bump it from the
    // sample-load callback (keys the warp cache so stale entries never hit).
    std::atomic<int> sourceVersionId_ { 0 };

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static constexpr int kNumVoices = 32;  // bumped 16→32 for LAYER mode headroom (4 slices × 8 keys)
    tw::SampleBuffer sampleBuffer;
    tw::SampleLoader sampleLoader;

    // Slice list — atomic shared_ptr<const vector<Slice>>. UI writes via
    // std::atomic_store (replaceSlices), audio reads via std::atomic_load.
    tw::SliceListPtr slicesPtr;  // accessed via std::atomic_load/store

    // Audition queue: pending (sliceIndex, midiNoteEquivalent) pairs that
    // the editor pushes via auditionSlice(); processBlock injects synthetic
    // note events into the MIDI buffer. Guarded by a SpinLock — the queue
    // is only ever written from the message thread and drained on audio.
    juce::SpinLock                 auditionLock;
    std::vector<int>               auditionQueue;  // slice indices to audition
    int                            auditionNoteCounter = 100;  // unique synthetic note numbers (cycles 100..115)

    // Most-recently-loaded sample's absolute path. Persisted across DAW
    // project save/restore so the editor can re-load on next open.
    mutable juce::CriticalSection sampleSourcePathLock;
    juce::String                  loadedSamplePath;

    // Cached JS-payload JSON for the currently-loaded sample. Lives only as
    // long as the processor instance; not persisted to DAW state (would
    // bloat XML by ~80KB per sample with no win — DAW reload re-decodes
    // anyway because the audio buffer needs re-populating).
    mutable juce::CriticalSection samplePayloadLock;
    juce::String                  cachedSamplePayloadJson;

    // Grain engines (one per channel)
    GrainEngine grainEngineL;
    GrainEngine grainEngineR;

    // Tape processors (one per channel)
    TapeProcessor tapeProcessorL;
    TapeProcessor tapeProcessorR;

    // Tape loop (stereo — single instance)
    TapeLoopProcessor tapeLoop;

    // MF-104S delay (v6) — single stereo instance, internal cross-feed.
    MoogDelay moogDelay;

    // Terrain chorus (v6) — single stereo instance.
    TerrainChorus terrainChorus;

    juce::SmoothedValue<float> smoothedChorusAmount;
    juce::SmoothedValue<float> smoothedChorusWidth;
    juce::SmoothedValue<float> smoothedChorusCharacter;

    // Space reverb (stereo — single instance handles both channels)
    SpaceReverb spaceReverb;

    // (Parametric EQ moved to public section so editor's setEqSolo native fn can call setSolo)
    // (Spectrum analyzers moved to public section so editor can readLatest() for WebView push)

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

    // Smoothed parameters — Harmonic Sculptor (Studio v2.0)
    juce::SmoothedValue<float> smoothedStudioSculpt;
    juce::SmoothedValue<float> smoothedStudioWeave;
    juce::SmoothedValue<float> smoothedStudioTilt;

    // Smoothed parameters — Wire machine wow/saturation/hiss
    juce::SmoothedValue<float> smoothedWireWow;
    juce::SmoothedValue<float> smoothedWireSat;
    juce::SmoothedValue<float> smoothedWireHiss;

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

    // Parametric EQ smoothed values (audio-thread reads)
    std::array<juce::LinearSmoothedValue<float>, 7> smoothedEqBandFreq, smoothedEqBandGain, smoothedEqBandQ;
    juce::LinearSmoothedValue<float> smoothedEqHpFreq, smoothedEqLpFreq;

    // Smoothed parameters — delay
    juce::SmoothedValue<float> smoothedDlyTime;
    juce::SmoothedValue<float> smoothedDlyFeedback;
    juce::SmoothedValue<float> smoothedDlyTone;
    juce::SmoothedValue<float> smoothedDlyCharacter;
    juce::SmoothedValue<float> smoothedDlyMod;
    juce::SmoothedValue<float> smoothedDlyModRate;
    juce::SmoothedValue<float> smoothedDlyMix;
    juce::SmoothedValue<float> smoothedDlyDuck;
    juce::SmoothedValue<float> smoothedDelayFreeze;  // DLY_FREEZE: APVTS 0..1, threshold 0.5

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
    bool prevTapeOn = true; // Track tape-section toggle transitions for filter-state reset on re-enable

    // ── Per-chop FX independence capture bus ────────────────────────────
    // SamplerVoices whose chop has fxIndependent=true redirect their output
    // into this buffer instead of the synth's main output. The bus skips the
    // entire global FX chain and is added directly to the master at the end
    // of processBlock — clean signal. Allocated to host block size in
    // prepareToPlay; pointer pushed onto every voice each processBlock.
    juce::AudioBuffer<float> indyCaptureBus;

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
