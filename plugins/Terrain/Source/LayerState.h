// LayerState.h
// Per-sampler state bundle for the Terrain Mark 2 Layers MVP.
//
// The Mark 1.5 sampler is functionally a single instance of LayerState (Layer A).
// Phase 1 instantiates 4 copies (layers A/B/C/D) on the processor.
//
// Ownership notes:
//   - SamplerVoices take references to THIS layer's own atomics (not the
//     processor's) so each layer is fully independent at the audio thread.
//   - layerIndex is set once after construction by the processor; it is NOT
//     an atomic because it is only written at initialization and read later.
//   - hasSample() checks the SampleBuffer — safe from any thread.
//
// DO NOT include PluginProcessor.h here; the dependency must go the other way.
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include "SampleBuffer.h"
#include "Slice.h"
#include "TerrainConstants.h"
#include "TerrainSynth.h"
#include "SamplerVoice.h"

namespace tw
{
    struct LayerState
    {
        // ── Sample storage ────────────────────────────────────────────────────
        SampleBuffer sampleBuffer;

        // ── Per-layer atomics consumed by SamplerVoice ────────────────────────
        // These mirror the processor-level atomics that existed in Mark 1.5 but
        // are now owned per-layer so voices can be configured independently.
        std::atomic<int>   rootMidiNote   { 60 };      // default C4
        std::atomic<float> attackMsAtomic { 5.0f };
        std::atomic<float> releaseMsAtomic{ 800.0f };
        std::atomic<float> chopFadeMs     { 5.0f };    // anti-click ramp at slice boundaries
        std::atomic<int>   sampleLoopMode { 0 };       // 0 = one-shot, 1 = forward loop

        // ── Per-layer mode atomics ─────────────────────────────────────────────
        std::atomic<int> sliceMode   { 0 };   // 0 = PITCH, 1 = SLICE
        std::atomic<int> playMode    { 0 };   // 0 = 1-SHOT, 1 = LOOP
        std::atomic<int> sliceCount  { 4 };   // default grid division

        // ── Mixer ─────────────────────────────────────────────────────────────
        std::atomic<float> volume { 1.0f };   // linear gain, 0..2
        std::atomic<float> pan    { 0.0f };   // -1.0 = full L, +1.0 = full R, 0 = center
        std::atomic<bool>  mute   { false };
        std::atomic<bool>  solo   { false };  // strip-level solo (mixer mute-others)

        // ── Mix page — trigger-mode + creative routing (Phase 2) ──────────────
        // probabilityWeight feeds the RANDOM trigger mode's weighted picker.
        // keyZoneMin/Max define the KEYTRACK trigger mode's MIDI note range for this layer.
        // velocityZoneMin/Max define the VELOCITY trigger mode's range for this layer.
        // tp55 — VIBRATO replaces the old one-shot JITTER. Max: "jitter isn't broken, it's the
        // wrong feature — I want it to wobble like a Casio SK-1."  The old field sampled ONE random
        // detune at note-on and held it for the voice's life (a de-phaser for stacked layers, which
        // is exactly why it did nothing audible on a lone one-shot). These two drive a real periodic
        // pitch LFO in SamplerVoice: depth is the PEAK swing in cents, rate is its speed.
        std::atomic<float> probabilityWeight { 0.25f }; // 0..1, default uniform
        std::atomic<int>   keyZoneMin        { 0   };   // 0..127 MIDI note (default seeded by processor)
        std::atomic<int>   keyZoneMax        { 127 };   // 0..127 MIDI note
        std::atomic<int>   velocityZoneMin   { 0   };   // 0..127 inclusive (default seeded by processor)
        std::atomic<int>   velocityZoneMax   { 127 };   // 0..127 inclusive
        std::atomic<float> vibratoDepthCents { 0.0f };  // 0..100 cents PEAK swing (0 = off)
        std::atomic<float> vibratoRateHz     { 5.0f };  // 0.05..12 Hz

