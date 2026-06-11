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
#include "TerrainConstants.h"
#include "LayerState.h"
#include "IndyFxChain.h"
#include "SynthVoice.h"
#include "WavetableBank.h"
#include "SpectralMorph.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <limits>
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
// Phase 8a/8b — Synth wrapper. Originally subclassed Synthesiser to override
// noteOn so it spawned UNISON×N voices per note. Phase 8b moved UNISON in-voice
// (one voice computes all unison sines internally).
// Phase 8b polish-3 — re-overrides noteOn to enforce VOICES knob as a true
// polyphony cap: count currently-active voices, steal the oldest with tail-off
// before allocating if cap reached. Mirrors Serum 2 behavior (8 voices = exactly
// 8 simultaneously, new notes steal old).
class UnisonSynth : public juce::Synthesiser
{
public:
    void setVoiceCap (int cap) noexcept { voiceCap_ = juce::jlimit (1, 96, cap); }

    /** MONO/LEGATO voice modes, pushed per-block from the processor (audio thread —
     *  same thread as noteOn/noteOff, no extra locking needed). The held-note stack
     *  is cleared whenever MONO flips so stale notes can't resurrect later. */
    void setVoiceModes (bool mono, bool legato) noexcept
    {
        if (mono != monoMode_) heldCount_ = 0;
        monoMode_   = mono;
        legatoMode_ = legato;
    }

    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override
    {
        if (monoMode_) { monoNoteOn (midiChannel, midiNoteNumber, velocity); return; }

        const juce::ScopedLock sl (lock);

        // Phase 12 — Serum-2 style smooth voice steal. Count only voices that
        // are AUDIBLY active (held or in release). Voices in steal-fade are
        // already dying — they don't count toward the cap, even though their
        // currentlyPlayingNote is still set so JUCE's findFreeVoice doesn't
        // hijack their slot mid-fade. The dying voice fades on its own slot
        // while the new note rises on a fresh idle slot from the 96-pool.
        int activeCount = 0;
        for (auto* v : voices)
        {
            if (v == nullptr) continue;
            if (v->getCurrentlyPlayingNote() < 0) continue;
            if (auto* sv = dynamic_cast<tw::SynthVoice*> (v); sv && sv->isStealing()) continue;
            ++activeCount;
        }

        // If at cap, steal the OLDEST non-stealing voice (Serum 2 picks oldest;
        // we use a monotonic noteStartStamp set in startNote). Loop in case we
        // need to steal multiple (e.g. rapid chord change with cap drop).
        while (activeCount >= voiceCap_)
        {
            tw::SynthVoice* oldest = nullptr;
            juce::uint32    oldestStamp = std::numeric_limits<juce::uint32>::max();
            for (auto* v : voices)
            {
                auto* sv = dynamic_cast<tw::SynthVoice*> (v);
                if (sv == nullptr) continue;
                if (sv->getCurrentlyPlayingNote() < 0) continue;
                if (sv->isStealing()) continue;
                const auto stamp = sv->getNoteStartStamp();
                if (stamp < oldestStamp) { oldestStamp = stamp; oldest = sv; }
            }
            if (oldest == nullptr) break;
            // stopVoice → SynthVoice::stopNote(0, allowTailOff=false) starts the
            // 30ms fade and (since Phase 12) does NOT clear the slot, so the next
            // findFreeVoice call lands on a different idle slot from the pool.
            stopVoice (oldest, 0.0f, false);
            --activeCount;
        }

        juce::Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
    }

    void noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override
    {
        if (monoMode_) { monoNoteOff (midiChannel, midiNoteNumber, velocity, allowTailOff); return; }
        juce::Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff);
    }

    void allNotesOff (int midiChannel, bool allowTailOff) override
    {
        heldCount_ = 0;   // never let panic/transport-stop leave stale stack entries
        juce::Synthesiser::allNotesOff (midiChannel, allowTailOff);
    }

