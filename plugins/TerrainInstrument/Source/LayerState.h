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
#include "TerrainSynth.h"
#include "SamplerVoice.h"

namespace tw
{
    struct LayerState
    {
        // ── Glow-slot constant (matches PluginProcessor kMaxGlowSlots) ────────
        static constexpr int kMaxGlowSlots = 256;

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
        std::atomic<bool>  mute   { false };
        std::atomic<bool>  solo   { false };

        // ── Slicer state ──────────────────────────────────────────────────────
        SliceListPtr  currentSlices;        // atomic snapshot — write from UI, read from audio
        Slice         pitchModeSlice;       // virtual whole-sample slice for PITCH mode
        std::atomic<int> activeSliceIndex { 0 }; // which chop is "active" in ChromaticOneSlice mode
        std::array<std::atomic<float>, kMaxGlowSlots> sliceGlowLevel {};  // per-slice envelope glow

        // ── Synth ─────────────────────────────────────────────────────────────
        TerrainSynth synth;

        // ── Identity ──────────────────────────────────────────────────────────
        int          layerIndex    = 0;     // assigned post-construction (0=A,1=B,2=C,3=D)
        juce::String sourceFileName;        // display name of the loaded sample

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
                    &chopFadeMs));
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
