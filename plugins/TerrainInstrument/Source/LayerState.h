// LayerState.h
// Per-sampler state bundle for the Terrain Instrument Mark 2 Layers MVP.
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
        // pitchJitterCents adds ±cents random per-voice pitch offset to thicken stacks.
        std::atomic<float> probabilityWeight { 0.25f }; // 0..1, default uniform
        std::atomic<int>   keyZoneMin        { 0   };   // 0..127 MIDI note (default seeded by processor)
        std::atomic<int>   keyZoneMax        { 127 };   // 0..127 MIDI note
        std::atomic<int>   velocityZoneMin   { 0   };   // 0..127 inclusive (default seeded by processor)
        std::atomic<int>   velocityZoneMax   { 127 };   // 0..127 inclusive
        std::atomic<float> pitchJitterCents  { 0.0f };  // 0..100 cents random

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
        // populates 32 SamplerVoices, each referencing THIS layer's atomics.
        // ModulationEngine and WarpRenderCache are optional; pass nullptr to omit
        // (they can be wired later via the processor, which holds the real instances).
        LayerState()
        {
            synth.addSound (new SamplerSound());

            for (int i = 0; i < 32; ++i)
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
                    &pitchJitterCents));         // Mix page Phase B per-layer jitter
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
