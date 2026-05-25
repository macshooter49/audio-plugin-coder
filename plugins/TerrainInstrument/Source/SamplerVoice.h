// SamplerVoice.h
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleBuffer.h"
#include "Slice.h"
#include "Warp/WarpProcessor.h"
#include "Warp/WarpRenderCache.h"
#include "ModulationEngine.h"
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
        int         sourceVersionId  = 0;        // monotonic; matches WarpRenderCache::Key::sourceVersionId
        WarpMode    warpMode         = WarpMode::None;   // per-chop warp engine; None = current resample behavior
        float       stretchRatio     = 1.0f;             // 0.25..4.0; ignored when warpMode == None
        float       attackMs         = -1.0f;            // -1 sentinel = inherit global ATTACK_MS
        float       releaseMs        = -1.0f;            // -1 sentinel = inherit global RELEASE_MS
        float       decayMs          = 0.0f;             // 0 = no decay phase, jump from attack peak to sustain
        float       sustainLevel     = 1.0f;             // 0..1; 1.0 = hold at peak (current behavior)
        float       volume           = 1.0f;             // linear gain multiplier, 0..2 (1.0=unity, 2.0=+6 dB boost)

        // ── Scan mode (Mark 1.5) ─────────────────────────────────────────────
        bool  scanEnabled = false;   // ping-pong scan within the slice
        float scanRate    = 1.0f;    // playback rate multiplier for scan (base; mod applied in Task 7)
        float scanWindow  = 1.0f;    // fraction of slice covered by ping-pong (0..1)

        // ── Per-chop FX independence ─────────────────────────────────────────
        // fxIndependent=false → voice flows through global FX chain (current).
        // fxIndependent=true  → processor reads the flags below to route this
        // voice through per-FX bus(es), or to the dry master if all flags are
        // off. The other flags are REMEMBERED while independent is off so
        // toggling back on restores the user's selection.
        bool  fxIndependent = false;
        bool  fxGrain       = false;
        int   fxTapeMachine = 0;     // 0=off, 1=Studio, 2=Cassette, 3=Wire
        bool  fxSpace       = false;
        bool  fxDelay       = false;
        bool  fxEq          = false;
        bool  fxJune        = false;

        // ── HOLD mode (Mark 1.5 final) ───────────────────────────────────────
        // When true, the voice IGNORES MIDI note-off (with allowTailOff) and
        // plays to natural completion (1-shot exhaustion / loop boundary).
        // MPC/FL "latch" feel — single key tap fires the chop through to end.
        // Captured at startNote so toggling holdMode mid-playback doesn't
        // retroactively change in-flight voices.
        bool  holdMode      = false;
    };

    class SamplerVoice : public juce::SynthesiserVoice
    {
    public:
        SamplerVoice (SampleBuffer& sb,
                      std::atomic<int>&   /*rootNoteRef*/,    // kept for API compat — pitch now comes from VoiceConfig
                      std::atomic<float>& attackMsRef,
                      std::atomic<float>& releaseMsRef,
                      std::atomic<int>&   loopModeRef,
                      ModulationEngine*   me = nullptr,
                      WarpRenderCache*    wc = nullptr,
                      std::atomic<float>* chopFadeRef = nullptr) noexcept
            : sample (sb),
              attackMsParam (attackMsRef),
              releaseMsParam (releaseMsRef),
              loopModeParam (loopModeRef),
              modEngine (me),
              warpCache_ (wc),
              chopFadeMsParam_ (chopFadeRef) {}

        bool canPlaySound (juce::SynthesiserSound*) override { return true; }

        void setCurrentPlaybackSampleRate (double sr) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (sr);
            sampleRateForEnv = sr > 0.0 ? sr : 48000.0;
            warp.prepare (sampleRateForEnv, 2, 512);
            // ~64 ms cross-block tail fade — generous enough for Signalsmith's
            // STFT pipeline to fully drain at extreme stretch ratios.
            warpTailFadeTotal = juce::jmax (1024, (int) (sampleRateForEnv * 0.064));
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
            // Per-chop override wins when >= 0; otherwise fall back to the
            // global APVTS param — so legacy presets and Whole-mode triggers
            // (sliceIndex == -1) behave exactly as before.
            const float atkMs  = activeConfig.attackMs  >= 0.0f ? activeConfig.attackMs  : attackMsParam.load();
            const float relMs  = activeConfig.releaseMs >= 0.0f ? activeConfig.releaseMs : releaseMsParam.load();
            const float decMs  = juce::jmax (0.0f, activeConfig.decayMs);
            const float attackSec  = juce::jmax (0.0f,   atkMs * 0.001f);
            const float releaseSec = juce::jmax (0.001f, relMs * 0.001f);
            attackInc  = attackSec  > 0.0f ? (1.0f / (attackSec  * (float) sampleRateForEnv)) : 1.0f;
            releaseDec = 1.0f / (releaseSec * (float) sampleRateForEnv);

            // Decay covers the descent from peak (1.0) to sustainLevel over
            // decayMs. When decayMs is 0 or sustainLevel >= 1.0 the decay
            // phase is skipped — current behavior preserved for legacy slices.
            sustainTarget = juce::jlimit (0.0f, 1.0f, activeConfig.sustainLevel);
            const float decaySec = decMs * 0.001f;
            const float decayDistance = juce::jmax (0.0f, 1.0f - sustainTarget);
            decayDec = (decaySec > 0.0f && decayDistance > 0.0f)
                          ? (decayDistance / (decaySec * (float) sampleRateForEnv))
                          : 0.0f;  // 0 = decay phase will be skipped at runtime

            // Linear per-chop volume multiplier — applied at gain stage so all
            // env phases (attack, sustain, release) inherit it.
            voiceGain = juce::jlimit (0.0f, 2.0f, activeConfig.volume);

            envLevel = 0.0f;
            envStage = (attackInc >= 1.0f) ? EnvStage::Sustaining : EnvStage::Attack;
            // When attack is instant AND there's a decay phase queued, we
            // skip Attack but still want to run Decay — handled by the env
            // tick below since EnvStage::Sustaining will branch to Decay if
            // we enter with envLevel=1 but sustainTarget<1. Simpler: jump
            // straight to Decay here.
            if (envStage == EnvStage::Sustaining)
            {
                envLevel = 1.0f;
                if (decayDec > 0.0f && sustainTarget < 1.0f)
                    envStage = EnvStage::Decay;
            }
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
            warpTailFadeRemaining = 0;
            warpTailFadeStarted   = false;

            // Latch scan state — base values only; mod resolved per-block in Task 7.
            scanActive      = activeConfig.scanEnabled;
            scanRateLive    = activeConfig.scanRate;
            scanWindowLive  = activeConfig.scanWindow;
            turnaroundFadeT = 0.0;
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (! allowTailOff)
            {
                // Hard-stop (voice stealing) always wins — HOLD mode can't keep
                // an unwanted voice alive when JUCE needs the voice slot back.
                envStage = EnvStage::Off;
                envLevel = 0.0f;
                clearCurrentNote();
                isActive = false;
                playhead = 0.0;
                return;
            }
            // HOLD mode (Mark 1.5): MPC/FL "latch" — single key tap plays the
            // chop through to natural completion. Ignore note-off; the voice
            // will end naturally via 1-shot exhaustion (renderNextBlock clears
            // its own note when playhead leaves the slice / tail-fade drains)
            // or stay in LOOP forever until user retriggers or stops the host.
            if (activeConfig.holdMode) return;
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

        // ── Scan-viz polling getters (called from UI thread — read-only) ──────
        bool  isScanActive()      const noexcept { return scanActive; }
        float getScanWindowLive() const noexcept { return scanWindowLive; }

        /** Returns playhead position normalized to [0, 1] within the slice bounds.
         *  Written from BOTH render paths (NONE and cache) each sample via a
         *  lock-free atomic so the viz always reads the correct coordinate space
         *  regardless of which path is active. Without the atomic, cache-path
         *  voices produced garbage (cache coords != source coords) → viz frozen.
         */
        float getScanPositionNormalized() const noexcept
        {
            return juce::jlimit (0.0f, 1.0f, scanPositionNorm_.load (std::memory_order_acquire));
        }

#if JUCE_DEBUG
        // Test-only accessors for scan-mode unit tests (Task 5).
        bool   getReversePlay() const noexcept { return reversePlay; }
        double getPlayhead()    const noexcept { return playhead; }
#endif

        // ── FX independence: PluginProcessor sets this pointer per block so
        // voices whose chop is fxIndependent capture their output into a
        // separate "indy" bus instead of the global FX chain. NULL = no
        // routing (current behavior — voice writes to the passed buffer). */
        void setIndyTargetBuffer (juce::AudioBuffer<float>* buf) noexcept { indyTargetBuffer = buf; }

        void renderNextBlock (juce::AudioBuffer<float>& passedBuffer,
                              int startSample, int numSamples) override
        {
            if (! isActive || envStage == EnvStage::Off) return;

            // FX-independence redirect — when this voice's chop is detached
            // from the global FX chain, its output goes to the indy capture
            // bus instead of the synth's main buffer. The local `outputBuffer`
            // reference shadows the parameter so the entire render body below
            // (NONE path + renderWarp) writes to the correct destination
            // without any per-line conditionals.
            juce::AudioBuffer<float>& outputBuffer
                = (activeConfig.fxIndependent && indyTargetBuffer != nullptr)
                    ? *indyTargetBuffer
                    : passedBuffer;

            auto buf = sample.load();
            if (! buf || buf->getNumSamples() == 0) return;

            // Cache the host block size so the loop crossfade gate (below
            // and in pullSourceIntoScratch) can derive its actual firing
            // rate from samples-consumed-per-block and pick its threshold
            // correctly across hosts.
            latestBlockSize = juce::jmax (1, numSamples);

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
            const double sliceLen = endIdx - startIdx;
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

            // Loop crossfade length — equal-power mix between the main
            // playhead and a leading head, masks the click at the abrupt
            // wrap.
            //
            // v10 (2026-05-22): scale the fade duration with the wrap rate
            // so the AM modulation index stays bounded. v9 used a binary
            // gate (full 20 ms below 20 Hz, OFF above), but at the very
            // highest octaves the bare wrap still produces a hard
            // 1-sample step at the wrap rate — that step's harmonic
            // amplitude is ~0 dB at the fundamental, audible as a click
            // train ("higher octave = clickier again"). The scaled
            // approach keeps SOME fade active at high rates so we trade
            // hard 0 dB step harmonics for soft −16 dB AM sidebands.
            //
            // Formula: fade ≈ period × 0.10 holds AM index ≈ 0.16
            // (sidebands at −16 dB) regardless of rate. Capped at 20 ms
            // at low rates (no need for more) and at sliceLen / 4 (so
            // overlap doesn't collapse on tiny chops).
            const double loopWrapsPerSec = (loopMode == 1 && sliceLen > 0.0)
                ? pitchRatio * sampleRateForEnv / sliceLen
                : 0.0;
            double xfadeLen = 0.0;
            if (loopMode == 1 && sliceLen > 8.0)
            {
                double xfadeTarget = 0.020 * sampleRateForEnv;
                if (loopWrapsPerSec > 5.0)
                {
                    const double periodSamples = sampleRateForEnv / loopWrapsPerSec;
                    xfadeTarget = juce::jmin (xfadeTarget, periodSamples * 0.10);
                }
                xfadeLen = juce::jmin (sliceLen * 0.25, xfadeTarget);
            }

            // ── Resolve scan rate + window with modulation applied ───────────
            // Per-block dispatch: voice asks mod engine for the modulated
            // value of "Active Chop Scan Rate" / "Window", applied on top of
            // its own per-slice base. Same LFO drives every active scan-on
            // voice with independent base differentiation per chop.
            if (scanActive && modEngine != nullptr)
            {
                scanRateLive   = juce::jlimit (0.1f, 8.0f,
                                     modEngine->getModulatedValue (ModulationEngine::pActiveChopScanRate,
                                                                    activeConfig.scanRate));
                scanWindowLive = juce::jlimit (0.05f, 1.0f,
                                     modEngine->getModulatedValue (ModulationEngine::pActiveChopScanWindow,
                                                                    activeConfig.scanWindow));
            }

            for (int i = 0; i < numSamples; ++i)
            {
                // ── Bounds + loop/wrap handling, direction-aware ────────────
                // When scan is active, effective playback bounds are narrowed
                // to the scan-window region (centred in the slice). This
                // computation is repeated per-sample to allow live mod updates
                // in Task 7 without needing to cache derived values.
                //
                // Pitch-decoupling (Mark 1.5 fix): when scanRate >= 1.0 we
                // additionally narrow the effective range by 1/scanRate so
                // the ping-pong completes faster without reading the source at
                // a non-native rate (which would shift pitch). At scanRate < 1.0
                // we keep the full window and let the advance-scaling below
                // carry the slower/drone character (varispeed-down, deliberate).
                const double windowSpan        = scanActive ? (sliceLen * (double) scanWindowLive) : sliceLen;
                const double rateScaledSpan    = (scanActive && scanRateLive > 1.0f)
                                                     ? windowSpan / (double) scanRateLive
                                                     : windowSpan;
                const double rateMargin        = (windowSpan - rateScaledSpan) * 0.5;
                const double margin            = (scanActive ? ((sliceLen - windowSpan) * 0.5) : 0.0) + rateMargin;
                const double effSliceStart     = startIdx + margin;
                const double effSliceEnd       = endIdx   - margin;

                const bool atForwardEnd = (! reversePlay) && playhead >= effSliceEnd;
                const bool atReverseEnd = (  reversePlay) && playhead <= effSliceStart;
                if (atForwardEnd || atReverseEnd)
                {
                    if (scanActive)
                    {
                        // Ping-pong boundary flip. Arm turnaroundFadeT for the
                        // 16ms crossfade (Task 6 consumes it). Clamp playhead to
                        // the effective edge for the new direction, then advance
                        // one step into the new direction so the NEXT iteration's
                        // boundary check doesn't immediately re-fire (pre-fix:
                        // the `continue` here skipped the advance at the bottom
                        // of the loop, leaving playhead exactly at the boundary
                        // → next iteration's atForwardEnd / atReverseEnd both
                        // true → back-to-back flips produced a 1-sample
                        // oscillation → audible click on every ping-pong turn).
                        reversePlay     = ! reversePlay;
                        turnaroundFadeT = 1.0;
                        playhead        = reversePlay ? effSliceEnd : effSliceStart;
                        // Step one sample into the new direction so the next
                        // boundary check starts strictly inside the window.
                        // Use same pitch-decoupled formula as the main advance.
                        const double scanAdvancePost = scanRateLive >= 1.0f ? 1.0 : (double) scanRateLive;
                        playhead += (reversePlay ? -pitchInc : pitchInc) * scanAdvancePost;
                        continue;  // skip wrap/stop logic for scan voices
                    }

                    if (loopMode == 1)
                    {
                        if (sliceLen <= 0.0) { envStage = EnvStage::Off; envLevel = 0.0f; clearCurrentNote(); isActive = false; return; }
                        // Wrap and skip past the crossfade region so the next
                        // samples pick up exactly where the leading head left
                        // off — the seam is bit-continuous with the fade-in.
                        if (! reversePlay)
                        {
                            playhead = startIdx + xfadeLen
                                       + std::fmod (playhead - endIdx, sliceLen);
                        }
                        else
                        {
                            playhead = endIdx - xfadeLen
                                       - std::fmod (startIdx - playhead, sliceLen);
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
                        if (envLevel >= 1.0f)
                        {
                            envLevel = 1.0f;
                            envStage = (decayDec > 0.0f && sustainTarget < 1.0f)
                                          ? EnvStage::Decay
                                          : EnvStage::Sustaining;
                        }
                        break;
                    case EnvStage::Decay:
                        envLevel -= decayDec;
                        if (envLevel <= sustainTarget)
                        {
                            envLevel = sustainTarget;
                            envStage = EnvStage::Sustaining;
                        }
                        break;
                    case EnvStage::Sustaining:
                        envLevel = sustainTarget;
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
                auto sampleL = inL[i0] + frac * (inL[i1] - inL[i0]);
                auto sampleR = inR[i0] + frac * (inR[i1] - inR[i0]);

                // ── Scan turnaround crossfade (28ms Hann-window) ─────────────
                // At each direction flip we briefly blend two reads: the "old
                // direction" (continuing past the boundary as if no flip
                // happened) and the "new direction" (post-flip).
                //
                // Three improvements over the original 16ms equal-power blend:
                // 1. oldPos is REFLECTED at slice boundaries so we always read
                //    valid slice audio instead of clamping into adjacent content.
                // 2. Extended to 28ms — wider window disperses the discontinuity
                //    energy further in time, reducing perceived click amplitude.
                // 3. Hann-window taper (sum=1 constant-amplitude) instead of
                //    cos/sin equal-power (sum peaks at sqrt(2)≈+3 dB at t=0.5).
                //    Both reads are CORRELATED (same slice after reflection), so
                //    Hann's constant-amplitude taper sounds cleaner than the
                //    equal-power dip+hump. cos/sin is correct for uncorrelated
                //    sources; Hann is correct for correlated.
                //
                // turnaroundFadeT is armed to 1.0 at the flip and decrements to 0.
                //   t=0 → 100% old direction (first sample after flip)
                //   t=1 → 100% new direction (fully transitioned)
                if (turnaroundFadeT > 0.0)
                {
                    // Bug A fix: scale turnaround window by 1/pitchInc so the
                    // oldPos projection doesn't race to the boundary at high MIDI
                    // pitches (pitchInc=4 at +2 octaves → old 28ms window projected
                    // 4× further → immediate reflection → asymmetric click).
                    // At root pitch (pitchInc=1) the window is unchanged at 28ms.
                    // At 2 octaves up (pitchInc=4) it shrinks to 7ms — still
                    // enough to taper the transient, and click amplitude at that
                    // rate is already small due to high flip frequency.
                    const double turnaroundLenMs = 28.0 / juce::jmax (1.0, pitchInc);
                    const double turnaroundLenSamples = turnaroundLenMs * 0.001 * sampleRateForEnv;
                    const double decrement = 1.0 / juce::jmax (1.0, turnaroundLenSamples);

                    // Old-direction read: project where the playhead would be
                    // if we hadn't flipped. Pre-flip direction is opposite of
                    // current reversePlay (which was toggled at the boundary).
                    const double oldDirSign  = reversePlay ? +1.0 : -1.0;
                    const double xScanAdv    = (scanActive && scanRateLive < 1.0f) ? (double) scanRateLive : 1.0;
                    double oldPos = playhead
                                    + oldDirSign * pitchInc * xScanAdv
                                      * (1.0 - turnaroundFadeT)
                                      * turnaroundLenSamples;

                    // Reflect oldPos at slice boundaries so we read valid slice
                    // audio throughout the entire fade (never stepping outside
                    // into adjacent slice content). Belt-and-suspenders clamp
                    // handles pathologically short slices.
                    if (oldPos > effSliceEnd)   oldPos = effSliceEnd   - (oldPos - effSliceEnd);
                    if (oldPos < effSliceStart) oldPos = effSliceStart + (effSliceStart - oldPos);
                    oldPos = juce::jlimit (effSliceStart, effSliceEnd, oldPos);

                    const auto oi0 = juce::jlimit (0, bufLen - 1, static_cast<int> (oldPos));
                    const int  oi1 = juce::jlimit (0, bufLen - 1, oi0 + 1);
                    const auto ofrac = static_cast<float> (oldPos - (double) oi0);
                    const auto oldL = inL[oi0] + ofrac * (inL[oi1] - inL[oi0]);
                    const auto oldR = inR[oi0] + ofrac * (inR[oi1] - inR[oi0]);

                    // Hann-window taper: sum = 1.0 throughout (constant amplitude).
                    const float t = static_cast<float> (1.0 - turnaroundFadeT);  // 0 → 1 across fade
                    const float oldGain = 0.5f * (1.0f + std::cos (t * juce::MathConstants<float>::pi));  // 1 at t=0, 0 at t=1
                    const float newGain = 0.5f * (1.0f - std::cos (t * juce::MathConstants<float>::pi));  // 0 at t=0, 1 at t=1

                    sampleL = oldL * oldGain + sampleL * newGain;
                    sampleR = oldR * oldGain + sampleR * newGain;

                    turnaroundFadeT -= decrement;
                    if (turnaroundFadeT < 0.0) turnaroundFadeT = 0.0;
                }

                // ── Loop-mode equal-power crossfade ──────────────────────────
                // In the last xfadeLen samples before the wrap boundary, mix
                // in a leading head reading from the opposite boundary. By the
                // time playhead reaches endIdx the main weight is 0 and the
                // leading sample fully covers the seam — wrap then jumps the
                // playhead by xfadeLen so the post-wrap read continues exactly
                // where the leading was.
                // Bypassed when scanActive — scan owns its own 28ms Hann
                // turnaround fade; the loop xfade fires on top → double-fade
                // clicks. Scan voices use only the turnaround path above.
                if (xfadeLen > 0.0 && !scanActive)
                {
                    const double samplesToEnd = reversePlay
                                                  ? (playhead - startIdx)
                                                  : (endIdx   - playhead);
                    if (samplesToEnd < xfadeLen)
                    {
                        const double leadOffset = xfadeLen - samplesToEnd;
                        const double leadPos = reversePlay
                                                  ? (endIdx - leadOffset)
                                                  : (startIdx + leadOffset);
                        const auto li0 = juce::jlimit (0, bufLen - 1, static_cast<int> (leadPos));
                        const int  li1 = juce::jlimit (0, bufLen - 1, li0 + 1);
                        const auto lfrac = static_cast<float> (leadPos - (double) li0);
                        const auto leadL = inL[li0] + lfrac * (inL[li1] - inL[li0]);
                        const auto leadR = inR[li0] + lfrac * (inR[li1] - inR[li0]);

                        // Equal-power: at samplesToEnd=xfadeLen→main only, at
                        // samplesToEnd=0→lead only. Uses sin/cos for ~3 dB hump
                        // at the midpoint that masks correlated source content.
                        const float t = static_cast<float> (1.0 - samplesToEnd / xfadeLen);
                        const float mainGain = std::cos (t * juce::MathConstants<float>::halfPi);
                        const float leadGain = std::sin (t * juce::MathConstants<float>::halfPi);
                        sampleL = sampleL * mainGain + leadL * leadGain;
                        sampleR = sampleR * mainGain + leadR * leadGain;
                    }
                }

                // Anti-click tail fade for one-shot mode: ~10 ms ramp to zero
                // before the slice end. Direction aware — fade as we approach
                // whichever end we're heading to.
                // Bypassed when scanActive — scan ping-pongs and never reaches
                // the slice end in one-shot fashion; applying this fade would
                // ramp the held note to silence mid-scan.
                float tailFade = 1.0f;
                if (loopMode == 0 && !scanActive)
                {
                    const double kTailLen = juce::jmin (sliceLen * 0.5,
                                                         0.010 * sampleRateForEnv);
                    const double samplesToEnd = reversePlay
                                                  ? (playhead - startIdx)
                                                  : (endIdx   - playhead);
                    if (samplesToEnd < kTailLen)
                        tailFade = juce::jmax (0.0f, static_cast<float> (samplesToEnd / kTailLen));
                }

                // ── Chop fade: fade-in at slice start + fade-out at slice end ──
                // Applied in both loop and one-shot mode — composes with tailFade
                // (tailFade is 1.0 in loop mode so they don't conflict).
                float chopFade = 1.0f;
                if (chopFadeMsParam_ != nullptr)
                {
                    const double chopFadeSamples = (double)(chopFadeMsParam_->load()) * 0.001 * sampleRateForEnv;
                    if (chopFadeSamples > 0.0)
                    {
                        const double distFromStart = playhead - effSliceStart;
                        const double distFromEnd   = effSliceEnd - playhead;
                        const double fadeIn  = juce::jlimit (0.0, 1.0, distFromStart / chopFadeSamples);
                        const double fadeOut = juce::jlimit (0.0, 1.0, distFromEnd   / chopFadeSamples);
                        chopFade = (float) juce::jmin (fadeIn, fadeOut);
                    }
                }

                const auto gain = currentVelocity * envLevel * tailFade * chopFade * voiceGain;
                outL[i] += sampleL * gain;
                outR[i] += sampleR * gain;

                // Update unified scan viz position (Bug B fix). Written here in
                // the NONE path so the viz getter just reads the atomic without
                // needing to know which render path is active.
                // Normalize to the EFFECTIVE scan range so the viz line sweeps the
                // full chop body regardless of how narrow effSliceStart/effSliceEnd are.
                if (scanActive)
                {
                    const double effRange = effSliceEnd - effSliceStart;
                    if (effRange > 0.0)
                        scanPositionNorm_.store (
                            juce::jlimit (0.0f, 1.0f,
                                static_cast<float> ((playhead - effSliceStart) / effRange)),
                            std::memory_order_release);
                }

                // Advance — direction-aware. Pitch-decoupling: at scanRate >= 1.0
                // we advance at the native pitch rate (no varispeed shift) — the
                // faster cycle is achieved by the narrowed effSliceEnd/Start above.
                // At scanRate < 1.0 we scale the advance by scanRate to give the
                // slow-drone / varispeed-down character the user liked at low rates.
                const double scanAdvance = scanActive
                    ? (scanRateLive >= 1.0f ? 1.0 : (double) scanRateLive)
                    : 1.0;
                playhead += (reversePlay ? -pitchInc : pitchInc) * scanAdvance;
            }
        }

    private:
        enum class EnvStage { Off, Attack, Decay, Sustaining, Release };

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
         *
         *  If `looping` is true and the source range is exhausted, the playhead
         *  wraps back to the slice start (or end for reverse) and reading
         *  continues — matching the NONE-mode loop behavior. If `looping` is
         *  false, returns false when exhausted with remaining samples in dst
         *  left as zero.
         */
        bool pullSourceIntoScratch (const juce::AudioBuffer<float>& buf,
                                    float* dstL, float* dstR, int count,
                                    bool looping)
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
            const double sliceLen = endIdx - startIdx;

            const auto* inL = buf.getReadPointer (0);
            const auto* inR = bufChans > 1 ? buf.getReadPointer (1) : inL;

            std::memset (dstL, 0, sizeof (float) * (size_t) count);
            std::memset (dstR, 0, sizeof (float) * (size_t) count);

            // Source-level crossfade so the warp engine sees a smooth stream
            // across the loop boundary instead of a one-sample discontinuity.
            //
            // v10 (2026-05-22): scale the fade duration with the wrap rate
            // so the AM modulation index stays bounded (~0.16 → −16 dB
            // sidebands) at any rate. Without scaling, v9 disabled the
            // fade entirely above 20 Hz and the bare wrap produced hard
            // 0 dB step harmonics at the wrap rate — audible as a click
            // train at the highest octaves on short slices. The scaled
            // approach keeps a tiny fade alive (e.g. 0.5 ms at 200 Hz
            // wrap rate) so the wrap is a soft AM modulation instead of
            // a hard step. Same fix is mirrored in renderNextBlock's
            // NONE-mode loop.
            //
            // Wrap rate proxy: count source samples per block × block
            // rate / sliceLen. Block rate ≈ sampleRate / latestBlockSize
            // (cached at top of renderNextBlock so the gate is host-
            // independent across block sizes).
            const double srcPerSec = (latestBlockSize > 0)
                ? (double) count * sampleRateForEnv / (double) latestBlockSize
                : 0.0;
            const double sourceWrapsPerSec = (looping && sliceLen > 0.0)
                ? srcPerSec / sliceLen
                : 0.0;
            double xfadeLen = 0.0;
            if (looping && sliceLen > 8.0)
            {
                double xfadeTarget = 0.020 * sampleRateForEnv;
                if (sourceWrapsPerSec > 5.0)
                {
                    const double periodSamples = sampleRateForEnv / sourceWrapsPerSec;
                    xfadeTarget = juce::jmin (xfadeTarget, periodSamples * 0.10);
                }
                xfadeLen = juce::jmin (sliceLen * 0.25, xfadeTarget);
            }

            for (int i = 0; i < count; ++i)
            {
                const bool past = reversePlay ? (playhead < startIdx)
                                              : (playhead > endIdx);
                if (past)
                {
                    if (looping && sliceLen > 0.0)
                    {
                        // Wrap and skip past the crossfade region so the next
                        // source samples pick up where the leading head left
                        // off — engine sees a bit-continuous stream.
                        if (! reversePlay)
                            playhead = startIdx + xfadeLen
                                       + std::fmod (playhead - endIdx, sliceLen);
                        else
                            playhead = endIdx - xfadeLen
                                       - std::fmod (startIdx - playhead, sliceLen);
                    }
                    else
                    {
                        return false;
                    }
                }

                const auto i0 = (int) playhead;
                const int  i1 = juce::jlimit (0, bufLen - 1, i0 + (reversePlay ? -1 : 1));
                const float frac = (float) (playhead - (double) i0);
                float s_L = inL[i0] + frac * (inL[i1] - inL[i0]);
                float s_R = inR[i0] + frac * (inR[i1] - inR[i0]);

                if (xfadeLen > 0.0)
                {
                    const double samplesToEnd = reversePlay
                                                  ? (playhead - startIdx)
                                                  : (endIdx   - playhead);
                    if (samplesToEnd < xfadeLen)
                    {
                        const double leadOffset = xfadeLen - samplesToEnd;
                        const double leadPos = reversePlay
                                                  ? (endIdx - leadOffset)
                                                  : (startIdx + leadOffset);
                        const auto li0 = juce::jlimit (0, bufLen - 1, static_cast<int> (leadPos));
                        const int  li1 = juce::jlimit (0, bufLen - 1, li0 + 1);
                        const float lfrac = static_cast<float> (leadPos - (double) li0);
                        const float leadL = inL[li0] + lfrac * (inL[li1] - inL[li0]);
                        const float leadR = inR[li0] + lfrac * (inR[li1] - inR[li0]);
                        const float t = static_cast<float> (1.0 - samplesToEnd / xfadeLen);
                        const float mainGain = std::cos (t * juce::MathConstants<float>::halfPi);
                        const float leadGain = std::sin (t * juce::MathConstants<float>::halfPi);
                        s_L = s_L * mainGain + leadL * leadGain;
                        s_R = s_R * mainGain + leadR * leadGain;
                    }
                }

                dstL[i] = s_L;
                dstR[i] = s_R;
                playhead += step;
            }
            return true;
        }

        // ── Task 11: read pre-rendered cache for scan + warp (non-None) mode. ──
        // Cache is at target stretch → scanRateLive controls pure motion through
        // the cache (no pitch artifact). Ping-pong and envelope are replicated
        // from the NONE-mode scan path (Tasks 5-7). The warp engine is NOT run.
        void renderScanFromCache (const juce::AudioBuffer<float>& cache,
                                  juce::AudioBuffer<float>& outputBuffer,
                                  int startSample, int numSamples)
        {
            const int cacheLen = cache.getNumSamples();
            if (cacheLen < 2) return;

            const auto* cL = cache.getReadPointer (0);
            const auto* cR = cache.getNumChannels() > 1 ? cache.getReadPointer (1) : cL;
            auto* outL = outputBuffer.getWritePointer (0, startSample);
            auto* outR = outputBuffer.getNumChannels() > 1
                         ? outputBuffer.getWritePointer (1, startSample) : outL;

            // Scan-window bounds within the cache (cache IS the stretched slice).
            // Pitch-decoupling (Mark 1.5 fix): same range-narrowing as NONE path.
            // At scanRate >= 1.0 the effective range is reduced by 1/scanRate so the
            // cycle completes faster WITHOUT reading the cache at a non-native rate.
            // At scanRate < 1.0 the full window is used and advance is scaled instead.
            const double startIdx  = 0.0;
            const double endIdx    = static_cast<double> (cacheLen - 1);
            const double sliceLen  = endIdx - startIdx;
            const double windowSpan      = sliceLen * static_cast<double> (scanWindowLive);
            const double rateScaledSpan  = (scanRateLive > 1.0f)
                                               ? windowSpan / static_cast<double> (scanRateLive)
                                               : windowSpan;
            const double rateMargin      = (windowSpan - rateScaledSpan) * 0.5;
            const double margin          = (sliceLen - windowSpan) * 0.5 + rateMargin;
            const double effSliceStart   = startIdx + margin;
            const double effSliceEnd     = endIdx   - margin;

            // Snap playhead into bounds if it drifted (e.g. from a pre-cache-miss block).
            if (playhead < effSliceStart) playhead = effSliceStart;
            if (playhead > effSliceEnd)   playhead = effSliceEnd;

            // Bug A fix (cache path): scale turnaround window by 1/pitchRatio so
            // oldPos projection stays within bounds at high MIDI pitches.
            // pitchRatio in the cache path plays the same role as pitchInc in NONE.
            const double turnaroundLenMs = 28.0 / juce::jmax (1.0, pitchRatio);
            const double turnaroundLenSamples = turnaroundLenMs * 0.001 * sampleRateForEnv;
            const double decrement = 1.0 / juce::jmax (1.0, turnaroundLenSamples);

            for (int i = 0; i < numSamples; ++i)
            {
                // Boundary check + ping-pong flip.
                const bool atForwardEnd = (! reversePlay) && playhead >= effSliceEnd;
                const bool atReverseEnd = (  reversePlay) && playhead <= effSliceStart;
                if (atForwardEnd || atReverseEnd)
                {
                    reversePlay     = ! reversePlay;
                    turnaroundFadeT = 1.0;
                    playhead        = reversePlay ? effSliceEnd : effSliceStart;
                    // Advance one step in the new direction so the next
                    // iteration's boundary check starts strictly inside the
                    // window (matching the fix applied to the NONE-mode path).
                    // Use pitch-decoupled rateMul: 1.0 at scanRate>=1, scanRate at <1.
                    const double rateMulPost = (scanRateLive >= 1.0f) ? 1.0 : static_cast<double> (scanRateLive);
                    playhead += (reversePlay ? -1.0 : 1.0) * pitchRatio * rateMulPost;
                }

                // Envelope tick (same state machine as NONE and renderWarp paths).
                switch (envStage)
                {
                    case EnvStage::Attack:
                        envLevel += attackInc;
                        if (envLevel >= 1.0f)
                        {
                            envLevel = 1.0f;
                            envStage = (decayDec > 0.0f && sustainTarget < 1.0f)
                                          ? EnvStage::Decay
                                          : EnvStage::Sustaining;
                        }
                        break;
                    case EnvStage::Decay:
                        envLevel -= decayDec;
                        if (envLevel <= sustainTarget)
                        {
                            envLevel = sustainTarget;
                            envStage = EnvStage::Sustaining;
                        }
                        break;
                    case EnvStage::Sustaining:
                        envLevel = sustainTarget;
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

                // Linear interpolation read from cache.
                const auto i0   = juce::jlimit (0, cacheLen - 1, static_cast<int> (playhead));
                const int  i1   = juce::jmin   (cacheLen - 1, i0 + 1);
                const auto frac = static_cast<float> (playhead - static_cast<double> (i0));
                auto sampleL = cL[i0] + frac * (cL[i1] - cL[i0]);
                auto sampleR = cR[i0] + frac * (cR[i1] - cR[i0]);

                // Turnaround crossfade (28ms Hann-window, mirroring NONE path fix):
                // reflect + extend + Hann taper (same three improvements as
                // the NONE path — see that comment block for full rationale).
                // In cache coords: effSliceStart/End = 0+margin / cacheLen-1-margin.
                if (turnaroundFadeT > 0.0)
                {
                    const double rateMul    = (scanRateLive >= 1.0f) ? 1.0 : static_cast<double> (scanRateLive);
                    const double oldDirSign = reversePlay ? +1.0 : -1.0;
                    double oldPos           = playhead
                                             + oldDirSign * pitchRatio * rateMul
                                               * (1.0 - turnaroundFadeT)
                                               * turnaroundLenSamples;

                    // Reflect at cache scan bounds so we never step outside the
                    // valid region (same logic as the NONE path reflection).
                    if (oldPos > effSliceEnd)   oldPos = effSliceEnd   - (oldPos - effSliceEnd);
                    if (oldPos < effSliceStart) oldPos = effSliceStart + (effSliceStart - oldPos);
                    oldPos = juce::jlimit (effSliceStart, effSliceEnd, oldPos);

                    const auto oi0 = juce::jlimit (0, cacheLen - 1, static_cast<int> (oldPos));
                    const int  oi1 = juce::jlimit (0, cacheLen - 1, oi0 + 1);
                    const auto ofrac = static_cast<float> (oldPos - static_cast<double> (oi0));
                    const auto oldL = cL[oi0] + ofrac * (cL[oi1] - cL[oi0]);
                    const auto oldR = cR[oi0] + ofrac * (cR[oi1] - cR[oi0]);

                    // Hann-window taper: constant-amplitude sum (correlated sources).
                    const float t       = static_cast<float> (1.0 - turnaroundFadeT);
                    const float oldGain = 0.5f * (1.0f + std::cos (t * juce::MathConstants<float>::pi));
                    const float newGain = 0.5f * (1.0f - std::cos (t * juce::MathConstants<float>::pi));
                    sampleL = oldL * oldGain + sampleL * newGain;
                    sampleR = oldR * oldGain + sampleR * newGain;

                    turnaroundFadeT -= decrement;
                    if (turnaroundFadeT < 0.0) turnaroundFadeT = 0.0;
                }

                // ── Chop fade in cache path (mirrors NONE path) ─────────────
                float chopFade = 1.0f;
                if (chopFadeMsParam_ != nullptr)
                {
                    const double chopFadeSamples = (double)(chopFadeMsParam_->load()) * 0.001 * sampleRateForEnv;
                    if (chopFadeSamples > 0.0)
                    {
                        const double distFromStart = playhead - effSliceStart;
                        const double distFromEnd   = effSliceEnd - playhead;
                        const double fadeIn  = juce::jlimit (0.0, 1.0, distFromStart / chopFadeSamples);
                        const double fadeOut = juce::jlimit (0.0, 1.0, distFromEnd   / chopFadeSamples);
                        chopFade = (float) juce::jmin (fadeIn, fadeOut);
                    }
                }

                const float gain = currentVelocity * envLevel * chopFade * voiceGain;
                outL[i] += sampleL * gain;
                outR[i] += sampleR * gain;

                // Update unified scan viz position (Bug B fix — cache path).
                // Normalize to the EFFECTIVE scan range so the viz sweeps the full
                // chop body even though the audio playhead covers only the narrowed
                // [effSliceStart, effSliceEnd] window within the cache.
                {
                    const double effRange = effSliceEnd - effSliceStart;
                    if (effRange > 0.0)
                        scanPositionNorm_.store (
                            juce::jlimit (0.0f, 1.0f,
                                static_cast<float> ((playhead - effSliceStart) / effRange)),
                            std::memory_order_release);
                }

                // Advance — cache is at target stretch; pitchRatio applies MIDI
                // pitch offset as a speed multiplier through the pre-stretched
                // cache (cache is pitched at root; different MIDI notes adjust
                // the playback rate through it → correct chromatic transposition
                // without re-running the warp engine per note).
                //
                // Pitch-decoupling: at scanRate >= 1.0 advance by pitchRatio only
                // (native rate, no pitch shift). At scanRate < 1.0 scale by scanRate
                // for the slow-drone / varispeed-down character at low rates.
                const double rateMul = (scanRateLive >= 1.0f) ? 1.0 : static_cast<double> (scanRateLive);
                playhead += (reversePlay ? -1.0 : 1.0) * pitchRatio * rateMul;
            }
        }

        void renderWarp (juce::AudioBuffer<float>& outputBuffer,
                         const juce::AudioBuffer<float>& buf,
                         int startSample, int numSamples)
        {
            const int bufLen = buf.getNumSamples();
            if (bufLen <= 0 || numSamples <= 0) return;

            // ── Task 11: Scan + Warp:Other path → read from cache ─────────────
            // Warp:None + Scan is handled in renderNextBlock's NONE loop (Tasks 5-7).
            if (scanActive && warpCache_ != nullptr)
            {
                WarpRenderCache::Key key {
                    activeConfig.sliceIndex,
                    activeConfig.sourceVersionId,
                    activeConfig.stretchRatio,
                    activeConfig.warpMode
                };
                const auto* cached = warpCache_->get (key);
                if (cached != nullptr)
                {
                    renderScanFromCache (*cached, outputBuffer, startSample, numSamples);
                    return;
                }
                // Cache miss → request prewarm, emit a silent block. Subsequent
                // blocks will see the populated cache entry and start reading.
                warpCache_->prewarm (key);
                return;
            }
            // warpCache_ == null → fall through to live warp path (degraded but no crash).

            // Source samples to feed the engine for this block. Beats and
            // Signalsmith have different formulas (Beats needs pitchRatio
            // factored in; Signalsmith handles pitch internally) — the
            // single source of truth is WarpProcessor::sourceSamplesPerBlock.
            // Computing this locally with the old `numSamples / stretchRatio`
            // formula starved Beats at pitchRatio > 1 → every chromatic note
            // above root buzzed because loopAnchor outpaced historyWriteIdx
            // in the engine's circular history.
            const int inputLen = juce::jmax (1, warp.sourceSamplesPerBlock (numSamples));

            // CRITICAL: compute MAX scratch size needed for this entire block
            // BEFORE caching any data pointers. Earlier code had two separate
            // ensureWarpScratch calls (one for the main process path, one inside
            // the prime branch with a typically-larger size). The second call
            // grew the HeapBlock via realloc, which freed the memory the cached
            // pointers (scratchInL/R, scratchOutL/R) pointed at — every first
            // warped block then wrote to freed memory → heap corruption →
            // host crash. Compute prime length up front and allocate once.
            int safePrime = 0;
            if (warpNeedsPrime)
            {
                const int primeLen = warp.inputLatency();
                if (primeLen > 0)
                {
                    const int sliceLen = (playEndIdx < 0)
                                            ? bufLen
                                            : (int) juce::jlimit ((juce::int64) 0, (juce::int64) bufLen,
                                                                  playEndIdx - playStartIdx);
                    safePrime = juce::jmin (primeLen, sliceLen / 2);
                    safePrime = juce::jmax (0, safePrime);
                }
            }

            const int scratchSize = juce::jmax (inputLen, juce::jmax (numSamples, safePrime));
            ensureWarpScratch (scratchSize);

            // Cache pointers ONLY after the final size is locked in. Anything
            // that reallocs the HeapBlock after this point would invalidate
            // these — but nothing in this function does.
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
                if (safePrime > 0)
                {
                    // Prime never loops — we want a clean opening from the
                    // slice's actual start, not a wrap-mixed prime.
                    pullSourceIntoScratch (buf, scratchInL, scratchInR, safePrime, /*looping=*/false);
                    warp.seek            (scratchInL, scratchInR, safePrime);
                }
            }

            // Loop-mode awareness — warped chops respect the global LOOP
            // toggle the same way NONE-mode chops do. forceOneShot (audition
            // path) still overrides and plays one-shot regardless.
            const int  loopMode = activeConfig.forceOneShot ? 0 : loopModeParam.load();
            const bool looping  = (loopMode == 1);

            // Pull source into scratchIn (linear-interp, reverse-aware). Engine
            // handles pitch + stretch; we feed unity-rate source. In loop mode
            // the pull wraps internally and always returns true, so endReached
            // stays false and the voice continues until note-off.
            const bool exhausted = ! pullSourceIntoScratch (buf, scratchInL, scratchInR, inputLen, looping);

            // Cross-block tail fade (v3 of warp declick). The single-block
            // 5–21 ms fade I had before couldn't actually cover Signalsmith's
            // ~50 ms STFT pipeline at high stretch — it got clamped to
            // numSamples (one host block, ~10 ms at typical settings) so
            // most of the Signalsmith buffer got cut off → residual click.
            //
            // New flow: when exhausted fires for the first time, START the
            // tail fade counter. Subsequent blocks keep processing zero-padded
            // scratchIn (Signalsmith flushes its buffer) and the counter
            // decrements per OUTPUT sample. When the counter reaches 0 the
            // voice finally terminates. Duration is 3072 samples (~64 ms at
            // 48 k) — generous enough for Signalsmith's STFT to drain even
            // at extreme stretch ratios.
            const bool justExhausted = exhausted && warpTailFadeRemaining == 0
                                        && ! warpTailFadeStarted;
            if (justExhausted)
            {
                warpTailFadeRemaining = warpTailFadeTotal;
                warpTailFadeStarted   = true;
            }
            const bool fadingThisBlock = warpTailFadeRemaining > 0;

            // Engine reads scratchIn (inputLen samples), writes scratchOut
            // (numSamples). DISTINCT BUFFERS — Signalsmith API requires it.
            // We KEEP calling process() during tail fade so the engine can
            // flush its internal buffer naturally.
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
                        if (envLevel >= 1.0f)
                        {
                            envLevel = 1.0f;
                            envStage = (decayDec > 0.0f && sustainTarget < 1.0f)
                                          ? EnvStage::Decay
                                          : EnvStage::Sustaining;
                        }
                        break;
                    case EnvStage::Decay:
                        envLevel -= decayDec;
                        if (envLevel <= sustainTarget)
                        {
                            envLevel = sustainTarget;
                            envStage = EnvStage::Sustaining;
                        }
                        break;
                    case EnvStage::Sustaining: envLevel = sustainTarget; break;
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

                // Cross-block tail fade ramp. While warpTailFadeRemaining > 0
                // we scale the engine output by remaining / total — that
                // ramps from 1.0 → 0.0 smoothly across however many blocks
                // it takes to consume the full warpTailFadeTotal. Voice
                // terminates after the counter hits 0 (see below).
                float tailFade = 1.0f;
                if (warpTailFadeRemaining > 0)
                {
                    tailFade = (float) warpTailFadeRemaining
                             / (float) juce::jmax (1, warpTailFadeTotal);
                    --warpTailFadeRemaining;
                }

                const float gain = currentVelocity * envLevel * tailFade * voiceGain;
                outL[i] += scratchOutL[i] * gain;
                outR[i] += scratchOutR[i] * gain;
            }

            outputSamplesSinceTrigger += numSamples;

            // Terminate ONLY when the cross-block tail fade is fully drained.
            // If exhausted but the fade is still in progress, we leave the
            // voice alive so subsequent blocks can keep flushing the engine.
            if (fadingThisBlock && warpTailFadeRemaining == 0)
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

        // ── Modulation engine reference (Task 7) ────────────────────────────
        ModulationEngine* modEngine  = nullptr;  // non-owning; set at construction via PluginProcessor

        // ── Warp render cache reference (Task 11) ───────────────────────────
        WarpRenderCache*  warpCache_ = nullptr;  // non-owning; set at construction via PluginProcessor

        // ── Chop fade parameter (anti-click ramp at slice boundaries) ────────
        std::atomic<float>* chopFadeMsParam_ = nullptr;  // non-owning; points to PluginProcessor::chopFadeMsAtomic

        // ── Scan state (Mark 1.5) ────────────────────────────────────────────
        bool   scanActive       = false;   // mirrors activeConfig.scanEnabled, captured at startNote
        float  scanRateLive     = 1.0f;    // resolved per-block with mod applied (Task 7)
        float  scanWindowLive   = 1.0f;    // resolved per-block with mod applied (Task 7)
        double turnaroundFadeT  = 0.0;     // 1.0 → 0.0 over 28ms; Task 6 consumes
        // Unified normalized scan position — written from BOTH NONE and cache
        // render paths each sample (audio thread), read by viz poll (UI thread).
        // Lock-free atomic avoids data races; torn reads produce at most one
        // frame of jitter, which is imperceptible.
        mutable std::atomic<float> scanPositionNorm_ { 0.0f };

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

        // Full ADSR envelope state — extended from the original AR (Attack /
        // Sustaining-at-1.0 / Release) model. decayDec drives the descent
        // from peak to sustainTarget; when 0 or sustainTarget>=1, decay is
        // skipped at runtime so legacy slices behave identically.
        EnvStage envStage         = EnvStage::Off;
        float    envLevel         = 0.0f;
        float    attackInc        = 1.0f;
        float    decayDec         = 0.0f;     // 0 = no decay phase
        float    sustainTarget    = 1.0f;     // env target during Sustaining
        float    releaseDec       = 0.001f;
        float    voiceGain        = 1.0f;     // per-chop volume multiplier (Slice.volume)
        double   sampleRateForEnv = 48000.0;
        // FX-independence target — when non-null and the active chop has
        // fxIndependent=true, render writes to this buffer instead of the
        // synth's main output. Set by PluginProcessor each processBlock via
        // TerrainSynth::setIndyTargetBufferForVoices.
        juce::AudioBuffer<float>* indyTargetBuffer = nullptr;
        // Latest numSamples handed to renderNextBlock — used by the loop
        // crossfade gate to compute the actual loop-wrap rate (loop crossfade
        // becomes audio-rate AM when sliceLen / (pitchRatio_eff) is short, see
        // BEATS v9 gotcha in the warp memory). Updated at the top of every
        // renderNextBlock; reads happen later in pullSourceIntoScratch +
        // renderNextBlock's NONE branch, both of which run synchronously
        // within the same call.
        int      latestBlockSize  = 512;
        // Cross-block tail fade for the warp path. When source exhausts
        // we don't terminate the voice immediately — Signalsmith has up
        // to ~50 ms buffered in its STFT pipeline that needs to drain.
        // Counter starts at warpTailFadeTotal and decrements once per
        // OUTPUT sample. Voice terminates when it hits 0.
        int      warpTailFadeTotal     = 3072;  // ~64 ms @ 48 k — set in prepare
        int      warpTailFadeRemaining = 0;
        bool     warpTailFadeStarted   = false;
    };

    struct SamplerSound : public juce::SynthesiserSound
    {
        bool appliesToNote    (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };
}
