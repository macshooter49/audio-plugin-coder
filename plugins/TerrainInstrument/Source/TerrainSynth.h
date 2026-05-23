// TerrainSynth.h
// juce::Synthesiser subclass that resolves slice/mode/pitch logic per
// note-on BEFORE handing off to the voice. The processor populates a
// SliceContext each block; this subclass reads it under the synth's lock
// during noteOn.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SamplerVoice.h"
#include "Slice.h"
#include "Warp/WarpRenderCache.h"
#include <atomic>
#include <memory>

namespace tw
{
    /** Slice playback context — set per-block by the processor. Cheap to copy
     *  (just a few ints + a shared_ptr to immutable slice data). */
    struct SliceContext
    {
        enum class Mode : int
        {
            Whole = 0,             // SLICE_MODE = PITCH: play whole sample, pitched by (note - root)
            ChopChromaticLayout,   // SLICE_MODE = SLICE & SUB = CHOP: each ascending key triggers next slice
            ChromaticOneSlice,     // SLICE_MODE = SLICE & SUB = CHROMATIC: active slice plays at (note - root + slice.pitch)
            ChromaticRandom        // SLICE_MODE = SLICE & SUB = RANDOM: each note picks a random no-repeat slice, pitched by (note - root + slice.pitch)
        };

        Mode          mode             = Mode::Whole;
        int           rootMidiNote     = 60;
        int           activeSliceIndex = 0;
        SliceListPtr  slices;          // immutable snapshot, may be null/empty
    };

    /** Synthesiser that dispatches each MIDI noteOn through slice-aware
     *  resolution before triggering the chosen voice. */
    class TerrainSynth : public juce::Synthesiser
    {
    public:
        /** Pre-rendered warp cache for scan mode. Public so the processor can
         *  call setSource() / setSliceBounds() / prewarm() directly. */
        WarpRenderCache warpCache;

        /** Push the indy-FX capture buffer pointer onto every SamplerVoice
         *  so voices whose chop has fxIndependent=true can redirect their
         *  output away from the main synth buffer. Pass nullptr to disable.
         *  Called from PluginProcessor::processBlock each block. */
        void setIndyTargetBufferForVoices (juce::AudioBuffer<float>* buf) noexcept
        {
            for (int i = 0; i < getNumVoices(); ++i)
                if (auto* sv = dynamic_cast<SamplerVoice*> (getVoice (i)))
                    sv->setIndyTargetBuffer (buf);
        }

        /** Set from the processor at the top of processBlock. Cheap copy. */
        void setSliceContext (const SliceContext& ctx) noexcept
        {
            // Stored as shared_ptr<const SliceContext> for atomic swap to
            // avoid taking the synth lock just to update context. The
            // noteOn override snapshots the pointer once per note.
            std::atomic_store (&context, std::make_shared<SliceContext> (ctx));
        }

        /** Audition a single slice once at unity pitch, force-one-shot.
         *  Bypasses MIDI dispatch — called directly from the audio thread
         *  (processor's audition queue drain). Channel/note are arbitrary
         *  internal values picked to never collide with real MIDI input. */
        void auditionSlice (const Slice& s, int sliceIndex) noexcept
        {
            VoiceConfig vc;
            vc.startSample    = s.startSample;
            vc.endSample      = s.endSample;
            vc.reverse        = s.reverse;
            vc.pitchSemitones = 0.0f;
            vc.forceOneShot   = true;
            vc.sliceIndex     = sliceIndex;
            vc.warpMode       = s.warpMode;
            vc.stretchRatio   = s.stretchRatio;
            vc.attackMs       = s.attackMs;
            vc.releaseMs      = s.releaseMs;
            vc.decayMs        = s.decayMs;
            vc.sustainLevel   = s.sustainLevel;
            vc.volume         = s.volume;
            vc.scanEnabled    = s.scanEnabled;
            vc.scanRate       = s.scanRate;
            vc.scanWindow     = s.scanWindow;
            vc.fxIndependent  = s.fxIndependent;
            vc.fxGrain        = s.fxGrain;
            vc.fxTapeMachine  = s.fxTapeMachine;
            vc.fxSpace        = s.fxSpace;
            vc.fxDelay        = s.fxDelay;
            vc.fxEq           = s.fxEq;
            vc.fxJune         = s.fxJune;

            const juce::ScopedLock sl (lock);
            if (sounds.size() == 0) return;
            auto* sound = sounds[0].get();
            if (sound == nullptr) return;
            const int channel = 16;   // audition pseudo-channel
            const int note    = 1;    // arbitrary — only used as a key for findFreeVoice

            if (auto* voice = findFreeVoice (sound, channel, note, true))
            {
                if (auto* sv = dynamic_cast<SamplerVoice*> (voice))
                    sv->prepareForNoteOn (vc);
                startVoice (voice, sound, channel, note, 1.0f);
            }
        }