        // ── tp57 — THE BPM LOCK ────────────────────────────────────────────────────────────
        //  Max: "if something is locked onto that BPM then it has to be stretched to the BPM so
        //  everything can stay in time."  sourceBpm is what this layer's sample IS (read from its
        //  name, else from its length — Source/LoopTempo.h); timeStretchMul is what the processor
        //  works out from it each block and the voice applies at note-on. 0 / 1.0 = untouched.
        //  tp58 — THREE STATES, NOT TWO. 0 = "not analysed yet", -1 = "analysed, and it has no
        //  tempo", > 0 = the reading. The two-state version re-ran the whole analysis on EVERY
        //  timer tick for any sample it could not read, which the audio-listening detector turns
        //  from free into a 30 Hz sweep over the buffer.
        std::atomic<float> sourceBpm      { 0.0f };   // 0 = unread · -1 = read, unknown · >0 = BPM
        //  tp58 — THE NUMBER THE USER TYPED, and it wins. Detection is a reading, not a fact: a
        //  break with no number in its name and an odd length is genuinely ambiguous, and at the
        //  exact half/double midpoint no estimator can know. One editable field is the difference
        //  between a lock that works on your library and a lock that works on ours.
        std::atomic<float> sourceBpmUser  { 0.0f };   // 0 = auto (use sourceBpm)
        //  tp58 — WHICH BUFFER THE READING BELONGS TO. There are five places a sample can land
        //  and the resolve deliberately knows about none of them; keying the analysis to the
        //  buffer it analysed means a new sample is re-read whatever route it arrived by, and a
        //  load site added tomorrow is covered the day it is written. NEVER DEREFERENCED — it is
        //  compared and nothing else, so it cannot outlive anything.
        std::atomic<const void*> bpmReadFor { nullptr };
        std::atomic<float> timeStretchMul { 1.0f };   // 1.0 = off

        /** What the lock is actually working from: the typed number if there is one, else the
         *  reading, else 0 — and 0 still means do not stretch. */
        float effectiveSourceBpm() const noexcept
        {
            const float u = sourceBpmUser.load();
            if (u > 0.0f) return u;
            const float d = sourceBpm.load();
            return (d > 0.0f) ? d : 0.0f;
        }

        // ── Meters — post-volume peak per channel for the strip meter widget ──
        // Audio thread writes after summing into master; UI polls at ~30 Hz.
        std::atomic<float> peakLevelL { 0.0f };
        std::atomic<float> peakLevelR { 0.0f };

        // ── Slicer state ──────────────────────────────────────────────────────
        // Access via std::atomic_store / std::atomic_load on shared_ptr — see
        // PluginProcessor::replaceSlices for the write pattern. DO NOT assign directly.
        SliceListPtr currentSlices;
        Slice         pitchModeSlice;       // virtual whole-sample slice for PITCH mode
        std::atomic<int> activeSliceIndex { 0 }; // which chop is "active" in ChromaticOneSlice mode
        std::array<std::atomic<float>, tw::kMaxGlowSlots> sliceGlowLevel {};  // per-slice envelope glow

        // ── Synth ─────────────────────────────────────────────────────────────
        TerrainSynth synth;

        // ── Identity ──────────────────────────────────────────────────────────
        int          layerIndex    = 0;     // assigned post-construction (0=A,1=B,2=C,3=D)
        juce::String sourceFileName;        // display name of the loaded sample
        juce::String sourcePath;            // full filesystem path — used by V2 preset save/restore

        // ── Constructor ───────────────────────────────────────────────────────
        // Adds one SamplerSound (so the synth has a valid sound list) then
        // populates kSamplerVoicesPerLayer (64, tp101 — was 32) SamplerVoices, each referencing THIS layer's atomics.
        // ModulationEngine and WarpRenderCache are optional; pass nullptr to omit
        // (they can be wired later via the processor, which holds the real instances).
        LayerState()
        {
            synth.addSound (new SamplerSound());

            for (int i = 0; i < tw::kSamplerVoicesPerLayer; ++i)
            {
                synth.addVoice (new SamplerVoice (
                    sampleBuffer,
                    rootMidiNote,
                    attackMsAtomic,
                    releaseMsAtomic,
                    sampleLoopMode,
                    nullptr,                     // ModulationEngine — wired by processor
                    &synth.warpCache,            // WarpRenderCache from this layer's synth
                    &chopFadeMs,
                    &vibratoDepthCents,          // tp55 — per-layer VIBRATO depth (cents)
                    &vibratoRateHz,              // tp55 — per-layer VIBRATO rate (Hz)
                    &timeStretchMul));           // tp57 — the BPM lock's stretch, resolved per block
            }
        }

        // Non-copyable, non-movable — holds atomics and owns synth voices
        LayerState (const LayerState&)            = delete;
        LayerState& operator= (const LayerState&) = delete;
        LayerState (LayerState&&)                 = delete;
        LayerState& operator= (LayerState&&)      = delete;

        // ── Accessors ─────────────────────────────────────────────────────────
        /** True when a sample has been loaded into this layer's buffer. */
        bool hasSample() const noexcept
        {
            auto buf = sampleBuffer.load();
            return buf && buf->getNumSamples() > 0;
        }
    };
}