private:
    // ── MONO / LEGATO ─────────────────────────────────────────────────────────
    // Last-note priority with return-to-held: releasing the sounding note while
    // older keys are still down returns to the most recent of them. LEGATO makes
    // overlapped transitions retarget the SAME voice (glide, no env retrigger);
    // non-legato mono steal-fades (30ms) and retriggers — both click-free.
    struct Held { int note = -1; float vel = 0.0f; };

    void pushHeld (int note, float vel) noexcept
    {
        removeHeld (note);                                   // dedupe (key repeat)
        if (heldCount_ < kMaxHeld) held_[heldCount_++] = { note, vel };
    }

    void removeHeld (int note) noexcept
    {
        for (int i = 0; i < heldCount_; ++i)
            if (held_[i].note == note)
            {
                for (int j = i; j < heldCount_ - 1; ++j) held_[j] = held_[j + 1];
                --heldCount_;
                return;
            }
    }

    /** Newest non-stealing active SynthVoice (the audible mono voice). */
    tw::SynthVoice* findActiveSynthVoice() noexcept
    {
        tw::SynthVoice* newest = nullptr;
        juce::uint32 newestStamp = 0;
        for (auto* v : voices)
        {
            auto* sv = dynamic_cast<tw::SynthVoice*> (v);
            if (sv == nullptr || sv->getCurrentlyPlayingNote() < 0 || sv->isStealing()) continue;
            if (newest == nullptr || sv->getNoteStartStamp() >= newestStamp)
            { newestStamp = sv->getNoteStartStamp(); newest = sv; }
        }
        return newest;
    }

    juce::SynthesiserSound* firstSoundFor (int midiChannel, int note) noexcept
    {
        for (auto* s : sounds)
            if (s->appliesToNote (note) && s->appliesToChannel (midiChannel))
                return s;
        return nullptr;
    }

    void monoNoteOn (int midiChannel, int midiNoteNumber, float velocity)
    {
        const juce::ScopedLock sl (lock);
        const bool wasHeld = heldCount_ > 0;
        pushHeld (midiNoteNumber, velocity);

        auto* active = findActiveSynthVoice();
        if (legatoMode_ && wasHeld && active != nullptr)
        {
            if (auto* sound = firstSoundFor (midiChannel, midiNoteNumber))
            {
                active->beginLegatoRetarget();     // glide to the new pitch, retrigger nothing
                startVoice (active, sound, midiChannel, midiNoteNumber, velocity);
                return;
            }
        }
        // Hard mono (or the first note of a legato phrase): steal-fade every sounding
        // voice (30ms smooth fade), then trigger the new note — envelope retriggers.
        for (auto* v : voices)
        {
            auto* sv = dynamic_cast<tw::SynthVoice*> (v);
            if (sv != nullptr && sv->getCurrentlyPlayingNote() >= 0 && ! sv->isStealing())
                stopVoice (sv, 0.0f, false);
        }
        juce::Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
    }

    void monoNoteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff)
    {
        const juce::ScopedLock sl (lock);
        removeHeld (midiNoteNumber);

        auto* active = findActiveSynthVoice();
        const bool releasedWasSounding = active != nullptr
                                      && active->getCurrentlyPlayingNote() == midiNoteNumber;
        if (! releasedWasSounding)
        {
            // A stacked (non-sounding) key came up — nothing audible changes; let the
            // base clean up any lingering voice bookkeeping for that note.
            juce::Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff);
            return;
        }
        if (heldCount_ > 0)
        {
            const Held ret = held_[heldCount_ - 1];   // most recent still-held key
            if (legatoMode_)
            {
                if (auto* sound = firstSoundFor (midiChannel, ret.note))
                {
                    active->beginLegatoRetarget();
                    startVoice (active, sound, midiChannel, ret.note, ret.vel);
                    return;
                }
            }
            stopVoice (active, 0.0f, false);                                // fade the released note
            juce::Synthesiser::noteOn (midiChannel, ret.note, ret.vel);     // retrigger the held one
            return;
        }
        juce::Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff);
    }

    static constexpr int kMaxHeld = 64;
    Held held_[kMaxHeld] = {};
    int  heldCount_   = 0;
    bool monoMode_    = false;
    bool legatoMode_  = false;

    int voiceCap_ = 32;  // safe default; PluginProcessor pushes the real value per-block
};

