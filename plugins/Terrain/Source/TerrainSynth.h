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
            ChromaticRandom,       // SLICE_MODE = SLICE & SUB = RANDOM: each note picks a random no-repeat slice, pitched by (note - root + slice.pitch)
            Layer                  // SLICE_MODE = SLICE & SUB = LAYER: every note fires ALL slices simultaneously, each pitched by (note - root + slice.pitch)
        };

        Mode          mode             = Mode::Whole;
        int           rootMidiNote     = 60;
        int           activeSliceIndex = 0;
        int           sourceVersionId  = 0;   // from PluginProcessor::getSourceVersionId()
        SliceListPtr  slices;          // immutable snapshot, may be null/empty
        Slice         pitchModeSlice;  // virtual single slice for Mode::Whole; spans whole sample by default
        // HOLD mode — when true, voices ignore note-off and play to natural end.
        // MPC/FL "latch" feel — single key tap fires the chop through. Captured
        // into each VoiceConfig at startNote so toggling holdMode mid-playback
        // doesn't retroactively change in-flight voices.
        bool          holdMode         = false;
    };

    /** Synthesiser that dispatches each MIDI noteOn through slice-aware
     *  resolution before triggering the chosen voice. */
    class TerrainSynth : public juce::Synthesiser
    {
    public:
        /** Pre-rendered warp cache for scan mode. Public so the processor can
         *  call setSource() / setSliceBounds() / prewarm() directly. */
        WarpRenderCache warpCache;

        // ── PITCH-MODE GLOBAL BASELINE ──────────────────────────────────────────
        //  The chop "Pitch mode" panel edits the layer's pitchModeSlice (`base`). Its
        //  tuning + envelope are the GLOBAL baseline that Slice AND Loop modes and every
        //  chop inherit, UNLESS the user overrides a specific slice:
        //   • ENVELOPE (attack/decay/sustain/release/volume) — a per-slice field is an
        //     OVERRIDE when it is >= 0; the negative sentinel (-1) means "inherit the
        //     pitch-mode baseline". Fresh grid/transient chops are born inheriting
        //     (Slice.h), so setting a release in pitch mode holds for every chop; a slice
        //     the user actually edited keeps its own value. attack/release may themselves
        //     inherit the global APVTS param when the baseline is also -1 (SamplerVoice's
        //     existing 3rd-level fallback) — so the chain is slice → pitch-mode → global.
        //   • TUNING is ADDITIVE: the pitch-mode transpose rides on top of the per-slice
        //     offset, so pitching down in pitch mode pitches EVERY slice down while a slice
        //     keeps its own relative detune.
        //  Resolved ONCE here at note-on so the voice only ever sees concrete envelope
        //  values — a leaked inherit-sentinel can never reach (and silence) a voice.
        static void applyPitchModeBaseline (VoiceConfig& vc, const Slice& s, const Slice& base,
                                            float noteMinusRootSemis) noexcept
        {
            vc.attackMs     = (s.attackMs     >= 0.0f) ? s.attackMs     : base.attackMs;
            vc.releaseMs    = (s.releaseMs    >= 0.0f) ? s.releaseMs    : base.releaseMs;
            vc.decayMs      = (s.decayMs      >= 0.0f) ? s.decayMs      : base.decayMs;
            vc.sustainLevel = (s.sustainLevel >= 0.0f) ? s.sustainLevel : base.sustainLevel;
            vc.volume       = (s.volume       >= 0.0f) ? s.volume       : base.volume;
            vc.pitchSemitones = noteMinusRootSemis + base.pitchOffsetSemis + s.pitchOffsetSemis;
        }

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
            // tp101 — NO ALLOCATION. This ran std::make_shared every block for every populated
            // layer: a heap allocation (and a free of the previous context) on the audio thread
            // 4x per block. The writer (processBlock) and every reader (noteOn inside
            // renderNextBlock, auditionSlice from the audition drain) are the SAME audio thread,
            // strictly sequenced, so a plain value member is race-free. The copy only bumps the
            // slice list's refcount.
            context = ctx;
        }

        /** Audition a single slice once, force-one-shot, at the pitch its own key plays (tp101).
         *  Bypasses MIDI dispatch — called directly from the audio thread
         *  (processor's audition queue drain). Channel/note are arbitrary
         *  internal values picked to never collide with real MIDI input. */
        void auditionSlice (const Slice& s, int sliceIndex, int sourceVersionId) noexcept
        {
            VoiceConfig vc;
            vc.startSample    = s.startSample;
            vc.endSample      = s.endSample;
            vc.reverse        = s.reverse;
            vc.forceOneShot   = true;
            vc.sliceIndex     = sliceIndex;
            // Stamp source version so warp+audition hits the cache instead of
            // every preview running the live warp path (or worse, getting a
            // cache key mismatch and falling through). Audit finding #5.
            vc.sourceVersionId = sourceVersionId;
            vc.warpMode       = s.warpMode;
            vc.stretchRatio   = s.stretchRatio;
            // Audition inherits the pitch-mode baseline EXACTLY as a played chop does — envelope,
            // volume AND tuning. 🚨 tp101: this used to keep the pitch at unity, so clicking a chop
            // to hear it dropped the pitch-mode transpose + fine tune (and the chop's own detune)
            // that the keyboard plays: "the chops don't keep my fine tune". The note offset is 0
            // — an audition is the chop at its own key, the same as CHOP mode's key map.
            applyPitchModeBaseline (vc, s, context.pitchModeSlice, 0.0f);
            vc.scanEnabled    = false;  // audition is fire-and-forget; scan would loop forever — override slice setting
            vc.scanRate       = s.scanRate;    // kept for state cleanliness
            vc.scanWindow     = s.scanWindow;
            vc.fxIndependent  = s.fxIndependent;
            vc.fxGrain        = s.fxGrain;
            vc.fxTapeMachine  = s.fxTapeMachine;
            vc.fxSpace        = s.fxSpace;
            vc.fxDelay        = s.fxDelay;
            vc.fxEq           = s.fxEq;
            vc.fxJune         = s.fxJune;
            vc.holdMode       = false;   // audition NEVER latches — explicit for clarity

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
            const SliceContext* ctx = &context;   // tp101 — see setSliceContext (audio thread only)

            // Resolve voice config based on mode.
            VoiceConfig vc;
            bool        triggerOk = true;

            switch (ctx->mode)
            {
                case SliceContext::Mode::Whole:
                {
                    const auto& ps    = ctx->pitchModeSlice;
                    // Wire IN/OUT bounds from pitchModeSlice so the voice actually
                    // plays within the user-defined region.  endSample==0 means
                    // "not yet set" (JS default) so fall back to sentinel -1 (full).
                    vc.startSample    = ps.startSample;
                    vc.endSample      = (ps.endSample > 0) ? ps.endSample : (juce::int64) -1;
                    vc.reverse        = ps.reverse;
                    vc.pitchSemitones = (float) (midiNoteNumber - ctx->rootMidiNote) + ps.pitchOffsetSemis;
                    vc.sliceIndex     = -1;          // sentinel: pitch-mode virtual slice
                    vc.warpMode       = ps.warpMode;
                    vc.stretchRatio   = ps.stretchRatio;
                    vc.attackMs       = ps.attackMs;
                    vc.releaseMs      = ps.releaseMs;
                    vc.decayMs        = ps.decayMs;
                    vc.sustainLevel   = ps.sustainLevel;
                    vc.volume         = ps.volume;
                    vc.scanEnabled    = ps.scanEnabled;
                    vc.scanRate       = ps.scanRate;
                    vc.scanWindow     = ps.scanWindow;
                    // FX independence not supported for pitch mode virtual slice
                    vc.fxIndependent  = false;
                    break;
                }

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
                    vc.sliceIndex     = idx;
                    vc.warpMode       = s.warpMode;
                    vc.stretchRatio   = s.stretchRatio;
                    // Envelope + tuning inherit the pitch-mode baseline unless this slice
                    // overrides them. ChopChromaticLayout maps each key to a DIFFERENT chop
                    // played at native pitch, so the note offset is 0 — only the pitch-mode
                    // global transpose rides on top of the per-slice detune.
                    applyPitchModeBaseline (vc, s, ctx->pitchModeSlice, 0.0f);
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
                    vc.sliceIndex     = sliceIdx;
                    vc.warpMode       = s.warpMode;
                    vc.stretchRatio   = s.stretchRatio;
                    // Envelope + tuning inherit the pitch-mode baseline unless overridden.
                    applyPitchModeBaseline (vc, s, ctx->pitchModeSlice,
                                            (float) (midiNoteNumber - ctx->rootMidiNote));
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
                    vc.sliceIndex     = pick;
                    vc.warpMode       = s.warpMode;
                    vc.stretchRatio   = s.stretchRatio;
                    // Envelope + tuning inherit the pitch-mode baseline unless overridden.
                    applyPitchModeBaseline (vc, s, ctx->pitchModeSlice,
                                            (float) (midiNoteNumber - ctx->rootMidiNote));
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

                case SliceContext::Mode::Layer:
                {
                    // LAYER: fire one voice per slice simultaneously, all transposed
                    // by (midiNote - rootMidiNote). Each voice keeps its own per-slice
                    // properties (scan, warp, ADSR, volume, reverse, pitch offset, FX).
                    if (! ctx->slices || ctx->slices->empty()) { triggerOk = false; break; }

                    const float semisFromRoot = (float) (midiNoteNumber - ctx->rootMidiNote);
                    const juce::ScopedLock sl (lock);

                    for (auto* sound : sounds)
                    {
                        if (! sound->appliesToNote (midiNoteNumber) || ! sound->appliesToChannel (midiChannel))
                            continue;

                        // Stop any voice already playing this note (avoids stale legato voices).
                        // HOLD-latched voices are hard-cut on same-key retrigger so they
                        // don't ignore the soft tail-off and leak the voice pool — see
                        // SamplerVoice::isHoldLatched + audit finding #1.
                        for (auto* v : voices)
                            if (v->getCurrentlyPlayingNote() == midiNoteNumber
                                && v->isPlayingChannel (midiChannel))
                            {
                                bool tailOff = true;
                                if (auto* sv = dynamic_cast<SamplerVoice*> (v))
                                    if (sv->isHoldLatched()) tailOff = false;
                                stopVoice (v, 1.0f, tailOff);
                            }

                        const int numSlices = (int) ctx->slices->size();
                        for (int sliceIdx = 0; sliceIdx < numSlices; ++sliceIdx)
                        {
                            const auto& s = (*ctx->slices)[(size_t) sliceIdx];
                            VoiceConfig lvc;
                            lvc.startSample    = s.startSample;
                            lvc.endSample      = s.endSample;
                            lvc.reverse        = s.reverse;
                            lvc.sliceIndex     = sliceIdx;
                            lvc.warpMode       = s.warpMode;
                            lvc.stretchRatio   = s.stretchRatio;
                            // Envelope + tuning inherit the pitch-mode baseline unless overridden.
                            applyPitchModeBaseline (lvc, s, ctx->pitchModeSlice, semisFromRoot);
                            lvc.scanEnabled    = s.scanEnabled;
                            lvc.scanRate       = s.scanRate;
                            lvc.scanWindow     = s.scanWindow;
                            lvc.forceOneShot   = false;
                            lvc.fxIndependent  = s.fxIndependent;
                            lvc.fxGrain        = s.fxGrain;
                            lvc.fxTapeMachine  = s.fxTapeMachine;
                            lvc.fxSpace        = s.fxSpace;
                            lvc.fxDelay        = s.fxDelay;
                            lvc.fxEq           = s.fxEq;
                            lvc.fxJune         = s.fxJune;
                            lvc.holdMode       = ctx->holdMode;
                            lvc.sourceVersionId = ctx->sourceVersionId;

                            if (auto* voice = findFreeVoice (sound, midiChannel, midiNoteNumber, isNoteStealingEnabled()))
                            {
                                if (auto* sv = dynamic_cast<SamplerVoice*> (voice))
                                    sv->prepareForNoteOn (lvc);
                                startVoice (voice, sound, midiChannel, midiNoteNumber, velocity);
                            }
                        }
                    }
                    return;  // already dispatched + locked — skip the standard dance below
                }
            }

            if (! triggerOk) return;  // out-of-range key in slice mode = silence

            // Stamp the source version so the cache key is fully specified.
            vc.sourceVersionId = ctx->sourceVersionId;
            // HOLD mode applies to all single-voice modes (Whole, Chop, Chromatic,
            // ChromaticRandom). Layer dispatch handles its own lvc.holdMode above.
            vc.holdMode        = ctx->holdMode;

            // Standard synth noteOn dance, but configure the voice between
            // findFreeVoice and startVoice so the voice has its slice info
            // ready when startNote() runs.
            const juce::ScopedLock sl (lock);

            for (auto* sound : sounds)
            {
                if (sound->appliesToNote (midiNoteNumber) && sound->appliesToChannel (midiChannel))
                {
                    // Stop any voice currently playing the same note (matches base behavior).
                    // HOLD-latched voices are hard-cut on same-key retrigger — without
                    // this, HOLD's stopNote ignores allowTailOff=true and the old voice
                    // keeps playing alongside the new one, leaking the 32-voice pool on
                    // repeated key taps (audit finding #1, user repro confirmed).
                    for (auto* v : voices)
                        if (v->getCurrentlyPlayingNote() == midiNoteNumber
                            && v->isPlayingChannel (midiChannel))
                        {
                            bool tailOff = true;
                            if (auto* sv = dynamic_cast<SamplerVoice*> (v))
                                if (sv->isHoldLatched()) tailOff = false;
                            stopVoice (v, 1.0f, tailOff);
                        }

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
        SliceContext context;   // tp101 — a value, written + read on the audio thread only

        // ── ChromaticRandom state ─────────────────────────────────────────
        // No-repeat random slice pick. noteOn runs on the audio thread under
        // the synth lock, so single-threaded access — atomic on lastRandomIdx
        // is just so a UI snapshot could read it later if we want a "last
        // played chop" indicator.
        juce::Random      random;
        std::atomic<int>  lastRandomIdx { -1 };
    };
}
