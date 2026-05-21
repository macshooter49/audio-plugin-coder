// SamplerVoice.h
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleBuffer.h"
#include "Slice.h"
#include "Warp/WarpProcessor.h"
#include <atomic>
#include <cmath>
#include <cstring>

namespace tw
{
    /**
     * VoiceConfig — set by TerrainSynth on the chosen voice immediately
     * BEFORE startNote(). Encodes the slice region + reverse + the pitch ratio
     * to use for this trigger. The voice no longer derives pitch from
     * (currentNote - rootNote) on its own; the synth dispatcher resolves all
     * mode-aware logic up front and hands the voice a fully-baked config.
     *
     * Defaults reproduce pre-slicer "play whole sample at unity pitch"
     * behavior, so a voice that didn't get a config (e.g. external MIDI
     * paths that don't go through TerrainSynth::noteOn) still functions.
     */
    struct VoiceConfig
    {
        juce::int64 startSample      = 0;        // inclusive
        juce::int64 endSample        = -1;       // exclusive; -1 sentinel = whole buffer
        bool        reverse          = false;
        float       pitchSemitones   = 0.0f;     // semitones from unity (additive with pitch wheel)
        bool        forceOneShot     = false;    // override LOOP mode for this voice (used for audition)
        int         sliceIndex       = -1;       // index in the slice list this voice plays; -1 = Whole-mode (no slice)
        WarpMode    warpMode         = WarpMode::None;   // per-chop warp engine; None = current resample behavior
        float       stretchRatio     = 1.0f;             // 0.25..4.0; ignored when warpMode == None
    };

    class SamplerVoice : public juce::SynthesiserVoice
    {
    public:
        SamplerVoice (SampleBuffer& sb,
                      std::atomic<int>&   /*rootNoteRef*/,    // kept for API compat — pitch now comes from VoiceConfig
                      std::atomic<float>& attackMsRef,
                      std::atomic<float>& releaseMsRef,
                      std::atomic<int>&   loopModeRef) noexcept
            : sample (sb),
              attackMsParam (attackMsRef),
              releaseMsParam (releaseMsRef),
              loopModeParam (loopModeRef) {}

        bool canPlaySound (juce::SynthesiserSound*) override { return true; }

        void setCurrentPlaybackSampleRate (double sr) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
            sampleRateForEnv = sr > 0.0 ? sr : 48000.0;
            warp.prepare (sampleRateForEnv, 2, 512);
        }

        /** Called by TerrainSynth immediately BEFORE startNote(). Latches the
         *  slice bounds + pitch + reverse for the upcoming trigger. */
        void prepareForNoteOn (const VoiceConfig& cfg) noexcept
        {
            pendingConfig = cfg;
            pendingConfigValid = true;
        }

        void startNote (int midiNoteNumber, float velocity,
                        juce::SynthesiserSound*, int /*pitchWheelPos*/) override
        {
            currentNote     = midiNoteNumber;
            currentVelocity = velocity;

            // Resolve the active config — either the pending one set by
            // TerrainSynth, or a default that mimics pre-slicer behavior.
            activeConfig = pendingConfigValid ? pendingConfig : VoiceConfig{};
            pendingConfigValid = false;

            updatePitchRatio();

            // Resolve buffer bounds for this trigger. endSample == -1 means
            // "whole buffer length, deferred until renderNextBlock since the
            // sample buffer might not be loaded yet at this instant".
            playStartIdx = activeConfig.startSample;
            playEndIdx   = activeConfig.endSample;  // -1 sentinel handled in render
            reversePlay  = activeConfig.reverse;

            // Initial playhead position depends on direction.
            if (reversePlay && playEndIdx > 0)
                playhead = (double) (playEndIdx - 1);
            else
                playhead = (double) playStartIdx;

            // Compute envelope increments from current attack/release in ms.
            const float attackSec  = juce::jmax (0.0f, attackMsParam.load()  * 0.001f);
            const float releaseSec = juce::jmax (0.001f, releaseMsParam.load() * 0.001f);
            attackInc  = attackSec  > 0.0f ? (1.0f / (attackSec  * (float) sampleRateForEnv)) : 1.0f;
            releaseDec = 1.0f / (releaseSec * (float) sampleRateForEnv);

            envLevel = 0.0f;
            envStage = (attackInc >= 1.0f) ? EnvStage::Sustaining : EnvStage::Attack;
            if (envStage == EnvStage::Sustaining) envLevel = 1.0f;
            isActive = true;

            // Warp engine: select mode + reset state for this trigger. The
            // dispatcher lazily allocates the underlying spectral engine on
            // the first non-None setMode, so voices that never warp pay zero
            // memory / CPU cost.
            warp.setMode           (activeConfig.warpMode);
            warp.setStretchRatio   (activeConfig.stretchRatio);
            warp.setPitchSemitones (activeConfig.pitchSemitones);
            warp.noteOnReset();
            outputSamplesSinceTrigger = 0;
            warpNeedsPrime = (activeConfig.warpMode != WarpMode::None);
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (! allowTailOff)
            {
                envStage = EnvStage::Off;
                envLevel = 0.0f;
                clearCurrentNote();
                isActive = false;
                playhead = 0.0;
                return;
            }
            envStage = EnvStage::Release;
        }