//==============================================================================
class TerrainInstrumentAudioProcessor  : public juce::AudioProcessor,
                                         private juce::Timer
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
    /** Returns the sample buffer for the currently-editing layer.
     *  Task 5: routes through layers[editingLayer] instead of the old singleton. */
    tw::SampleBuffer& getSampleBuffer() noexcept { return layers[(size_t) editingLayer.load()].sampleBuffer; }
    tw::SampleLoader& getSampleLoader() noexcept { return sampleLoader; }

    // ── Slicer state ──────────────────────────────────────────────────────
    // Slice list — atomic snapshot pointer. UI thread writes via
    // replaceSlices(); audio thread reads via loadSlices() / readSlices().
    // The shared_ptr is treated as immutable — never modified after store.
    // Task 5: routes through layers[editingLayer].currentSlices.
    void              replaceSlices (tw::SliceList newSlices);
    tw::SliceListPtr  loadSlices() const;
    int               getNumSlices() const;
    juce::String      getSlicesJson() const;
    void              setSlicesFromJson (const juce::String& json);

    // ── Pitch-mode virtual slice ───────────────────────────────────────────
    // When SLICE_MODE == 0 (PITCH), the whole sample is played as a single
    // chromatic one-shot. This virtual slice carries per-chop features
    // (warp, ADSR, scan, reverse, volume) editable via the lab card UI.
    // Access is message-thread only (read + write from JS native fns and
    // state-info callbacks). The audio thread reads a copy via SliceContext.
    // Task 5: owned by LayerState; the processor delegates to layers[editingLayer].
    juce::String      getPitchSliceJson() const;

    // ── HOLD mode (Mark 1.5) ───────────────────────────────────────────────
    // When true, voices ignore MIDI note-off (with tail-off allowed) and play
    // their chop to natural completion (1-shot exhaustion / loop boundary).
    // MPC/FL "latch" feel. Toggled by clicking the CHOP submode pill a second
    // time (cycle: CHOP → HOLD → CHOP). Read into SliceContext.holdMode at the
    // top of processBlock; captured into each VoiceConfig at startNote so
    // toggling mid-playback doesn't retroactively change in-flight voices.
    std::atomic<bool> holdMode { false };

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
    // Task 5: owned by LayerState; route via layers[editingLayer].activeSliceIndex.

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

    // Cached JSON payload for each layer's loaded sample (filename + peaks + meta).
    // Per-layer as of Mark 2 Phase 1 editor-reopen fix: each pad load caches its
    // own payload so close/reopen restores ALL 4 layers' waveforms, not just the
    // editing one. Populated by the editor after each successful load.
    // layerIdx = -1 (default) means "use the currently-editing layer".
    void setCachedSamplePayload (const juce::String& jsonPayload, int layerIdx = -1);
    juce::String getCachedSamplePayload (int layerIdx = -1) const;
    // Snapshot of all 4 layer payloads (empty strings for layers with no sample).
    // Used by the getAllLayerPayloads native fn to hydrate JS mirrors on editor open.
    std::array<juce::String, 4> getAllLayerPayloads() const;

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

    /** Snapshot the current glow levels for the first getNumSlices() slots
     *  into a juce::var array suitable for returning from a native fn.
     *  Task 5: reads from layers[editingLayer].sliceGlowLevel. */
    juce::var snapshotSliceGlowLevels() const;

    // Source version counter — public so the editor can bump it from the
    // sample-load callback (keys the warp cache so stale entries never hit).
    std::atomic<int> sourceVersionId_ { 0 };

    // ── Mark 2 layers (A/B/C/D) — see LayerState.h ──────────────────────────
    // Phase 1 task 2: array declared and constructed; not yet wired into the
    // audio path (that's task 3/4). editingLayer tracks which layer the UI
    // is currently targeting (0..3 = A/B/C/D).
    std::array<tw::LayerState, 4> layers;
    std::atomic<int> editingLayer { 0 };

    // Mark 2 task 4: per-layer scratch buffers for the 4 layer synths to render
    // into, summed (with vol/mute/solo) into the master `buffer` in processBlock.
    // Sized in prepareToPlay to (2 channels, samplesPerBlock).
    std::array<juce::AudioBuffer<float>, 4> layerScratch;

    // ── Mix page Phase 2: trigger-mode state ──────────────────────────────────
    // triggerMode picks how MIDI notes are dispatched across the 4 layers:
    //   0 = LAYER         (all populated layers fire — current Phase 1 behavior)
    //   1 = ROUND-ROBIN   (one layer per note, cycles A→B→C→D, skips empties)
    //   2 = RANDOM        (one layer per note, weighted by probabilityWeight)
    //   3 = KEYTRACK      (one layer per note, picked by keyZone match)
    //   4 = VELOCITY      (one layer per note, picked by velocityZone match)
    std::atomic<int>  triggerMode    { 0 };
    std::atomic<int>  roundRobinPos  { 0 };   // cursor for RR (0..3)
    std::atomic<bool> rrSyncToBar    { false };
    std::atomic<bool> rrShuffle      { false }; // RR: random non-repeating order
    int               lastRrLayer    { -1 };    // audio-thread: last layer RR fired (shuffle anti-repeat)
    // LAYER-mode MORPH: 0..1 travels a blend focus across the populated layers.
    // 0.5 (default) keeps all audible with B/C forward; sweep isolates toward A or D.
    std::atomic<float> layerMorph    { 0.5f };
    // Stem source: 0 = DRY (pre volume/pan), 1 = MIX (post volume/pan, pre shared FX).
    std::atomic<int>  stemSourceMode { 0 };
    // Live "what's being written to the buffer" level per layer, decaying peak
    // (audio thread writes, UI polls ~30Hz). Drives the 4 mini-meters on the stem row.
    std::atomic<float> stemCaptureLevel[4] = { {0.0f}, {0.0f}, {0.0f}, {0.0f} };

    // PRNG for the RANDOM trigger picker. juce::Random::nextFloat is realtime-safe.
    juce::Random      triggerRandom;

    // ── Mix page Phase D: per-layer rolling stem buffers ──────────────────────
    // 1 minute @ session SR per layer, stereo float32. Total ~92 MB at 48kHz.
    // Always allocated (in prepareToPlay) so capture is "alive" from session
    // start; user clicks STEM A/B/C/D button → snapshot the ring as a WAV.
    // Race-safe enough for v1: audio thread writes at writeIndex, UI thread
    // reads the whole buffer + writeIndex on export; one-sample tear at the
    // wrap boundary is inaudible.
    static constexpr int kStemSeconds = 600;  // 10 min rolling history per layer (user request)
    // NOTE: 600s x 4 layers x stereo x float32 @ 48k ~= 921 MB allocated in
    // prepareToPlay. Heavy but user-accepted. If RAM becomes an issue, switch
    // to int16 storage (halves it) or lazy per-layer allocation on sample load.
    struct StemBuffer
    {
        juce::AudioBuffer<float> ring;
        std::atomic<int>         writeIndex     { 0 };
        // Cumulative samples written since the buffer started or was last CLEARed.
        // Saturates at totalSize — once it reaches totalSize, the ring is "full"
        // and exports unwrap the full 10-min rolling window; below totalSize, the
        // export only writes the actual captured portion (no silent pad).
        std::atomic<int>         samplesWritten { 0 };
        int                      totalSize  { 0 };  // ring.getNumSamples()
    };
    std::array<StemBuffer, 4> stemBuffers;
    // 5th ring: post-FX master output (the final mixed-through-effects signal).
    // Captured in lockstep with the layer rings. WET stem export uses
    // per-sample energy ratios to attribute master_fx back to each layer.
    StemBuffer masterFxBuffer;
    void allocateStemBuffers (double sampleRate);
    void writeToStemBuffer (int layerIdx, const float* L, const float* R, int numSamples);
    void writeToMasterFxRing (const float* L, const float* R, int numSamples);
    // Export. layerIdx in [0..3] = single stem. dest is the chosen folder.
    // Returns the written file path; empty if export failed.
    juce::File exportStemToFile (int layerIdx, const juce::File& dest);
    // CLEAR — zero all 4 rolling buffers + reset write cursors so the next
    // capture starts fresh (user-triggered button).
    void clearStemBuffers();

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static constexpr int kNumVoices = 32;  // bumped 16→32 for LAYER mode headroom (4 slices × 8 keys)

    // ── Synth section (Phase 1 MPV — see Design/v1-syn-spec.md) ──────────
    // Parallel pipeline beside the 4 layers. juce::Synthesiser owns 8
    // SynthVoices + 1 SynthSound. Renders into synthScratch each block,
    // summed into the master `buffer` before the FX chain.
    static constexpr int kSynthVoiceCount = 96;  // 8a polish-2: bumped from 32 — UNISON=8 × polyphony=8 fits without steal

    // PORTAMENTO — audio-thread glide tracking (origin note + held count for ALWAYS-off gating).
    float synthGlideFrom_  = -1.0f;   // last synth note (pitch to glide FROM); -1 = none yet
    int   synthNotesHeld_  = 0;       // synth notes currently sounding
    UnisonSynth                 synthEngine;   // Phase 8a: was juce::Synthesiser
    juce::AudioBuffer<float>    synthScratch;

    // ── Synth wavetable bank (Phase 2A) ──────────────────────────────────
    // Owns the 6 iconic analog tables, constructed at startup (~750KB RAM).
    // SynthVoices hold const Wavetable* pointers into this bank; bank
    // outlives all voices (member-of-processor lifetime).
    tw::WavetableBank           wavetableBank;

    // ── Spectral Morph (Phase 11c rework) ────────────────────────────────
    // Per-OSC morphed wavetable. SpectralMorph::apply + buildFromSpec is ~5.6ms
    // (too heavy for the audio thread), so the morphed table is rebuilt on the
    // message thread (timerCallback) into a DOUBLE BUFFER and atomic-published
    // to the voices. The audio thread only ever does an atomic pointer load and
    // reports which buffer it's reading (audioReadingIdx); the message thread
    // never rebuilds the buffer the audio thread is currently on. mode == None
    // publishes nullptr → voices fall back to the plain bank table.
    struct MorphSlot
    {
        tw::Wavetable                     buf[2];
        std::atomic<const tw::Wavetable*> live { nullptr };
        std::atomic<int>                  audioReadingIdx { -1 };  // 0/1 = buf in use, -1 = none
        int   buildIdx    = 0;            // message-thread: next buffer to build into
        int   builtPreset = -1;
        int   builtMode   = -1;
        float builtAmount = -1.0f;
    };
    MorphSlot morphA_, morphB_;

    void timerCallback() override;        // message thread — rebuilds morph tables
    void rebuildMorphIfNeeded (MorphSlot& slot,
                               const juce::String& presetId,
                               const juce::String& modeId,
                               const juce::String& amtId);
    // Resolve the wavetable a voice should read for one OSC: the morphed table
    // when a morph mode is active, else the plain bank table. Publishes which
    // buffer the audio thread is reading (for the rebuild guard).
    const tw::Wavetable* resolveMorphTable (MorphSlot& slot, int presetIdx) noexcept;
    // sampleBuffer removed in Task 5 — owned by LayerState. Access via layers[editingLayer].sampleBuffer.
    tw::SampleLoader sampleLoader;
    // slicesPtr removed in Task 5 — owned by LayerState as currentSlices. Access via layers[editingLayer].currentSlices.

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

    // Per-layer cached JS-payload JSON for each loaded sample (Mark 2 Phase 1).
    // Index 0..3 = layers A/B/C/D. Empty string for layers with no sample.
    // Lives only as long as the processor instance; not persisted to DAW state
    // (would bloat XML by ~80KB × 4 with no win — DAW reload re-decodes anyway
    // because the audio buffers need re-populating).
    mutable juce::CriticalSection             samplePayloadLock;
    std::array<juce::String, 4>               cachedLayerPayloads;

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

    // Per-chop FX-independence (option 1: shared chain). After layers
    // render, the processor ORs all currently-playing indy voices' masks
    // into activeIndyMask, then runs indyChain.processInto(indyCaptureBus,
    // indySumBuffer, numSamples) BEFORE the per-sample master loop. The
    // loop reads indySumBuffer per sample and adds it to leftChannel /
    // rightChannel at the same spot the old indy add-back lived.
    tw::IndyFxChain          indyChain;
    juce::AudioBuffer<float> indySumBuffer;

    // Rolling capture buffer
    RollingCaptureBuffer captureBuffer;
    std::unique_ptr<std::thread> captureExportThread;
    juce::String lastCaptureFilePath;

    /** Snapshots APVTS-driven FX parameter values into a IndyFxChain::
     *  ParamTargets struct. Reads only atomics + dynamic_cast lookups —
     *  RT-safe. Scaling conventions match the global chain (see lessons
     *  baked-in section of the implementation plan). */
    tw::IndyFxChain::ParamTargets snapshotFxParamTargets() const noexcept;

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

    // ── Preset load helpers (Task 13) ────────────────────────────────────────
    // Split from setStateInformation so V1 and V2 blobs follow separate paths.
    // Both helpers clear ALL 4 layers first, then populate their respective sets.
    void loadV1State (const juce::ValueTree& loaded);
    void loadV2State (const juce::ValueTree& loaded);
    // Parses a pitchSliceJson string into a LayerState's pitchModeSlice.
    // Centralises the deserialization logic shared by V1 and V2 paths.
    static void applyPitchSliceJson (const juce::String& psJson,
                                     tw::LayerState& layer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TerrainInstrumentAudioProcessor)
};