    protected:
        void noteOn (int midiChannel, int midiNoteNumber, float velocity) override
        {
            auto ctx = std::atomic_load (&context);
            if (! ctx) ctx = std::make_shared<SliceContext>();

            // Resolve voice config based on mode.
            VoiceConfig vc;
            bool        triggerOk = true;

            switch (ctx->mode)
            {
                case SliceContext::Mode::Whole:
                    vc.startSample    = 0;
                    vc.endSample      = -1;        // sentinel: voice uses full buffer
                    vc.reverse        = false;
                    vc.pitchSemitones = (float) (midiNoteNumber - ctx->rootMidiNote);
                    break;

                case SliceContext::Mode::ChopChromaticLayout:
                {
                    if (! ctx->slices || ctx->slices->empty()) { triggerOk = false; break; }
                    // MPC-style: every MIDI key plays a chop, wrapping the
                    // chop list in BOTH directions from ROOT. ROOT = chop 0,
                    // ROOT+1 = chop 1, … ROOT+N = chop 0 again. ROOT-1 = last
                    // chop, ROOT-2 = second-to-last, etc. C++ % is truncated
                    // toward zero for negatives, so we add N and re-mod to
                    // get the mathematical (positive) modulus.
                    const int n = (int) ctx->slices->size();
                    int idx = (midiNoteNumber - ctx->rootMidiNote) % n;
                    if (idx < 0) idx += n;
                    const auto& s = (*ctx->slices)[(size_t) idx];
                    vc.startSample    = s.startSample;
                    vc.endSample      = s.endSample;
                    vc.reverse        = s.reverse;
                    vc.pitchSemitones = s.pitchOffsetSemis;
                    vc.sliceIndex     = idx;
                    vc.warpMode       = s.warpMode;
                    vc.stretchRatio   = s.stretchRatio;
                    vc.attackMs       = s.attackMs;
                    vc.releaseMs      = s.releaseMs;
                    vc.decayMs        = s.decayMs;
                    vc.sustainLevel   = s.sustainLevel;
                    vc.volume         = s.volume;
                    vc.scanEnabled    = s.scanEnabled;
                    vc.scanRate       = s.scanRate;
                    vc.scanWindow     = s.scanWindow;
            vc.fxIndependent  = s.fxIndependent;
            vc.fxGrain        = s.fxGrain;
            vc.fxTapeMachine  = s.fxTapeMachine;
            vc.fxSpace        = s.fxSpace;
            vc.fxDelay        = s.fxDelay;
            vc.fxEq           = s.fxEq;
            vc.fxJune         = s.fxJune;
                    break;
                }

                case SliceContext::Mode::ChromaticOneSlice:
                {
                    if (! ctx->slices || ctx->slices->empty()) { triggerOk = false; break; }
                    const int sliceIdx = juce::jlimit (0, (int) ctx->slices->size() - 1, ctx->activeSliceIndex);
                    const auto& s = (*ctx->slices)[(size_t) sliceIdx];
                    vc.startSample    = s.startSample;
                    vc.endSample      = s.endSample;
                    vc.reverse        = s.reverse;
                    vc.pitchSemitones = (float) (midiNoteNumber - ctx->rootMidiNote) + s.pitchOffsetSemis;
                    vc.sliceIndex     = sliceIdx;
                    vc.warpMode       = s.warpMode;
                    vc.stretchRatio   = s.stretchRatio;
                    vc.attackMs       = s.attackMs;
                    vc.releaseMs      = s.releaseMs;
                    vc.decayMs        = s.decayMs;
                    vc.sustainLevel   = s.sustainLevel;
                    vc.volume         = s.volume;
                    vc.scanEnabled    = s.scanEnabled;
                    vc.scanRate       = s.scanRate;
                    vc.scanWindow     = s.scanWindow;
            vc.fxIndependent  = s.fxIndependent;
            vc.fxGrain        = s.fxGrain;
            vc.fxTapeMachine  = s.fxTapeMachine;
            vc.fxSpace        = s.fxSpace;
            vc.fxDelay        = s.fxDelay;
            vc.fxEq           = s.fxEq;
            vc.fxJune         = s.fxJune;
                    break;
                }

                case SliceContext::Mode::ChromaticRandom:
                {
                    if (! ctx->slices || ctx->slices->empty()) { triggerOk = false; break; }
                    // Pick a random chop that's NOT the same as the previous pick
                    // (re-pick up to 8 times — expected calls < 2 for any reasonable
                    // chop count; guaranteed terminating). When there's only one
                    // chop we just play it; no-repeat is meaningless.
                    const int n = (int) ctx->slices->size();
                    const int last = lastRandomIdx.load();
                    int pick = last;
                    if (n <= 1)
                    {
                        pick = 0;
                    }
                    else
                    {
                        for (int attempt = 0; attempt < 8 && pick == last; ++attempt)
                            pick = random.nextInt (n);
                    }
                    lastRandomIdx.store (pick);
                    const auto& s = (*ctx->slices)[(size_t) pick];
                    vc.startSample    = s.startSample;
                    vc.endSample      = s.endSample;
                    vc.reverse        = s.reverse;
                    vc.pitchSemitones = (float) (midiNoteNumber - ctx->rootMidiNote) + s.pitchOffsetSemis;
                    vc.sliceIndex     = pick;
                    vc.warpMode       = s.warpMode;
                    vc.stretchRatio   = s.stretchRatio;
                    vc.attackMs       = s.attackMs;
                    vc.releaseMs      = s.releaseMs;
                    vc.decayMs        = s.decayMs;
                    vc.sustainLevel   = s.sustainLevel;
                    vc.volume         = s.volume;
                    vc.scanEnabled    = s.scanEnabled;
                    vc.scanRate       = s.scanRate;
                    vc.scanWindow     = s.scanWindow;
            vc.fxIndependent  = s.fxIndependent;
            vc.fxGrain        = s.fxGrain;
            vc.fxTapeMachine  = s.fxTapeMachine;
            vc.fxSpace        = s.fxSpace;
            vc.fxDelay        = s.fxDelay;
            vc.fxEq           = s.fxEq;
            vc.fxJune         = s.fxJune;
                    break;
                }
            }

            if (! triggerOk) return;  // out-of-range key in slice mode = silence

            // Standard synth noteOn dance, but configure the voice between
            // findFreeVoice and startVoice so the voice has its slice info
            // ready when startNote() runs.
            const juce::ScopedLock sl (lock);

            for (auto* sound : sounds)
            {
                if (sound->appliesToNote (midiNoteNumber) && sound->appliesToChannel (midiChannel))
                {
                    // Stop any voice currently playing the same note (matches base behavior).
                    for (auto* v : voices)
                        if (v->getCurrentlyPlayingNote() == midiNoteNumber
                            && v->isPlayingChannel (midiChannel))
                            stopVoice (v, 1.0f, true);

                    if (auto* voice = findFreeVoice (sound, midiChannel, midiNoteNumber, isNoteStealingEnabled()))
                    {
                        if (auto* sv = dynamic_cast<SamplerVoice*> (voice))
                            sv->prepareForNoteOn (vc);

                        startVoice (voice, sound, midiChannel, midiNoteNumber, velocity);
                    }
                }
            }
        }

    private:
        std::shared_ptr<SliceContext> context;

        // ── ChromaticRandom state ─────────────────────────────────────────
        // No-repeat random slice pick. noteOn runs on the audio thread under
        // the synth lock, so single-threaded access — atomic on lastRandomIdx
        // is just so a UI snapshot could read it later if we want a "last
        // played chop" indicator.
        juce::Random      random;
        std::atomic<int>  lastRandomIdx { -1 };
    };
}