        void pitchWheelMoved (int newPitchWheelValue) override
        {
            // 0..16383 → -1..+1 → ±2 semitones
            const double normalized = (newPitchWheelValue - 8192) / 8192.0;
            pitchBendSemis = normalized * 2.0;
            if (isActive) updatePitchRatio();
        }

        void controllerMoved (int, int) override {}

        // ── Read-only state used by TerrainSynth to publish per-slice glow ──
        float getEnvelopeLevel() const noexcept { return envLevel; }
        int   getSliceIndex()    const noexcept { return activeConfig.sliceIndex; }
        bool  isPlaying()        const noexcept { return isActive && envStage != EnvStage::Off; }

        void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                              int startSample, int numSamples) override
        {
            if (! isActive || envStage == EnvStage::Off) return;

            auto buf = sample.load();
            if (! buf || buf->getNumSamples() == 0) return;

            // Warp branch — entire path lives in renderWarp(). NONE mode
            // (the vast majority of voices) falls through to the existing
            // per-sample loop below, behaviorally unchanged.
            if (activeConfig.warpMode != WarpMode::None)
            {
                renderWarp (outputBuffer, *buf, startSample, numSamples);
                return;
            }

            const int    bufLen   = buf->getNumSamples();
            const int    bufChans = buf->getNumChannels();

            // Resolve slice bounds — clamp to actual buffer extents and apply
            // the -1 sentinel for "use whole buffer".
            const juce::int64 bStart = juce::jlimit ((juce::int64) 0, (juce::int64) bufLen - 1, playStartIdx);
            const juce::int64 bEndRaw = (playEndIdx < 0)
                                          ? (juce::int64) bufLen
                                          : juce::jlimit (bStart + 1, (juce::int64) bufLen, playEndIdx);
            const double endIdx   = static_cast<double> (bEndRaw - 1);  // last accessible sample for interpolation
            const double startIdx = static_cast<double> (bStart);
            const double pitchInc = pitchRatio;  // always positive — direction is reversePlay

            auto* outL = outputBuffer.getWritePointer (0, startSample);
            auto* outR = outputBuffer.getNumChannels() > 1
                         ? outputBuffer.getWritePointer (1, startSample) : outL;

            const auto* inL = buf->getReadPointer (0);
            const auto* inR = bufChans > 1 ? buf->getReadPointer (1) : inL;

            // forceOneShot in VoiceConfig overrides the global LOOP param —
            // used by audition triggers so a clicked-to-preview slice stops
            // naturally even when the user has LOOP enabled globally.
            const int loopMode = activeConfig.forceOneShot ? 0 : loopModeParam.load();

            for (int i = 0; i < numSamples; ++i)
            {
                // ── Bounds + loop/wrap handling, direction-aware ────────────
                const bool atForwardEnd = (! reversePlay) && playhead >= endIdx;
                const bool atReverseEnd = (  reversePlay) && playhead <= startIdx;
                if (atForwardEnd || atReverseEnd)
                {
                    if (loopMode == 1)
                    {
                        // Forward-loop in slice space: wrap to the opposite
                        // boundary, preserving fractional offset so pitch
                        // ratio stays consistent across the seam.
                        const double sliceLen = endIdx - startIdx;
                        if (sliceLen <= 0.0) { envStage = EnvStage::Off; envLevel = 0.0f; clearCurrentNote(); isActive = false; return; }
                        if (! reversePlay)
                        {
                            playhead = startIdx + std::fmod (playhead - startIdx, sliceLen);
                        }
                        else
                        {
                            playhead = endIdx - std::fmod (endIdx - playhead, sliceLen);
                        }
                    }
                    else
                    {
                        // One-shot: voice goes silent.
                        envStage = EnvStage::Off;
                        envLevel = 0.0f;
                        clearCurrentNote();
                        isActive = false;
                        return;
                    }
                }

                // ── Envelope tick ───────────────────────────────────────────
                switch (envStage)
                {
                    case EnvStage::Attack:
                        envLevel += attackInc;
                        if (envLevel >= 1.0f) { envLevel = 1.0f; envStage = EnvStage::Sustaining; }
                        break;
                    case EnvStage::Sustaining:
                        envLevel = 1.0f;
                        break;
                    case EnvStage::Release:
                        envLevel -= releaseDec;
                        if (envLevel <= 0.0f)
                        {
                            envLevel = 0.0f;
                            envStage = EnvStage::Off;
                            clearCurrentNote();
                            isActive = false;
                            return;
                        }
                        break;
                    case EnvStage::Off:
                        return;
                }

                // ── Linear interpolation read ───────────────────────────────
                const auto i0 = static_cast<int> (playhead);
                const int  i1 = juce::jmin (bufLen - 1, i0 + 1);
                const auto frac = static_cast<float> (playhead - (double) i0);
                const auto sampleL = inL[i0] + frac * (inL[i1] - inL[i0]);
                const auto sampleR = inR[i0] + frac * (inR[i1] - inR[i0]);

                // Anti-click tail fade for one-shot mode: linear ramp to zero
                // across the last 256 samples (~5 ms at 48 kHz). Direction
                // aware — fade as we approach whichever end we're heading to.
                float tailFade = 1.0f;
                if (loopMode == 0)
                {
                    constexpr double kTailLen = 256.0;
                    const double samplesToEnd = reversePlay
                                                  ? (playhead - startIdx)
                                                  : (endIdx   - playhead);
                    if (samplesToEnd < kTailLen)
                        tailFade = juce::jmax (0.0f, static_cast<float> (samplesToEnd / kTailLen));
                }

                const auto gain = currentVelocity * envLevel * tailFade;
                outL[i] += sampleL * gain;
                outR[i] += sampleR * gain;

                // Advance — direction-aware.
                playhead += reversePlay ? -pitchInc : pitchInc;
            }
        }

    private:
        enum class EnvStage { Off, Attack, Sustaining, Release };

        void updatePitchRatio()
        {
            const double semitones = (double) activeConfig.pitchSemitones + pitchBendSemis;
            pitchRatio = std::pow (2.0, semitones / 12.0);
        }

        // ────────────────────────────────────────────────────────────────
        // Warp branch — only invoked when activeConfig.warpMode != None.
        //
        // Phase 1 design notes:
        //   - One-shot only (loop mode ignored). Voice ends naturally when
        //     the source playhead reaches the slice end.
        //   - Reverse supported via reading source backwards into the scratch
        //     buffer; the warp engine consumes scratch identically either way.
        //   - Pitch bend ignored on warped voices (v1.1 polish). Only the
        //     activeConfig.pitchSemitones value, set at startNote, is used.
        //   - Tail fade: applied to the OUTPUT samples in the last 256-sample
        //     ramp before the source reaches its end (in source-space, scaled
        //     by stretchRatio).
        // ────────────────────────────────────────────────────────────────
        void ensureWarpScratch (int n)
        {
            if (n > warpScratchCapacity)
            {
                warpScratchInL .realloc ((size_t) n);
                warpScratchInR .realloc ((size_t) n);
                warpScratchOutL.realloc ((size_t) n);
                warpScratchOutR.realloc ((size_t) n);
                warpScratchCapacity = n;
            }
        }

        /** Pull `count` source samples (linear-interpolated, reverse-aware)
         *  into the destination buffers starting at sample 0. Advances playhead.
         *  Returns false if the source range is exhausted before count samples
         *  are read; remaining samples in dst are zeroed. */
        bool pullSourceIntoScratch (const juce::AudioBuffer<float>& buf,
                                    float* dstL, float* dstR, int count)
        {
            const int bufLen   = buf.getNumSamples();
            const int bufChans = buf.getNumChannels();
            const juce::int64 bStart = juce::jlimit ((juce::int64) 0, (juce::int64) bufLen - 1, playStartIdx);
            const juce::int64 bEnd   = (playEndIdx < 0)
                                         ? (juce::int64) bufLen
                                         : juce::jlimit (bStart + 1, (juce::int64) bufLen, playEndIdx);
            const double endIdx   = static_cast<double> (bEnd - 1);
            const double startIdx = static_cast<double> (bStart);
            const double step     = reversePlay ? -1.0 : 1.0;

            const auto* inL = buf.getReadPointer (0);
            const auto* inR = bufChans > 1 ? buf.getReadPointer (1) : inL;

            std::memset (dstL, 0, sizeof (float) * (size_t) count);
            std::memset (dstR, 0, sizeof (float) * (size_t) count);

            for (int i = 0; i < count; ++i)
            {
                const bool past = reversePlay ? (playhead < startIdx)
                                              : (playhead > endIdx);
                if (past) return false;

                const auto i0 = (int) playhead;
                const int  i1 = juce::jlimit (0, bufLen - 1, i0 + (reversePlay ? -1 : 1));
                const float frac = (float) (playhead - (double) i0);
                dstL[i] = inL[i0] + frac * (inL[i1] - inL[i0]);
                dstR[i] = inR[i0] + frac * (inR[i1] - inR[i0]);
                playhead += step;
            }
            return true;
        }

        void renderWarp (juce::AudioBuffer<float>& outputBuffer,
                         const juce::AudioBuffer<float>& buf,
                         int startSample, int numSamples)
        {
            const int bufLen = buf.getNumSamples();
            if (bufLen <= 0 || numSamples <= 0) return;

            const double sr = (double) activeConfig.stretchRatio;
            const double srInv = sr > 0.0001 ? 1.0 / sr : 1.0;  // safety, never div by zero

            // How many source samples we'll consume this block. Stretch ratio > 1
            // means we consume FEWER source samples per output sample.
            const int inputLen = juce::jmax (1, (int) std::round ((double) numSamples * srInv));

            // Reserve scratch space large enough for both input read and output
            // write paths (we need DISTINCT buffers — Signalsmith requires it).
            ensureWarpScratch (juce::jmax (inputLen, numSamples));

            auto* scratchInL  = warpScratchInL.getData();
            auto* scratchInR  = warpScratchInR.getData();
            auto* scratchOutL = warpScratchOutL.getData();
            auto* scratchOutR = warpScratchOutR.getData();

            // One-time seek() priming after note-on. Without this the first
            // outputLatency() samples of engine output are silent ramp (per
            // Signalsmith README "Seeking and starting" section).
            if (warpNeedsPrime)
            {
                warpNeedsPrime = false;
                const int primeLen = warp.inputLatency();
                if (primeLen > 0)
                {
                    // Cap prime length to avoid eating the entire chop on short slices.
                    const int sliceLen = (playEndIdx < 0)
                                            ? bufLen
                                            : (int) juce::jlimit ((juce::int64) 0, (juce::int64) bufLen,
                                                                  playEndIdx - playStartIdx);
                    const int safePrime = juce::jmin (primeLen, sliceLen / 2);
                    if (safePrime > 0)
                    {
                        ensureWarpScratch (juce::jmax (safePrime, juce::jmax (inputLen, numSamples)));
                        pullSourceIntoScratch (buf, warpScratchInL.getData(), warpScratchInR.getData(), safePrime);
                        warp.seek (warpScratchInL.getData(), warpScratchInR.getData(), safePrime);
                    }
                }
            }

            // Pull source into scratchIn (linear-interp, reverse-aware). Engine
            // handles pitch + stretch; we feed unity-rate source.
            const bool exhausted = ! pullSourceIntoScratch (buf, scratchInL, scratchInR, inputLen);
            bool endReached = exhausted;

            // Engine reads scratchIn (inputLen samples), writes scratchOut
            // (numSamples). DISTINCT BUFFERS — Signalsmith API requires it.
            warp.process (scratchInL, scratchInR, scratchOutL, scratchOutR, numSamples);

            // Envelope ticks numSamples times this block (output rate).
            auto* outL = outputBuffer.getWritePointer (0, startSample);
            auto* outR = outputBuffer.getNumChannels() > 1
                          ? outputBuffer.getWritePointer (1, startSample) : outL;

            for (int i = 0; i < numSamples; ++i)
            {
                // Envelope tick — same state machine as NONE path.
                switch (envStage)
                {
                    case EnvStage::Attack:
                        envLevel += attackInc;
                        if (envLevel >= 1.0f) { envLevel = 1.0f; envStage = EnvStage::Sustaining; }
                        break;
                    case EnvStage::Sustaining: envLevel = 1.0f; break;
                    case EnvStage::Release:
                        envLevel -= releaseDec;
                        if (envLevel <= 0.0f)
                        {
                            envLevel = 0.0f;
                            envStage = EnvStage::Off;
                            clearCurrentNote();
                            isActive = false;
                            return;
                        }
                        break;
                    case EnvStage::Off: return;
                }

                // Tail fade — last 256 output samples before source-end (or
                // immediately if we already hit the end this block).
                float tailFade = 1.0f;
                if (endReached && i >= numSamples - 256)
                {
                    const float ramp = (float) (numSamples - i) / 256.0f;
                    tailFade = juce::jmax (0.0f, ramp);
                }

                const float gain = currentVelocity * envLevel * tailFade;
                outL[i] += scratchOutL[i] * gain;
                outR[i] += scratchOutR[i] * gain;
            }

            outputSamplesSinceTrigger += numSamples;

            // One-shot end: source consumed past its end, fade-out finished.
            if (endReached)
            {
                envStage = EnvStage::Off;
                envLevel = 0.0f;
                clearCurrentNote();
                isActive = false;
            }
        }

        SampleBuffer&       sample;
        std::atomic<float>& attackMsParam;
        std::atomic<float>& releaseMsParam;
        std::atomic<int>&   loopModeParam;

        // Voice state
        int      currentNote      = -1;
        float    currentVelocity  = 0.0f;
        double   playhead         = 0.0;
        double   pitchRatio       = 1.0;
        double   pitchBendSemis   = 0.0;
        bool     isActive         = false;

        // Slice playback state — resolved at startNote from pendingConfig.
        VoiceConfig activeConfig;
        VoiceConfig pendingConfig;
        bool        pendingConfigValid = false;
        juce::int64 playStartIdx = 0;
        juce::int64 playEndIdx   = -1;
        bool        reversePlay  = false;

        // Warp dispatcher + scratch buffers. Engine is lazily allocated inside
        // WarpProcessor on the first non-None setMode call; voices that never
        // warp pay zero memory cost beyond the WarpProcessor itself.
        // Two scratch pairs — Signalsmith requires DISTINCT input/output buffers.
        WarpProcessor          warp;
        juce::HeapBlock<float> warpScratchInL,  warpScratchInR;
        juce::HeapBlock<float> warpScratchOutL, warpScratchOutR;
        int                    warpScratchCapacity       = 0;
        int                    outputSamplesSinceTrigger = 0;
        bool                   warpNeedsPrime            = false;

        // AR envelope (one-shot model — no Decay/Sustain knee, just Attack-then-hold-then-Release).
        EnvStage envStage         = EnvStage::Off;
        float    envLevel         = 0.0f;
        float    attackInc        = 1.0f;
        float    releaseDec       = 0.001f;
        double   sampleRateForEnv = 48000.0;
    };

    struct SamplerSound : public juce::SynthesiserSound
    {
        bool appliesToNote    (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };
}
