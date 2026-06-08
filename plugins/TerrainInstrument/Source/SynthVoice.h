// SynthVoice.h — Terrain Instrument synth section, Phase 1 (MPV)
// One PolyBLEP saw oscillator → juce::dsp::LadderFilter (LPF24) → juce::ADSR.
// Mirrors the SamplerVoice.h pattern (header-only). 8 voices allocated by
// PluginProcessor against a single SynthSound sentinel.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Wavetable.h"
#include "TerrainFilters.h"
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>

namespace tw
{
    /** Sentinel sound — accepts every MIDI note and channel so the
     *  Synthesiser will always dispatch to SynthVoice. */
    class SynthSound : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote    (int /*midiNoteNumber*/) override { return true; }
        bool appliesToChannel (int /*midiChannel*/)    override { return true; }
    };

    /** One synth voice — Phase 1 MPV.
     *  PolyBLEP saw oscillator → AMP ADSR → pan.
     *  Subsequent phases (per Design/v1-syn-spec.md) add filter,
     *  more engines, filter envelope, cross-mod, FLOW glide, etc. */
    class SynthVoice : public juce::SynthesiserVoice
    {
    public:
        SynthVoice() = default;

        /** Phase 3 — OSC engine choice. Order matches the SYN_OSC_A_ENGINE
         *  StringArray in createParameterLayout: WT, SAMP, GRAN, SPEC, FM, NOISE. */
        enum class Engine : int { WT = 0, SAMP = 1, GRAN = 2, SPEC = 3, FM = 4, NOISE = 5 };

        static constexpr int kMaxUnison = 8;

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<SynthSound*> (s) != nullptr;
        }

        void setCurrentPlaybackSampleRate (double sr) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
            sampleRate_ = (sr > 0.0) ? sr : 48000.0;
            ampEnv_.setSampleRate (sampleRate_);
            ampEnv_.setParameters (ampParams_);
        }

        /** Set AMP envelope params. attackMs/decayMs/releaseMs are milliseconds;
         *  sustain is 0..1. Called from PluginProcessor each block. */
        void setAmpEnvelopeParameters (float attackMs, float decayMs,
                                       float sustain, float releaseMs) noexcept
        {
            ampParams_.attack  = juce::jmax (0.001f, attackMs  * 0.001f);
            ampParams_.decay   = juce::jmax (0.001f, decayMs   * 0.001f);
            ampParams_.sustain = juce::jlimit (0.0f, 1.0f, sustain);
            ampParams_.release = juce::jmax (0.001f, releaseMs * 0.001f);
            ampEnv_.setParameters (ampParams_);
        }

        /** Called from PluginProcessor::prepareToPlay. Sizes filter + caches
         *  block-size for the per-block AudioBlock view. Phase 9: always stereo
         *  (2 channels) so OSC A + OSC B can be panned independently.
         *  Phase 8a: also prepares per-channel HORIZON high-shelf filters. */
        void prepareToPlay (double sr, int samplesPerBlock, int /*numChannels*/) noexcept
        {
            setCurrentPlaybackSampleRate (sr);
            sampleRate_ = sr;
            juce::dsp::ProcessSpec spec;
            spec.sampleRate       = sr;
            spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
            spec.numChannels      = 2;   // always stereo for OSC A + B per-osc pan

            // Batch 1 Filter — FilterSlot replaces the hardwired juce::dsp::LadderFilter.
            // Filter selection + cutoff + res + drive all come per-block from
            // PluginProcessor via setFilterType/setFilterCut/...; cutoff is
            // modulated per-sample inside renderNextBlock by the FLT envelope
            // and per-voice EROSION drift.
            filterSlot_.prepare (sr);
            filterSlot2_.prepare (sr);

            // Filter ADSR — independent from amp env, drives cutoff via the
            // bipolar SYN_FILTER1_ENV knob (the "signed amount" of this env).
            fltEnv_.setSampleRate (sr);
            fltEnv_.setParameters (fltEnvParams_);
            fltEnv_.reset();

            // Per-voice EROSION drift state. One-pole-LP'd uniform noise at
            // ~0.5 Hz so the random walk happens slowly (analog-like). Per-
            // voice seed so two voices don't drift in lockstep.
            const std::uint32_t voiceHash = static_cast<std::uint32_t> (
                                                reinterpret_cast<std::uintptr_t> (this))
                                          ^ 0xC0FFEE17u;
            driftRng_.setSeed ((juce::int64) voiceHash);
            driftState_ = 0.0f;
            const float driftCutHz = 0.5f;   // §5 of prompt — sub-Hz LP
            driftCoef_  = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                                 * driftCutHz / (float) sr);

            // Phase 8a — HORIZON shelves (one per stereo channel, mono spec)
            juce::dsp::ProcessSpec monoSpec;
            monoSpec.sampleRate       = sr;
            monoSpec.maximumBlockSize = (juce::uint32) samplesPerBlock;
            monoSpec.numChannels      = 1;
            horizonShelfL_.prepare (monoSpec);
            horizonShelfR_.prepare (monoSpec);
            horizonShelfL_.reset();
            horizonShelfR_.reset();
            *horizonShelfL_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 2500.0f, 0.7071f, 1.0f);
            *horizonShelfR_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 2500.0f, 0.7071f, 1.0f);

            // Phase 11c — SPECTRAL filter init (per OSC per channel)
            spectralFilterAL_.prepare (monoSpec);
            spectralFilterAR_.prepare (monoSpec);
            spectralFilterBL_.prepare (monoSpec);
            spectralFilterBR_.prepare (monoSpec);
            spectralFilterAL_.reset();
            spectralFilterAR_.reset();
            spectralFilterBL_.reset();
            spectralFilterBR_.reset();
            // Initialize with safe passthrough (high-cutoff LP)
            auto passthrough = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 20000.0f, 0.707f);
            *spectralFilterAL_.coefficients = *passthrough;
            *spectralFilterAR_.coefficients = *passthrough;
            *spectralFilterBL_.coefficients = *passthrough;
            *spectralFilterBR_.coefficients = *passthrough;
        }

        /** Batch 1 Filter — per-block from PluginProcessor. Cutoff/res are
         *  the BASE values; the per-sample audio loop adds env and drift
         *  before feeding setParams to the active FilterSlot. */
        void setFilterParameters (float cutoffHz, float resonance) noexcept
        {
            baseCutHz_   = juce::jlimit (20.0f, 20000.0f, cutoffHz);
            baseRes01_   = juce::jlimit (0.0f,  1.0f,    resonance);
        }
        void setFilterType (int typeIdx) noexcept
        {
            const int clamped = juce::jlimit (0, (int) tw::filters::kNumTypes - 1, typeIdx);
            filterType1_ = clamped;
            filterSlot_.setType (static_cast<tw::filters::Type> (clamped));
        }
        // ── Filter 2 (independent) + routing/mix setters ──
        void setFilterParameters2 (float cutoffHz, float resonance) noexcept
        {
            baseCutHz2_ = juce::jlimit (20.0f, 20000.0f, cutoffHz);
            baseRes012_ = juce::jlimit (0.0f,  1.0f,    resonance);
        }
        void setFilterType2 (int typeIdx) noexcept
        {
            const int clamped = juce::jlimit (0, (int) tw::filters::kNumTypes - 1, typeIdx);
            filterType2_ = clamped;
            filterSlot2_.setType (static_cast<tw::filters::Type> (clamped));
        }
        void setFilterDrive2 (float drv01) noexcept   { drv012_ = juce::jlimit (0.0f, 1.0f, drv01); }
        void setFilterEnvAmount2 (float env) noexcept { envAmount2_ = juce::jlimit (-1.0f, 1.0f, env); }
        void setFilterMix1 (float mix) noexcept       { filterMix1_ = juce::jlimit (0.0f, 1.0f, mix); }
        void setFilterMix2 (float mix) noexcept       { filterMix2_ = juce::jlimit (0.0f, 1.0f, mix); }
        void setFilterRouting (int mode) noexcept     { filterRouting_ = (mode != 0) ? 1 : 0; }
        void setFilterDrive (float drv01) noexcept
        {
            drv01_ = juce::jlimit (0.0f, 1.0f, drv01);
        }
        /** Bipolar -1..+1 amount of the FLT envelope applied to cutoff
         *  in semitone space (±96 ST at ±1.0). Sign inverts the env. */
        void setFilterEnvAmount (float env) noexcept
        {
            envAmount_ = juce::jlimit (-1.0f, 1.0f, env);
        }
        /** FLT envelope ADSR (ms / 0..1 / ms / ms). */
        void setFilterEnvParameters (float attackMs, float decayMs,
                                     float sustain,  float releaseMs) noexcept
        {
            fltEnvParams_.attack  = juce::jmax (0.001f, attackMs  * 0.001f);
            fltEnvParams_.decay   = juce::jmax (0.001f, decayMs   * 0.001f);
            fltEnvParams_.sustain = juce::jlimit (0.0f, 1.0f, sustain);
            fltEnvParams_.release = juce::jmax (0.001f, releaseMs * 0.001f);
            fltEnv_.setParameters (fltEnvParams_);
        }
        /** EROSION 0..1 (per-block from APVTS / 100). Scaled erosion^1.8
         *  applied as semitone drift to cutoff inside renderNextBlock. */
        void setErosionAmount_filter (float e) noexcept
        {
            // (Different field from the existing erosionAmount_ which feeds
            //  pitch drift — this one is for cutoff drift.)
            fltErosionAmount_ = juce::jlimit (0.0f, 1.0f, e);
        }

        /** Linear gain 0..1 — applied after filter, before pan. */
        void setLevel (float level) noexcept
        {
            level_ = juce::jlimit (0.0f, 1.0f, level);
        }

        /** Equal-power pan -1 (full L) .. 0 (center) .. +1 (full R). */
        void setPan (float pan) noexcept
        {
            const float p = juce::jlimit (-1.0f, 1.0f, pan);
            const float angle = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panL_ = std::cos (angle);
            panR_ = std::sin (angle);
        }

        /** Octave (-3..+3), semitone (-12..+12), cents (-100..+100). Applied
         *  on top of the MIDI note when computing the next note-on frequency.
         *  Updates current note pitch live if a note is already playing. */
        void setTuning (int oct, int semi, float cent) noexcept
        {
            octOffset_   = oct;
            semiOffset_  = semi;
            centsOffset_ = cent;
            if (playing_)
                updateUnisonPhaseIncrementsA (currentMidiNote_);
        }

        /** Set which wavetable this voice reads from. Pointer is borrowed —
         *  caller (PluginProcessor) guarantees lifetime ≥ voice lifetime
         *  (WavetableBank lives for the entire plugin instance). */
        void setWavetable (const tw::Wavetable* wt) noexcept
        {
            currentWavetable_ = wt;
        }

        /** Set frame position 0..1 within the current wavetable. */
        void setWavetableFrame (float pos) noexcept
        {
            framePos_ = juce::jlimit (0.0f, 1.0f, pos);
        }

        /** Phase 2C — Warp mode (0=NONE, 1=BEND, 2=SYNC, 3=FORMANT) +
         *  amount 0..1. Applied to phase BEFORE wavetable lookup, so warp
         *  composes cleanly with any wavetable choice. */
        void setWarp (int mode, float amount) noexcept
        {
            warpMode_   = juce::jlimit (0, 10, mode);
            warpAmount_ = juce::jlimit (0.0f, 1.0f, amount);
        }

        /** Select which engine renders this voice. Idx 0..5 from
         *  SYN_OSC_A_ENGINE APVTS choice. Out-of-range clamps to nearest end. */
        void setEngine (int idx) noexcept
        {
            const int clamped = juce::jlimit (0, 5, idx);
            engine_ = static_cast<Engine> (clamped);
        }

        /** Test-only accessor — not used in production audio path. */
        Engine engineForTesting() const noexcept { return engine_; }

        // ── Phase 9 — OSC B setters (mirror of OSC A) ─────────────────────

        void setTuningB (int oct, int semi, float cent) noexcept
        {
            octOffsetB_   = oct;
            semiOffsetB_  = semi;
            centsOffsetB_ = cent;
            if (playing_)
                updateUnisonPhaseIncrementsB (currentMidiNote_);
        }

        void setLevelB (float level) noexcept
        {
            levelB_ = juce::jlimit (0.0f, 1.0f, level);
        }

        void setPanB (float pan) noexcept
        {
            const float p = juce::jlimit (-1.0f, 1.0f, pan);
            const float angle = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panLB_ = std::cos (angle);
            panRB_ = std::sin (angle);
        }

        void setWavetableB (const tw::Wavetable* wt) noexcept { currentWavetableB_ = wt; }
        void setWavetableFrameB (float pos) noexcept { framePosB_ = juce::jlimit (0.0f, 1.0f, pos); }

        void setWarpB (int mode, float amount) noexcept
        {
            warpModeB_   = juce::jlimit (0, 10, mode);
            warpAmountB_ = juce::jlimit (0.0f, 1.0f, amount);
        }

        void setEngineB (int idx) noexcept
        {
            const int clamped = juce::jlimit (0, 5, idx);
            engineB_ = static_cast<Engine> (clamped);
        }

        // ── Phase 8b — Unison + EROSION + HORIZON setters ────────────────

        /** Phase 8b — Set UNISON config (count + spread) on this voice. Called
         *  per-block from PluginProcessor broadcast. Computes per-sine detune
         *  cents (max ±25 at spread=1.0) + equal-power pan L/R. */
        void setUnison (int count, float spread01) noexcept
        {
            activeUnison_   = juce::jlimit (1, kMaxUnison, count);
            unisonSpread01_ = juce::jlimit (0.0f, 1.0f, spread01);

            // Compute per-sine detune cents + pan.
            for (int u = 0; u < activeUnison_; ++u)
            {
                if (activeUnison_ <= 1)
                {
                    uDetuneCents_[(size_t) u] = 0.0f;
                    uPanL_[(size_t) u] = 0.7071f;
                    uPanR_[(size_t) u] = 0.7071f;
                    continue;
                }
                // u_norm in [-1, +1] across the unison stack.
                const float u_norm = ((float) u / (float) (activeUnison_ - 1)) * 2.0f - 1.0f;
                uDetuneCents_[(size_t) u] = u_norm * unisonSpread01_ * 25.0f;
                // Equal-power pan: angle in [0, π/2].
                const float panAmt = u_norm * unisonSpread01_;       // -1..+1
                const float angle  = (panAmt + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                uPanL_[(size_t) u] = std::cos (angle);
                uPanR_[(size_t) u] = std::sin (angle);
            }
            // Zero out unused slots so render-loop summation is safe even if
            // activeUnison_ changes mid-playback.
            for (int u = activeUnison_; u < kMaxUnison; ++u)
            {
                uDetuneCents_[(size_t) u] = 0.0f;
                uPanL_[(size_t) u] = 0.0f;
                uPanR_[(size_t) u] = 0.0f;
            }
            updateUnisonFramePositions();
        }

        /** Phase 11a — Set per-OSC FRAME SPREAD (0..1). Pushed per-block from
         *  PluginProcessor broadcast. Caches the amount; per-sine offsets are
         *  recomputed in updateUnisonFramePositions() each block (and on
         *  setUnison/startNote) so SPREAD tracks UNISON count changes correctly. */
        /** WT BLUR amount per OSC (0..1). Replaces the old per-sine FRAME_SPREAD: turns
         *  the knob into a frame-blend width. Smoothed + applied per block in renderNextBlock. */
        void setBlur (float blurA01, float blurB01) noexcept
        {
            blurTargetA_ = juce::jlimit (0.0f, 1.0f, blurA01);
            blurTargetB_ = juce::jlimit (0.0f, 1.0f, blurB01);
        }

        /** PHASE mode per OSC (0=RETRIG, 1=FREE, 2=RANDOM, 3=SPREAD). Governs how each
         *  unison sine's phase accumulator is initialised at note-on. Pushed per block. */
        void setPhaseMode (int modeA, int modeB) noexcept
        {
            phaseModeA_ = juce::jlimit (0, 3, modeA);
            phaseModeB_ = juce::jlimit (0, 3, modeB);
        }

    private:
        // One-time decorrelated seed for FREE mode (per voice ptr / sine / osc), 0..1.
        double seedPhase (int u, int osc) const noexcept
        {
            const std::uint32_t h = static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this))
                                  ^ static_cast<std::uint32_t> ((u   + 1) * 0x9E3779B9u)
                                  ^ static_cast<std::uint32_t> ((osc + 1) * 2654435761u);
            return (double) (h & 0xFFFF) / 65535.0;
        }
        // Fresh decorrelated phase each call (xorshift32) for RANDOM mode, 0..1.
        double nextPhaseRandom () noexcept
        {
            phaseRng_ ^= phaseRng_ << 13;
            phaseRng_ ^= phaseRng_ >> 17;
            phaseRng_ ^= phaseRng_ << 5;
            return (double) (phaseRng_ & 0xFFFFFFu) / (double) 0x1000000;
        }
        // Resolve one unison sine's start phase for a given mode. `carried` is the
        // accumulator's current value (used by FREE so it keeps running across notes).
        double resolvePhase (int mode, int u, int osc, double carried) noexcept
        {
            switch (mode)
            {
                case 0: return 0.0;                                                              // RETRIG — aligned, punchy
                case 3: return (activeUnison_ > 1) ? (double) u / (double) activeUnison_ : 0.0;   // SPREAD — even fan
                case 2: return nextPhaseRandom();                                                 // RANDOM — fresh each note
                case 1: default: return phaseSeeded_ ? carried : seedPhase (u, osc);              // FREE — seed once, then carry
            }
        }
    public:

        /** Phase 11d — Set per-OSC FOLD shape + amount. Pushed per-block from
         *  PluginProcessor broadcast. Applies in the unison loop, post engine compute. */
        void setFold (int shapeA, float amountA, int shapeB, float amountB) noexcept
        {
            foldShapeA_  = juce::jlimit (0, 2, shapeA);
            foldAmountA_ = juce::jlimit (0.0f, 1.0f, amountA);
            foldShapeB_  = juce::jlimit (0, 2, shapeB);
            foldAmountB_ = juce::jlimit (0.0f, 1.0f, amountB);
        }

        /** Phase 11c — Set per-OSC SPECTRAL type + amount. Pushed per-block from
         *  PluginProcessor broadcast. Updates biquad coefficients on the fly.
         *  Phase 11g: types 3/4/5 (Comb/RingMod/BitCrush) bypass the biquad filter. */
        void setSpectral (int typeA, float amtA, int typeB, float amtB) noexcept
        {
            spectralTypeA_ = juce::jlimit (0, 9, typeA);
            spectralAmtA_  = juce::jlimit (0.0f, 1.0f, amtA);
            spectralTypeB_ = juce::jlimit (0, 9, typeB);
            spectralAmtB_  = juce::jlimit (0.0f, 1.0f, amtB);

            spectralBypassA_ = (spectralAmtA_ < 1.0e-4f);
            spectralBypassB_ = (spectralAmtB_ < 1.0e-4f);

            updateSpectralCoefficients (spectralTypeA_, spectralAmtA_, spectralFilterAL_, spectralFilterAR_);
            updateSpectralCoefficients (spectralTypeB_, spectralAmtB_, spectralFilterBL_, spectralFilterBR_);
        }

        /** Phase 11g — Set per-OSC INTERP mode (0=Linear, 1=Stepped). */
        void setInterpMode (int modeA, int modeB) noexcept
        {
            interpModeA_ = juce::jlimit (0, 1, modeA);
            interpModeB_ = juce::jlimit (0, 1, modeB);
        }

        /** EROSION amount 0..1 (set per-block from APVTS SYN_EROSION/100). */
        void setErosionAmount (float a) noexcept { erosionAmount_ = juce::jlimit (0.0f, 1.0f, a); }

        /** HORIZON tilt -1..+1 (set per-block from APVTS SYN_HORIZON/100). */
        void setHorizonAmount (float h) noexcept { horizonAmount_ = juce::jlimit (-1.0f, 1.0f, h); }

        void startNote (int midiNote, float velocity,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override
        {
            currentMidiNote_ = midiNote;
            currentVelocity_ = velocity;
            // OSC A resets
            noiseLpZ_        = 0.0f;     // Phase 3 — NOISE filter memory reset
            // OSC B resets (Phase 9)
            noiseLpZB_       = 0.0f;

            // PHASE — initialise each unison sine's phase accumulator per the selected
            // mode (RETRIG/FREE/RANDOM/SPREAD). The amp env starts at 0, so any reset here
            // is masked → click-free. FREE keeps the running accumulator (carried) across
            // notes for true analog behaviour; it's seeded decorrelated once (phaseSeeded_).
            if (phaseRng_ == 0u)
                phaseRng_ = (static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this)) ^ 0xA5A5A5A5u) | 1u;

            for (int u = 0; u < kMaxUnison; ++u)
            {
                uPhaseA_[(size_t) u]      = resolvePhase (phaseModeA_, u, 0, uPhaseA_[(size_t) u]);
                uModPhaseA_[(size_t) u]   = 0.0;
                uSyncPhaseA_[(size_t) u]  = 0.0;
                uPhaseB_[(size_t) u]      = resolvePhase (phaseModeB_, u, 1, uPhaseB_[(size_t) u]);
                uModPhaseB_[(size_t) u]   = 0.0;
                uSyncPhaseB_[(size_t) u]  = 0.0;
            }
            phaseSeeded_ = true;

            // Phase 8a — EROSION: randomize rate + initial phase per voice/note combination
            // Hash voice pointer XOR midiNote for decorrelated drift across voices
            const std::uint32_t hash = static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (this))
                                       ^ static_cast<std::uint32_t> (midiNote * 2654435761u);
            const float r1 = static_cast<float> (hash & 0xFFFF)         / 65535.0f;  // 0..1
            const float r2 = static_cast<float> ((hash >> 16) & 0xFFFF) / 65535.0f;  // 0..1
            erosionRate_         = 0.3f + r1 * 0.4f;  // 0.3..0.7 Hz
            erosionPhase_        = r2;                 // random initial phase
            currentErosionCents_ = 0.0f;

            // Phase 8b — populate per-sine increments
            updateUnisonFramePositions();
            updateUnisonPhaseIncrementsA (midiNote);
            updateUnisonPhaseIncrementsB (midiNote);
            playing_         = true;
            foldStateA_.fill ({});   // Phase 11d ADAA — clear per-sine fold history on note start
            foldStateB_.fill ({});
            ampEnv_.reset();
            ampEnv_.noteOn();

            // Batch 1 Filter — trigger FLT envelope alongside AMP env.
            fltEnv_.reset();
            fltEnv_.noteOn();
            // Reset filter state on note-on so a stale tail from a stolen
            // voice doesn't bleed into the new note's onset.
            filterSlot_.reset();
            filterSlot2_.reset();

            // Phase 8a polish — reset steal-fade state on new note
            stealing_         = false;
            stealingFade_     = 1.0f;
            stealingFadeStep_ = 0.0f;

            // Phase 12 — monotonic stamp so UnisonSynth::noteOn can find the
            // oldest non-stealing voice when the cap is hit. Static atomic so all
            // SynthVoice instances share one counter; thread-safe even though
            // startNote is invoked under the Synthesiser lock.
            static std::atomic<juce::uint32> globalNoteCounter { 1 };
            noteStartStamp_ = globalNoteCounter.fetch_add (1, std::memory_order_relaxed);
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                ampEnv_.noteOff();
                fltEnv_.noteOff();
            }
            else
            {
                // Phase 12 — Serum-2 style smooth voice steal. 30ms exponential fade
                // (audibly smooth, not just click-prevention) and the slot stays
                // OCCUPIED throughout (no clearCurrentNote here). Keeping the slot
                // means JUCE's findFreeVoice picks a different idle slot from the
                // 96-voice pool for the incoming note — so the dying note fades out
                // on its slot WHILE the new note rises on its own slot. The cap is
                // still enforced because UnisonSynth::noteOn excludes stealing
                // voices from the active count (see PluginProcessor.h). When the
                // fade completes, renderNextBlock calls clearCurrentNote().
                stealing_         = true;
                stealingFade_     = 1.0f;
                const float fadeSamples = static_cast<float>(0.030 * sampleRate_);
                stealingFadeStep_ = std::pow(0.001f, 1.0f / std::max(1.0f, fadeSamples));
            }
        }

        // Phase 12 — used by UnisonSynth to skip stealing voices when counting
        // toward the polyphony cap (a fading voice no longer "owns" a slot in
        // the user's perception, even though its currentlyPlayingNote is still
        // set so the slot doesn't get hijacked mid-fade).
        bool isStealing() const noexcept { return stealing_; }
        juce::uint32 getNoteStartStamp() const noexcept { return noteStartStamp_; }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out,
                              int startSample, int numSamples) override
        {
            if (! playing_) return;

            // Phase 9: stereo scratch (OSC A + OSC B each pan independently).
            if (scratch_.getNumChannels() < 2 || scratch_.getNumSamples() < numSamples)
                scratch_.setSize (2, numSamples, false, true, true);
            scratch_.clear();
            auto* scratchL = scratch_.getWritePointer (0);
            auto* scratchR = scratch_.getWritePointer (1);

            // Phase 8a — Compute this block's EROSION drift (slow sine LFO, per-voice)
            {
                constexpr double pi2 = 6.2831853071795865;
                currentErosionCents_ = std::sin (static_cast<float> (pi2 * erosionPhase_))
                                       * erosionAmount_ * 15.0f;  // max ±15 cents
                erosionPhase_ += static_cast<float> (erosionRate_ * numSamples / sampleRate_);
                if (erosionPhase_ >= 1.0f) erosionPhase_ -= std::floor (erosionPhase_);
            }
            // Re-derive per-sine phase increments with updated erosion drift
            updateUnisonPhaseIncrementsA (currentMidiNote_);
            updateUnisonPhaseIncrementsB (currentMidiNote_);

            // Phase 10a / Phase 8b — pick mip level using sine 0 (centre-detuned,
            // no spread offset) as the reference — ±25 cents of unison detune
            // doesn't cross a mip boundary in practice.
            // Phase 11d AA — phase-multiply warps (SYNC, FORMANT, FRACTALIZE) read the
            // table faster than the base increment, so pick the mip for the WARPED rate
            // (more band-limited → far less aliasing). Other modes use rate ×1.
            auto warpRateMul = [] (int mode, float amt) -> double
            {
                if (mode == 2 || mode == 3) return std::pow (2.0, (double) amt * 4.0); // SYNC / FORMANT (1..16x)
                if (mode == 7)              return 1.0 + (double) amt * 7.0;            // FRACTALIZE (1..8x)
                return 1.0;
            };
            currentMipLevelA_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncA_[0] * warpRateMul (warpMode_,  warpAmount_));
            currentMipLevelB_ = tw::Wavetable::mipLevelForPhaseIncrement (uPhaseIncB_[0] * warpRateMul (warpModeB_, warpAmountB_));

            // ── WT BLUR — smooth the amount, then (re)build each OSC's blended single-
            // cycle buffer ONCE per block (only when frame pos / blur / mip changed). Every
            // unison sine reads it via readCycle, so frame-blend cost is per-block, not
            // per-sample. The blend is the mip's frames summed at one band edge → alias-free;
            // RMS-matched inside renderBlend → no level change; blur 0 → exact old lookup.
            if (std::abs (blurTargetA_ - blurA_) < 1.0e-4f) blurA_ = blurTargetA_;
            else                                            blurA_ += (blurTargetA_ - blurA_) * 0.25f;
            if (std::abs (blurTargetB_ - blurB_) < 1.0e-4f) blurB_ = blurTargetB_;
            else                                            blurB_ += (blurTargetB_ - blurB_) * 0.25f;

            if (currentWavetable_ != nullptr)
            {
                float fpA = framePos_;
                if (interpModeA_ == 1) { const float Nf = 16.0f; fpA = std::round (fpA * (Nf - 1.0f)) / (Nf - 1.0f); }
                if (fpA != lastFpA_ || blurA_ != lastBlurA_ || currentMipLevelA_ != lastMipA_ || currentWavetable_ != lastWtA_)
                {
                    currentWavetable_->renderBlend (currentMipLevelA_, fpA, blurA_, blendA_.data());
                    lastFpA_ = fpA; lastBlurA_ = blurA_; lastMipA_ = currentMipLevelA_; lastWtA_ = currentWavetable_;
                }
            }
            if (currentWavetableB_ != nullptr)
            {
                float fpB = framePosB_;
                if (interpModeB_ == 1) { const float Nf = 16.0f; fpB = std::round (fpB * (Nf - 1.0f)) / (Nf - 1.0f); }
                if (fpB != lastFpB_ || blurB_ != lastBlurB_ || currentMipLevelB_ != lastMipB_ || currentWavetableB_ != lastWtB_)
                {
                    currentWavetableB_->renderBlend (currentMipLevelB_, fpB, blurB_, blendB_.data());
                    lastFpB_ = fpB; lastBlurB_ = blurB_; lastMipB_ = currentMipLevelB_; lastWtB_ = currentWavetableB_;
                }
            }

            // Phase 8a — HORIZON: per-note tilt depending on midiNote and amount.
            // midiNote 60 = neutral; lower notes get high-shelf cut (warmer),
            // higher notes get high-shelf boost (airier).
            {
                // Phase 8a polish — boost HORIZON range so it's audible at normal MIDI notes
                const float horizonTilt = horizonAmount_ * static_cast<float>(currentMidiNote_ - 60) / 24.0f;
                const float shelfGain   = std::pow (2.0f, horizonTilt);  // ±12dB at extremes
                *horizonShelfL_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate_, 2500.0f, 0.7071f, shelfGain);
                *horizonShelfR_.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate_, 2500.0f, 0.7071f, shelfGain);
            }

            for (int i = 0; i < numSamples; ++i)
            {
                // ── OSC A — sum across activeUnison_ sines (Phase 8b) ─────
                float sumAL = 0.0f, sumAR = 0.0f;

                for (int u = 0; u < activeUnison_; ++u)
                {
                    float sAu = 0.0f;

                    switch (engine_)
                    {
                        case Engine::WT:
                        {
                            if (currentWavetable_ != nullptr)
                            {
                                double warpedPhase = uPhaseA_[(size_t) u];
                                float  window      = 1.0f;   // PWM, FORMANT use this post-lookup window
                                bool   skipLookup  = false;  // PWM silence half-cycle

                                switch (warpMode_)
                                {
                                    case 0:  // NONE
                                        break;

                                    case 1:  // BEND
                                    {
                                        const double pi2 = 2.0 * 3.14159265358979323846;
                                        warpedPhase = uPhaseA_[(size_t) u]
                                                    + (double) warpAmount_ * 0.5 * std::sin (pi2 * uPhaseA_[(size_t) u]);
                                        warpedPhase -= std::floor (warpedPhase);
                                        break;
                                    }

                                    case 2:  // SYNC — 1×..16× exponential (Vital-style)
                                    {
                                        const double ratio = std::pow (2.0, (double) warpAmount_ * 4.0);
                                        warpedPhase = uPhaseA_[(size_t) u] * ratio;
                                        warpedPhase -= std::floor (warpedPhase);
                                        break;
                                    }

                                    case 3:  // FORMANT — windowed sync (Vital-style rebuild)
                                    {
                                        const double ratio = std::pow (2.0, (double) warpAmount_ * 4.0);
                                        warpedPhase = uPhaseA_[(size_t) u] * ratio;
                                        warpedPhase -= std::floor (warpedPhase);
                                        // Half-sine bell keyed off the un-multiplied master phase
                                        const double pi = 3.14159265358979323846;
                                        window = static_cast<float> (std::sin (pi * uPhaseA_[(size_t) u]));
                                        break;
                                    }

                                    case 4:  // PWM — duty-cycle window
                                    {
                                        const double duty = juce::jmax (0.10, 1.0 - (double) warpAmount_ * 0.45);
                                        if (uPhaseA_[(size_t) u] >= duty)
                                            skipLookup = true;
                                        else
                                            warpedPhase = uPhaseA_[(size_t) u] / duty;
                                        break;
                                    }

                                    case 5:  // SKEW — piecewise 2-segment peak shift
                                    {
                                        const double knee = juce::jmax (0.05, 0.5 - (double) warpAmount_ * 0.4);
                                        const double p    = uPhaseA_[(size_t) u];
                                        if (p < knee)
                                            warpedPhase = p / knee * 0.5;
                                        else
                                            warpedPhase = 0.5 + (p - knee) / (1.0 - knee) * 0.5;
                                        break;
                                    }

                                    case 6:  // MIRROR — squeezed-mirror blend
                                    {
                                        const double p = uPhaseA_[(size_t) u];
                                        const double mirrored = (p < 0.5) ? p * 2.0 : 2.0 - p * 2.0;
                                        warpedPhase = p * (1.0 - (double) warpAmount_) + mirrored * (double) warpAmount_;
                                        warpedPhase -= std::floor (warpedPhase);
                                        break;
                                    }

                                    case 7:  // FRACTALIZE — fmod cascade, N = 1..8
                                    {
                                        const double N = 1.0 + (double) warpAmount_ * 7.0;
                                        warpedPhase = uPhaseA_[(size_t) u] * N;
                                        warpedPhase -= std::floor (warpedPhase);
                                        break;
                                    }

                                    case 8:  // P-QUANTIZE — phase staircase, 32→1 steps
                                    {
                                        const double inv = 1.0 - (double) warpAmount_;
                                        const double t   = inv * inv;
                                        const int    steps = juce::jmax (1, (int) std::round (1.0 + t * 31.0));
                                        warpedPhase = std::floor (uPhaseA_[(size_t) u] * (double) steps) / (double) steps;
                                        break;
                                    }

                                    case 9:  // RECTIFY — amp-domain, handled post-lookup
                                    case 10: // SINE SHAPER — amp-domain, handled post-lookup
                                        break;

                                    default: break;
                                }

                                if (skipLookup)
                                {
                                    sAu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer
                                    // (frame position, stepped-interp and blur already applied at block rate).
                                    sAu = tw::Wavetable::readCycle (blendA_.data(), (float) warpedPhase);
                                    sAu *= window;

                                    if (warpMode_ == 9)
                                    {
                                        // RECTIFY: blend dry with |x|×2−1 by amount
                                        const float rect = std::abs (sAu) * 2.0f - 1.0f;
                                        sAu = sAu * (1.0f - warpAmount_) + rect * warpAmount_;
                                    }
                                    else if (warpMode_ == 10)
                                    {
                                        // SINE SHAPER: sin(x × π/2 × (1 + amount×4))
                                        const float drive = 1.0f + warpAmount_ * 4.0f;
                                        sAu = std::sin (sAu * (float) (3.14159265358979323846 * 0.5) * drive);
                                    }
                                }
                            }
                            else
                            {
                                sAu = static_cast<float> (2.0 * uPhaseA_[(size_t) u] - 1.0);
                                sAu -= static_cast<float> (polyBlep (uPhaseA_[(size_t) u], uPhaseIncA_[(size_t) u]));
                            }
                            uPhaseA_[(size_t) u] += uPhaseIncA_[(size_t) u];
                            if (uPhaseA_[(size_t) u] >= 1.0) uPhaseA_[(size_t) u] -= 1.0;
                            break;
                        }

                        case Engine::NOISE:
                        {
                            // Noise is a single stream per voice — all unison sines hear the
                            // same noise sample. Compute PRNG + LP filter once at u==0,
                            // then broadcast the same noiseLpZ_ to all sines.
                            if (u == 0)
                            {
                                noiseState_ ^= noiseState_ << 13;
                                noiseState_ ^= noiseState_ >> 17;
                                noiseState_ ^= noiseState_ << 5;
                                const float white = static_cast<float> (static_cast<int32_t> (noiseState_))
                                                  * (1.0f / 2147483648.0f);
                                const float alpha = 1.0f - 0.98f * framePos_;
                                noiseLpZ_ += alpha * (white - noiseLpZ_);
                            }
                            const float drive = 1.0f + 8.0f * warpAmount_;
                            sAu = std::tanh (noiseLpZ_ * drive);
                            sAu *= 1.0f + 0.5f * framePos_;
                            break;
                        }

                        case Engine::FM:
                        {
                            const double ratio  = 0.25 + std::pow (32.0, (double) framePos_) * 0.234375;
                            const double modInc = uPhaseIncA_[(size_t) u] * ratio;
                            const double depth  = (double) warpAmount_ * 6.2831853071795865;
                            const double pi2    = 6.2831853071795865;
                            const double modOut = std::sin (pi2 * uModPhaseA_[(size_t) u]);
                            sAu = static_cast<float> (std::sin (pi2 * uPhaseA_[(size_t) u] + depth * modOut));
                            uModPhaseA_[(size_t) u] += modInc;
                            if (uModPhaseA_[(size_t) u] >= 1.0) uModPhaseA_[(size_t) u] -= std::floor (uModPhaseA_[(size_t) u]);
                            uPhaseA_[(size_t) u] += uPhaseIncA_[(size_t) u];
                            if (uPhaseA_[(size_t) u] >= 1.0) uPhaseA_[(size_t) u] -= 1.0;
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
                            sAu = 0.0f; break;
                    }

                    // Phase 11d — FOLD applied per-sine, post-engine, pre-pan.
                    sAu = applyFoldADAA (sAu, foldShapeA_, foldAmountA_, foldStateA_[(size_t) u]);

                    // Per-sine pan into the OSC A stereo sum.
                    sumAL += sAu * uPanL_[(size_t) u];
                    sumAR += sAu * uPanR_[(size_t) u];
                }

                // Average across active sines (preserves perceived loudness as UNISON grows).
                if (activeUnison_ > 1)
                {
                    const float invN = 1.0f / (float) activeUnison_;
                    sumAL *= invN;
                    sumAR *= invN;
                }
                float sA_L = sumAL;
                float sA_R = sumAR;
                if (! spectralBypassA_)
                {
                    if (spectralTypeA_ <= 2)
                    {
                        // LP, HP, Smear — biquad
                        sA_L = spectralFilterAL_.processSample (sA_L);
                        sA_R = spectralFilterAR_.processSample (sA_R);
                    }
                    else if (spectralTypeA_ == 3)
                    {
                        // Comb — feedforward y = x + x[n-N]
                        const int N = juce::jlimit (1, kSpectralCombSize - 1,
                                                     (int) (4.0f + spectralAmtA_ * (float) (kSpectralCombSize - 8)));
                        const int readIdx = (spectralCombWriteA_ - N + kSpectralCombSize) % kSpectralCombSize;
                        const float dryL = sA_L;
                        const float dryR = sA_R;
                        sA_L = dryL + spectralCombAL_[(size_t) readIdx] * spectralAmtA_;
                        sA_R = dryR + spectralCombAR_[(size_t) readIdx] * spectralAmtA_;
                        spectralCombAL_[(size_t) spectralCombWriteA_] = dryL;
                        spectralCombAR_[(size_t) spectralCombWriteA_] = dryR;
                        spectralCombWriteA_ = (spectralCombWriteA_ + 1) % kSpectralCombSize;
                        // Normalize loudness — comb output can grow up to 2x
                        sA_L *= 0.5f;
                        sA_R *= 0.5f;
                    }
                    else if (spectralTypeA_ == 4)
                    {
                        // Ring Mod — modulate by sine at frequency 30..2000 Hz scaled by amount²
                        const double modHz = 30.0 + (double) (spectralAmtA_ * spectralAmtA_) * 1970.0;
                        const double inc = modHz / sampleRate_;
                        const float modL = static_cast<float> (std::sin (6.2831853071795865 * spectralRingPhaseA_));
                        spectralRingPhaseA_ += inc;
                        if (spectralRingPhaseA_ >= 1.0) spectralRingPhaseA_ -= 1.0;
                        // Wet/dry blend by amount
                        sA_L = sA_L * (1.0f - spectralAmtA_) + (sA_L * modL) * spectralAmtA_;
                        sA_R = sA_R * (1.0f - spectralAmtA_) + (sA_R * modL) * spectralAmtA_;
                    }
                    else if (spectralTypeA_ == 5)
                    {
                        // Bit Crush — quantize to N levels, N = 64 → 4 by amount²
                        const float levels = 64.0f - (spectralAmtA_ * spectralAmtA_) * 60.0f;
                        const float L = juce::jmax (4.0f, levels);
                        sA_L = std::round (sA_L * L) / L;
                        sA_R = std::round (sA_R * L) / L;
                    }
                    else if (spectralTypeA_ == 6)
                    {
                        // Downsample — sample-and-hold at lower rate
                        const float divisor = 1.0f + spectralAmtA_ * spectralAmtA_ * 31.0f;  // 1..32
                        spectralDsCounterA_ += 1.0f;
                        if (spectralDsCounterA_ >= divisor)
                        {
                            spectralDsHeldAL_ = sA_L;
                            spectralDsHeldAR_ = sA_R;
                            spectralDsCounterA_ -= divisor;
                        }
                        sA_L = spectralDsHeldAL_;
                        sA_R = spectralDsHeldAR_;
                    }
                    else if (spectralTypeA_ == 7)
                    {
                        // Tube — asymmetric soft clipping with positive bias
                        const float drive = 1.0f + spectralAmtA_ * spectralAmtA_ * 9.0f;
                        const float bias = 0.15f * spectralAmtA_;
                        const float invSat = 1.0f / std::tanh (drive);
                        sA_L = std::tanh (sA_L * drive + bias) * invSat - bias * invSat;
                        sA_R = std::tanh (sA_R * drive + bias) * invSat - bias * invSat;
                    }
                    else if (spectralTypeA_ == 8)
                    {
                        // Tilt — low-shelf cut + high-shelf boost, one-pole based
                        const float alpha = 0.005f;  // ~120 Hz at 48kHz
                        spectralTiltLowAL_ += alpha * (sA_L - spectralTiltLowAL_);
                        spectralTiltLowAR_ += alpha * (sA_R - spectralTiltLowAR_);
                        const float lowL = spectralTiltLowAL_;
                        const float lowR = spectralTiltLowAR_;
                        const float highL = sA_L - lowL;
                        const float highR = sA_R - lowR;
                        const float lowGain  = 1.0f - spectralAmtA_;
                        const float highGain = 1.0f + spectralAmtA_ * 2.0f;
                        sA_L = lowL * lowGain + highL * highGain;
                        sA_R = lowR * lowGain + highR * highGain;
                    }
                    else if (spectralTypeA_ == 9)
                    {
                        // Vibrato — short modulated delay creates pitch wobble
                        const double modHz = 1.0 + (double) spectralAmtA_ * 8.0;
                        const double inc   = modHz / sampleRate_;
                        spectralVibPhaseA_ += inc;
                        if (spectralVibPhaseA_ >= 1.0) spectralVibPhaseA_ -= 1.0;
                        const float lfo = static_cast<float> (std::sin (6.2831853071795865 * spectralVibPhaseA_));
                        const float depthSamples = spectralAmtA_ * 20.0f;
                        const float delaySamples = (float) (kSpectralVibSize - 4) * 0.5f + lfo * depthSamples;
                        const int   intDel       = juce::jlimit (1, kSpectralVibSize - 2, (int) delaySamples);
                        const int   readIdx      = (spectralVibWriteA_ - intDel + kSpectralVibSize) % kSpectralVibSize;
                        const float dryL = sA_L, dryR = sA_R;
                        sA_L = dryL * (1.0f - spectralAmtA_) + spectralVibAL_[(size_t) readIdx] * spectralAmtA_;
                        sA_R = dryR * (1.0f - spectralAmtA_) + spectralVibAR_[(size_t) readIdx] * spectralAmtA_;
                        spectralVibAL_[(size_t) spectralVibWriteA_] = dryL;
                        spectralVibAR_[(size_t) spectralVibWriteA_] = dryR;
                        spectralVibWriteA_ = (spectralVibWriteA_ + 1) % kSpectralVibSize;
                    }
                }

                // ── OSC B — sum across activeUnison_ sines (Phase 8b) ─────
                float sumBL = 0.0f, sumBR = 0.0f;

                for (int u = 0; u < activeUnison_; ++u)
                {
                    float sBu = 0.0f;

                    switch (engineB_)
                    {
                        case Engine::WT:
                        {
                            if (currentWavetableB_ != nullptr)
                            {
                                double warpedPhase = uPhaseB_[(size_t) u];
                                float  window      = 1.0f;
                                bool   skipLookup  = false;

                                switch (warpModeB_)
                                {
                                    case 0:  // NONE
                                        break;

                                    case 1:  // BEND
                                    {
                                        const double pi2 = 2.0 * 3.14159265358979323846;
                                        warpedPhase = uPhaseB_[(size_t) u]
                                                    + (double) warpAmountB_ * 0.5 * std::sin (pi2 * uPhaseB_[(size_t) u]);
                                        warpedPhase -= std::floor (warpedPhase);
                                        break;
                                    }

                                    case 2:  // SYNC — 1×..16× exponential
                                    {
                                        const double ratio = std::pow (2.0, (double) warpAmountB_ * 4.0);
                                        warpedPhase = uPhaseB_[(size_t) u] * ratio;
                                        warpedPhase -= std::floor (warpedPhase);
                                        break;
                                    }

                                    case 3:  // FORMANT — windowed sync (Vital-style)
                                    {
                                        const double ratio = std::pow (2.0, (double) warpAmountB_ * 4.0);
                                        warpedPhase = uPhaseB_[(size_t) u] * ratio;
                                        warpedPhase -= std::floor (warpedPhase);
                                        const double pi = 3.14159265358979323846;
                                        window = static_cast<float> (std::sin (pi * uPhaseB_[(size_t) u]));
                                        break;
                                    }

                                    case 4:  // PWM
                                    {
                                        const double duty = juce::jmax (0.10, 1.0 - (double) warpAmountB_ * 0.45);
                                        if (uPhaseB_[(size_t) u] >= duty)
                                            skipLookup = true;
                                        else
                                            warpedPhase = uPhaseB_[(size_t) u] / duty;
                                        break;
                                    }

                                    case 5:  // SKEW
                                    {
                                        const double knee = juce::jmax (0.05, 0.5 - (double) warpAmountB_ * 0.4);
                                        const double p    = uPhaseB_[(size_t) u];
                                        if (p < knee)
                                            warpedPhase = p / knee * 0.5;
                                        else
                                            warpedPhase = 0.5 + (p - knee) / (1.0 - knee) * 0.5;
                                        break;
                                    }

                                    case 6:  // MIRROR
                                    {
                                        const double p = uPhaseB_[(size_t) u];
                                        const double mirrored = (p < 0.5) ? p * 2.0 : 2.0 - p * 2.0;
                                        warpedPhase = p * (1.0 - (double) warpAmountB_) + mirrored * (double) warpAmountB_;
                                        warpedPhase -= std::floor (warpedPhase);
                                        break;
                                    }

                                    case 7:  // FRACTALIZE
                                    {
                                        const double N = 1.0 + (double) warpAmountB_ * 7.0;
                                        warpedPhase = uPhaseB_[(size_t) u] * N;
                                        warpedPhase -= std::floor (warpedPhase);
                                        break;
                                    }

                                    case 8:  // P-QUANTIZE
                                    {
                                        const double inv = 1.0 - (double) warpAmountB_;
                                        const double t   = inv * inv;
                                        const int    steps = juce::jmax (1, (int) std::round (1.0 + t * 31.0));
                                        warpedPhase = std::floor (uPhaseB_[(size_t) u] * (double) steps) / (double) steps;
                                        break;
                                    }

                                    case 9:  // RECTIFY — post-lookup
                                    case 10: // SINE SHAPER — post-lookup
                                        break;

                                    default: break;
                                }

                                if (skipLookup)
                                {
                                    sBu = 0.0f;
                                }
                                else
                                {
                                    // WT BLUR — read the per-block blended single-cycle buffer.
                                    sBu = tw::Wavetable::readCycle (blendB_.data(), (float) warpedPhase);
                                    sBu *= window;

                                    if (warpModeB_ == 9)
                                    {
                                        const float rect = std::abs (sBu) * 2.0f - 1.0f;
                                        sBu = sBu * (1.0f - warpAmountB_) + rect * warpAmountB_;
                                    }
                                    else if (warpModeB_ == 10)
                                    {
                                        const float drive = 1.0f + warpAmountB_ * 4.0f;
                                        sBu = std::sin (sBu * (float) (3.14159265358979323846 * 0.5) * drive);
                                    }
                                }
                            }
                            else
                            {
                                sBu = static_cast<float> (2.0 * uPhaseB_[(size_t) u] - 1.0);
                                sBu -= static_cast<float> (polyBlep (uPhaseB_[(size_t) u], uPhaseIncB_[(size_t) u]));
                            }
                            uPhaseB_[(size_t) u] += uPhaseIncB_[(size_t) u];
                            if (uPhaseB_[(size_t) u] >= 1.0) uPhaseB_[(size_t) u] -= 1.0;
                            break;
                        }

                        case Engine::NOISE:
                        {
                            // OSC B noise is single-stream per voice — all unison sines hear the same noise.
                            if (u == 0)
                            {
                                noiseStateB_ ^= noiseStateB_ << 13;
                                noiseStateB_ ^= noiseStateB_ >> 17;
                                noiseStateB_ ^= noiseStateB_ << 5;
                                const float white = static_cast<float> (static_cast<int32_t> (noiseStateB_))
                                                  * (1.0f / 2147483648.0f);
                                const float alpha = 1.0f - 0.98f * framePosB_;
                                noiseLpZB_ += alpha * (white - noiseLpZB_);
                            }
                            const float drive = 1.0f + 8.0f * warpAmountB_;
                            sBu = std::tanh (noiseLpZB_ * drive);
                            sBu *= 1.0f + 0.5f * framePosB_;
                            break;
                        }

                        case Engine::FM:
                        {
                            const double ratio  = 0.25 + std::pow (32.0, (double) framePosB_) * 0.234375;
                            const double modInc = uPhaseIncB_[(size_t) u] * ratio;
                            const double depth  = (double) warpAmountB_ * 6.2831853071795865;
                            const double pi2    = 6.2831853071795865;
                            const double modOut = std::sin (pi2 * uModPhaseB_[(size_t) u]);
                            sBu = static_cast<float> (std::sin (pi2 * uPhaseB_[(size_t) u] + depth * modOut));
                            uModPhaseB_[(size_t) u] += modInc;
                            if (uModPhaseB_[(size_t) u] >= 1.0) uModPhaseB_[(size_t) u] -= std::floor (uModPhaseB_[(size_t) u]);
                            uPhaseB_[(size_t) u] += uPhaseIncB_[(size_t) u];
                            if (uPhaseB_[(size_t) u] >= 1.0) uPhaseB_[(size_t) u] -= 1.0;
                            break;
                        }

                        case Engine::SAMP:
                        case Engine::GRAN:
                        case Engine::SPEC:
                            sBu = 0.0f; break;
                    }

                    // Phase 11d — FOLD applied per-sine, post-engine, pre-pan.
                    sBu = applyFoldADAA (sBu, foldShapeB_, foldAmountB_, foldStateB_[(size_t) u]);

                    // Per-sine pan into the OSC B stereo sum.
                    sumBL += sBu * uPanL_[(size_t) u];
                    sumBR += sBu * uPanR_[(size_t) u];
                }

                // Average across active sines (preserves perceived loudness).
                if (activeUnison_ > 1)
                {
                    const float invN = 1.0f / (float) activeUnison_;
                    sumBL *= invN;
                    sumBR *= invN;
                }
                float sB_L = sumBL;
                float sB_R = sumBR;
                if (! spectralBypassB_)
                {
                    if (spectralTypeB_ <= 2)
                    {
                        // LP, HP, Smear — biquad
                        sB_L = spectralFilterBL_.processSample (sB_L);
                        sB_R = spectralFilterBR_.processSample (sB_R);
                    }
                    else if (spectralTypeB_ == 3)
                    {
                        // Comb — feedforward y = x + x[n-N]
                        const int N = juce::jlimit (1, kSpectralCombSize - 1,
                                                     (int) (4.0f + spectralAmtB_ * (float) (kSpectralCombSize - 8)));
                        const int readIdx = (spectralCombWriteB_ - N + kSpectralCombSize) % kSpectralCombSize;
                        const float dryL = sB_L;
                        const float dryR = sB_R;
                        sB_L = dryL + spectralCombBL_[(size_t) readIdx] * spectralAmtB_;
                        sB_R = dryR + spectralCombBR_[(size_t) readIdx] * spectralAmtB_;
                        spectralCombBL_[(size_t) spectralCombWriteB_] = dryL;
                        spectralCombBR_[(size_t) spectralCombWriteB_] = dryR;
                        spectralCombWriteB_ = (spectralCombWriteB_ + 1) % kSpectralCombSize;
                        sB_L *= 0.5f;
                        sB_R *= 0.5f;
                    }
                    else if (spectralTypeB_ == 4)
                    {
                        // Ring Mod
                        const double modHz = 30.0 + (double) (spectralAmtB_ * spectralAmtB_) * 1970.0;
                        const double inc = modHz / sampleRate_;
                        const float modL = static_cast<float> (std::sin (6.2831853071795865 * spectralRingPhaseB_));
                        spectralRingPhaseB_ += inc;
                        if (spectralRingPhaseB_ >= 1.0) spectralRingPhaseB_ -= 1.0;
                        sB_L = sB_L * (1.0f - spectralAmtB_) + (sB_L * modL) * spectralAmtB_;
                        sB_R = sB_R * (1.0f - spectralAmtB_) + (sB_R * modL) * spectralAmtB_;
                    }
                    else if (spectralTypeB_ == 5)
                    {
                        // Bit Crush
                        const float levels = 64.0f - (spectralAmtB_ * spectralAmtB_) * 60.0f;
                        const float L = juce::jmax (4.0f, levels);
                        sB_L = std::round (sB_L * L) / L;
                        sB_R = std::round (sB_R * L) / L;
                    }
                    else if (spectralTypeB_ == 6)
                    {
                        // Downsample — sample-and-hold at lower rate
                        const float divisor = 1.0f + spectralAmtB_ * spectralAmtB_ * 31.0f;
                        spectralDsCounterB_ += 1.0f;
                        if (spectralDsCounterB_ >= divisor)
                        {
                            spectralDsHeldBL_ = sB_L;
                            spectralDsHeldBR_ = sB_R;
                            spectralDsCounterB_ -= divisor;
                        }
                        sB_L = spectralDsHeldBL_;
                        sB_R = spectralDsHeldBR_;
                    }
                    else if (spectralTypeB_ == 7)
                    {
                        // Tube — asymmetric soft clipping with positive bias
                        const float drive = 1.0f + spectralAmtB_ * spectralAmtB_ * 9.0f;
                        const float bias = 0.15f * spectralAmtB_;
                        const float invSat = 1.0f / std::tanh (drive);
                        sB_L = std::tanh (sB_L * drive + bias) * invSat - bias * invSat;
                        sB_R = std::tanh (sB_R * drive + bias) * invSat - bias * invSat;
                    }
                    else if (spectralTypeB_ == 8)
                    {
                        // Tilt — low-shelf cut + high-shelf boost, one-pole based
                        const float alpha = 0.005f;
                        spectralTiltLowBL_ += alpha * (sB_L - spectralTiltLowBL_);
                        spectralTiltLowBR_ += alpha * (sB_R - spectralTiltLowBR_);
                        const float lowL = spectralTiltLowBL_;
                        const float lowR = spectralTiltLowBR_;
                        const float highL = sB_L - lowL;
                        const float highR = sB_R - lowR;
                        const float lowGain  = 1.0f - spectralAmtB_;
                        const float highGain = 1.0f + spectralAmtB_ * 2.0f;
                        sB_L = lowL * lowGain + highL * highGain;
                        sB_R = lowR * lowGain + highR * highGain;
                    }
                    else if (spectralTypeB_ == 9)
                    {
                        // Vibrato — short modulated delay creates pitch wobble
                        const double modHz = 1.0 + (double) spectralAmtB_ * 8.0;
                        const double inc   = modHz / sampleRate_;
                        spectralVibPhaseB_ += inc;
                        if (spectralVibPhaseB_ >= 1.0) spectralVibPhaseB_ -= 1.0;
                        const float lfo = static_cast<float> (std::sin (6.2831853071795865 * spectralVibPhaseB_));
                        const float depthSamples = spectralAmtB_ * 20.0f;
                        const float delaySamples = (float) (kSpectralVibSize - 4) * 0.5f + lfo * depthSamples;
                        const int   intDel       = juce::jlimit (1, kSpectralVibSize - 2, (int) delaySamples);
                        const int   readIdx      = (spectralVibWriteB_ - intDel + kSpectralVibSize) % kSpectralVibSize;
                        const float dryL = sB_L, dryR = sB_R;
                        sB_L = dryL * (1.0f - spectralAmtB_) + spectralVibBL_[(size_t) readIdx] * spectralAmtB_;
                        sB_R = dryR * (1.0f - spectralAmtB_) + spectralVibBR_[(size_t) readIdx] * spectralAmtB_;
                        spectralVibBL_[(size_t) spectralVibWriteB_] = dryL;
                        spectralVibBR_[(size_t) spectralVibWriteB_] = dryR;
                        spectralVibWriteB_ = (spectralVibWriteB_ + 1) % kSpectralVibSize;
                    }
                }

                const float env    = ampEnv_.getNextSample();
                const float velEnv = currentVelocity_ * env;

                // Sum to stereo with INDEPENDENT per-osc level + pan
                scratchL[i] = (sA_L * level_ * panL_ + sB_L * levelB_ * panLB_) * velEnv;
                scratchR[i] = (sA_R * level_ * panR_ + sB_R * levelB_ * panRB_) * velEnv;
            }

            // Phase 8a polish — apply steal-fade and decide if voice should die
            if (stealing_)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    scratchL[i] *= stealingFade_;
                    scratchR[i] *= stealingFade_;
                    stealingFade_ *= stealingFadeStep_;
                }
                if (stealingFade_ < 0.001f)
                {
                    // Fade complete — clear and exit early; existing post-fade code will write
                    // mostly-silence to the output. Mark playing_ = false so the next block
                    // skips this voice entirely.
                    stealing_   = false;
                    playing_    = false;
                    ampEnv_.reset();
                    // We still let the filter process this block's tiny tail so the filter
                    // state settles — don't return early.
                    clearCurrentNote();
                }
            }

            // Batch 1 Filter — per-sample FilterSlot processing with cutoff
            // modulated by FLT envelope (bipolar amount) + per-voice EROSION
            // drift, summed in semitone space and converted to Hz at the end
            // (so low cutoffs barely move and high cutoffs swing wide stays
            // musical, per report §6).
            {
                const double sr = sampleRate_;
                const float  nyq = 0.5f * (float) sr;
                const float  baseCutSemis = hzToSemi (baseCutHz_);
                // EROSION scale: pow(e, 1.8) per prompt §5 / report §5. The
                // resulting random walk drifts within ±~6 ST at max erosion.
                const float  driftDepthSemis = std::pow (fltErosionAmount_, 1.8f) * 6.0f;
                const bool   driftActive     = fltErosionAmount_ > 0.001f;
                // Slight resonance wander at high erosion (§5 of prompt).
                const float  resWander = (fltErosionAmount_ > 0.7f)
                                            ? (fltErosionAmount_ - 0.7f) * 0.20f
                                            : 0.0f;

                float* sL = scratch_.getWritePointer (0);
                float* sR = scratch_.getWritePointer (1);
                const bool oversample = filterSlot_.needsOversampling()
                                     || filterSlot2_.needsOversampling();
                // Coefficient sample rate doubles when oversampling so the
                // filter's prewarp + ZDF math sees the upsampled Nyquist.
                const double coefSr = oversample ? sr * 2.0 : sr;
                const float  baseCutSemis2 = hzToSemi (baseCutHz2_);
                const int    kNoneType = (int) tw::filters::Type::NONE;
                for (int i = 0; i < numSamples; ++i)
                {
                    // Per-sample FLT envelope tick (single value 0..1).
                    const float fltEnvVal = fltEnv_.getNextSample();

                    // Per-sample drift (one-pole LP of uniform white noise).
                    if (driftActive)
                    {
                        const float w = driftRng_.nextFloat() * 2.0f - 1.0f;
                        driftState_ += driftCoef_ * (w - driftState_);
                    }
                    const float driftSemis = driftState_ * driftDepthSemis;
                    const float fmax = juce::jmin (20000.0f, 0.45f * (float) coefSr);

                    // Filter 1 cutoff (own env amount, shared FLT env + drift).
                    const float cutSemis1 = baseCutSemis
                                          + envAmount_ * fltEnvVal * 96.0f + driftSemis;
                    float cutHz1 = 440.0f * std::pow (2.0f, (cutSemis1 - 69.0f) / 12.0f);
                    cutHz1 = juce::jlimit (20.0f, fmax, cutHz1);
                    const float res1 = juce::jlimit (0.0f, 1.0f,
                        baseRes01_ + resWander * driftState_ * 0.5f);
                    filterSlot_.setParams (cutHz1, res1, drv01_, coefSr);

                    // Filter 2 cutoff (fully independent — its own knobs).
                    const float cutSemis2 = baseCutSemis2
                                          + envAmount2_ * fltEnvVal * 96.0f + driftSemis;
                    float cutHz2 = 440.0f * std::pow (2.0f, (cutSemis2 - 69.0f) / 12.0f);
                    cutHz2 = juce::jlimit (20.0f, fmax, cutHz2);
                    const float res2 = juce::jlimit (0.0f, 1.0f,
                        baseRes012_ + resWander * driftState_ * 0.5f);
                    filterSlot2_.setParams (cutHz2, res2, drv012_, coefSr);

                    // Routing + per-filter wet/dry mix. NONE-aware so a bypassed
                    // slot drops out of the sum cleanly. Filters are independent.
                    const bool a1 = (filterType1_ != kNoneType);
                    const bool a2 = (filterType2_ != kNoneType);
                    auto applyFilters = [&] (float& L, float& R)
                    {
                        const float dryL = L, dryR = R;
                        if (filterRouting_ == 0)          // SERIES: x → F1 → F2
                        {
                            float l = L, r = R;
                            if (a1) { float wl = l, wr = r; filterSlot_.processStereo (wl, wr);
                                      l = filterMix1_ * wl + (1.0f - filterMix1_) * l;
                                      r = filterMix1_ * wr + (1.0f - filterMix1_) * r; }
                            if (a2) { float wl = l, wr = r; filterSlot2_.processStereo (wl, wr);
                                      l = filterMix2_ * wl + (1.0f - filterMix2_) * l;
                                      r = filterMix2_ * wr + (1.0f - filterMix2_) * r; }
                            L = l; R = r;
                        }
                        else                              // PARALLEL: F1(x) + F2(x)
                        {
                            float b1L = dryL, b1R = dryR, b2L = dryL, b2R = dryR;
                            if (a1) { filterSlot_.processStereo (b1L, b1R);
                                      b1L = filterMix1_ * b1L + (1.0f - filterMix1_) * dryL;
                                      b1R = filterMix1_ * b1R + (1.0f - filterMix1_) * dryR; }
                            if (a2) { filterSlot2_.processStereo (b2L, b2R);
                                      b2L = filterMix2_ * b2L + (1.0f - filterMix2_) * dryL;
                                      b2R = filterMix2_ * b2R + (1.0f - filterMix2_) * dryR; }
                            if (a1 && a2) { L = 0.5f * (b1L + b2L); R = 0.5f * (b1R + b2R); }
                            else if (a1)  { L = b1L; R = b1R; }
                            else if (a2)  { L = b2L; R = b2R; }
                            else          { L = dryL; R = dryR; }
                        }
                    };

                    if (oversample)
                    {
                        // 2× linear-interp upsample → filter twice → box decimate.
                        const float midL = 0.5f * (osPrevL_ + sL[i]);
                        const float midR = 0.5f * (osPrevR_ + sR[i]);
                        float yMidL = midL, yMidR = midR; applyFilters (yMidL, yMidR);
                        float yL = sL[i], yR = sR[i];      applyFilters (yL, yR);
                        sL[i] = 0.5f * (yMidL + yL);
                        sR[i] = 0.5f * (yMidR + yR);
                        osPrevL_ = sL[i]; osPrevR_ = sR[i];
                    }
                    else
                    {
                        applyFilters (sL[i], sR[i]);
                    }
                }

                // NaN/Inf guard — Pirkle/Stilson note that ZDF ladders can blow
                // up under pathological coefficient updates. One bad sample
                // cascades into a stuck-on voice; cheap to detect once/block.
                if (! std::isfinite (sL[numSamples - 1])
                 || ! std::isfinite (sR[numSamples - 1])
                 || std::abs (sL[numSamples - 1]) > 30.0f
                 || std::abs (sR[numSamples - 1]) > 30.0f)
                {
                    filterSlot_.reset();
            filterSlot2_.reset();
                    osPrevL_ = osPrevR_ = 0.0f;
                    juce::FloatVectorOperations::clear (sL, numSamples);
                    juce::FloatVectorOperations::clear (sR, numSamples);
                }
            }

            // Phase 8a — HORIZON tilt filter (per-channel high-shelf).
            {
                float* chL = scratch_.getWritePointer (0);
                float* chR = scratch_.getWritePointer (1);
                juce::dsp::AudioBlock<float> blockL (&chL, 1, 0, (size_t) numSamples);
                juce::dsp::ProcessContextReplacing<float> ctxL (blockL);
                horizonShelfL_.process (ctxL);
                juce::dsp::AudioBlock<float> blockR (&chR, 1, 0, (size_t) numSamples);
                juce::dsp::ProcessContextReplacing<float> ctxR (blockR);
                horizonShelfR_.process (ctxR);
            }

            // Sum filtered stereo scratch into output.
            auto* L = out.getWritePointer (0, startSample);
            auto* R = out.getNumChannels() > 1
                          ? out.getWritePointer (1, startSample) : L;
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] += scratchL[i];
                R[i] += scratchR[i];
            }

            if (! ampEnv_.isActive())
            {
                playing_ = false;
                clearCurrentNote();
            }
        }

    private:
        // Phase 8b — populate per-sine phase-increment update helpers to SynthVoice. They populate the `uPhaseIncA_` / `uPhaseIncB_` arrays from MIDI note + octave/semi/cents tuning + per-sine `uDetuneCents_[u]` + EROSION drift. Called from `startNote` after the existing scalar updates, and from `renderNextBlock` per-block right after the existing erosion-drift recompute.
        void updateUnisonPhaseIncrementsA (int midiNote) noexcept
        {
            for (int u = 0; u < kMaxUnison; ++u)
            {
                const double semitones =
                      static_cast<double> (midiNote - 69)
                    + static_cast<double> (octOffset_) * 12.0
                    + static_cast<double> (semiOffset_)
                    + static_cast<double> (centsOffset_)             * 0.01
                    + static_cast<double> (uDetuneCents_[(size_t) u]) * 0.01
                    + static_cast<double> (currentErosionCents_)     * 0.01;
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncA_[(size_t) u] = hz / sampleRate_;
            }
        }

        void updateUnisonPhaseIncrementsB (int midiNote) noexcept
        {
            for (int u = 0; u < kMaxUnison; ++u)
            {
                const double semitones =
                      static_cast<double> (midiNote - 69)
                    + static_cast<double> (octOffsetB_) * 12.0
                    + static_cast<double> (semiOffsetB_)
                    + static_cast<double> (centsOffsetB_)            * 0.01
                    + static_cast<double> (uDetuneCents_[(size_t) u]) * 0.01
                    + static_cast<double> (currentErosionCents_)     * 0.01;
                const double hz = 440.0 * std::pow (2.0, semitones / 12.0);
                uPhaseIncB_[(size_t) u] = hz / sampleRate_;
            }
        }

        // Phase 11a — populate per-sine uFramePosA_/B_ offsets from current
        // frameSpreadA01_/B01_ and activeUnison_. Each sine u in [0, activeUnison_)
        // gets offset u_norm × spread × 0.5 (max ±0.5 of [0,1] frame range).
        // At UNISON=1 or SPREAD=0 every entry is 0.0 → render path falls back
        // to the voice-global framePos_ exactly (zero behaviour change vs pre-11a).
        void updateUnisonFramePositions() noexcept
        {
            for (int u = 0; u < activeUnison_; ++u)
            {
                if (activeUnison_ <= 1)
                {
                    uFramePosA_[(size_t) u] = 0.0f;
                    uFramePosB_[(size_t) u] = 0.0f;
                    continue;
                }
                const float u_norm = ((float) u / (float) (activeUnison_ - 1)) * 2.0f - 1.0f;
                uFramePosA_[(size_t) u] = u_norm * frameSpreadA01_ * 0.5f;
                uFramePosB_[(size_t) u] = u_norm * frameSpreadB01_ * 0.5f;
            }
            for (int u = activeUnison_; u < kMaxUnison; ++u)
            {
                uFramePosA_[(size_t) u] = 0.0f;
                uFramePosB_[(size_t) u] = 0.0f;
            }
        }

        // Phase 11d — wavefolder. 3 shapes, each output-bounded to ±1.
        //   0 = Linear (Serge — triangle-wave fold, near-infinite odd harmonics)
        //   1 = Sine   (Vital — sin(drive·x), bounded, bell character)
        //   2 = Triangle (Buchla 259 — 3-stage cascade, warm West-Coast)
        // amount in [0,1]; pre-gain is quadratic so the lower half of the knob
        // ramps gently and the upper half drives hard.
        static inline float applyFold (float x, int shape, float amount) noexcept
        {
            if (amount <= 1.0e-6f) return x;   // identity fast-path

            switch (shape)
            {
                case 0:
                {
                    // Linear (Serge) — pre 1..10, closed-form triangle wave fold.
                    const float pre    = 1.0f + amount * amount * 9.0f;
                    const float driven = x * pre;
                    const float q      = (driven + 1.0f) * 0.25f;
                    return 4.0f * std::fabs (q - std::round (q)) - 1.0f;
                }
                case 1:
                {
                    // Sine (Vital) — pre 1..2π, sin(drive·x), bounded ±1.
                    const float pre = 1.0f + amount * amount * 5.28318530f;
                    return std::sin (x * pre);
                }
                case 2:
                {
                    // Triangle (Buchla 259) — 3-stage cascade.
                    const float pre = 1.0f + amount * amount * 5.0f;
                    const float driven = x * pre;
                    auto linfold = [] (float v) -> float
                    {
                        const float q = (v + 1.0f) * 0.25f;
                        return 4.0f * std::fabs (q - std::round (q)) - 1.0f;
                    };
                    const float s1 = linfold (driven * 1.0f)        * 0.50f;
                    const float s2 = linfold (driven * 1.41421356f) * 0.35f;
                    const float s3 = linfold (driven * 2.0f)        * 0.15f;
                    return s1 + s2 + s3;
                }
                default: return x;
            }
        }

        // ── ADAA wavefolder (1st-order antiderivative anti-aliasing) ─────────
        // The triangle-based folds (Linear/Serge, Buchla) generate dense high
        // harmonics that alias badly at base rate. Rather than 2x oversample the
        // whole voice, we apply 1st-order ADAA — the same technique as the
        // nonlinear filters. Offline FFT proof: 2.4–5.6x less aliasing on the
        // triangle folds, neutral on the (already-smooth) sine fold, low-freq
        // shape preserved. State is per-sine (see foldStateA_/B_).
        //
        // foldAntideriv(x) = ∫ applyFold dx, so applyFold = d/dx foldAntideriv.
        //   triangle-wave antiderivative: G(q) = 2 r|r| − r,  r = q − round(q)
        //   ∫ linfold(x·a) dx = (4/a)·G((x·a+1)/4)
        static inline float foldGtri (float q) noexcept
        {
            const float r = q - std::round (q);
            return 2.0f * r * std::fabs (r) - r;
        }
        static inline float foldFlin (float x, float a) noexcept
        {
            return (4.0f / a) * foldGtri ((x * a + 1.0f) * 0.25f);
        }
        static inline float foldAntideriv (float x, int shape, float amount) noexcept
        {
            switch (shape)
            {
                case 1: { const float pre = 1.0f + amount * amount * 5.28318530f; return -std::cos (pre * x) / pre; }
                case 2: { const float pre = 1.0f + amount * amount * 5.0f;
                          return 0.5f  * foldFlin (x, pre)
                               + 0.35f * foldFlin (x, pre * 1.41421356f)
                               + 0.15f * foldFlin (x, pre * 2.0f); }
                default:{ const float pre = 1.0f + amount * amount * 9.0f; return foldFlin (x, pre); }
            }
        }

        struct FoldState { float x1 = 0.0f; };

        // 1st-order ADAA: y[n] = (F(x[n]) − F(x[n−1])) / (x[n] − x[n−1]).
        // NEVER cache F — recompute BOTH antiderivatives live each sample. This is
        // correct after a note-on reset (x1=0 evaluates the real F(0), which is NOT 0
        // for any fold shape) and stays correct when shape/amount change between samples
        // (both terms evaluate on the current curve). [ADAA audit vs Waveshaper 75cb6a9]
        // A midpoint-naive fallback handles the low-slew 0/0 case.
        static inline float applyFoldADAA (float x, int shape, float amount, FoldState& st) noexcept
        {
            float y;
            if (amount <= 1.0e-6f)
            {
                y = x;                                                   // fold off → identity
            }
            else if (std::fabs (x - st.x1) < 1.0e-5f)
            {
                y = applyFold (0.5f * (x + st.x1), shape, amount);       // low-slew fallback
            }
            else
            {
                const float Fx  = foldAntideriv (x,     shape, amount);
                const float Fx1 = foldAntideriv (st.x1, shape, amount);  // recomputed live, never cached
                y = (Fx - Fx1) / (x - st.x1);
            }
            st.x1 = x;
            return y;
        }


        // 3 modes: 0=LowPass (20k → 200 Hz quadratic), 1=HighPass (20 → 8000 Hz quadratic),
        // 2=Smear (allpass, 4000 → 200 Hz quadratic, Q 0.707 → 4.0 linear).
        void updateSpectralCoefficients (int type, float amount,
                                          juce::dsp::IIR::Filter<float>& filterL,
                                          juce::dsp::IIR::Filter<float>& filterR) noexcept
        {
            const float amtSq = amount * amount;
            using Coeffs = juce::dsp::IIR::Coefficients<float>;

            switch (type)
            {
                case 0:  // Low Pass
                {
                    const float cutoff = 200.0f + (1.0f - amtSq) * 19800.0f;
                    auto c = Coeffs::makeLowPass (sampleRate_, cutoff, 0.707f);
                    *filterL.coefficients = *c;
                    *filterR.coefficients = *c;
                    break;
                }
                case 1:  // High Pass
                {
                    const float cutoff = 20.0f + amtSq * 7980.0f;
                    auto c = Coeffs::makeHighPass (sampleRate_, cutoff, 0.707f);
                    *filterL.coefficients = *c;
                    *filterR.coefficients = *c;
                    break;
                }
                case 2:  // Smear (allpass with rising Q)
                {
                    const float cutoff = 200.0f + (1.0f - amtSq) * 3800.0f;
                    const float Q      = 0.707f + amount * 3.293f;
                    auto c = Coeffs::makeAllPass (sampleRate_, cutoff, Q);
                    *filterL.coefficients = *c;
                    *filterR.coefficients = *c;
                    break;
                }
                default:
                {
                    // For non-biquad modes (Comb/RingMod/BitCrush, types 3-5), set passthrough
                    // so the IIR filter has no effect; the per-sample render code
                    // applies the actual transform.
                    auto c = Coeffs::makeLowPass (sampleRate_, 20000.0f, 0.707f);
                    *filterL.coefficients = *c;
                    *filterR.coefficients = *c;
                    break;
                }
            }
        }

        // Standard PolyBLEP residual — subtract from the naive saw at the
        // discontinuity to suppress alias harmonics above Nyquist. Public
        // domain reference: Välimäki & Huovilainen, "Antialiasing Oscillators
        // in Subtractive Synthesis," IEEE SP Mag 2007.
        static double polyBlep (double t, double dt) noexcept
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0;
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return t * t + t + t + 1.0;
            }
            return 0.0;
        }

        double sampleRate_      = 48000.0;
        int    currentMidiNote_ = 60;
        float  currentVelocity_ = 1.0f;
        bool   playing_         = false;

        juce::ADSR             ampEnv_;
        juce::ADSR::Parameters ampParams_ { 0.005f, 0.1f, 0.7f, 0.2f };

        // Batch 1 Filter — FilterSlot replaces juce::dsp::LadderFilter.
        // baseCutHz / baseRes01 are the knob values; the renderNextBlock
        // loop adds envAmount * fltEnv + drift before each sample's
        // filterSlot_.setParams call (per-sample modulation, semitone space).
        tw::filters::FilterSlot filterSlot_;
        juce::ADSR              fltEnv_;
        juce::ADSR::Parameters  fltEnvParams_ { 0.005f, 0.2f, 0.0f, 0.3f };
        float                   baseCutHz_   = 20000.0f;
        float                   baseRes01_   = 0.0f;
        float                   drv01_       = 0.0f;
        float                   envAmount_   = 0.0f;   // -1..+1 (bipolar)
        float                   fltErosionAmount_ = 0.0f;

        // Per-voice EROSION drift state (cutoff random walk, ~0.5 Hz LP)
        juce::Random            driftRng_;
        float                   driftState_  = 0.0f;
        float                   driftCoef_   = 0.0f;   // 1 - exp(-2π·fc/sr)

        // 2× oversampling input-prev for linear-interp upsample (Ladder + Acid303)
        float                   osPrevL_     = 0.0f;
        float                   osPrevR_     = 0.0f;

        // Filter 2 — fully independent second FilterSlot (own type/cut/res/drv/env).
        // Shares the FLT envelope shape + EROSION drift, with its own ENV amount.
        tw::filters::FilterSlot filterSlot2_;
        float                   baseCutHz2_  = 20000.0f;
        float                   baseRes012_  = 0.0f;
        float                   drv012_      = 0.0f;
        float                   envAmount2_  = 0.0f;   // -1..+1 (bipolar)
        int                     filterType1_ = 0;      // tracked for NONE-aware routing
        int                     filterType2_ = (int) tw::filters::Type::NONE;
        // Routing between the two filters + per-filter wet/dry mix.
        int                     filterRouting_ = 0;    // 0 = series, 1 = parallel
        float                   filterMix1_  = 1.0f;   // 0 = dry, 1 = fully filtered
        float                   filterMix2_  = 1.0f;

        static float hzToSemi (float hz) noexcept
        {
            return 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, hz) / 440.0f);
        }

        juce::AudioBuffer<float>       scratch_;
        float                          level_ = 0.7f;
        float                          panL_  = 0.7071f;  // cos(pi/4)
        float                          panR_  = 0.7071f;  // sin(pi/4)

        int   octOffset_   = 0;
        int   semiOffset_  = 0;
        float centsOffset_ = 0.0f;

        const tw::Wavetable* currentWavetable_ = nullptr;
        float                framePos_         = 0.0f;

        // Phase 2C — Warp state.
        int                  warpMode_         = 0;     // 0=NONE,1=BEND,2=SYNC,3=FORMANT
        float                warpAmount_       = 0.0f;  // 0..1

        // Phase 3 — Engine choice.
        Engine               engine_           = Engine::WT;

        // Phase 3 — NOISE engine state.
        // xorshift32 PRNG seeded per-voice from a const offset XOR'd with the
        // voice's `this` pointer so each voice has decorrelated noise streams.
        // One-pole low-pass for "color" (FRAME=0 → bright/white, FRAME=1 →
        // dark/brown). Tanh post-saturation for "drive" (WARP AMT).
        std::uint32_t noiseState_  = 0x9E3779B9u
                                   ^ static_cast<std::uint32_t> (
                                         reinterpret_cast<std::uintptr_t> (this));
        float         noiseLpZ_    = 0.0f;

        // ── Phase 9 — OSC B state (mirror of OSC A, B suffix on each) ────
        float  levelB_          = 0.5f;      // default lower than A so they sum tastefully
        float  panLB_           = 0.7071f;   // cos(pi/4) — center
        float  panRB_           = 0.7071f;   // sin(pi/4) — center
        int    octOffsetB_      = 0;
        int    semiOffsetB_     = 0;
        float  centsOffsetB_    = 0.0f;
        const tw::Wavetable* currentWavetableB_ = nullptr;
        float  framePosB_       = 0.0f;
        int    warpModeB_       = 0;
        int currentMipLevelA_ = 0;   // Phase 10a — refreshed per block from uPhaseIncA_[0]
        int currentMipLevelB_ = 0;   // Phase 10a — refreshed per block from uPhaseIncB_[0]
        float  warpAmountB_     = 0.0f;
        Engine engineB_         = Engine::WT;
        // xorshift32 PRNG for OSC B — seeded with sqrt(2) fractional constant
        // (0x6A09E667) XOR'd with this pointer so OSC B has a decorrelated noise
        // stream from OSC A (which uses the golden-ratio constant 0x9E3779B9).
        std::uint32_t noiseStateB_ = 0x6A09E667u
                                   ^ static_cast<std::uint32_t> (
                                         reinterpret_cast<std::uintptr_t> (this));
        float  noiseLpZB_       = 0.0f;

        // ── Phase 8b — Unison-in-voice state (per-sine arrays) ──────────
        // Each unison sub-voice u in [0, activeUnison_) has its own pitch state.
        // The single voice now renders all UNISON sines internally.
        std::array<double, kMaxUnison> uPhaseA_       {};
        std::array<double, kMaxUnison> uPhaseIncA_    {};
        std::array<double, kMaxUnison> uModPhaseA_    {};
        std::array<double, kMaxUnison> uSyncPhaseA_   {};

        std::array<double, kMaxUnison> uPhaseB_       {};
        std::array<double, kMaxUnison> uPhaseIncB_    {};
        std::array<double, kMaxUnison> uModPhaseB_    {};
        std::array<double, kMaxUnison> uSyncPhaseB_   {};

        // Phase 11a — per-sine WT frame position (centre = framePos_, offset = SPREAD × u_norm × 0.5).
        // Render path wraps to [0,1] before wavetable lookup.
        std::array<float, kMaxUnison> uFramePosA_   {};
        std::array<float, kMaxUnison> uFramePosB_   {};

        // Phase 11a — SPREAD amount per OSC (0..1, pushed per-block from APVTS).
        float frameSpreadA01_ = 0.0f;
        float frameSpreadB01_ = 0.0f;

        // ── WT BLUR (frame blend) — repurposes the old FRAME_SPREAD knob ─────────
        // The param sets blurTarget*; it's smoothed into blur* per block for clickless
        // changes; blend* hold the pre-built blended single-cycle buffer that every
        // unison sine reads via Wavetable::readCycle. last* gate rebuilds to "on change".
        // blur 0 ⇒ renderBlend reproduces the old bilinear lookup exactly.
        float blurTargetA_ = 0.0f, blurTargetB_ = 0.0f;
        float blurA_ = 0.0f, blurB_ = 0.0f;
        std::array<float, tw::Wavetable::kFrameSize> blendA_ {};
        std::array<float, tw::Wavetable::kFrameSize> blendB_ {};
        float lastFpA_ = -2.0f, lastBlurA_ = -2.0f; int lastMipA_ = -2;
        float lastFpB_ = -2.0f, lastBlurB_ = -2.0f; int lastMipB_ = -2;
        // The source table pointer is ALSO a blend dependency: Spectral Morph and live
        // preset changes swap currentWavetable_ with frame/blur/mip unchanged, so the
        // gate must watch the pointer too or the blend keeps stale (pre-morph) bytes.
        const tw::Wavetable* lastWtA_ = nullptr;
        const tw::Wavetable* lastWtB_ = nullptr;

        // ── PHASE (note-on phase-init mode) ──────────────────────────────────────
        // 0=RETRIG, 1=FREE, 2=RANDOM (default), 3=SPREAD. phaseSeeded_ guards the
        // one-time FREE seed; phaseRng_ is the per-voice xorshift state for RANDOM.
        int phaseModeA_ = 2, phaseModeB_ = 2;
        bool phaseSeeded_ = false;
        std::uint32_t phaseRng_ = 0u;

        // Phase 11d — FOLD state (per OSC).
        int   foldShapeA_   = 0;     // 0=Linear, 1=Sine, 2=Triangle
        float foldAmountA_  = 0.0f;
        int   foldShapeB_   = 0;
        float foldAmountB_  = 0.0f;
        // Phase 11d ADAA — per-sine fold state (1st-order antiderivative AA).
        std::array<FoldState, kMaxUnison> foldStateA_ {};
        std::array<FoldState, kMaxUnison> foldStateB_ {};

        // Phase 11c — SPECTRAL filter state (per OSC).
        int   spectralTypeA_   = 0;     // 0=LP, 1=HP, 2=Smear, 3=Comb, 4=RingMod, 5=BitCrush
        float spectralAmtA_    = 0.0f;
        int   spectralTypeB_   = 0;
        float spectralAmtB_    = 0.0f;
        bool  spectralBypassA_ = true;  // optimization: skip processing when amount near zero
        bool  spectralBypassB_ = true;

        // Phase 11g — INTERP mode (frame interpolation control).
        int interpModeA_ = 0;   // 0 = Linear (bilinear, default), 1 = Stepped (snap to nearest frame)
        int interpModeB_ = 0;

        // Phase 11g — Comb delay line + Ring Mod phase + Bit Crush state per OSC per channel
        static constexpr int kSpectralCombSize = 256;
        std::array<float, kSpectralCombSize> spectralCombAL_{}, spectralCombAR_{};
        std::array<float, kSpectralCombSize> spectralCombBL_{}, spectralCombBR_{};
        int spectralCombWriteA_ = 0;
        int spectralCombWriteB_ = 0;
        double spectralRingPhaseA_ = 0.0;   // for Ring Mod
        double spectralRingPhaseB_ = 0.0;

        juce::dsp::IIR::Filter<float> spectralFilterAL_, spectralFilterAR_;
        juce::dsp::IIR::Filter<float> spectralFilterBL_, spectralFilterBR_;

        // Phase 11i — Downsample S&H state per OSC per channel
        float spectralDsHeldAL_ = 0.0f, spectralDsHeldAR_ = 0.0f;
        float spectralDsHeldBL_ = 0.0f, spectralDsHeldBR_ = 0.0f;
        float spectralDsCounterA_ = 0.0f, spectralDsCounterB_ = 0.0f;

        // Phase 11i — Tilt EQ filter pair per OSC per channel (one-pole LP for low band)
        float spectralTiltLowAL_ = 0.0f, spectralTiltLowAR_ = 0.0f;
        float spectralTiltLowBL_ = 0.0f, spectralTiltLowBR_ = 0.0f;

        // Phase 11i — Vibrato modulator phase per OSC + tiny delay buffer
        static constexpr int kSpectralVibSize = 64;
        std::array<float, kSpectralVibSize> spectralVibAL_{}, spectralVibAR_{};
        std::array<float, kSpectralVibSize> spectralVibBL_{}, spectralVibBR_{};
        int spectralVibWriteA_ = 0, spectralVibWriteB_ = 0;
        double spectralVibPhaseA_ = 0.0, spectralVibPhaseB_ = 0.0;

        // Per-sine unison config (computed at setUnison / startNote).
        std::array<float,  kMaxUnison> uDetuneCents_  {};
        std::array<float,  kMaxUnison> uPanL_         {};
        std::array<float,  kMaxUnison> uPanR_         {};

        int   activeUnison_     = 1;       // 1..kMaxUnison
        float unisonSpread01_   = 0.0f;    // 0..1

        // Phase 8a — EROSION state (per-voice slow LFO wobbles pitch ±2 cents max)
        float erosionAmount_       = 0.0f;  // 0..1 from SYN_EROSION/100
        float erosionRate_         = 0.5f;  // sub-1Hz, randomized per voice in startNote
        float erosionPhase_        = 0.0f;  // 0..1, advances per block
        float currentErosionCents_ = 0.0f;  // cached this-block drift value

        // Phase 8a — HORIZON tilt filter (per-voice high-shelf, gain depends on midiNote * horizon)
        float horizonAmount_   = 0.0f;  // -1..+1 from SYN_HORIZON/100
        juce::dsp::IIR::Filter<float> horizonShelfL_;
        juce::dsp::IIR::Filter<float> horizonShelfR_;

        // Phase 8a polish — exponential fade on voice steal (~30ms, Phase 12) to avoid clicks
        float stealingFade_     = 1.0f;     // 1.0 = no fade, 0.0 = silent
        float stealingFadeStep_ = 0.0f;     // multiplier per sample during fade
        bool  stealing_         = false;

        // Phase 12 — monotonic timestamp from startNote, used by UnisonSynth
        // to find the oldest non-stealing voice when the polyphony cap is hit.
        juce::uint32 noteStartStamp_ = 0;
    };
}
