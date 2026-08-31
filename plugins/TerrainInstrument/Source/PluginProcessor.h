#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <unordered_map>
#include <memory>
#include <thread>    // fb481 — the Windows stall beacon
#include <mutex>     // fb528 — the PREPARE LOCK (see prepLock_)
#include <chrono>
#include <map>
#include "GrainEngine.h"
#include "TapeProcessor.h"
#include "TapeLoopProcessor.h"
#include "SpaceReverb.h"
#include "HallReverb.h"        // fb276 — synth FX-rack Hall reverb (8-line Jot/Hadamard FDN)
#include "RoomReverb.h"        // fb281 — synth FX-rack Room reverb (early reflections + short dense FDN tail)
#include "PlateReverb.h"       // fb282 — synth FX-rack Plate reverb (Dattorro figure-8 tank + dispersion)
#include "SpringReverb.h"      // fb284 — synth FX-rack Spring reverb (dispersive allpass boing loop)
#include "DigitalReverb.h"     // fb285 — synth FX-rack Digital reverb (Lexicon 224 cross-coupled tank + random chorus)
#include "VintageReverb.h"     // fb288 — synth FX-rack Vintage reverb (80s digital rack: reduced-SR + bit-crush + drive + gated/reverse shapes)
#include "BasinReverb.h"       // fb289 — synth FX-rack Basin reverb (huge dark ambient wash: Hall FDN retuned + bass-safe crossover + deep motion)
#include "ShimmerReverb.h"     // fb290 — synth FX-rack Shimmer reverb (ethereal octave wash: Hall FDN + granular pitch-shifter in the feedback loop)
#include "ConvolutionReverb.h" // fb291 — synth FX-rack Convolution reverb (true FFT convolution + synth/user IR + Reverse/Attack/Distance/Density bake)
#include "DelayEngine.h"       // fb296 — synth FX-rack Delay (one shared fractional line: Digital/Tape/BBD/Diffuse + ping-pong + HQ interp)
#include "FxChainTopology.h"   // fb351 — who taps which osc + whose output feeds whom (the SERIAL rack)
#include "FxModValue.h"        // fb453 — the rack's per-block modulation math (JUCE-free; the cert calls it too)
#include "DistortionEngine.h"  // fb315 — synth FX-rack Distortion (the 3rd device; 23 modes / 6 families, one shared shell)
#include "MoogDelay.h"
#include "TerrainChorus.h"
#include "ParametricEQ.h"
#include "SpectrumAnalyzer.h"
#include "RollingCaptureBuffer.h"
#include "GranularFxEngine.h"   // fb362 — the FX-rack granular
#include "TapeFxEngine.h"   // fb365 — the FX-rack tape (the three shipped machines + a transport)
#include "FilterFxEngine.h"   // fb377 — the FX-rack filter (hosts one FilterSlot, 94 engines)
#include "TerrainChorusFx.h"    // fb413 — the FX-rack chorus  (kind 6, 8 Types)
#include "TerrainFlangerFx.h"   // fb413 — the FX-rack flanger (kind 7, 6 Types)
#include "TerrainPhaserFx.h"
#include "TerrainEqualizerFx.h"   // fb426 — chain kind 9
#include "TerrainWidenFx.h"       // fb426 — chain kind 10
#include "TerrainCompressFx.h"    // fb426 — chain kind 11
#include "TerrainOttFx.h"
#include "TerrainBodeFx.h"        // fb444 — kind 13, the Bode SSB shifter
#include "TerrainSplitterFx.h"    // fb444 — kind 15, the band Splitter
#include "TerrainUtilityFx.h"     // fb444 — kind 14, the glue strip         // fb426 — chain kind 12    // fb413 — the FX-rack phaser  (kind 8, 9 Types)
#include "ModulationEngine.h"
#include "ParameterIDs.hpp"
#include "SamplerVoice.h"
#include "SampleBuffer.h"
#include "GeodeEngine.h"        // GEODE resynthesis engine (Engine::SPEC) — analyzer + frame store
#include "SampleLoader.h"
#include "Slice.h"
#include "TerrainSynth.h"
#include "FlowArp.h"            // FLOW · ARP engine   (mode 1)
#include "FlowChop.h"           // FLOW · CHOP engine  (mode 2) — audio insert
#include "FlowGlitch.h"         // FLOW · GLITCH engine(mode 3) — audio insert
#include "FlowDrift.h"          // FLOW · DRIFT engine (mode 4) — generative mod source
#include "FlowChain.h"          // fb131 — MODE CHAIN resolver (click order = signal path; pure)
#include <map>                  // fb137 — per-card slots+chain store
#include "ResonatorNode.h"      // ANNULUS resonator — global key-tracked physical-modeling node
#include "SynthLFO.h"          // block-rate global FLOW LFO bank (guarded; likely transitive)
#include "TerrainConstants.h"
#include "LayerState.h"
#include "IndyFxChain.h"
#include "SynthVoice.h"
#include "WavetableBank.h"
#include "SpectralMorph.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <vector>
#include <algorithm>
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
    void setRobinBrain (wc::FlowRobin* b) noexcept { robinBrain_ = b; }   // fb122 — the Wheel

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
        bool stoleAny = false; int stolenStation = -1;                 // fb122 — Steal Stay
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
            stoleAny = true; stolenStation = oldest->robinStation();
            // stopVoice → SynthVoice::stopNote(0, allowTailOff=false) starts the
            // 30ms fade and (since Phase 12) does NOT clear the slot, so the next
            // findFreeVoice call lands on a different idle slot from the pool.
            stopVoice (oldest, 0.0f, false);
            --activeCount;
        }

        robinStage (midiNoteNumber, stoleAny, stolenStation, true);    // fb122 — brain + handover
        juce::Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
    }

    void noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override
    {
        if (robinBrain_ != nullptr) robinBrain_->onNoteOff (midiNoteNumber);   // fb122
        if (monoMode_) { monoNoteOff (midiChannel, midiNoteNumber, velocity, allowTailOff); return; }
        juce::Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff);
    }

    void allNotesOff (int midiChannel, bool allowTailOff) override
    {
        heldCount_ = 0;   // never let panic/transport-stop leave stale stack entries
        if (robinBrain_ != nullptr) robinBrain_->allOff();                     // fb122
        juce::Synthesiser::allNotesOff (midiChannel, allowTailOff);
    }

    // fb122 — stage a ROBIN hit for the next startNote + hand the OLD stations'
    // ringing tails over (Fade/Overlap). Called under the synth lock.
    void robinStage (int note, bool stole, int stolenStation, bool countHeld)
    {
        if (robinBrain_ == nullptr || ! robinBrain_->active()) return;
        robinBrain_->onNoteOn (note, stole, stolenStation, countHeld);
        const wc::RobinHit& h = robinBrain_->peekHit();
        if (h.changed && h.station >= 0 && robinBrain_->fadeEnabled())
        {
            const int   wait = robinBrain_->handoverWaitSamp();
            const float fsec = robinBrain_->handoverFadeSec();
            for (auto* v : voices)
            {
                auto* sv = dynamic_cast<tw::SynthVoice*> (v);
                if (sv == nullptr || ! sv->isVoiceActive() || sv->isKeyDown()) continue;
                if (sv->robinStation() >= 0 && sv->robinStation() != h.station)
                    sv->robinHandover (wait, fsec);
            }
        }
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
        const bool robinRetrig = robinBrain_ != nullptr && robinBrain_->retrigOn();   // fb122
        if (legatoMode_ && wasHeld && active != nullptr && ! robinRetrig)
        {
            if (auto* sound = firstSoundFor (midiChannel, midiNoteNumber))
            {
                if (robinBrain_ != nullptr)                                   // fb122 Legato Keep/New
                {
                    const int st = robinBrain_->onLegatoRetarget (midiNoteNumber);
                    if (st >= 0) active->robinSwapStation (st);               // New: oscs crossfade
                }
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
        robinStage (midiNoteNumber, false, -1, true);                         // fb122
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
            if (legatoMode_ && ! (robinBrain_ != nullptr && robinBrain_->retrigOn()))
            {
                if (auto* sound = firstSoundFor (midiChannel, ret.note))
                {
                    if (robinBrain_ != nullptr)                               // fb122 (return leg)
                    {
                        const int st = robinBrain_->onLegatoRetarget (ret.note, false);
                        if (st >= 0) active->robinSwapStation (st);
                    }
                    active->beginLegatoRetarget();
                    startVoice (active, sound, midiChannel, ret.note, ret.vel);
                    return;
                }
            }
            stopVoice (active, 0.0f, false);                                // fade the released note
            robinStage (ret.note, false, -1, false);                        // fb122: key already counted
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
    wc::FlowRobin* robinBrain_ = nullptr;   // fb122 — owned by the processor
};

//==============================================================================
// NOISE PREVIEW GENERATOR — a stand-alone, self-contained copy of SynthVoice::noiseTick's
// 13-type DSP (White/Pink/Brown/Geiger/Tape×4/Vinyl×2/Space×3), used ONLY by the browser
// headphone preview so the audition sounds BIT-IDENTICAL to the engine (Max's fb50 lesson:
// the 13 types must be genuinely distinct). Verbatim math — do not "improve" it here or it
// drifts from the voice. Keyless, per-block-cheap, its own L/R RNG + filter state.
struct NoisePreviewGen
{
    void setSR (float sr) noexcept { noiseSR_ = sr > 0.0f ? sr : 48000.0f; }
    void setType (int t)  noexcept { noiseType_ = t; }
    void reset () noexcept
    {
        for (int i = 0; i < 7; ++i) { pkL_[i] = 0.0f; pkR_[i] = 0.0f; }
        brL_ = brR_ = geValL_ = geValR_ = 0.0f;
        tpL_ = tpR_ = tpL2_ = tpR2_ = 0.0f;
        spL_ = spL2_ = spR_ = spR2_ = 0.0f;
        humPh_ = windPh_ = windPh2_ = gustL_ = 0.0f;
        rumbL_[0] = rumbL_[1] = rumbR_[0] = rumbR_[1] = 0.0f;
        noiseRngL_ = 0x9E3779B9u; noiseRngR_ = 0x85EBCA6Bu;
    }
    static inline float noiseWhite (std::uint32_t& s) noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) ((std::int32_t) s) * (1.0f / 2147483648.0f);
    }
    static inline float noiseSine (float ph) noexcept
    {
        const float x = 2.0f * ph - 1.0f;
        const float q = 4.0f * x * (1.0f - std::fabs (x));
        return q * (0.775f + 0.225f * std::fabs (q));
    }
    inline void tick (float& oL, float& oR) noexcept
    {
        const float wl = noiseWhite (noiseRngL_), wr = noiseWhite (noiseRngR_);
        switch (noiseType_)
        {
            case 1: {   // Pink — Paul Kellet economy filter
                auto pk = [] (float w, float* b) noexcept {
                    b[0] = 0.99886f*b[0] + w*0.0555179f; b[1] = 0.99332f*b[1] + w*0.0750759f;
                    b[2] = 0.96900f*b[2] + w*0.1538520f; b[3] = 0.86650f*b[3] + w*0.3104856f;
                    b[4] = 0.55000f*b[4] + w*0.5329522f; b[5] = -0.7616f*b[5] - w*0.0168980f;
                    const float o = b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+w*0.5362f;
                    b[6] = w*0.115926f; return o * 0.11f;
                };
                oL = pk (wl, pkL_); oR = pk (wr, pkR_); break;
            }
            case 2:     // Brown — leaky integrator
                brL_ = (brL_ + 0.02f*wl) * 0.996f; brR_ = (brR_ + 0.02f*wr) * 0.996f;
                oL = brL_ * 3.5f; oR = brR_ * 3.5f; break;
            case 3: {   // Geiger — dry Poisson clicks
                auto click = [] (float w, float w2, float& env) noexcept {
                    if (w > 0.9993f || w < -0.9993f)
                        env = (0.6f + 0.4f*std::fabs (w2)) * (w > 0.0f ? 1.0f : -1.0f);
                    const float o = env; env *= 0.80f; return o;
                };
                oL = click (wl, wr, geValL_); oR = click (wr, wl, geValR_); break;
            }
            case 4: {   // Tape Hiss
                tpL_  += 0.21f*(wl - tpL_);   tpR_  += 0.21f*(wr - tpR_);
                const float hpL = wl - tpL_,  hpR = wr - tpR_;
                tpL2_ += 0.55f*(hpL - tpL2_); tpR2_ += 0.55f*(hpR - tpR2_);
                oL = tpL2_ * 2.0f; oR = tpR2_ * 2.0f; break;
            }
            case 5: {   // Tape Hum
                humPh_ += 60.0f / noiseSR_; if (humPh_ >= 1.0f) humPh_ -= 1.0f;
                float h2 = humPh_*2.0f; if (h2 >= 1.0f) h2 -= 1.0f;
                float h3 = humPh_*3.0f; while (h3 >= 1.0f) h3 -= 1.0f;
                const float hum = noiseSine (humPh_)*0.70f + noiseSine (h2)*0.22f + noiseSine (h3)*0.10f;
                tpL_ += 0.25f*(wl - tpL_); tpR_ += 0.25f*(wr - tpR_);
                oL = hum*0.82f + (wl - tpL_)*0.12f;
                oR = hum*0.82f + (wr - tpR_)*0.12f; break;
            }
            case 6: {   // Tape Air
                tpL_ += 0.38f*(wl - tpL_); tpR_ += 0.38f*(wr - tpR_);
                const float airL = wl - tpL_, airR = wr - tpR_;
                windPh_ += 0.25f / noiseSR_; if (windPh_ >= 1.0f) windPh_ -= 1.0f;
                const float breath = 0.72f + 0.28f * noiseSine (windPh_);
                oL = airL * 1.3f * breath; oR = airR * 1.3f * breath; break;
            }
            case 7: {   // Tape Crackle
                tpL_ += 0.22f*(wl - tpL_); tpR_ += 0.22f*(wr - tpR_);
                const float hissL = wl - tpL_, hissR = wr - tpR_;
                auto pop = [] (float w, float w2, float& env) noexcept {
                    if      (w >  0.9995f)  env =  (0.7f + 0.3f*std::fabs (w2));
                    else if (w < -0.99985f) env = -(0.5f + 0.3f*std::fabs (w2));
                    const float o = env; env *= 0.86f; return o;
                };
                oL = pop (wl, wr, geValL_) + hissL*0.28f;
                oR = pop (wr, wl, geValR_) + hissR*0.28f; break;
            }
            case 8: case 9: {   // Vinyl
                const bool dirty = (noiseType_ == 9);
                rumbL_[0] += 0.006f*(wl - rumbL_[0]); rumbL_[1] += 0.006f*(rumbL_[0] - rumbL_[1]);
                rumbR_[0] += 0.006f*(wr - rumbR_[0]); rumbR_[1] += 0.006f*(rumbR_[0] - rumbR_[1]);
                const float rmb = dirty ? 24.0f : 18.0f;
                auto pk = [] (float w, float* b) noexcept {
                    b[0]=0.99886f*b[0]+w*0.0555179f; b[1]=0.99332f*b[1]+w*0.0750759f;
                    b[2]=0.96900f*b[2]+w*0.1538520f; b[3]=0.86650f*b[3]+w*0.3104856f;
                    b[4]=0.55000f*b[4]+w*0.5329522f; b[5]=-0.7616f*b[5]-w*0.0168980f;
                    const float o=b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+w*0.5362f; b[6]=w*0.115926f; return o*0.11f;
                };
                const float surfL = pk (wl, pkL_) * (dirty ? 0.50f : 0.22f);
                const float surfR = pk (wr, pkR_) * (dirty ? 0.50f : 0.22f);
                const float thr = dirty ? 0.9975f : 0.9993f;
                auto crk = [thr] (float w, float w2, float& env) noexcept {
                    if (w > thr || w < -thr) env = (0.55f + 0.45f*std::fabs (w2)) * (w > 0.0f ? 1.0f : -1.0f);
                    const float o = env; env *= 0.845f; return o;
                };
                const float ckL = crk (wl, wr, geValL_) * (dirty ? 0.9f : 0.7f);
                const float ckR = crk (wr, wl, geValR_) * (dirty ? 0.9f : 0.7f);
                oL = rumbL_[1]*rmb + surfL + ckL;
                oR = rumbR_[1]*rmb + surfR + ckR; break;
            }
            case 10: case 11: case 12: {   // Space — Chamberlin SVF resonant band-pass
                const float w0 = 6.2831853f / noiseSR_;
                float fc, qd, amp;
                if (noiseType_ == 10) {
                    windPh2_ += 0.07f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                    fc = 1100.0f + 500.0f * noiseSine (windPh2_); qd = 0.90f; amp = 2.6f;
                } else if (noiseType_ == 11) {
                    windPh2_ += 0.05f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                    fc = 3200.0f + 400.0f * noiseSine (windPh2_); qd = 0.28f; amp = 1.8f;
                } else {
                    windPh_  += 0.13f / noiseSR_; if (windPh_  >= 1.0f) windPh_  -= 1.0f;
                    windPh2_ += 0.09f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                    fc = 700.0f + 450.0f * noiseSine (windPh2_); qd = 0.60f;
                    gustL_ += 0.00035f*(wl - gustL_);
                    amp = 2.6f * juce::jlimit (0.0f, 1.4f, 0.45f + 0.55f*noiseSine (windPh_) + 2.5f*gustL_);
                }
                float f = juce::jlimit (0.0f, 0.9f, fc * w0);
                spL_ += f * spL2_; const float hpL = wl - spL_ - qd*spL2_; spL2_ += f * hpL;
                spR_ += f * spR2_; const float hpR = wr - spR_ - qd*spR2_; spR2_ += f * hpR;
                oL = spL2_ * amp; oR = spR2_ * amp; break;
            }
            default: oL = wl; oR = wr; break;   // White (0)
        }
    }
    int   noiseType_ = 0;
    float noiseSR_   = 48000.0f;
    std::uint32_t noiseRngL_ = 0x9E3779B9u, noiseRngR_ = 0x85EBCA6Bu;
    float pkL_[7] = { 0 }, pkR_[7] = { 0 }, brL_ = 0.0f, brR_ = 0.0f, geValL_ = 0.0f, geValR_ = 0.0f;
    float tpL_ = 0.0f, tpR_ = 0.0f, tpL2_ = 0.0f, tpR2_ = 0.0f;
    float spL_ = 0.0f, spL2_ = 0.0f, spR_ = 0.0f, spR2_ = 0.0f;
    float humPh_ = 0.0f, windPh_ = 0.0f, windPh2_ = 0.0f, gustL_ = 0.0f;
    float rumbL_[2] = { 0.0f, 0.0f }, rumbR_[2] = { 0.0f, 0.0f };
};

//==============================================================================
// fb158 — card-window forensic log gate (cardwin.log in ~/Library/WavesCrate). The
// fb82-90 pop-out saga and the fb149-151 drag hunt are closed; a shipped plugin must
// not grow a log on every card event. Flip to 1 to hunt card lifecycle bugs again —
// single-sourced here so BOTH loggers (processor + editor) obey the same switch.
#define TERRAIN_CARDWIN_LOG 0

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

    // fb522 · LANE P — the version-3 blob migration. Runs inside setStateInformation, on the
    // ValueTree, BEFORE apvts.replaceState() and BEFORE synModJson is handed to
    // setSynthModMatrix(). No-op for a blob that already carries version >= 3.
    // 🚨 It must NOT rewrite the "version" property: a V1 blob has none at all, and the V1/V2
    // branch further down re-reads that property to pick loadV1State vs loadV2State.
    static void migrateBlobToVersion3 (juce::ValueTree& state);

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // ── fb83: popped-out FLOW card windows live on the PROCESSOR ──
    // FL Studio auto-closes an unfocused plugin editor window, which used to destroy
    // any popped card the instant it (or anything else) took focus. Owned here, a card
    // survives the editor closing and only dies with the plugin instance (dtor clears
    // on the message thread) or its own ✕/dock. Type-erased as juce::Component so the
    // concrete window class stays private to PluginEditor.cpp.
    std::map<juce::String, std::unique_ptr<juce::Component>> cardWindows_;
    // fb516 -- THE KEEP-ALIVE UI CORE (same idiom): the whole main UI survives editor close,
    // parked in a hidden holder window; the shell adopts it on open. Type-erased so the
    // concrete TerrainUiCore stays in PluginEditor.h/cpp. See the protocol laws there.
    std::unique_ptr<juce::Component> uiCore_;
    std::unique_ptr<juce::Component> uiCoreHolder_;   // (fb516e: superseded by the raw HWND below; kept null)
    void* uiCoreRawHolder_ = nullptr;   // fb516e -- raw Win32 hidden park window (TerrainUiPark.cpp)
    juce::uint64 parkedGen_ = 0;
    juce::Component* ensureUiCoreComponent();       // fb516 -- create-or-adopt (implemented in PluginEditor.cpp)
    void parkUiCore();                              // fb516 -- shell dtor hands the core here
    void destroyParkedUiIfGen (juce::uint64 gen);   // fb516 -- deferred LRU eviction target
    void releaseUiCoreForShutdown();                // fb516 -- dtor / escape-hatch teardown
    void destroyUiCoreOnly();                       // fb520 -- kill a dead core, keep the park window
    void rebuildUiCoreNow();                        // fb520 -- wd9 escalation target (message thread)
    // fb236 — the cross-window LIVE STROKE lane: whichever surface is being drawn on posts
    // the active shape here; the 60Hz editor timer relays it to the OTHER window's page.
    juce::CriticalSection         lfoLiveLock_;
    juce::String                  lfoLiveJson_;
    std::atomic<juce::int64>      lfoLiveSeq_ { 0 };
    // fb145 — mod-drag blackboard: the main window streams LFO-chip drags here (screen
    // coords); popped card windows poll it each frame. Message thread only.
    int modDragLfo_ = 0, modDragPhase_ = 2;            // phase: 0 move · 1 drop · 2 idle
    int modDragSrc_ = 0;                                // fb149 — 0 = main window · 1 = the LFO palette
    int modDragIdleTicks_ = 0;                          // fb150 — drop phase auto-idles after ~200ms
    float modDragX_ = 0.f, modDragY_ = 0.f;
    // fb163 — LIVE FILTER CURVE: loudest voice's post-mod cutoff/res per slot (Hz, 0..1);
    // hz = -1 while idle so the display falls back to the base params.
    std::atomic<float> fltVisHz1_ { -1.f }, fltVisRes1_ { 0.f }, fltVisHz2_ { -1.f }, fltVisRes2_ { 0.f };
    // fb457 — the loudest voice's EFFECTIVE wavetable frame per osc. -1 = nothing sounding, and
    // the UI then falls back to the knob, exactly as it always did (so an idle rack is unchanged).
    std::atomic<float> wtFrameVis_[4] { { -1.f }, { -1.f }, { -1.f }, { -1.f } };
    // (this section is already public: — line 552. An access specifier added here would have
    //  closed it and made every member below private; it did, and the editor stopped compiling.)
    float wtFrameVis (int o) const noexcept
    { return (o >= 0 && o < 4) ? wtFrameVis_[o].load (std::memory_order_relaxed) : -1.f; }

    // fb458 — the SHAPING the waterfall must draw on top of the frame. wtDispLive_ = 0 means no
    // voice is sounding, and every consumer then falls back to the base parameters, so an idle
    // panel and a hand-turned knob look exactly as they did.
    std::atomic<int>   wtDispLive_[4] { { 0 }, { 0 }, { 0 }, { 0 } };
    std::atomic<float> wtWarpAmtVis_[4] {}, wtWarp2AmtVis_[4] {}, wtFoldAmtVis_[4] {}, wtBlurVis_[4] {};
    std::atomic<int>   wtWarpModeVis_[4] {}, wtWarp2ModeVis_[4] {}, wtFoldShapeVis_[4] {};
    tw::SynthVoice::WtDisp wtDispEffective (int o) const noexcept;
    // fb546 — WARP EXTENSION CARD. The curve for one warp slot, computed by the SHIPPED statics
    // (applyPhaseWarp / applyAmpWarp / warpFiltCoef + warpFiltTick) exactly as getOscWavetableJson
    // does for the waterfall — never a JS reimplementation of thirty-odd modes (the fb458 law).
    juce::String getWarpCurveJson (int osc, int slot);

    // ═══ fb550 — DRAW YOUR OWN WARP SHAPE (OVERPASS ONE item 6C) ══════════════════════════════
    //  8 curves, indexed [osc * 2 + slot]. DOUBLE-BUFFERED: the message thread fills the spare
    //  and then publishes it with one atomic pointer store, so the audio thread never observes a
    //  half-redrawn curve and neither side ever locks or allocates.
    //  A curve that is (within 1e-4) the identity publishes NULLPTR, which is what makes mode 37
    //  bit-exact dry until something is actually drawn.
    tw::SynthVoice::DrawCurve            drawCurve_[8][2];
    // fb554 — THE MOD-CONNECTION CURVES. Double-buffered and published by pointer, the same shape
    //  as drawCurve_ above and for the same reason: the message thread rewrites the whole set on
    //  every matrix edit while the audio thread is reading it. Publishing a POINTER TO THE WHOLE
    //  SET (rather than per-curve) means a block can never see one route's new curve next to
    //  another's old one — the set is atomic, not the curve.
    wc::ModCurveSet                      modCurveSet_[2];
    std::atomic<const wc::ModCurveSet*>  modCurves_ { nullptr };
    int                                  modCurveSpare_ = 0;
    int                                  drawSpare_[8] = { 0,0,0,0,0,0,0,0 };
    tw::SynthVoice::DrawSlot             drawTable_[8];      // what every voice reads
    void         setWarpDrawCurve (int osc, int slot, const juce::String& csv);
    juce::String getWarpDrawCurveCsv (int osc, int slot) const;
    // fb459 — what the SPECTRAL morph is doing right now. The amount is the EFFECTIVE one the
    // audio thread publishes each block (fb252/fb76: base + LFO/env, quantised to 1/128 only when
    // routed), which is the same number rebuildMorphIfNeeded builds from — so the picture the
    // waterfall re-bakes is the morph the ear is hearing, not the knob.
    void spectralDisplay (int o, float& amtOut, int& typeOut, float& loOut, float& hiOut) const noexcept;
    juce::uint32 modDragSeq_ = 0;
    static bool physicalLeftButtonDown();               // fb151 — window-server button truth (see PluginProcessor.cpp)
    void adoptCardWindow (const juce::String& id, std::unique_ptr<juce::Component> w);
    void closeCardWindow (const juce::String& id);   // erase + notify the active editor (if any)
    void dockCardWindow  (const juce::String& id);   // erase + reopen in-plugin via the active editor

    /** GEODE viz — the live analysed frame store for one osc (nullptr if that osc isn't on
     *  Engine::SPEC or has no store). Read by the editor's display engine for the waterfall
     *  visualizer. Safe on the message thread: rebuilds run on the same (message) thread. */
    const tw::GeodeFrameStore* geodeLiveStore (int osc) const noexcept
    { return (osc >= 0 && osc < 4) ? geodeSlot_[(size_t) osc].live.load (std::memory_order_acquire) : nullptr; }
    /** HARM viz — the latest gathered knob snapshot for one osc. The editor's DISPLAY
     *  HarmonicEngine instances rebuild their banks from this on the message-thread tick
     *  (plain struct copy of block-rate floats — a torn read costs one cosmetic frame). */
    bool  harmVizLive (int osc) const noexcept { return harmVizLive_[(size_t) juce::jlimit (0, 3, osc)].load (std::memory_order_relaxed) != 0; }
    float harmVizBin (int osc, int b) const noexcept { return harmVizBins_[(size_t) juce::jlimit (0, 3, osc)][(size_t) juce::jlimit (0, 95, b)].load (std::memory_order_relaxed); }
    const tw::HarmParams& harmDisplayParams (int osc) const noexcept
    { return harmDisplayParams_[(size_t) juce::jlimit (0, 3, osc)]; }
    /** Returns the sample buffer for the currently-editing layer.
     *  Task 5: routes through layers[editingLayer] instead of the old singleton. */
    tw::SampleBuffer& getSampleBuffer() noexcept { return layers[(size_t) editingLayer.load()].sampleBuffer; }
    tw::SampleLoader& getSampleLoader() noexcept { return sampleLoader; }

    // PEROSC-BUFFERS — per-OSC Sample oscillator buffers (synth-side; A/B/C/D independent).
    tw::SampleBuffer& getOscSampleBuffer (int idx) noexcept { return oscSampleBuffers_[(size_t) juce::jlimit (0, 3, idx)]; }
    // NOISE IMPORT (P5) — one shared looping-sample source for the Noise module (user drop or factory sample).
    tw::SampleBuffer& getNoiseSampleBuffer () noexcept { return noiseSampleBuffer_; }
    // NOISE IMPORT (P5c) — persisted noise-sample selection descriptor (JSON): factory path or embedded user audio.
    void         setNoiseSampleSel (const juce::String& j) { noiseSampleSelJson_ = j; }
    juce::String getNoiseSampleSel () const                { return noiseSampleSelJson_; }
    // fb66 — NOISE right-click engine bridges: waveform follower + peaks + persisted viz choice.
    float        getNoiseFollow () const noexcept { return noiseVizPos_.load (std::memory_order_relaxed); }   // representative follower 0..1 (-1 = none)
    juce::String getNoiseWavePeaksJson ();                        // min/max envelope of the loaded noise sample ("" if none)
    void         setNoiseVizMode (int m) noexcept { noiseVizMode_ = juce::jlimit (1, 2, m); }   // 1 particle · 2 waveform (UI state, persisted)
    int          getNoiseVizMode () const noexcept { return noiseVizMode_; }
    void startNoiseAudition () noexcept { noiseAuditionReq_.fetch_add (1, std::memory_order_relaxed); }   // headphone preview (browser)
    void startWavetableAudition (int osc) noexcept { wtAudReqOsc_.store (juce::jlimit (0, 3, osc), std::memory_order_relaxed); wtAuditionReq_.fetch_add (1, std::memory_order_relaxed); }   // WT headphone preview
    void startOscSampleAudition (int osc) noexcept { sampAudReqOsc_.store (juce::jlimit (0, 3, osc), std::memory_order_relaxed); sampAuditionReq_.fetch_add (1, std::memory_order_relaxed); }   // fb74 — SAMPLE browser headphone preview (plays the osc's current sample once)
    void stopPreview () noexcept { previewStop_.store (true, std::memory_order_relaxed); }   // kill the active one-shot audition immediately (Max: double-click/close stops it)

    // ── IMPORTS REGISTRY (fb60, 3-way fb74) — reference-in-place user imports: single FILES → an "Imports"
    //    category, whole FOLDERS → their own named category (scanned live for count). Paths only, no audio
    //    copied. kind: 0 = noise · 1 = wavetable · 2 = sample (Sample/Granular/Resynth share ONE registry).
    //    Msg-thread only. Persisted best-effort to a small JSON in app-data (survives where writable).
    void         addImportPath (int kind, const juce::String& path);   // decides file-vs-folder via File::isDirectory
    void         removeImportPath (int kind, const juce::String& path);// un-reference a user folder OR single import (NEVER touches the file on disk)
    juce::String getImportsJson (int kind);                            // {files:[{name,path}], folders:[{name,path,count,items:[…]}]}
    void         loadImportsRegistry ();
    void         saveImportsRegistry (int kind);
    tw::SampleLoader& getOscSampleLoader (int idx) noexcept { return oscSampleLoaders_[(size_t) juce::jlimit (0, 3, idx)]; }
    juce::String&     oscSourcePath      (int idx) noexcept { return oscSourcePaths_  [(size_t) juce::jlimit (0, 3, idx)]; }
    /** BLEND — persisted source-pair paths (which: 0 = A, 1 = B). Empty = no live blend.
     *  The editor reloads both files on reopen so the blend knobs stay LIVE across sessions. */
    juce::String&     blendSrcPath (int idx, int which) noexcept { return blendSrcPaths_[(size_t) juce::jlimit (0, 3, idx)][(size_t) (which & 1)]; }
    void setCachedOscPayload (const juce::String& json, int idx)
    { if (idx < 0 || idx > 3) return; juce::ScopedLock sl (samplePayloadLock); cachedOscPayloads_[(size_t) idx] = json; }
    // Wavetable EXTENDER (message thread) — build/clear an imported table for osc 0..3.
    void importAudioAsWavetable (int osc, const float* pcm, int numSamples);
    void clearImportedWavetable (int osc);
    void setImportFrames (int osc, int frames);   // re-slice the stored import at a new frame count (resolution)
    void setImportName (int osc, const juce::String& name);   // display/persist name for an import
    juce::String getImportStateJson();                        // {a:{active,name},...} — UI restores this on reopen
    void setWaterfallView (int osc, bool on);                 // remember the 3D-waterfall toggle per osc
    juce::String getWaterfallViewJson();                      // {a:bool,...} — UI restores the view on reopen
    // Wavetable EXTENDER viz — compact JSON of the osc's LIVE table (imported or factory) for the
    // 3D waterfall: { n:<displayFrames>, p:<pointsPerFrame>, nf:<realFrames>, d:[ n*p samples ] }.
    juce::String getOscWavetableJson (int osc);
    juce::String getOscLfoWaveJson (int osc);   // fb248 — exact current frame for WT→LFO

    juce::String getCachedOscPayload (int idx) const
    { if (idx < 0 || idx > 3) return {}; juce::ScopedLock sl (samplePayloadLock); return cachedOscPayloads_[(size_t) idx]; }

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

    // Synth mod-matrix bridge (JS setSynthMod native fn → parsed route list).
    void              setSynthModMatrix (const juce::String& json);
    juce::String      getSynthModMatrix() const { return synModJson; }

    // fb177 — dynamic envelopes (Env 6..32): shapes arrive from the UI as ONE JSON
    // blob (relay-ceiling law: no APVTS params for dynamic slots), persist in state,
    // broadcast to voices per block via a version-bumped shared copy (arp-lanes
    // pattern). Natural units: ms / 0..1 sustain / curve -1..+1.
    struct DynEnvShape { float dl=0, a=5, h=0, d=200, s=0.7f, r=300, ca=0, cd=0, cr=0; bool loop=false; };
    static constexpr int kMaxDynEnvs = 27;
    void              setSynthDynEnvs (const juce::String& json);
    juce::String      getSynthDynEnvsJson() const;
    void              setSynthLfoShapes (const juce::String& json);   // LFO ARC L1 — the shaper blob
    juce::String      getSynthLfoShapesJson() const;
    void              setDistortionCurves (const juce::String& json);  // fb328 — curve-card blob (banks + bars)
    juce::String      getDistortionCurvesJson() const;                 // fb328 — card boot + cross-window pull
    juce::String      getDistortionCurveVizJson();                     // fb328 — live core feed (curve+occ+bloom)
    juce::String      getFilterVizJson();                               // fb382 — the 60 Hz filter card feed
    juce::String      getGranularVizJson();                            // fb362 — granular cards, one entry per instance
    juce::String      getTapeVizJson();                                // fb365 — tape cards, one entry per instance
    juce::String      getFx3VizJson();                                 // fb413 — chorus + flanger + phaser, one payload
    juce::String      getFx4VizJson();                                 // fb437 — equalizer + widen + compress + multiband, one payload
    juce::String      getFxModEffJson();                               // fb457 — OVERPASS 1: every ROUTED rack dial's EFFECTIVE value
    void              setDistortionTableSrc (int osc);                 // fb339 — Table source: -1=generated, 0..3=Osc A-D
    int               getDistortionTableSrc() const noexcept { return dstTableSrc_; }

    // ── FLOW · ARP extension card (fb105) — lane pattern + live playhead feed ──
    void              setArpLanesFromJson (const juce::String& json);   // message thread: parse -> swap under lock
    // fb137 — CARD STATE (slots + chain JSON per card): both surfaces share ONE truth so
    // pop-out / dock-back / editor reopen never lose the chain (the arp-lanes precedent).
    void setCardStateJson (const juce::String& card, const juce::String& json)
    { const juce::ScopedLock sl (cardStateLock_); cardStates_[card] = json; }
    juce::String getCardStateJson (const juce::String& card) const
    { const juce::ScopedLock sl (cardStateLock_); const auto it = cardStates_.find (card);
      return it != cardStates_.end() ? it->second : juce::String(); }
    mutable juce::CriticalSection cardStateLock_;
    std::map<juce::String, juce::String> cardStates_;
    std::atomic<int> flowPlayingViz_ { 0 };   // fb137 — transport state for the feeds ("pl")
    juce::String      getArpLanesJson() const;                          // for JS restore + state save
    float             getReverbBloom() const noexcept { return hallBloomViz_.load (std::memory_order_relaxed); }  // fb280 — wet bloom 0..1 for the FX-rack core viz
    float             getDelayBloom()  const noexcept { return dlyBloomViz_.load  (std::memory_order_relaxed); }  // fb296 — delay wet level 0..1 for the delay core viz
    // fb352 — the SAME reading for pooled REVERB instance e (0 = Reverb 2 … 4 = Reverb 6).
    float             getReverbBloomPool (int e) const noexcept
    { return ((unsigned) e < (unsigned) kFxExtra) ? poolRvbBloomViz_[(size_t) e].load (std::memory_order_relaxed) : 0.0f; }
    // fb350 — the SAME reading for pooled delay instance e (0 = Delay 2 … 4 = Delay 6).
    float             getDelayBloomPool (int e) const noexcept
    { return ((unsigned) e < (unsigned) kFxExtra) ? poolDlyBloomViz_[(size_t) e].load (std::memory_order_relaxed) : 0.0f; }
    // fb292 — Convolution USER IR loading: decode IN-MEMORY → SR-correct → trim leading silence → convolutionReverb.setUserIR.
    bool          loadConvIRFromMemory (const void* data, size_t size, const juce::String& name, int inst = 1);  // drag-drop (base64 → bytes) — no disk
    bool          loadConvIRFromFile   (const juce::File& f, int inst = 1);                         // "Load IR…" file chooser
    void          clearConvUserIR      (int inst = 1);                                              // revert to the synthetic factory Space
    int           getConvIREnvelope    (float* out, int n, int inst = 1)                            // baked-IR viz envelope; returns baked len
    { auto* e = convEngineFor (inst); return e != nullptr ? e->irEnvelope (out, n) : 0; }
    juce::String  getConvIRName        (int inst = 1) const { return convIRName_[(size_t) convSlot (inst)]; }   // "" ⇒ synthetic Space
    bool          isConvIRUser         (int inst = 1) const { return convIRUser_[(size_t) convSlot (inst)]; }
    juce::String  getConvIRRawJson     (int inst = 1) const;                  // fb311 — {name,n,L,R} (base64 float) for embedding in a preset
    void          setConvIRRawFromJson (const juce::String& json, int inst = 1);   // fb311 — restore the EXACT one-shot from a preset
    juce::String      getArpFeedJson() const;                           // playhead/fire/wave snapshot (rAF-polled)
    juce::String      getChopFeedJson() const;                          // fb106: Ribbon playhead/slice/wet snapshot
    void              requestChopWipe() noexcept { chopWipeReq_.store (true); }   // Wipe button → audio thread
    juce::String      getGliFeedJson() const;                           // fb115: Monitor playhead/fire/levels snapshot
    juce::String      getRbnFeedJson() const;                           // fb122: Wheel now/next/notes snapshot
    void              requestGliRoll() noexcept { gliRollReq_.store (true); }     // Roll button → audio thread (quantized)

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
    // Live AMP envelope output [0,1] of the most-active synth voice, for the UI
    // envelope follower (playhead dot). Written each audio block, read by the editor timer.
    std::atomic<float> ampEnvVis { 0.f };
    // fb552 — the followers' GLOBAL feed. A mod source lives on TWO paths (fb259's law): the
    //  per-voice one in SynthVoice, and this one for destinations that are not per-voice (the FX
    //  rack, the macros). Published from the most-active voice, exactly like velVis_ below.
    //  ⚠️ Rest value is 0, not −1: a follower at rest genuinely IS zero, so there is no sentinel.
    std::atomic<float> followVis_[wc::kNumFollowers] { {0.f}, {0.f}, {0.f}, {0.f}, {0.f} };
    std::atomic<float> velVis_ { -1.f };   // fb262 — most-active voice velocity (−1 = silent) for the live velocity streak
    // fb264 — master peak-limiter state (audio-thread only; coeffs set in prepareToPlay). Stereo-linked
    // gain-reduction before the safety clip so dense chords stay loud without squaring into hard-clip buzz.
    float limEnv_ = 0.0f, limGain_ = 1.0f, limAtkCoef_ = 0.0f, limRelCoef_ = 0.0f;
    // fb189 — LIVING UNDERLINE feed: most-active voice's env outputs (0..1, −1 = silent)
    // + the global LFO bank (bipolar). 32 slots = 5 legacy + 27 dynamic.
    std::atomic<float> modVizEnv_[32] {};
    std::atomic<float> modVizLfo_[wc::NUM_LFOS] {};
    std::atomic<float> modVizLfoPh_[wc::NUM_LFOS] {};   // fb217 — the LFO bank's REAL phases: the pane follower rides DSP truth, not a free-running JS clock
    std::atomic<float> modVizLfoVX_[wc::NUM_LFOS] {};   // fb239 — free-run 2D trajectory (chaos/dune): the swirl on screen IS the audible swirl
    std::atomic<float> modVizLfoVY_[wc::NUM_LFOS] {};
    float modVizEnv (int k) const noexcept { return (k >= 0 && k < 32) ? modVizEnv_[k].load (std::memory_order_relaxed) : -1.f; }
    float modVizLfo (int k) const noexcept { return (k >= 0 && k < wc::NUM_LFOS) ? modVizLfo_[k].load (std::memory_order_relaxed) : 0.f; }
    float modVizVel () const noexcept { return velVis_.load (std::memory_order_relaxed); }
    // fb552 — a follower's live level for the UI. Max's hard rule is that anything audible must be
    //  visible, and a route whose underline never moves reads as a dead route.
    float modVizFollow (int k) const noexcept
    { return (k >= 0 && k < wc::kNumFollowers) ? followVis_[k].load (std::memory_order_relaxed) : 0.0f; }   // fb262 — live velocity for the streak
    float modVizLfoPh (int k) const noexcept { return (k >= 0 && k < wc::NUM_LFOS) ? modVizLfoPh_[k].load (std::memory_order_relaxed) : 0.f; }
    float modVizLfoVX (int k) const noexcept { return (k >= 0 && k < wc::NUM_LFOS) ? modVizLfoVX_[k].load (std::memory_order_relaxed) : 0.f; }
    float modVizLfoVY (int k) const noexcept { return (k >= 0 && k < wc::NUM_LFOS) ? modVizLfoVY_[k].load (std::memory_order_relaxed) : 0.f; }
    std::atomic<float> noiseVizLevel_ { 0.f };   // NOISE viz — env level while noise is sounding (0 when off/silent)
    std::atomic<float> noiseVizPos_  { -1.f };   // fb66 — NOISE waveform follower position 0..1 (representative), -1 = none
    std::atomic<float> noiseFreeNorm_{ 0.f  };   // fb66 — NOISE Free-mode global tape position 0..1
    // HARM-VIZ — live partial bins from the most-active voice (audio thread writes; editor
    // reads on its tick — cosmetic, tear-tolerant)
    std::atomic<float> harmVizBins_[4][96] {};
    std::atomic<int>   harmVizLive_[4] {};
    std::atomic<float> synthLfo1Vis { 0.f };   // Batch 1 — live L1 LFO value for the editor dot
    // ANNULUS resonator — live feed read by the editor timer → purple audio-reactive harmonograph layer.
    std::atomic<float> resoVizEnergy_[4] { {0.f}, {0.f}, {0.f}, {0.f} };   // per-band modal energy
    std::atomic<float> resoVizOut_ { 0.f };                                 // resonator output level (purple glow/streaks)
    // Packed stage+fraction of the same voice (e.g. 2.37 = 37% through Attack), so the
    // UI dot rides the exact x-position on the curve. -1 = no voice sounding.
    std::atomic<float> ampEnvFollowVis { -1.f };
    // SAMPLE-FOLLOWER (multi) — one playhead per SOUNDING voice so the UI can draw a fading white
    // line for every held note (Phase Plant style). Per osc: a compact list of {voiceIndex, pos01}
    // for active sample voices + a count. Keyed by voiceIndex so the editor can give each note a
    // stable line (smooth fade on release). Capped at kMaxFollowers.
    static constexpr int kMaxFollowers = 16;
    std::atomic<int>   sampleFollowIdx_[4][kMaxFollowers] {};   // voice index (identity)
    std::atomic<float> sampleFollowPos_[4][kMaxFollowers] {};   // read position 0..1
    std::atomic<int>   sampleFollowCount_[4] {};               // active count per osc
    // (GRANULAR-FOLLOWER grain-cloud atomics retired 2026-07-02 — granular now rides the sampleFollow_
    //  arrays above via SynthVoice::granScanPos01, drawn as reactive white lines in the UI.)

    // ── OSC SCOPE — published per-osc live waveform windows (A/B/C/D) ──────────
    // SPSC seqlock handoff: the audio thread brackets its window stores with an odd/even
    // oscScopeSeq (odd = write in progress, even = complete); the editor's 60 Hz timer
    // reads oscScopeSeq before+after copying the window and retries on a mismatch, so it
    // always gets a tear-free snapshot. 4 x 1024 floats (well under the proven 80 KB EQ
    // push). active=false => no voice sounding (JS falls back to the static cycle).
    static constexpr int OSC_SCOPE_SIZE = 1024;   // full per-osc ring → more cycles on the scope (less zoom)
    std::array<std::array<std::atomic<float>, OSC_SCOPE_SIZE>, 4> oscScope {};
    std::atomic<float> oscScopeHz     { 0.f };    // fundamental Hz of the displayed voice
    std::atomic<float> oscScopeSr     { 48000.f };// sample rate (for the JS trigger period)
    std::atomic<bool>  oscScopeActive { false };  // false = no voice sounding (JS falls back to static cycle)
    std::atomic<int>   oscScopeSeq    { 0 };      // SPSC seqlock: odd = write in progress, even = complete
    double             oscScopePubAccum_ = 0.0;   // audio-thread sample accumulator — gates the voice-sum publish to ~60 Hz
    // VIZDBG + TAIL-MODE — published alongside the window so the UI can (a) show the
    // MASTER-OUTPUT tail when no voice is amp-active but audio still rings (grain delay /
    // tape loops ring for MINUTES — the "scope flat while audio plays" report), and
    // (b) self-diagnose on screen if the anomaly ever recurs (the overlay prints these).
    std::atomic<int>   oscScopeNv   { 0 };        // amp-active voice count at publish
    std::atomic<float> oscScopeLv   { 0.f };      // most-active voice's level at publish
    std::atomic<bool>  oscScopeTail { false };    // true = published window IS the output tail
    std::atomic<float> oscScopeORms { 0.f };      // master output ring RMS at publish
    bool               oscScopeTailGate_ = false; // audio-thread hysteresis for tail mode
    // VIZDBG forensics — per-osc snapshot of the best voice at publish: window peak,
    // engine idx, active unison, mip level, FM effective index, unison auto-gain.
    std::array<std::atomic<float>, 4> oscScopeWpk {};   // per-osc published window peak
    std::array<std::atomic<int>, 4>   oscScopeEng {};   // engine index per osc
    std::array<std::atomic<int>, 4>   oscScopeAu  {};   // active unison per osc
    std::array<std::atomic<int>, 4>   oscScopeMip {};   // current mip level per osc
    std::array<std::atomic<float>, 4> oscScopeD1e {};   // FM d1 effective per osc
    std::array<std::atomic<float>, 4> oscScopeUn  {};   // unison auto-gain per osc
    std::array<std::atomic<float>, 4> oscScopeGt  {};   // solo/mute/enable gate target per osc (0 = configured silent)
    std::atomic<int>                  oscScopeBad { 0 };// non-finite window samples sanitized at the last publish (overlay: F:PUSH-POISON)

    std::atomic<int> currentPresetIndex { 0 };
    std::atomic<int> editorWidth { 0 };   // fb95/fb514 — remembered editor width (0 = default 820); saved in state again since fb514 (fb96 dropped it; the editor-side latch now keeps junk sizes out of this atomic)

    // XY automation state (synced from JS, captured into presets)
    std::atomic<float> xyAutoEnabled { 0.f };
    std::atomic<float> xyAutoMode    { 0.f };
    std::atomic<float> xyAutoSpeed   { 0.5f };

    // Grain BPM sync state (synced from JS, captured into presets)
    std::atomic<float> grainSyncEnabled { 0.f };
    std::atomic<float> currentBPM { 120.f }; // populated from playhead

    // Grain engine master on/off (synced from JS, captured into presets)
    std::atomic<float> grainEngineEnabled { 0.f }; // fresh instance: OFF (user powers it on). Old projects restore their saved value (fallback 1 = legacy behaviour).

    // Tape engine master on/off (synced from JS, captured into presets)
    std::atomic<float> tapeEnabled { 0.f }; // fresh instance: effects OFF (user powers them on). Old projects restore their saved value (fallback 1 = legacy behaviour).

    // Tape LOOP transport on/off — independent of tapeEnabled. Toggling tape
    // FX off no longer freezes the loop transport; users wanted these split
    // so they can bypass tape effects while the loop keeps playing/recording.
    std::atomic<float> tapeLoopEnabled { 1.f }; // 1 = on, 0 = bypass loop transport

    // EQ panel open/closed UI state (editor-side only, persists via PluginSettings.json)
    std::atomic<float> eqPanelOpen { 0.f };  // editor UI state, persists via PluginSettings.json

    // Last-viewed UI page for THIS instance (0=front 1=syn 2=eq 3=dly 4=mod). In-memory ONLY —
    // deliberately NOT saved in getStateInformation or the settings file: closing/reopening the
    // editor restores the page, while every NEW instance (or project reload) starts on the front page.
    // fb537 — 1 == the SYN page. Was 0 (front/hero): a fresh instance opened on the hero page
    // and every session reload went back to it. Codes are shared with the JS UI_PAGE_IDS list:
    // 0 front, 1 syn, 2 eq, 3 dly, 4 mod. Persisted in the state blob as "uiPage".
    std::atomic<int> uiPage { 1 };

    // fb148 — UI-consumer census: the main editor + every popped card window. The audio
    // thread gates PURE-VIZ production on this (spectrum FFTs, osc-scope publish), so a
    // closed UI costs nothing to visualize for — "closed <= open" by construction.
    std::atomic<int> uiClients_ { 0 };
    int tiTimerHz_ = 60;   // fb514 — closed-editor idle rate governor (timerCallback: 15 Hz with no UI client, 60 Hz otherwise)
    // fb484 — STANDALONE QWERTY-TO-MIDI: the WebView owns keyboard focus, so JS key events call
    // the qwertyNote native fn (message thread), which pushes into this lock-free SPSC ring;
    // processBlock (audio thread) drains it into the normal MIDI stream. No locks, no deps.
    struct QwertyEvt { int note; bool on; };
    QwertyEvt         qwertyQ_[64] = {};
    std::atomic<int>  qwertyW_ { 0 }, qwertyR_ { 0 };
    void pushQwertyNote (int note, bool on);
    // fb484 — BEACON v2: the editor's viz-transport state, so a freeze names its own cause.
    std::atomic<uint32_t> dbgFramesSent_ { 0 };     // coalesced frames + keep-alives sent
    std::atomic<uint32_t> dbgAcks_       { 0 };     // completions that came back
    std::atomic<uint32_t> dbgLastFrameB_ { 0 };     // bytes of the last sent frame
    // fb488 — DSP LOAD METER: processBlock's own time vs the audio time it produced. The beacon
    // turns it into "% of one core" in %TEMP%\terrain-cpu.txt every 5 s, so a CPU report is a
    // number instead of a guess.
    std::atomic<long long> dspTicks_   { 0 };
    std::atomic<long long> dspSamples_ { 0 };
    // fb489 — PHASE SPLIT. 26.1% of a core with NO NOTES needs a section name, not a theory.
    // gather = the ~2,400 lines of block-rate parameter/mod work that run whether or not a note
    // sounds; voices = renderNextBlock; fx+master = the remainder (total - gather - voices).
    std::atomic<long long> dspGather_ { 0 };
    std::atomic<long long> dspVoices_ { 0 };
    std::atomic<int>       dspLastBlk_ { 0 };
    std::atomic<long long> dspBlocks_ { 0 };   // fb492 — the meter reported the LAST block size; FL
                                               // varies it, so only the AVERAGE means anything.
    // fb492 — CONTROL-RATE GATHER. FL Studio subdivides its buffer and calls processBlock with
    // ~45-88 sample blocks (measured), i.e. 500-1000 times a second, and ~900 lines of pure
    // knob-reading ran on EVERY one of them. Those reads now run at ~172 Hz instead: enough for
    // any knob or modulation move (a 512-sample host already gathers at 86 Hz and that is the
    // reference platform), and 3-6x fewer at FL's call rate. Forced immediately whenever the
    // block carries MIDI, so a note never starts on stale parameters.
    static constexpr int kGatherSpan = 256;   // samples between gathers (~5.8 ms at 44.1 k)
    // fb494 — cache-on-change for two libm pow() calls that ran PER SAMPLE (measured). The
    // smoothers return an identical float once landed, so the branch is taken essentially never
    // at rest and the result is bit-identical when it is.
    float lastOutGainDb_ = 1e30f, lastOutGain_ = 1.0f, lastFreezeRaw_ = 1e30f, lastFreeze_ = 0.0f;
    int gatherSpan_ = 1 << 20;                // huge => the very first block always gathers
    wc::ModConfig synModCfgPersist_;          // assigned inside the gather, consumed after it
    float         synModBpmPersist_ = 0.0f;
    long long dspT0_ = 0, dspTA_ = 0;   // audio thread only

    // fb501 — MESSAGE-THREAD METER (the editor's timerCallback). Public so the editor can write
    // them; message thread only, so plain relaxed atomics are ample. Reported in the same probe
    // line as the DSP split, because "the window costs +17.5 points" needed a number and had none.
    std::atomic<long long> uiTicksTotal_ { 0 }, uiTicksBuild_ { 0 }, uiTicksShip_ { 0 };
    // fb509 — PER-SEGMENT tick meter: the 55%-of-a-core play-state build was guessed at twice
    // (spectrum cadence, formatting) and both guesses missed. Segment names in the editor.
    static constexpr int kUiSegs = 10;
    std::atomic<long long> uiSegTicks_[kUiSegs] {};
    std::atomic<int>       uiTickCount_  { 0 };
    long long              uiBuildMark_  = 0;   // message thread only — where build ends / ship begins
    bool vizConsumersLive() const noexcept { return uiClients_.load (std::memory_order_relaxed) > 0; }

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

    // ── Synth mod-matrix (drag-to-assign): LFO source → synth dest, depth. The editor
    //    pushes the route list as JSON via the setSynthMod native fn; we parse it into a
    //    thread-safe vector the audio thread copies into synModCfg each block. Persisted.
    struct SynModRoute { int src = 0; int dest = 0; float depth = 0.0f; int curve = -1; };   // fb554 — curve = index into the published ModCurveSet, -1 = a straight line
    // FLOW · ARP lane pattern (fb105): UI pushes JSON, audio thread copies into the
    // engine on version bump (synModLock pattern). Raw JSON kept verbatim for state save.
    mutable juce::CriticalSection arpLaneLock_;
    wc::ArpLaneData               arpLanesShared_;                 // guarded by arpLaneLock_
    juce::String                  arpLanesJson_;                   // guarded by arpLaneLock_
    std::atomic<int>              arpLanesVersion_ { 1 };
    int                           arpLanesSeen_ = 0;               // audio-thread last-copied version
    // ARP viz feed (audio thread writes after flowArp.process, UI rAF-polls getArpFeed)
    std::atomic<float>            arpVizStepF_ { 0.0f };
    std::atomic<int>              arpVizCount_ { 0 }, arpVizNote_ { -1 }, arpVizVel_ { 0 }, arpVizActive_ { 0 };
    float                         arpWaveMod_ = 0.0f;              // audio-thread only; voices consume NEXT block (drift-lane pattern)
    // FLOW · CHOP viz feed + Wipe request (fb106)
    std::atomic<float>            chopVizStepF_ { 0.0f }, chopVizWet_ { 0.0f };
    std::atomic<int>              chopVizCount_ { 0 }, chopVizSlice_ { 0 }, chopVizActive_ { 0 };
    std::atomic<bool>             chopWipeReq_ { false };
    // FLOW · GLITCH viz feed + Roll request (fb115)
    std::atomic<float>            gliVizStepF_ { 0.0f }, gliVizLoopF_ { 0.0f }, gliVizFireS_ { 0.0f },
                                  gliVizHold_ { 1.0f }, gliVizWet_ { 0.0f };
    std::atomic<int>              gliVizFx_ { -1 }, gliVizCount_ { 0 }, gliVizActive_ { 0 };
    std::atomic<float>            gliVizLvl_[16] {};
    std::atomic<float>            gliVizOut_ { 0.0f };   // fb124 — speaker meter level
    std::atomic<float>*           gliFxFltP_[8] { nullptr }, * gliFxPanP_[8] { nullptr },
                                * gliFxTrgP_[8] { nullptr }, * gliFxGrdP_[8] { nullptr };   // fb125/127 — cached at prepare
    std::atomic<bool>             gliRollReq_ { false };
    // FLOW · ROBIN viz feed (fb122)
    std::atomic<int>              rbnVizNow_ { -1 }, rbnVizNext_ { -1 }, rbnVizNotes_ { 1 },
                                  rbnVizMask_ { 15 }, rbnVizWrap_ { 0 }, rbnVizHits_ { 0 };

    mutable juce::CriticalSection synModLock;
    std::vector<SynModRoute>      synModRoutes;   // guarded by synModLock
    juce::String                  synModJson;     // last JSON received (for state save)
    juce::CriticalSection         dynEnvLock_;    // fb177 — dynamic envelope blob
    DynEnvShape                   dynEnvShapes_[kMaxDynEnvs];
    int                           dynEnvCount_ = 0;
    juce::String                  dynEnvJson_;
    std::atomic<int>              dynEnvVersion_ { 0 };
    // fb178 — MONO ENVELOPE TAP: the processor-side (global) dests — Res/Drive/mixes/
    // sends/card knobs — have no per-voice context, so envelope sources there read a
    // mono pool retriggered by ANY note-on (released when the last note lifts).
    terrain::TerrainEnvelope      monoLegEnv_[5];
    terrain::TerrainEnvelope      monoDynEnv_[kMaxDynEnvs];
    int                           monoHeld_ = 0;
    std::atomic<uint32_t>         monoEnvGlobalMask_ { 0 };   // env n bit set = env n routed to a global dest
    float monoEnvLevelOf (int modSource) const noexcept   // fb179 — KNOB-IS-THE-PEAK (level−1)
    {
        double lv = 0.0;
        switch ((wc::ModSource) modSource)
        {
            case wc::ModSource::EnvAmp:    lv = monoLegEnv_[0].level(); break;
            case wc::ModSource::EnvFilter: lv = monoLegEnv_[1].level(); break;
            case wc::ModSource::EnvPitch:  lv = monoLegEnv_[2].level(); break;
            case wc::ModSource::EnvMod1:   lv = monoLegEnv_[3].level(); break;
            case wc::ModSource::EnvMod2:   lv = monoLegEnv_[4].level(); break;
            default:
            {
                const int k = modSource - (int) wc::ModSource::EnvD1;
                if (k >= 0 && k < kMaxDynEnvs) lv = monoDynEnv_[k].level();
                break;
            }
        }
        return (float) lv - 1.0f;
    }
    DynEnvShape                   dynEnvAudio_[kMaxDynEnvs];   // audio-thread mirror
    int                           dynEnvAudioCount_ = 0;
    int                           dynEnvSeen_ = -1;

    // LFO ARC L1 — THE SHAPER: drawn LFO shapes. UI pushes {"shapes":[{n,pts:[[x,y,c]..],gh,gv,sn}..]};
    // the message thread bakes each into a bipolar (kLfoTableN+1)-float table using the ENV-EDITOR bias
    // math (the graph IS what you hear); the audio thread copies shared→audio once per version bump
    // (try-lock, never blocks); every SynthLFO (per-voice banks + flowLfo_) reads the audio mirror via
    // a pointer wired once at prepare — so table edits reach all consumers at ZERO per-block cost.
    juce::CriticalSection         lfoShapeLock_;
    juce::String                  lfoShapesJson_;
    juce::CriticalSection         dstCurveLock_;    // fb328 — curve-card blob (source of truth for popout)
    juce::String                  dstCurvesJson_;
    int                           dstTableSrc_ = -1;   // fb339 — Table source (-1 gen, 0-3 osc)
    float                         lfoTableShared_[wc::NUM_LFOS][wc::kLfoTableN + 1] {};
    float                         lfoTableAudio_ [wc::NUM_LFOS][wc::kLfoTableN + 1] {};
    // fb238 — PER-POINT MODULATION (Serum 'Modulating LFO Points'): a drawn point may carry
    // {xs,xa,ys,ya} — per-axis source LFO (1..10, 0 = none) and amount (−1..+1). The message
    // thread stores the parsed list beside the base bake; the audio thread re-bakes the AUDIO
    // table in place at block top from the live flowLfo peeks (the same carrier truth the
    // follower dot rides), gated so an un-modded LFO costs zero and a modded one only pays
    // (256 lerps) when a source it listens to actually moved.
    struct LfoShapePtM { float x = 0, y = 0, c = 0; int xs = 0; float xa = 0; int ys = 0; float ya = 0; };
    static void bakeLfoShapeTable (const LfoShapePtM* pts, int np, float* tb) noexcept;   // one bake, both threads
    static void dstBakeEff (const LfoShapePtM* pts, int np, float* out, int n) noexcept;  // fb340 — NON-periodic curve bake (the dstBakePts tension twin, float-point input)
    static void bakeLfoPathTable  (const LfoShapePtM* pts, int np, float* tb) noexcept;   // fb239 — Path: arc-length traversal of a free 2D drawing
    LfoShapePtM                   lfoPtShared_[wc::NUM_LFOS][160];
    LfoShapePtM                   lfoPtAudio_ [wc::NUM_LFOS][160];
    // fb340 — per-point CURVE mod (the fb238 machinery on the distortion banks; same handoff grammar)
    LfoShapePtM                   dstPtShared_[4][32];
    LfoShapePtM                   dstPtAudio_ [4][32];
    int                           dstPtNpShared_[4] { 0,0,0,0 }, dstPtNpAudio_[4] { 0,0,0,0 };
    bool                          dstPtHasModShared_ = false, dstPtHasModAudio_ = false, dstPtDirty_ = false;
    std::atomic<int>              dstPtVersion_ { 0 };
    int                           dstPtSeen_ = 0;
    float                         dstPtSrcLast_[10] {};
    float                         dstMorphEff_ = 0.65f;   // fb340 — modded Morph, computed in modP's scope, consumed at the FX push
    int                           lfoPtNpShared_[wc::NUM_LFOS] {};
    int                           lfoPtNpAudio_ [wc::NUM_LFOS] {};
    bool                          lfoPtHasModShared_[wc::NUM_LFOS] {};
    bool                          lfoPtHasModAudio_ [wc::NUM_LFOS] {};
    float                         lfoPtSrcLast_[wc::NUM_LFOS][wc::NUM_LFOS] {};
    bool                          lfoPtDirty_[wc::NUM_LFOS] {};
    // fb228 — L5 MOTION per LFO (blob-fed beside the tables; same shared->audio copy discipline).
    // Defaults ARE the product defaults: tg=1 (RETRIG — Max: 'every LFO retrigs from here on out').
    struct LfoMotion { int tg = 0; float lb = -1.0f; int dir = 0; int mn = 0;   // fb235 — FREE is the product default again (Max reversed the fb228 Retrig call)
                       float ri = 0.0f, de = 0.0f, sm = 0.0f, sw = 0.0f; int td = 0; int ho = 1; int rs = 0; int pol = 0; };   // fb245 rs=reseed · fb246 pol=polarity (0 Bi · 1 Uni+ · 2 Uni−)
    LfoMotion                     lfoMotionShared_[wc::NUM_LFOS];
    LfoMotion                     lfoMotionAudio_ [wc::NUM_LFOS];
    std::atomic<int>              lfoShapeVersion_ { 0 };
    int                           lfoShapeSeen_ = -1;

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
    //
    // fb496 — LAZY ARM. 600 s x 5 rings x stereo x float32 @ 44.1k is ~1,058 MB, and
    // prepareToPlay used to pay ALL of it, unconditionally, on every instance.
    //
    // 🔑 THE RINGS WERE PROVABLY DEAD UNTIL A SAMPLE WAS LOADED. The per-layer write
    // is inside the render loop, BELOW `if (! layer.hasSample()) continue;` — so a
    // layer with no sample never wrote one byte, and exportStemToFile/dragStem only
    // ever offer a populated layer. So arming on the first loaded sample (see
    // ensureStemBuffersAllocated, driven from timerCallback = message thread) leaves
    // the CONTENT of every ring identical to what it was before: the empty prologue
    // is the only thing that stops being allocated.
    //
    // 🚨 totalSize IS THE PUBLICATION FLAG, and that is why it is an atomic. The
    // audio thread tests it before it touches ring/writeIndex, so it is stored 0
    // BEFORE setSize and stored non-zero (release) only AFTER the ring is sized,
    // cleared and its cursors reset. An audio thread that sees non-zero is
    // guaranteed to see a finished ring behind it. Nothing ever un-arms mid-audio.
    struct StemBuffer
    {
        juce::AudioBuffer<float> ring;
        std::atomic<int>         writeIndex     { 0 };
        // Cumulative samples written since the buffer started or was last CLEARed.
        // Saturates at totalSize — once it reaches totalSize, the ring is "full"
        // and exports unwrap the full 10-min rolling window; below totalSize, the
        // export only writes the actual captured portion (no silent pad).
        std::atomic<int>         samplesWritten { 0 };
        std::atomic<int>         totalSize  { 0 };  // ring.getNumSamples(); 0 = not armed
    };
    std::array<StemBuffer, 4> stemBuffers;
    // 5th ring: post-FX master output (the final mixed-through-effects signal).
    // Captured in lockstep with the layer rings. WET stem export uses
    // per-sample energy ratios to attribute master_fx back to each layer.
    StemBuffer masterFxBuffer;
    void allocateStemBuffers (double sampleRate);
    // fb496 — lazy-arm plumbing. MESSAGE THREAD ONLY (it allocates ~1 GB).
    // Idempotent: after the first successful arm it is a single relaxed load.
    // Public so the stem UI can arm explicitly the moment it is wired up.
    void ensureStemBuffersAllocated();
    bool areStemBuffersArmed() const noexcept
        { return stemBuffersArmed_.load (std::memory_order_acquire); }
    // fb496 — the Export ring (RollingCaptureBuffer, ~202 MB) is armed the first time
    // an editor is created: exportCapture() is reachable from the UI and nowhere else,
    // and the "N seconds captured" readout is only drawn while an editor exists.
    void ensureCaptureBufferAllocated();
    std::atomic<bool>   stemBuffersArmed_ { false };
    // fb517 — PER-LAYER stem arming (laneD audit: one loaded sample armed ALL FIVE rings at
    // once = +1,009 MB; now only the receiving layer's ring + the master ring on first arm).
    // stemBuffersArmed_ keeps meaning "master armed / any layer armed" for the re-arm path.
    std::array<std::atomic<bool>, 4> stemLayerArmed_ { };
    void ensureStemLayerAllocated (int layerIdx);
    // Latched by ensureCaptureBufferAllocated(). It is a REQUEST, not the arm itself:
    // a host may create the editor BEFORE the first prepareToPlay, and then there is no
    // sample rate to size the ring with yet — prepareToPlay honours the request instead.
    std::atomic<bool>   captureArmRequested_ { false };
    // The sample rate the host last prepared us at — the rate a lazy arm must use.
    // 0 until the first prepareToPlay; arming before that is deferred, not guessed.
    std::atomic<double> preparedSampleRate_ { 0.0 };
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

    // CPU: pointer-keyed memo over getRawParameterValue — each raw call builds a juce::String
    // (heap alloc) + hash lookup, and processBlock made ~260 of them PER BLOCK. Keys are the
    // ADDRESSES of the ParameterIDs string constants (stable for the process lifetime), so a
    // hit costs one pointer-hash lookup, no allocation. Populated lazily during the first
    // blocks (each unique call site inserts once), read-only forever after. Do NOT call this
    // with a temporary/dynamic string — juce::String args won't compile, by design.
    // fb490 — FLAT, ALLOCATION-FREE PARAMETER TABLE. A real audio-thread profile of an IDLE
    // plugin (no notes) put std::__hash_table::find<void const*> at 14% of processBlock: this memo
    // was a std::unordered_map, i.e. a hash and then a NODE CHASE into scattered heap. libc++
    // makes that merely wasteful; MSVC's node-based map makes it expensive, which is part of why
    // identical work costs multiples more on Windows than on the Mac.
    //
    // Keys are the ADDRESSES of the ParameterIDs string constants — stable for the process
    // lifetime — so a power-of-two open-addressed table with linear probing resolves a hit in one
    // or two cache lines, never allocates and never rehashes.
    //
    // fb490b — MEASURED CORRECTION, and the reason this comment is long. The first cut used
    // std::atomic key slots with ACQUIRE ordering. On Apple Silicon an acquire load is `ldar`, a
    // barrier; with hundreds of parameter reads per block that cost inlined straight into
    // processBlock and a re-profile read 5.6% of a core idle against 3.33% before — even though
    // all four items the change targeted had gone to ZERO. Barriers are free on x86 and expensive
    // on ARM, so the "fix" was a Windows win paid for by a Mac regression. Instead:
    //   · PLAIN loads on the hot path (no barrier on any platform). Insert publishes the value,
    //     then a release FENCE (a few times per id, ever — cost irrelevant), then the key. A
    //     reader that sees the key sees the value; one that sees a key with a null value takes
    //     the slow path once rather than dereferencing null.
    //   · The table lives OFF the processor object, so 64 KB of slots cannot push hot members
    //     apart and wreck the locality of everything declared after it.
    struct RawCache
    {
        static constexpr size_t kSlots = 4096, kMask = kSlots - 1;
        const void*         keys[kSlots] {};
        std::atomic<float>* vals[kSlots] {};
    };
    mutable std::unique_ptr<RawCache> rawCache_ { new RawCache() };
    std::atomic<float>* rawParam (const char* id) const
    {
        auto& c = *rawCache_;
        const uintptr_t k = (uintptr_t) id;
        size_t h = (size_t) (((k >> 3) * 11400714819323198485ull) >> 52) & RawCache::kMask;
        for (int probe = 0; probe < 64; ++probe)
        {
            const void* key = c.keys[h];                       // plain load
            if (key == (const void*) id)
            {
                if (auto* v = c.vals[h]) return v;              // null only mid-insert -> slow path
                break;
            }
            if (key == nullptr) break;                         // empty slot: this is where it goes
            h = (h + 1) & RawCache::kMask;
        }
        auto* p = const_cast<juce::AudioProcessorValueTreeState&> (apvts).getRawParameterValue (id);
        c.vals[h] = p;
        std::atomic_thread_fence (std::memory_order_release);
        c.keys[h] = (const void*) id;
        return p;
    }

    // fb131 — MODE CHAIN: FLOW_CHAIN_1..4 (click order) resolved to the active set; an
    // all-empty chain falls back to the legacy single FLOW_MODE (old saves untouched).
    // Cheap enough to call per block AND from the message-thread feeds (atomic loads only).
    wc::FlowChainState flowChainNow() const
    {
        const int s[4] = { (int) rawParam (ParameterIDs::FLOW_CHAIN_1)->load(),
                           (int) rawParam (ParameterIDs::FLOW_CHAIN_2)->load(),
                           (int) rawParam (ParameterIDs::FLOW_CHAIN_3)->load(),
                           (int) rawParam (ParameterIDs::FLOW_CHAIN_4)->load() };
        return wc::resolveFlowChain (s, (int) rawParam (ParameterIDs::FLOW_MODE)->load());
    }
    static constexpr int kNumVoices = 32;  // bumped 16→32 for LAYER mode headroom (4 slices × 8 keys)

    // ── Synth section (Phase 1 MPV — see Design/v1-syn-spec.md) ──────────
    // Parallel pipeline beside the 4 layers. juce::Synthesiser owns 8
    // SynthVoices + 1 SynthSound. Renders into synthScratch each block,
    // summed into the master `buffer` before the FX chain.
    static constexpr int kSynthVoiceCount = 96;  // 8a polish-2: bumped from 32 — UNISON=8 × polyphony=8 fits without steal
    // Typed views of synthEngine's voices (same order/indices as getVoice(i)); filled once in
    // the constructor — the audio thread reads these instead of dynamic_cast'ing per voice.
    std::array<tw::SynthVoice*, kSynthVoiceCount> synthVoices_ {};

    // CPU: instance-wide live-grain counter + cap, shared by every GranularEngine (audio-thread
    // only — no atomics). ~8.5 ns per grain-sample → 256 grains ≈ 10% of a core, the ceiling
    // for ALL granular oscs/voices/unison combined; spawns skip-and-wait past it (grain clouds
    // thin gracefully, no click — window-at-birth is always 0).
    static constexpr int kGranBudget = 256;
    int granGrainsLive_ = 0;

    // CPU: change-gates for the HEAVY per-voice broadcast pushes. The ModConfig (~1KB copy +
    // 10 LFO reconfigs) and the 8 engine-param structs were pushed to all 96 voices EVERY
    // block; they're pure functions of params/BPM, so they now push only on actual change
    // (every change still broadcasts to the FULL pool, so voices are never stale). The
    // pushed_ flags force the first push (and re-push after prepareToPlay resets voices).
    wc::ModConfig lastSynModCfg_;
    float         lastSynModBpm_    = -1.0f;
    bool          synCfgPushed_     = false;
    tw::SynthVoice::SampleEngineParams   lastSpA_, lastSpB_, lastSpC_, lastSpD_;
    tw::GranularEngineParams             lastGpA_, lastGpB_, lastGpC_, lastGpD_;
    bool          engParamsPushed_  = false;

    // PORTAMENTO — audio-thread glide tracking (origin note + held count for ALWAYS-off gating).
    float synthGlideFrom_  = -1.0f;   // last synth note (pitch to glide FROM); -1 = none yet
    int   synthNotesHeld_  = 0;       // synth notes currently sounding
    UnisonSynth                 synthEngine;   // Phase 8a: was juce::Synthesiser
    wc::FlowArp                 flowArp;                    // FLOW · ARP engine (one global instance)
    wc::FlowChop                chop;                       // FLOW · CHOP engine (mode 2) — audio insert at end of processBlock
    wc::FlowGlitch              glitch;                     // FLOW · GLITCH engine (mode 3) — audio insert at end of processBlock
    bool                        prevGlitchOn_ = false;      // FLOW · prev-block glitch-on (enable-edge detection; resets glitch clock on (re)enable)
    wc::FlowDrift               drift;                      // FLOW · DRIFT engine (mode 4) — generative mod source
    float                       driftLane_[wc::kDriftLanes] {};  // per-block DRIFT lane values (mod sources; matrix routing = phase-2)
    wc::FlowRobin               flowRobin_;                      // fb122 — the Wheel rotation brain (audio thread)
    float                       robinDriftCents_[4] {};          // per-station wander (drift lanes × card Drift)
    wc::SynthLFO                flowLfo_[wc::NUM_LFOS];     // block-rate global LFO bank for FLOW-knob mod
    wc::ResonatorNode           reso;                       // ANNULUS resonator — global node, audio insert at end of processBlock
    int                         resoHeld_[16] {};           // held MIDI notes (resonator polyphony — one voice per note)
    int                         resoHeldN_ = 0;             // count of held notes (audio-thread only)
    juce::AudioBuffer<float>    synthScratch;

    // ── Synth wavetable bank (Phase 2A) ──────────────────────────────────
    // Owns the 6 iconic analog tables, constructed at startup (~750KB RAM).
    // SynthVoices hold const Wavetable* pointers into this bank; bank
    // outlives all voices (member-of-processor lifetime).
    tw::WavetableBank           wavetableBank;

    // ── Spectral Morph (Phase 11c rework) ────────────────────────────────
    // Per-OSC morphed wavetable. SpectralMorph::apply + buildFromSpec is ~2.3 ms (fb467, M2 Max;
    // it was 19.1 ms until the bake's transform moved to vDSP — and the '~5.6 ms' this comment
    // used to claim stopped being true at fb301, when the mip ladder went from 8 levels to 34)
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
        // RACE HARDENING (2026-07-05 scope-flatline root cause): buildFromSpec zeroes then
        // refills IN PLACE (~2.3 ms, fb467) while audioReadingIdx only refreshes at block START — a
        // voice could render its blend composite from a mid-build (zeroed) table and, with
        // pointer-keyed caching, latch SILENCE for minutes. ready[] parks the audio thread
        // on the plain bank table while a build is in flight; retireCooldown gives in-flight
        // blocks time to leave a just-retired buffer before it may be rebuilt.
        std::atomic<bool> ready[2] { true, true };
        int   retireCooldown = 0;         // message-thread ticks before a retired buffer may rebuild
        int   buildIdx    = 0;            // message-thread: next buffer to build into
        int   builtPreset = -1;
        int   builtMode   = -1;
        float builtAmount = -1.0f;
        // fb467 — the partial WINDOW is part of the built table's identity. Without these two the
        // skip gate below matches on preset/mode/amount alone and Lo/Hi move NOTHING until some
        // other knob happens to force a rebuild — the failure that builds clean and looks wired.
        float builtLo     = -1.0f;
        float builtHi     = -1.0f;
        const tw::Wavetable* builtImportPtr = nullptr;   // fb253 — morph SOURCE was this import (nullptr = a factory preset)
        int   builtImportEpoch = -1;                     // fb253 — the import's buildEpoch when morphed (re-import → re-morph)
    };
    MorphSlot morphA_, morphB_, morphC_, morphD_;
    // fb76 — the audio thread publishes the EFFECTIVE (LFO-modulated) spectral amount per osc each
    // block; the 60 Hz timer's rebuildMorphIfNeeded reads it instead of the raw param. -1 = not yet
    // published (fresh instance / suspended host) → the timer falls back to the raw param.
    std::atomic<float> spectralEffAmt_[4] { { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f } };
    // fb467 — same publish for the partial window's two edges (SpecLo/SpecHi are mod destinations,
    // so the timer must build from the MODULATED value, not the raw param). -1 = not yet published.
    // 🚨 fb469 — the EFFECTIVE blur, published unconditionally. The twin build first keyed off
    //    wtBlurVis_, and that is a DISPLAY value: it is written only when `vizLive` (the editor is
    //    open) AND a voice is sounding. With the editor closed the twin was therefore never built
    //    and blur silently kept its old behaviour — caught on the installed AU, where Square's
    //    centroid still fell 7.93 -> 2.83 while the offline gate said 13.97 -> 17.55. A display feed
    //    is not a control signal (fb373).
    std::atomic<float> blurEff_[4] { { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f } };
    std::atomic<float> specLoEff_[4] { { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f } };
    std::atomic<float> specHiEff_[4] { { -1.0f }, { -1.0f }, { -1.0f }, { -1.0f } };
    // fb467 — the eight window parameters, RESOLVED ONCE in the constructor. The publish needs the
    // parameter's own NormalisableRange to modulate in its skewed space, and the general helper
    // (fb193 modP) reaches it via apvts.getParameter(juce::String(pid)) — a juce::String is a
    // ref-counted heap buffer, so that is a malloc per routed edge per block on the AUDIO THREAD.
    // fb456's law: an allocation on the audio thread is a violation regardless of what it costs.
    juce::RangedAudioParameter* specLoParam_[4] { nullptr, nullptr, nullptr, nullptr };
    juce::RangedAudioParameter* specHiParam_[4] { nullptr, nullptr, nullptr, nullptr };

    // ── fb522 · LANE P — THE OVERPASS STAGE ───────────────────────────────────────────────────
    // URANGE is EXPONENTIAL (5..4800 cents) and PHASE is 0..360 degrees, so a mod route on either
    // must move it through the parameter's OWN NormalisableRange (fb193 modP), not by a flat delta
    // in raw units. modP reaches the parameter with apvts.getParameter(juce::String(pid)) — a
    // juce::String is a ref-counted heap buffer, i.e. a malloc PER ROUTED KNOB PER BLOCK on the
    // audio thread, which fb456's law forbids outright. Same fix as fb467's window edges: resolve
    // the pointers once, in the constructor.
    juce::RangedAudioParameter* uniRangeParam_[4] { nullptr, nullptr, nullptr, nullptr };
    juce::RangedAudioParameter* phaseOffParam_[4] { nullptr, nullptr, nullptr, nullptr };
    // The per-block resolved values, in the units the voice will want. Written once per block in
    // processBlock (audio thread) and read in the same pass by the voice push loop — same thread,
    // so no atomics and no publish. 🚧 The push itself is NOT wired yet: SynthVoice.h has no setter
    // for any of these and this lane does not own that file. The exact setter signatures Lane V has
    // to add are in this lane's handoff list; until they exist the stage is filled and parked, which
    // is deliberate — it means the reads, the mod routing and the clamps are all in place and only
    // the one-line push is outstanding.
    struct OverpassOscStage
    {
        float uniRangeCents = 50.0f;   // 5..4800 cents. 50 == SynthVoice.h:6217 kUniMaxDetuneCents.
        float uniWarp       = 0.0f;    // -1..+1  (UWARP % / 100)
        int   uniStack      = 0;       // 0..8, 0 = Off
        float warpVar       = 0.0f;    // 0..1    (WVAR  % / 100)
        float warp2Var      = 0.0f;    // 0..1    (W2VAR % / 100)
        float phaseOffDeg   = 0.0f;    // 0..360 degrees
        float phaseAmt      = 1.0f;    // 0..1    (PHASE_AMT % / 100); 1 = unscaled = today
    };
    OverpassOscStage overpassOsc_[4];

    // ── Wavetable EXTENDER — per-osc imported tables ("turn anything into a wavetable") ──
    // Built on the message thread from dropped audio (Wavetable::buildFromPcm) then atomic-
    // published to voices, exactly like MorphSlot. When live != null the voice reads the
    // imported table instead of the factory bank. 2-buffer rotation: a re-import builds into
    // the buffer the audio thread is NOT on, then publishes, so there is no torn/zeroed read.
    struct ImportSlot
    {
        tw::Wavetable                     buf[2];
        std::atomic<const tw::Wavetable*> live { nullptr };
        int nextIdx = 0;
    };
    ImportSlot importSlot_[4];
    std::vector<float> importedPcm_[4];                          // stored mono source per osc (re-slice on resolution change)
    int                importFrames_[4] = { 40, 40, 40, 40 };    // current frame count per osc (resolution mode)
    bool               importIsFile_[4] = { false, false, false, false };  // true = a real wavetable FILE (fixed frames), false = arbitrary audio (resolution-adjustable)
    // fb253 — SPECTRAL ON CUSTOM TABLES: SpectralMorph consumes a 16-frame WavetableSpec. For an imported
    // table we derive one via Wavetable::toSpec() and cache it (message-thread only in rebuildMorphIfNeeded),
    // re-deriving only when the import changes (pointer/buildEpoch). The morph then acts on THIS instead of
    // the factory preset spec — so custom tables get spectral (and modulate, via fb252) exactly like presets.
    tw::WavetableSpec  importSpec_[4];
    const tw::Wavetable* importSpecSrc_[4]   = { nullptr, nullptr, nullptr, nullptr };
    int                importSpecEpoch_[4]   = { -1, -1, -1, -1 };
    // fb248 — imported-table build pool (1 serialized worker). The FFT reconstruction of 8 mip levels ×
    // frames × 2048 is heavy (Serum-size tables freeze the UI when built on the message thread + flash purple).
    // Declared AFTER importSlot_/importedPcm_ so it destructs FIRST (joins any in-flight build before those die).
    juce::ThreadPool   wtBuildPool_ { 1 };
    juce::String       importName_[4];                                     // display/persist name (file/table) per osc
    bool               wt3dView_[4] = { false, false, false, false };       // 3D-waterfall view toggle per osc (survives editor reopen + preset)
    void rebuildImport (int osc);                                // message thread — (re)build importSlot_[osc] from importedPcm_
    void rebuildImportAsync (int osc);                           // fb248 — snapshot on msg thread, build on wtBuildPool_ (UI never freezes)

    // fb481 — STALL BEACON. Max's Windows laptop froze every control in the host while audio ran;
    // the message thread was the casualty and NOTHING reported it. The beacon is a tiny watchdog
    // thread (Windows only) that watches a heartbeat the 60 Hz processor timer bumps; if the
    // message thread goes silent >3 s it appends one line to %TEMP%\terrain-stall.txt with the
    // numbers that decide the diagnosis: how long, how many bakes, how slow the last bake was.
    std::atomic<uint32_t> mtHeartbeat_ { 0 };
    std::atomic<float>    lastBakeMs_  { 0.0f };
    std::atomic<uint32_t> bakeCount_   { 0 };
   #if JUCE_WINDOWS
    std::unique_ptr<std::thread> stallBeacon_;
    std::atomic<bool>            beaconStop_ { false };
   #endif
    // Audio thread: the wavetable a voice should read for one osc — the imported table if one
    // was dropped, else the morphed/factory table. Atomic load only (no locks).
    // ═══ fb459 — THE SAME TABLE, RESOLVED FOR THE DISPLAY ═════════════════════════════════════
    // The waterfall resolved import -> bank and NEVER consulted the morph slot, so everything
    // SPECTRAL was invisible: turn the Spectral knob and you hear the harmonics reshape while the
    // table sits still. This is the voice's own preference order — a live+ready morph wins
    // (morph-of-import or morph-of-preset), else the raw import, else the bank.
    //
    // 🚨 IT IS A SEPARATE FUNCTION FOR ONE REASON: wavetableForOsc() WRITES audioReadingIdx, which
    //    is the AUDIO thread's claim on a buffer and the thing rebuildMorphIfNeeded parks on before
    //    it refills one in place (~2.3 ms, fb467). Calling it from the message thread would forge that
    //    claim and could stall or mis-sequence the rebuild. This twin only READS. It still honours
    //    ready[], so a half-built table is never drawn.
    // fb469 — the SAME resolution, mutable, for the ONE message-thread job that needs to write to
    // a table: building its blur twin. Same preference order and the same ready[] honour as the
    // display twin, and like it, it never touches audioReadingIdx. The const_casts are safe: every
    // one of these objects is a non-const member of this processor; only the ACCESSOR is const.
    tw::Wavetable* wavetableForBlurTwin (int osc, MorphSlot& slot, int presetIdx) noexcept
    {
        osc = juce::jlimit (0, 3, osc);
        if (auto* m = slot.live.load (std::memory_order_acquire))
        {
            const int idx = (m == &slot.buf[1]) ? 1 : 0;
            if (slot.ready[idx].load (std::memory_order_acquire)) return &slot.buf[idx];
        }
        if (auto* imp = importSlot_[(size_t) osc].live.load (std::memory_order_acquire))
            return const_cast<tw::Wavetable*> (imp);
        return const_cast<tw::Wavetable*> (wavetableBank.getTable (presetIdx));
    }

    const tw::Wavetable* wavetableForDisplay (int osc, const MorphSlot& slot, int presetIdx) const noexcept
    {
        osc = juce::jlimit (0, 3, osc);
        if (auto* m = slot.live.load (std::memory_order_acquire))
        {
            const int idx = (m == &slot.buf[1]) ? 1 : 0;
            if (slot.ready[idx].load (std::memory_order_acquire)) return m;
        }
        if (auto* imp = importSlot_[(size_t) osc].live.load (std::memory_order_acquire)) return imp;
        return wavetableBank.getTable (presetIdx);
    }

    const tw::Wavetable* wavetableForOsc (int osc, MorphSlot& slot, int presetIdx) noexcept
    {
        osc = juce::jlimit (0, 3, osc);
        auto* imp = importSlot_[(size_t) osc].live.load (std::memory_order_acquire);
        // fb253 — the morph now applies to the IMPORT too (rebuildMorphIfNeeded sources its spec from the
        // loaded table). A live+ready morph wins (morph-of-import OR morph-of-preset); otherwise fall back to
        // the RAW import if one is loaded (NEVER the factory bank — that was the wrong sound), else the bank.
        // (Inlines resolveMorphTable's race-hardened read so the fallback can be the import, not the bank.)
        if (auto* m = slot.live.load (std::memory_order_acquire))
        {
            const int idx = (m == &slot.buf[1]) ? 1 : 0;
            if (slot.ready[idx].load (std::memory_order_acquire))
            {
                slot.audioReadingIdx.store (idx, std::memory_order_release);
                return m;
            }
        }
        slot.audioReadingIdx.store (-1, std::memory_order_release);
        if (imp != nullptr) return imp;
        // fb496 — the bank builds its factory tables lazily now (see WavetableBank.h).
        // prepareToPlay and setStateInformation prefetch all four osc presets, so the
        // ONLY way to arrive here on an unbuilt table is a preset changed while audio
        // runs, and then only until the next 60 Hz tick. Rather than take the bank's
        // Sine fallback for those few ms, hold the table this osc was ALREADY playing —
        // so the change lands one tick late instead of blipping to a sine.
        // bankLastGood_ is written ONLY here, and wavetableForOsc is called only from
        // processBlock, so it is plain audio-thread-local state (no atomic needed).
        if (const auto* built = wavetableBank.getTableIfBuilt (presetIdx))
        {
            bankLastGood_[(size_t) osc] = built;
            return built;
        }
        if (bankLastGood_[(size_t) osc] != nullptr) return bankLastGood_[(size_t) osc];
        return wavetableBank.getTable (presetIdx);   // first block ever: Sine, never null
    }

    // Audio-thread-only: the last built bank table each osc actually used. See above.
    const tw::Wavetable* bankLastGood_[4] { nullptr, nullptr, nullptr, nullptr };

    /** fb496 — build the factory tables the four oscs currently point at.
        MESSAGE THREAD ONLY (2.5 ms per table it actually has to build).
        `budget` caps how many tables one call may build: prepareToPlay and
        setStateInformation pass 4 (neither is realtime, and a preset load must be
        complete before the first block); timerCallback passes 1 so a tick can never
        cost more than the morph rebuild that already lives in it. Returns how many
        it built. */
    int prefetchOscWavetables (int budget);

    // ── GEODE resynthesis analysis (Engine::SPEC) ────────────────────────
    // Heavy STFT peak-track analysis runs on the message thread (timerCallback) into a
    // double buffer and atomic-publishes a const store pointer to the voices (MorphSlot-
    // style). Source = the osc's loaded sample; if none, a default harmonic store so the
    // engine always sounds. Change-gated on the source buffer + engine + wavetable preset.
    struct GeodeSlot
    {
        tw::GeodeFrameStore                     buf[2];
        std::atomic<const tw::GeodeFrameStore*> live { nullptr };
        int         buildIdx      = 0;
        bool        built         = false;
        const void* builtSample   = nullptr;
        int         builtEngine   = -1;
        int         builtWtPreset = -999;
    };
    GeodeSlot geodeSlot_[4];
    void rebuildGeodeIfNeeded (int osc);                 // message thread
    // fb498 — MODAL's lazy arm. Message thread only (timerCallback + prepareToPlay). See the
    // definition in PluginProcessor.cpp for why 1,152 MiB used to be spent in the constructor.
    void prepareModalEnginesIfNeeded();
    void prepareHarmonicEnginesIfNeeded();   // fb517 — HARM's clone of the modal arm (~65 MB/instance)
    // ══ fb528 — THE PREPARE LOCK ═══════════════════════════════════════════════════════════
    //  prepareToPlay IS NOT A MESSAGE-THREAD CALLBACK. JUCE's AU wrapper runs it on whatever
    //  thread calls AudioUnitInitialize/AudioUnitReset (juce_audio_plugin_client_AU_1.mm:274) —
    //  in pluginval that is a background test thread, in a host it is host-dependent. Every
    //  "MESSAGE THREAD ONLY (timerCallback and prepareToPlay)" comment in this file was therefore
    //  asserting something nothing guarantees, and the 60 Hz timer and prepareToPlay ran the SAME
    //  lazy arms on the SAME objects at the same time: two std::vector<float>::assign()s on one
    //  vector, one thread's __vdeallocate() nulling __begin_ while the other wrote through it →
    //  EXC_BAD_ACCESS (address=0x0) on a non-main thread. ThreadSanitizer named both stacks
    //  (HarmonicEngine.h:103 from PluginProcessor.cpp:6859 vs :1256).
    //
    //  EVERY non-realtime path that BUILDS or RESIZES shared engine state takes this lock:
    //  prepareToPlay, timerCallback, and createEditor's export-ring arm.
    //  🚨 THE AUDIO THREAD NEVER TAKES IT, and must never be made to. processBlock only reads
    //  the acquire-published flags/pointers those builders store LAST (harmReady_/modalReady_,
    //  StemBuffer::totalSize, the pooled-engine pointers), so this is mutual exclusion between
    //  NON-RT threads only — no lock, no allocation and no blocking on the render thread. It is
    //  WavetableBank::ensureBuilt's pattern (acquire fast path + a mutex the audio thread never
    //  touches, WavetableBank.h:104-110) hoisted one level, to the whole arm.
    std::mutex prepLock_;
    // Shared real-time ceiling on TOTAL active partials across ALL SPEC voices/unison in this
    // instance. Additive resynth costs ~1 sine-osc per partial per sample; 3072 pegged a core
    // (measured ~40%% for the oscillator alone, and STRETCH pinned it there). 640 ≈ <10%% worst
    // case and thins gracefully (keepLoudest) under heavy chords. (rs2 CPU-cliff fix.)
    static constexpr int kGeodePartialBudget = 640;      // all SPEC + HARM voices/unison combined
    int geodePartialsLive_ = 0;                          // audio-thread only (no atomics)
    // HARM-ENGINE — per-osc knob snapshots for the editor's display banks (written in the
    // processBlock gather; read on the message thread — cosmetic, tear-tolerant)
    std::array<tw::HarmParams, 4> harmDisplayParams_ {};

    void timerCallback() override;        // message thread — rebuilds morph tables
    void rebuildMorphIfNeeded (MorphSlot& slot, int oscIdx,
                               const juce::String& presetId,
                               const juce::String& modeId,
                               const juce::String& amtId,
                               const juce::String& loId,
                               const juce::String& hiId);
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
    // PEROSC-BUFFERS — dedicated per-OSC buffers/loaders/payloads/paths (one loader each so
    // rapid drops on A..D don't cancel one another). Guarded by samplePayloadLock for payloads.
    std::array<tw::SampleBuffer, 4>           oscSampleBuffers_;
    tw::SampleBuffer                          noiseSampleBuffer_;   // NOISE IMPORT (P5) — shared looping-sample noise source
    juce::String                              noiseSampleSelJson_;  // NOISE IMPORT (P5c) — persisted selection (factory path / user audio)
    double                                    noiseFreePos_ = 0.0;  // fb66 — NOISE Free-mode global tape playhead (samples; audio thread)
    int                                       noiseVizMode_ = 1;    // fb66 — NOISE viz choice (1 particle · 2 waveform), persisted
    std::atomic<int> noiseAuditionReq_ { 0 };                       // NOISE AUDITION — bumped (msg thread) to trigger a headphone preview
    int    noiseAudSeen_ = 0, noiseAudCtr_ = 0, noiseAudTotal_ = 0; // audio thread: last-seen trigger + samples remaining + one-shot length
    int    noiseAudType_ = 0;                                       // -1 = sample one-shot, else the algorithmic type index being previewed
    double noiseAudPos_ = 0.0;
    NoisePreviewGen noisePrevGen_;                                  // faithful 13-type generator for previewing algorithmic noise (keyless)
    // WAVETABLE AUDITION — headphone preview of an osc's current table: one-shot plucked note at a fixed pitch,
    // slow frame-scan so you hear the whole table. Reads the table exactly like the voice/viz (import slot else bank).
    std::atomic<int> wtAuditionReq_ { 0 };
    std::atomic<int> wtAudReqOsc_ { 0 };
    int    wtAudSeen_ = 0, wtAudCtr_ = 0, wtAudTotal_ = 0, wtAudOsc_ = 0;
    double wtAudPhase_ = 0.0, wtAudInc_ = 0.0;
    std::atomic<bool> previewStop_ { false };                       // set by msg thread → audio thread kills the one-shot
    // PREVIEW DECLICK (fb62) — a short retrigger fade-out so scanning through sounds doesn't cut/click, and a HELD
    // buffer so the noise preview stays on the sound it started (the shared buffer swaps under it → "bleeds over").
    tw::SampleBuffer::BufferPtr noiseAudHeld_;                       // buffer captured at trigger → stable preview
    double noiseAudRatio_  = 1.0;                                    // captured resample ratio for the held buffer
    int    noiseAudFade_   = 0, noiseAudFadeLen_ = 0;               // retrigger fade-out (samples left / length)
    bool   noiseAudPending_ = false;                                // a new preview queued to start after the fade-out
    int    wtAudFade_ = 0, wtAudFadeLen_ = 0;                        // WT retrigger fade-out
    bool   wtAudPending_ = false;
    // fb74 — SAMPLE AUDITION (browser headphone preview): one-shot of an osc's current sample buffer,
    // held at trigger (stable during a scan), same retrigger declick as the noise preview.
    std::atomic<int> sampAuditionReq_ { 0 };
    std::atomic<int> sampAudReqOsc_ { 0 };
    int    sampAudSeen_ = 0, sampAudCtr_ = 0, sampAudTotal_ = 0, sampAudOsc_ = 0;
    double sampAudPos_ = 0.0, sampAudRatio_ = 1.0;
    tw::SampleBuffer::BufferPtr sampAudHeld_;
    int    sampAudFade_ = 0, sampAudFadeLen_ = 0;
    bool   sampAudPending_ = false;
    // IMPORTS REGISTRY (fb60, 3-way fb74) — msg-thread-only path lists (reference-in-place); [0]=noise, [1]=wavetable, [2]=sample
    juce::StringArray importFiles_[3], importFolders_[3];
    std::array<tw::SampleLoader, 4>           oscSampleLoaders_;
    std::array<juce::String, 4>               cachedOscPayloads_;
    std::array<juce::String, 4>               oscSourcePaths_;
    std::array<std::array<juce::String, 2>, 4> blendSrcPaths_;   // BLEND source pair (A/B) per osc

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
    // fb352 — instance 1's engines as a set, so it shares applyRvbTypeParams with the pool.
    // Defined out-of-line in the .cpp (RvbEngineSet is file-local there).
    struct RvbEngineSet rvbEngineSet1() noexcept;
    SpaceReverb spaceReverb;
    HallReverb  hallReverb;                       // fb276/277 — synth FX-rack reverb (Hall). Gated additive send, click-free.
    RoomReverb  roomReverb;                       // fb281 — Room reverb (early reflections + short dense tail)
    PlateReverb plateReverb;                      // fb282 — Plate reverb (Dattorro figure-8 tank + dispersion)
    SpringReverb springReverb;                    // fb284 — Spring reverb (dispersive allpass boing loop)
    DigitalReverb digitalReverb;                  // fb285 — Digital reverb (Lexicon 224 cross-coupled tank + random chorus)
    VintageReverb vintageReverb;                  // fb288 — Vintage reverb (80s digital rack: SR-reduction + grit + drive + gated/reverse shapes)
    BasinReverb  basinReverb;                      // fb289 — Basin reverb (huge dark ambient wash: Hall FDN retuned + bass-safe crossover + deep motion)
    ShimmerReverb shimmerReverb;                   // fb290 — Shimmer reverb (ethereal octave wash: Hall FDN + granular pitch-shifter in the feedback loop)
    ConvolutionReverb convolutionReverb;           // fb291 — Convolution reverb (true FFT partitioned convolution + synth/user IR + Reverse/Attack/Distance/Density)
    // fb359 — PER-INSTANCE user IR. Index 0 = Reverb 1, 1..5 = the pool. This was single-instance,
    // so a duplicate reverb set to Convolution could never be given its own IR (it ran the synthetic
    // Space) and dropping a file onto it silently retargeted instance 1. The last multi-instance gap.
    static constexpr int kConvSlots = ParameterIDs::kFxInstances;
    std::array<juce::String, (size_t) kConvSlots>       convIRName_ {};   // "" ⇒ synthetic factory Space
    std::array<bool, (size_t) kConvSlots>               convIRUser_ {};   // true ⇒ a user IR is loaded
    std::array<std::vector<float>, (size_t) kConvSlots> convUserIrL_ {}, convUserIrR_ {};   // fb311 — retained raw IR
    ConvolutionReverb* convEngineFor (int inst) noexcept                  // 1 = the resident engine, 2..6 = pooled (may be null)
    { return (inst <= 1) ? &convolutionReverb
                         : (((unsigned) (inst - 2) < (unsigned) kFxExtra) ? rvbPool_[(size_t) (inst - 2)].conv.get() : nullptr); }
    static int convSlot (int inst) noexcept { return juce::jlimit (0, kConvSlots - 1, inst - 1); }
    int   activeRvbType_ = -1;                    // 0=Hall 1=Room 2=Plate 3=Spring 4=Digital (live engine); -1 = uninitialised
    bool  rvbSwapping_ = false;                   // type change in progress → wet dips through 0 (click-free swap)
    bool  hallRouteActive_ = false;               // any A/B/C/D/S/N route enabled this block (PILLS ⇒ per-osc send)
    bool  hallPower_    = false;                   // fb303 — SYN_RVB_POWER on ⇒ reverb runs (main-send OR per-osc)
    float hallRvbEnv_ = 0.0f, hallEnvT_ = 0.0f;   // fb277 — on/off FADE env (0 = fully bypassed) so route toggles don't click
    float hallRvbDry_ = 1.0f, hallRvbWet_ = 0.0f; // equal-power mix — RAMPED per sample (no zipper)
    float hallRvbDryT_ = 1.0f, hallRvbWetT_ = 0.0f; // mix targets
    float hallSm_ = 0.0015f;                       // per-sample smoothing coeff (~15 ms), set in prepareToPlay
    // fb280 — PER-OSC NO-BLEED SEND: the synth voices accumulate ONLY the routed oscillators
    // (A/B/C/D/Sub/Noise) into this bus during render; the master loop reverbs it and adds the wet
    // back (true wet/dry on the routed portion), so unrouted oscs stay bone dry.
    juce::AudioBuffer<float> reverbSendBuf_;
    float hallRvbG_[6] = { 0,0,0,0,0,0 };          // per-source route gains this block (A,B,C,D,Sub,Noise)
    // fb280 — audio-reactive BLOOM: smoothed wet level published to the UI (core breathes with the tail).
    std::atomic<float> hallBloomViz_ { 0.0f };
    float hallBloomEnv_ = 0.0f;                     // fast-attack / slow-release wet envelope
    // fb287 — DUCK (Room/Spring 2nd pill): a ducking reverb pulls the WET down while the routed dry input
    // is present and blooms it back in the gaps. Env-follows the send level; gain = 1/(1+k·env) (dynamic,
    // self-normalizing). Fast attack, slow release. Only engages when the Duck pill is on for Room/Spring.
    float duckEnv_ = 0.0f, duckAtkCoef_ = 0.0f, duckRelCoef_ = 0.0f;
    bool  rvbDuckActive_ = false;                   // resolved per block (Duck pill on AND type is Room/Spring)

    // ── fb296 — FX-RACK DELAY: parallel per-osc send, mirrors the reverb above. One shared DelayEngine
    // (4 characters). Its own route pills + send bus; the wet is added back with the same Mix-100%-wet
    // duck term so the routed dry is fully cancelled at Mix=1. Power gates routing exactly like the reverb.
    DelayEngine delayEngine;
    tw::DistortionEngine distortionEngine;         // fb315 — FX-rack Distortion (POWER default OFF ⇒ dry init)
    // ── fb346 — THE INSTANCE POOL (the dynamic chain). Instance 1 is the member above; these are
    //    instances 2..kFxInstances, pre-allocated because the audio thread may never allocate and the
    //    host caches the param list at load. An UNCLAIMED instance costs only its (small) engine
    //    state — no processing whatsoever: the chain loop skips any instance whose _ACTIVE is false,
    //    which is the "an empty slot costs exactly zero" law Max set when he ruled the pool generous.
    //    Delay + Distortion pool first because each is ONE engine object; the Reverb device is SIX
    //    (Space/Hall/Digital/Basin/Shimmer/Convolution) and would cost 6x per slot, so its extra
    //    instances get lazy construction in the next pass instead of eager allocation here.
    static constexpr int kFxExtra = ParameterIDs::kFxInstances - 1;
    // fb352 — 5 delay + 5 distortion + 5 reverb · fb362 — + ALL SIX granular.
    // ⚠️ Granular puts all 6 instances in this pool, instance 1 included, where the other three
    // devices keep instance 1 on dedicated members and pool only 2..6. That is deliberate: it is
    // the fb350 POOL LAW applied structurally. One code path for every instance means a per-block
    // engine call can never exist for instance 1 and not for a duplicate — the bug class simply
    // cannot be written here. Granular is new, so there is no historical instance-1 ID to protect.
    static constexpr int kGrnSendBase  = 15;
    static constexpr int kTpeSendBase  = kGrnSendBase + ParameterIDs::kFxInstances;    // 21 — fb365 tape
    static constexpr int kFltSendBase  = kTpeSendBase + ParameterIDs::kFxInstances;    // 27 — fb377 filter
    static_assert (kFltSendBase + ParameterIDs::kFxInstances
                   <= tw::SynthVoice::kPoolSends, "pool send bases outgrew kPoolSends");
    // 🔑 fb377 — THE FOURTH CONSTANT. This sizes poolSendBuf_ / poolRouteAny_ / poolRouteG_ /
    // poolEntryG_, so it must cover the LAST send base, not the second-to-last. Adding the filter
    // moved kPoolSends (SynthVoice) and introduced kFltSendBase, and this one was missed: it still
    // read kTpeSendBase + 6 = 27 while the filter wrote 27..32 — six slots past the end of four
    // arrays. auval came back 139 (SIGSEGV) and pluginval segfaulted 2 of 3. The memory note says
    // these constants "move TOGETHER"; that is only enforceable if it is asserted, so it is now.
    // fb413 — three more device kinds. THE FOUR CONSTANTS MOVE TOGETHER: these bases,
    // kPoolSendCount below, tw::SynthVoice::kPoolSends, and kFxKinds. The static_asserts are
    // what make "together" enforceable rather than a note somebody has to remember.
    static constexpr int kChoSendBase  = kFltSendBase + ParameterIDs::kFxInstances;    // 33 — chorus
    static constexpr int kFlaSendBase  = kChoSendBase + ParameterIDs::kFxInstances;    // 39 — flanger
    static constexpr int kPhaSendBase  = kFlaSendBase + ParameterIDs::kFxInstances;    // 45 — phaser
    static_assert (kPhaSendBase + ParameterIDs::kFxInstances
                   <= tw::SynthVoice::kPoolSends, "pool send bases outgrew kPoolSends");
    // fb426 — the fx4 four. THE FOUR CONSTANTS STILL MOVE TOGETHER: these bases, kPoolSendCount
    // below, tw::SynthVoice::kPoolSends, and kFxKinds. The static_asserts are what make "together"
    // enforceable rather than a note somebody has to remember — fb391 missed one and auval came
    // back 139 (SIGSEGV), six slots past the end of four pool arrays.
    static constexpr int kEqzSendBase  = kPhaSendBase + ParameterIDs::kFxInstances;    // 51 — equalizer
    static constexpr int kWidSendBase  = kEqzSendBase + ParameterIDs::kFxInstances;    // 57 — widen
    static constexpr int kCmpSendBase  = kWidSendBase + ParameterIDs::kFxInstances;    // 63 — compress
    static constexpr int kOttSendBase  = kCmpSendBase + ParameterIDs::kFxInstances;    // 69 — ott
    static_assert (kOttSendBase + ParameterIDs::kFxInstances
                   <= tw::SynthVoice::kPoolSends, "pool send bases outgrew kPoolSends");
    // fb444 — the last three kinds. THE FOUR CONSTANTS MOVE TOGETHER, and this time three
    // devices land at once: each of the three scouts independently derived "sendBase = 75",
    // which is correct for whichever device is the ONLY new one. Three of them stacked is
    // 75 / 81 / 87, and writing 75 three times is fb391 again — green build, six slots past
    // the end of four pool arrays, auval 139.
    static constexpr int kBodSendBase  = kOttSendBase + ParameterIDs::kFxInstances;    // 75 — bode
    static constexpr int kUtlSendBase  = kBodSendBase + ParameterIDs::kFxInstances;    // 81 — utility
    static constexpr int kSplSendBase  = kUtlSendBase + ParameterIDs::kFxInstances;    // 87 — splitter
    static_assert (kSplSendBase + ParameterIDs::kFxInstances
                   <= tw::SynthVoice::kPoolSends, "pool send bases outgrew kPoolSends");
    static constexpr int kPoolSendCount = kSplSendBase + ParameterIDs::kFxInstances;   // 93
    static_assert (kPoolSendCount >= kSplSendBase + ParameterIDs::kFxInstances,
                   "kPoolSendCount must cover the LAST send base + its instances");
    static_assert (kPoolSendCount <= tw::SynthVoice::kPoolSends,
                   "the voice's kPoolSends must cover every pool send the processor writes");
    std::array<DelayEngine, (size_t) kFxExtra>          delayPool_;
    std::array<tw::DistortionEngine, (size_t) kFxExtra> distPool_;
    // per-extra-instance runtime state, mirroring the instance-1 members below
    std::array<int,   (size_t) kFxExtra> poolDlyType_ {};    // active delay type per extra instance
    std::array<float, (size_t) kFxExtra> poolDlyEnv_  {};    // on/off fade env (click-free power)
    std::array<float, (size_t) kFxExtra> poolDstEnv_  {};
    // fb350 — per-pooled-delay type-swap fade (mirrors instance 1's dlySwapping_) + its OWN viz bloom.
    // The bloom is per instance because a duplicate's echo timeline must light from ITS audio; sharing
    // instance 1's scalar made delay 2's taps flash to delay 1 and read as "linked".
    std::array<bool,  (size_t) kFxExtra> poolDlySwap_ {};
    std::array<float, (size_t) kFxExtra> poolDlyBloomEnv_ {};
    std::array<std::atomic<float>, (size_t) kFxExtra> poolDlyBloomViz_ {};
    // ── the chain order. Rebuilt at the TOP of each processBlock from CACHED param pointers, so it
    //    is audio-thread safe by construction: no strings, no allocation, no lock, no race with the
    //    UI (the UI only writes _ACTIVE/_RANK params; the next block simply reads the new values).
    // kind: 0=Reverb 1=Delay 2=Distortion 3=Granular 4=Tape 5=Filter 6=Chorus 7=Flanger
    //       8=Phaser 9=Equalizer 10=Widen 11=Compress 12=Multiband
    //       13=Bode 14=Utility 15=Splitter
    struct ChainEntry { int kind; int inst; float rank; };
    // fb375 — one slot per addable device, so the rack can never silently drop one. This had been
    // stale since fb365: it read `4 * kFxInstances` (=24) with a "6 of each of 4 devices" comment
    // while FIVE kinds shipped, so the 25th..30th device a user added hit the `>= kChainMax` guard
    // at :4392/:4417 and vanished with no message. The guard fails safe (no overflow — that was
    // checked) but a silently-dropped device reads as "the rack is broken". Derive it from the kind
    // count instead of hand-maintaining a number: 6 kinds x 6 instances, ~432 bytes.
    static constexpr int kFxKinds  = 16;     // fb413 — + chorus 6, flanger 7, phaser 8 · fb426 — + equalizer 9, widen 10, compress 11, ott 12 · fb444 — + bode 13, utility 14, splitter 15
    static constexpr int kChainMax = kFxKinds * ParameterIDs::kFxInstances;    // 96 (16 x 6)
    static_assert (kChainMax <= tw::FxChainTopology::kMaxSlots,
                   "every activatable device must fit in the topology's slot table");
    std::array<ChainEntry, (size_t) kChainMax> chainOrder_ {};
    int chainCount_ = 0;
    // fb351 — THE RACK IS SERIAL AGAIN. fb348 gave every device its own per-osc send bus and had
    // each ADD its result into the output, which made the whole rack PARALLEL: chain order stopped
    // meaning anything (Max: "I don't think the distortion is in the chain… it doesn't do any of
    // that"). This works out, per block, which device TAPS which oscillator and whose output feeds
    // whom. See FxChainTopology.h for the model + scratchpad/fx_topology_test.cpp for the proof.
    tw::FxChainTopology fxTopo_;
    // ══ fb495 — THE ROUTE-BROADCAST CACHE ═════════════════════════════════════════════════════
    // The broadcast in processBlock writes route state into all 96 voices x 93 pool sends on
    // EVERY call: 17,856 setter calls and ~90,000 memory writes across 1.36 MB of state, measured
    // at 16.06 us per block = 1.57% of a core at FL Studio's ~980 calls/s, and 2-4x that on
    // Windows where the L2 is smaller. It only ever needs to run when something it writes has
    // CHANGED. This mirrors every input it reads -- the write POINTERS included, because the
    // buffers are resized just above it and a missed resize would leave voices holding stale
    // write pointers, which is a use-after-free rather than a glitch.
    bool   poolPushValid_ = false;
    float  lastHallEntryG_[6] {}, lastDlyEntryG_[6] {}, lastDstEntryG_[6] {}, lastExUnionG_[6] {};
    float  lastPoolEntryG_[kPoolSendCount * 6] {};
    bool   lastPoolRouteAny_[kPoolSendCount] {};
    float* lastPoolPtrL_[kPoolSendCount] {};
    float* lastPoolPtrR_[kPoolSendCount] {};
    float* lastRsL_ = nullptr; float* lastRsR_ = nullptr;
    float* lastDsL_ = nullptr; float* lastDsR_ = nullptr;
    float* lastDtL_ = nullptr; float* lastDtR_ = nullptr;
    float* lastExL_ = nullptr; float* lastExR_ = nullptr;

    float hallEntryG_[6] { 0,0,0,0,0,0 };            // ENTRY masks — a source enters the rack exactly
    float dlyEntryG_ [6] { 0,0,0,0,0,0 };            // once, at the FIRST device routed to it. These
    float dstEntryG_ [6] { 0,0,0,0,0,0 };            // (not the full route masks) are what the voices
    std::array<float, (size_t) kPoolSendCount * 6> poolEntryG_ {};   // tap.
    void rebuildChainOrder() noexcept;                       // audio-thread safe (cached pointers only)

    // ── cached parameter pointers for the pooled instances (resolved once in prepareToPlay, where
    //    building juce::Strings is legal). The audio thread NEVER builds an ID string.
    struct DlyRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr; std::atomic<float>* syncdiv;
        std::atomic<float>* syncdivR; std::atomic<float>* time; std::atomic<float>* timeR;
        std::atomic<float>* fb; std::atomic<float>* tone; std::atomic<float>* mix;
        std::atomic<float>* lowcut; std::atomic<float>* hicut; std::atomic<float>* spread;
        std::atomic<float>* width; std::atomic<float>* modrate; std::atomic<float>* moddepth;
        std::atomic<float>* wow; std::atomic<float>* duck; std::atomic<float>* sync;
        std::atomic<float>* link; std::atomic<float>* ping; std::atomic<float>* hq;
        std::atomic<float>* src[6]; };   // fb348 — per-osc route pills (A,B,C,D,Sub,Noise)
    // fb352 — POOLED REVERB (instances 2..6). Params are eager (they must be: the host caches the
    //   list at load), ENGINES are lazy — an instance builds only the ONE engine its current type
    //   needs, on the MESSAGE THREAD (timerCallback), never on the audio thread. That is what makes
    //   6 reverbs affordable: eager would be 9 engines x 5 instances, with Convolution among them.
    struct RvbRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr; std::atomic<float>* modmode;
        std::atomic<float>* size; std::atomic<float>* decay; std::atomic<float>* tone;
        std::atomic<float>* mix; std::atomic<float>* predelay; std::atomic<float>* diffuse;
        std::atomic<float>* moddepth; std::atomic<float>* modrate; std::atomic<float>* hidamp;
        std::atomic<float>* lowdecay; std::atomic<float>* lowcut; std::atomic<float>* width;
        std::atomic<float>* mod; std::atomic<float>* freeze; std::atomic<float>* duck;
        std::atomic<float>* src[6]; };
    struct PoolRvbEngines
    {
        std::unique_ptr<HallReverb>        hall;     std::unique_ptr<RoomReverb>    room;
        std::unique_ptr<PlateReverb>       plate;    std::unique_ptr<SpringReverb>  spring;
        std::unique_ptr<DigitalReverb>     digital;  std::unique_ptr<VintageReverb> vintage;
        std::unique_ptr<BasinReverb>       basin;    std::unique_ptr<ShimmerReverb> shimmer;
        std::unique_ptr<ConvolutionReverb> conv;
    };
    std::array<RvbRefs, (size_t) kFxExtra>         rvbRefs_ {};
    std::array<PoolRvbEngines, (size_t) kFxExtra>  rvbPool_;
    // fb528 — the same publication edge for the nine lazily-built pooled reverbs, one bit per
    // type (0 Hall … 8 Convolution). buildPendingReverbEngines sets the bit with a RELEASE
    // fetch_or after the engine is built AND prepared; rvbEngineSetPool acquire-loads the mask
    // and only then reads the matching unique_ptr, so the audio thread can never see a pointer
    // without also seeing the engine behind it. Nine plain `.get()`s had no such edge.
    std::array<std::atomic<juce::uint32>, (size_t) kFxExtra> rvbBuilt_ {};
    std::array<int,   (size_t) kFxExtra> poolRvbType_ {};      // adopted type (-1 = not adopted yet)
    std::array<bool,  (size_t) kFxExtra> poolRvbSwap_ {};      // type-swap fade in progress
    std::array<float, (size_t) kFxExtra> poolRvbEnv_  {};      // on/off fade env
    std::array<float, (size_t) kFxExtra> poolRvbDry_  {};      // ramped mix (equal-power)
    std::array<float, (size_t) kFxExtra> poolRvbWet_  {};
    // fb358 — per-instance DUCK (Room/Spring's 2nd pill). The pool-law diff caught this: instance 1
    // resolves rvbDuckActive_ and runs a duckEnv_ follower, the pool did neither, so a duplicate
    // Room/Spring had a DEAD Duck pill. Same shape as the fb350 delay bug.
    std::array<bool,  (size_t) kFxExtra> poolRvbDuckOn_ {};
    std::array<float, (size_t) kFxExtra> poolRvbDuckEnv_ {};

    // ══ fb362 — GRANULAR, all six instances uniform ═══════════════════════════════════════════
    // Engines are LAZY and built on the MESSAGE THREAD (the fb352 reverb pattern): the ring is
    // 8.4 MB per instance at 44.1/48 k (16.5 s rounds up to 2^20 samples, stereo float) and 16.8 MB
    // at 96 k, so allocating six eagerly would cost 50 MB for a rack that usually holds one. The
    // audio thread only ever publishes an int request and reads the resulting pointer; until the
    // engine exists the slot passes its input through untouched.
    struct GrnRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr; std::atomic<float>* key;
        std::atomic<float>* syncdiv; std::atomic<float>* density; std::atomic<float>* size;
        std::atomic<float>* decay; std::atomic<float>* mix; std::atomic<float>* scan;
        std::atomic<float>* window; std::atomic<float>* spray; std::atomic<float>* pitch;
        std::atomic<float>* detune; std::atomic<float>* shape; std::atomic<float>* width;
        std::atomic<float>* freeze; std::atomic<float>* freezePill; std::atomic<float>* sync;
        std::atomic<float>* src[6]; };
    std::array<GrnRefs, (size_t) ParameterIDs::kFxInstances> grnRefs_ {};
    std::array<std::unique_ptr<tw::GranularFxEngine>, (size_t) ParameterIDs::kFxInstances> grnPool_;
    // fb528 — THE PUBLICATION POINTER. A std::unique_ptr's stored pointer is a PLAIN object:
    // `grnPool_[i] = std::move (e)` on the message thread is an ordinary store and applyGrn's
    // `.get()` on the AUDIO thread an ordinary load, so nothing orders the 8.4 MB ring that
    // prepare() just built against the audio thread that starts running it — "publish LAST" is
    // a comment, not a fence, and arm64 is free to make the pointer visible first.
    // ThreadSanitizer caught exactly this pair (GranularFxEngine::recomputeDerived from
    // applyGrn/processBlock vs from buildPendingGranularEngines/timerCallback).
    // grnPool_ still OWNS the engine; THIS is what the audio thread reads, and the release
    // store in buildPendingGranularEngines is the publication its acquire load pairs with.
    std::array<std::atomic<tw::GranularFxEngine*>, (size_t) ParameterIDs::kFxInstances> grnLive_ {};
    std::array<std::atomic<bool>,  (size_t) ParameterIDs::kFxInstances> grnWantBuild_ {};   // audio→message
    std::array<float, (size_t) ParameterIDs::kFxInstances> grnEnv_ {};        // on/off fade (click-free)
    std::array<float, (size_t) ParameterIDs::kFxInstances> grnDry_ {};        // ramped equal-power mix
    std::array<float, (size_t) ParameterIDs::kFxInstances> grnWet_ {};
    std::array<int,   (size_t) ParameterIDs::kFxInstances> grnType_ {};       // adopted type (-1 = none)
    std::array<bool,  (size_t) ParameterIDs::kFxInstances> grnSwap_ {};       // type-swap fade running
    std::array<float, (size_t) ParameterIDs::kFxInstances> grnBloomEnv_ {};
    std::array<std::atomic<float>, (size_t) ParameterIDs::kFxInstances> grnBloomViz_ {};
    std::array<float, (size_t) ParameterIDs::kFxInstances> grnBlockPk_ {};   // wet peak this block
    void applyGrn (int inst0, float inL, float inR, float& outL, float& outR) noexcept;
    void buildPendingGranularEngines();      // MESSAGE THREAD ONLY (timerCallback)

    // fb365 — TAPE. Same shape as the granular pool above, for the same reasons: the
    // params are eager (a param can never be born at runtime), the ENGINES are lazy on
    // the message thread (one 8 s stereo loop is ~3 MB, and a rack usually holds one).
    struct TpeRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr; std::atomic<float>* heads;
        std::atomic<float>* syncdiv; std::atomic<float>* p1; std::atomic<float>* p2;
        std::atomic<float>* p3; std::atomic<float>* mix; std::atomic<float>* time;
        std::atomic<float>* repeats; std::atomic<float>* drive; std::atomic<float>* age;
        std::atomic<float>* flutter; std::atomic<float>* bump; std::atomic<float>* width;
        std::atomic<float>* duck; std::atomic<float>* sync; std::atomic<float>* delay;
        std::atomic<float>* sculpt; std::atomic<float>* weave; std::atomic<float>* tilt;
        std::atomic<float>* src[6]; };
    std::array<TpeRefs, (size_t) ParameterIDs::kFxInstances> tpeRefs_ {};
    std::array<std::unique_ptr<tw::TapeFxEngine>, (size_t) ParameterIDs::kFxInstances> tpePool_;
    std::array<std::atomic<tw::TapeFxEngine*>, (size_t) ParameterIDs::kFxInstances> tpeLive_ {};   // fb528 — see grnLive_
    std::array<std::atomic<bool>, (size_t) ParameterIDs::kFxInstances> tpeWantBuild_ {};
    std::array<float, (size_t) ParameterIDs::kFxInstances> tpeEnv_ {};    // power fade, click-free
    void applyTpe (int inst0, float inL, float inR, float& outL, float& outR) noexcept;

    // fb377 — FILTER, chain kind 5. Unlike granular/tape these engines are EAGER: a FilterSlot
    // holds coefficient state, not buffers (no 8 MB ring, no 4 MB loop), so six of them cost
    // almost nothing and the whole lazy-build + wantBuild handshake disappears with them.
    struct FltRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* engine; std::atomic<float>* chr;
        std::atomic<float>* cut; std::atomic<float>* res; std::atomic<float>* drive;
        std::atomic<float>* mix; std::atomic<float>* env; std::atomic<float>* track;
        std::atomic<float>* poles; std::atomic<float>* sense; std::atomic<float>* attack;
        std::atomic<float>* release; std::atomic<float>* rate; std::atomic<float>* sweep;
        std::atomic<float>* wide; std::atomic<float>* punch;
        std::atomic<float>* src[6]; };
    std::array<FltRefs, (size_t) ParameterIDs::kFxInstances> fltRefs_ {};
    std::array<tw::FilterFxEngine, (size_t) ParameterIDs::kFxInstances> fltPool_ {};
    std::array<float, (size_t) ParameterIDs::kFxInstances> fltEnv_ {};   // power fade, click-free
    void cacheFilterRefs();
    double fltPrepSr_ = 0.0;
    void applyFlt (int inst0, float inL, float inR, float& outL, float& outR) noexcept;

    // ═══ fb413 — CHORUS (6) · FLANGER (7) · PHASER (8) ═══════════════════════════════════════
    // All three are EAGER, like the filter and unlike granular/tape: the biggest of them holds a
    // few hundred ms of delay line, not an 8 MB grain ring or a 4 MB tape loop, so six instances
    // each cost far less than the lazy-build handshake would cost in complexity. One `apply`
    // routine per device, run by EVERY instance — the pool law made structural (fb350).
    // fb426 — ONE refs struct for all four fx4 devices. They share the CONTRACT §2 Params
    // shape (type · character · axis · 3 front + mix · b1..b8), so four near-identical structs
    // would be four places to get the same thing wrong. `pill1/pill2/sync` are per-device and
    // are NULL where the device has none — every read below is guarded.
    //   EQ       f1 Slant   f2 Air    f3 Amount    axis Focus    pills —
    //   Widen    f1 Amount  f2 Width  f3 Rate      axis Field    pills Retrig · Hear Mono · sync
    //   Compress f1 Push    f2 Ratio  f3 Lift      axis Detect   pill  Auto
    //   OTT      f1 Amount  f2 Chase  f3 Top Lift  axis Stereo   pill  Crest
    struct Fx4Refs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr; std::atomic<float>* axis;
        std::atomic<float>* f1; std::atomic<float>* f2; std::atomic<float>* f3;
        std::atomic<float>* mix; std::atomic<float>* b[8];
        std::atomic<float>* pill1 = nullptr; std::atomic<float>* pill2 = nullptr;
        std::atomic<float>* sync  = nullptr;
        std::atomic<float>* xsh[4] {};                                // fb470 — each free band's shape (Bell/Low Cut/High Cut/shelves)
        std::atomic<float>* x[8] {}; std::atomic<float>* xon[4] {};   // fb438 — the Equalizer's free bells (null on the other three)
        std::atomic<float>* q[8] {};                                  // fb441 — the Equalizer's per-band Q (4 roles + 4 free; null on the other three)
        std::atomic<float>* src[6]; };

    struct ChoRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr;
        std::atomic<float>* rate; std::atomic<float>* depth; std::atomic<float>* feedback;
        std::atomic<float>* mix; std::atomic<float>* time; std::atomic<float>* detune;
        std::atomic<float>* width; std::atomic<float>* flutter; std::atomic<float>* drift;
        std::atomic<float>* colour; std::atomic<float>* lowkeep; std::atomic<float>* phase;
        std::atomic<float>* sync; std::atomic<float>* wide;
        std::atomic<float>* motion;                       // fb418 — the back panel's 2nd dropdown
        std::atomic<float>* src[6]; };
    struct FlaRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr;
        std::atomic<float>* rate; std::atomic<float>* depth; std::atomic<float>* feedback;
        std::atomic<float>* mix; std::atomic<float>* manual; std::atomic<float>* spread;
        std::atomic<float>* width; std::atomic<float>* damping; std::atomic<float>* shape;
        std::atomic<float>* bounce; std::atomic<float>* tail; std::atomic<float>* lowcut;
        std::atomic<float>* sync; std::atomic<float>* invert;
        std::atomic<float>* route;                        // fb418 — where the loop is wired
        std::atomic<float>* src[6]; };
    struct PhaRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr;
        std::atomic<float>* rate; std::atomic<float>* depth; std::atomic<float>* feedback;
        std::atomic<float>* mix; std::atomic<float>* center; std::atomic<float>* stages;
        std::atomic<float>* spread; std::atomic<float>* stereo; std::atomic<float>* touch;
        std::atomic<float>* lag; std::atomic<float>* floorK; std::atomic<float>* color;
        std::atomic<float>* sync; std::atomic<float>* invert;
        std::atomic<float>* motion;                       // fb418 — LFO shape override
        std::atomic<float>* src[6]; };
    std::array<ChoRefs, (size_t) ParameterIDs::kFxInstances> choRefs_ {};
    std::array<FlaRefs, (size_t) ParameterIDs::kFxInstances> flaRefs_ {};
    std::array<PhaRefs, (size_t) ParameterIDs::kFxInstances> phaRefs_ {};
    std::array<tw::TerrainChorusFx,  (size_t) ParameterIDs::kFxInstances> choPool_ {};
    // fb426 — the fx4 four. Six instances each, routable · chainable · duplicatable.
    std::array<Fx4Refs, (size_t) ParameterIDs::kFxInstances> eqzRefs_ {}, widRefs_ {}, cmpRefs_ {}, ottRefs_ {};
    std::array<Fx4Refs, (size_t) ParameterIDs::kFxInstances> bodRefs_ {};   // fb444 — Bode reuses the fx4 shape
    std::array<tw::TerrainEqualizerFx, (size_t) ParameterIDs::kFxInstances> eqzPool_ {};
    std::array<tw::TerrainWidenFx,     (size_t) ParameterIDs::kFxInstances> widPool_ {};
    std::array<tw::TerrainCompressFx,  (size_t) ParameterIDs::kFxInstances> cmpPool_ {};
    std::array<tw::TerrainOttFx,       (size_t) ParameterIDs::kFxInstances> ottPool_ {};
    std::array<tw::TerrainBodeFx,      (size_t) ParameterIDs::kFxInstances> bodPool_ {};   // fb444
    std::array<tw::TerrainSplitterFx,  (size_t) ParameterIDs::kFxInstances> splPool_ {};   // fb444
    std::array<tw::TerrainUtilityFx,   (size_t) ParameterIDs::kFxInstances> utlPool_ {};   // fb444
    // Utility carries SIX pills (Flip L · Flip R · Trade · Sum · DC · Dim) where Fx4Refs has room
    // for three. Max asked for "a whole bunch of buttons" and that is the device — so it gets its
    // own refs shape rather than bending the shared one.
    struct UtlRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type;                                   // fb450 — Character / Wiring are gone
        std::atomic<float>* f1; std::atomic<float>* f2; std::atomic<float>* f3;
        std::atomic<float>* mix; std::atomic<float>* b[8];
        std::atomic<float>* pill[5]; std::atomic<float>* src[6]; };   // fb450 — five switches (the DC lamp is gone)
    std::array<UtlRefs, (size_t) ParameterIDs::kFxInstances> utlRefs_ {};
    std::array<float,   (size_t) ParameterIDs::kFxInstances> utlEnv_  {};
    void cacheUtlRefs();
    void applyUtl (int inst0, float inL, float inR, float& outL, float& outR) noexcept;
    // The Splitter's roster does not fit Fx4Refs: on top of the 2 choices, 3 front + Mix and the
    // back 8, it carries a LANE STRIP of 12 switches (mute/solo/flip per lane), which are glyphs
    // and not knobs (the switch law). So it gets its own refs struct rather than bending the
    // shared one out of shape for one device.
    struct SplRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* slope;
        std::atomic<float>* split; std::atomic<float>* balance; std::atomic<float>* spread;
        std::atomic<float>* mix; std::atomic<float>* b[8];
        std::atomic<float>* mute[4]; std::atomic<float>* solo[4]; std::atomic<float>* flip[4];
        std::atomic<float>* src[6]; };
    std::array<SplRefs, (size_t) ParameterIDs::kFxInstances> splRefs_ {};
    std::array<float,   (size_t) ParameterIDs::kFxInstances> splEnv_  {};
    void cacheSplRefs();
    // The Splitter is the ONE device that is not a one-in-one-out insert, so it cannot use
    // TW_FX4_APPLY. It splits at its own slot and merges after the whole chain has run.
    void applySplSplit (int inst0, float inL, float inR,
                        float laneL[4], float laneR[4]) noexcept;
    void applySplMerge (int inst0, const float laneL[4], const float laneR[4],
                        float& outL, float& outR) noexcept;
    void applyEqz (int inst0, float inL, float inR, float& outL, float& outR) noexcept;
    void applyWid (int inst0, float inL, float inR, float& outL, float& outR) noexcept;
    void applyCmp (int inst0, float inL, float inR, float& outL, float& outR) noexcept;
    void applyOtt (int inst0, float inL, float inR, float& outL, float& outR) noexcept;
    void applyBod (int inst0, float inL, float inR, float& outL, float& outR) noexcept;   // fb444
    std::array<tw::TerrainFlangerFx, (size_t) ParameterIDs::kFxInstances> flaPool_ {};
    std::array<tw::TerrainPhaserFx,  (size_t) ParameterIDs::kFxInstances> phaPool_ {};
    std::array<float, (size_t) ParameterIDs::kFxInstances> choEnv_ {}, flaEnv_ {}, phaEnv_ {};
    std::array<float, (size_t) ParameterIDs::kFxInstances> eqzEnv_ {}, widEnv_ {}, cmpEnv_ {}, ottEnv_ {};  // fb426
    std::array<float, (size_t) ParameterIDs::kFxInstances> bodEnv_ {};                                     // fb444
    // fb437 — change-gating for the two big fx4 viz arrays (EQ curve, compressor knee) + the keepalive tick
    std::array<float, (size_t) ParameterIDs::kFxInstances> eqzCurveSent_ {}, cmpKneeSent_ {};
    uint32_t fx4VizTick_ = 0;
    double fx3PrepSr_ = 0.0;
    // fb414 — SEND MODE, per device kind x instance. [kind][inst0]; nullptr reads as insert.
    // fb435 — sized off kFxKinds, not a literal. At 9 it silently excluded the fx4 four.
    std::atomic<float>* sendRef_[kFxKinds][(size_t) ParameterIDs::kFxInstances] {};
    // fb444 — THE LANE MAP. Which band of an upstream Splitter each device lives in, plus the
    // per-block resolution of that into slot indices. Resolved once per block in
    // resolveLanes(), read per sample by the serial chain — no strings, no allocation.
    std::atomic<float>* laneRef_[kFxKinds][(size_t) ParameterIDs::kFxInstances] {};
    static constexpr int kMaxLanes = 4;
    std::array<int,  (size_t) kChainMax> laneSplitter_ {};   // slot -> the Splitter slot above it, or -1
    std::array<int,  (size_t) kChainMax> laneIdx_      {};   // slot -> which lane (0-based), or -1
    std::array<int,  (size_t) kChainMax> lanePrev_     {};   // slot -> previous device in the SAME lane, or -1
    std::array<bool, (size_t) kChainMax> laneConsumed_ {};   // slot -> a later same-lane device eats me
    std::array<int,  (size_t) kChainMax> laneSplSlot_  {};   // Splitter slot -> its 0..5 buffer index, else -1
    bool laneClaimed_[(size_t) ParameterIDs::kFxInstances][(size_t) kMaxLanes] {};
    bool laneAny_ = false;                                   // fast bail: no Splitter in the chain
    std::array<int, (size_t) ParameterIDs::kFxInstances> splLanes_ {};   // live lane count per Splitter
    // The engine's contract is ONE mergeStereo per splitStereo, in the SAME sample slot, because
    // split stashes the phase-matched dry that merge needs for Mix and applies the per-lane trims
    // on the way back in. The lane devices run AFTER the Splitter's slot, so the merge cannot
    // happen at that slot — it happens once the chain loop has finished, from these.
    std::array<int, (size_t) ParameterIDs::kFxInstances> splSlotOf_ {};   // Splitter inst -> its chain slot, or -1
    int laneLast_[(size_t) ParameterIDs::kFxInstances][(size_t) kMaxLanes] {};   // lane -> its LAST device slot, or -1
    void resolveLanes() noexcept;
    static int poolBaseForKind (int kind) noexcept;   // fb446 — one switch over the send bases
    void cacheSendRefs();
    void cacheFx3Refs();
    void cacheFx4Refs();     // fb426 — equalizer / widen / compress / ott

    // ═══ fb453 — EVERY RACK KNOB IS A MODULATION DESTINATION ═════════════════════════════════
    // The generated dial→parameter map (Tools/gen_fx_mod_ids.py reads the UI's own DEV_TEMPLATES,
    // so the dial and its destination are authored in exactly ONE place). Included INSIDE the
    // class so the two tables are members, not a second pair of file-scope names.
    #include "fx_mod_ids.inc"
    // Resolved once on the message thread: the parameter behind every (kind, instance, knob).
    // nullptr = that device has no such dial — only the Filter's 8 back slots (fb384).
    // 🚨 EVERY bound comes from the SAME constant cacheFxModRefs() loops on. Hand-typed 16/6/12
    //    here would let a bumped constant walk this array off its end with no diagnostic, and this
    //    codebase has bumped exactly this class of constant before (kMaxSlots 96->128, fb444).
    std::atomic<float>* fxModRef_[wc::kFxModKinds][wc::kFxModInsts][wc::kFxModKnobs] {};
    static_assert (ParameterIDs::kFxInstances == wc::kFxModInsts,
                   "fb453 - the rack's instance count and the destination block's must agree: "
                   "fxModDest() reserves kFxModInsts slots per kind, and cacheFxModRefs() walks "
                   "one row per plugin instance. Bump one without the other and either the array "
                   "overruns or the top instances have no destinations.");
    int  fxModRefsResolved_ = 0;   // fb373 — how many of the 1,104 cells actually bound to a param
    void cacheFxModRefs();         // message thread only (builds ID strings)

    // ═══ fb457 — OVERPASS 1, "if it's modulated, it MOVES" ═══════════════════════════════════
    // A card draws its geometry from the UI's own knob model (DEVS), which cannot know that a
    // route moved the dial — so a modulated Bode/Widen/EQ dial sounded different and looked
    // frozen. fb453 already resolves the EFFECTIVE value of every routed dial each block; this
    // publishes it so the drawer can read what the ENGINE is using.
    //
    // Keyed by DESTINATION ID, not by pointer: the UI already computes that same integer in
    // fxModDest(core,inst,knob), so there is no new index convention to get wrong. The alias
    // (SYN_DLY_TIME is delay knob 0 AND knob 10) is why one pointer can publish TWO dests — the
    // sorted table keeps them adjacent and both are emitted with the slot's summed value.
    struct FxEffPair { const void* ptr; int dest; };
    std::vector<FxEffPair> fxEffByPtr_;            // sorted by ptr; built once in cacheFxModRefs()
    static constexpr int kFxEffMax = 256;
    std::atomic<int>   fxEffN_ { 0 };
    std::atomic<int>   fxEffDest_[kFxEffMax] {};
    std::atomic<float> fxEffVal_ [kFxEffMax] {};
    void publishFxModEff() noexcept;               // audio thread; allocation-free
    // The per-block sparse map, KEYED BY POINTER. See FxModValue.h for why that is the whole
    // answer to the SYN_DLY_TIME alias: two destinations, one parameter, one summed slot.
    wc::FxModAccum fxMod_;
    // fb453 — the modulated value for a rack parameter, matched BY POINTER so a read site can
    // never mis-map (there is no knob index here to get wrong). Zero routes = one branch.
    inline float M (std::atomic<float>* p) const noexcept
    {
       #ifdef FXMOD_MUT_NO_APPLY
        return p->load();          // MUTATION: the rack ignores the matrix entirely
       #endif
        return fxMod_.lookup ((const void*) p, p->load());
    }

    void pushFx3Params() noexcept;      // ONCE PER BLOCK — see the note in the .cpp
    void applyCho (int inst0, float inL, float inR, float& outL, float& outR) noexcept;
    void applyFla (int inst0, float inL, float inR, float& outL, float& outR) noexcept;
    void applyPha (int inst0, float inL, float inR, float& outL, float& outR) noexcept;

    void buildPendingTapeEngines();          // MESSAGE THREAD ONLY (timerCallback)
    std::array<float, (size_t) kFxExtra> poolRvbBloomEnv_ {};
    std::array<std::atomic<float>, (size_t) kFxExtra> poolRvbBloomViz_ {};
    std::array<std::atomic<int>,   (size_t) kFxExtra> rvbWantType_ {};   // audio→message: build me this
    double rvbPoolSr_ = 44100.0;
    void   buildPendingReverbEngines();      // MESSAGE THREAD ONLY (timerCallback) — allocates
    struct RvbEngineSet rvbEngineSetPool (int e) noexcept;
    struct DstRefs { std::atomic<float>* active; std::atomic<float>* rank; std::atomic<float>* power;
        std::atomic<float>* type; std::atomic<float>* chr; std::atomic<float>* qual;
        std::atomic<float>* drive; std::atomic<float>* sig; std::atomic<float>* tone;
        std::atomic<float>* mix; std::atomic<float>* autoP; std::atomic<float>* pill2;
        std::atomic<float>* p[8];
        std::atomic<float>* src[6]; };   // fb348 — per-osc route pills
    std::array<DlyRefs, (size_t) kFxExtra> dlyRefs_ {};
    std::array<DstRefs, (size_t) kFxExtra> dstRefs_ {};
    // instance-1 chain membership (the three shipped devices)
    std::atomic<float> *rvbActive_ = nullptr, *rvbRank_ = nullptr;
    std::atomic<float> *dlyActive_ = nullptr, *dlyRank_ = nullptr;
    std::atomic<float> *dstActive_ = nullptr, *dstRank_ = nullptr;
    void cacheFxInstanceParams();                            // message thread (builds ID strings)
    void cacheGranularParams();                              // fb362 — same contract, all 6 instances
    void cacheTapeParams();                                  // fb365 — ditto
    std::array<int, (size_t) kFxExtra> poolDstType_ {};       // active distortion mode per extra instance
    int   activeDlyType_ = -1;                      // 0=Digital 1=Tape 2=BBD 3=Diffuse; -1 = uninitialised
    bool  dlySwapping_ = false;                     // type change → wet dips through 0 (click-free swap)
    bool  dlyRouteActive_ = false;                  // any delay route enabled this block (PILLS ⇒ per-osc send)
    bool  dlyPower_    = false;                      // fb303 — SYN_DLY_POWER on ⇒ delay runs (main-send OR per-osc)
    int   fxPerm_ = 0;                               // fb341 — serial chain permutation 0-5 (legacy bool: norm 0→0 R·D·T, norm 1→5 D·R·T)
    float dlyEnv_ = 0.0f, dlyEnvT_ = 0.0f;          // on/off FADE env (0 = fully bypassed)
    float dlyDry_ = 1.0f, dlyWet_ = 0.0f;           // equal-power mix — RAMPED per sample
    float dlyDryT_ = 1.0f, dlyWetT_ = 0.0f;         // mix targets
    juce::AudioBuffer<float> delaySendBuf_;         // routed-osc send bus (parallel to reverbSendBuf_)
    float dlyG_[6] = { 0,0,0,0,0,0 };               // per-source delay route gains (A,B,C,D,Sub,Noise)
    std::atomic<float> dlyBloomViz_ { 0.0f };       // audio-reactive wet level for the delay core viz
    float dlyBloomEnv_ = 0.0f;

    // fb315 — DISTORTION (3rd FX device). TODAY: MAIN SEND ONLY — power ON ⇒ the whole mix runs
    // through it as a wet/dry insert, exactly like the reverb/delay main-send path. Power OFF ⇒ the
    // insert never runs ⇒ byte-identical default sound.
    // ⚠️ The per-osc route pills need their own send bus (distortionSendBuf_ + SynthVoice tap). That
    // lands next, and it MUST carry the fb305 fix in the same commit: PluginProcessor.cpp:6979/:7111
    // sum ONLY rvbSend+dlySend, so a third send bus silently re-breaks fb305 (an osc routed to the
    // distortion would get its bus AND the reverb main send). See bible §4.5.
    bool  dstPower_ = false;                         // SYN_DST_POWER (default OFF)
    juce::AudioBuffer<float> distortionSendBuf_;     // fb338 — routed-osc send bus (third, parallel to reverb/delay)
    // ── fb347 — THE SHARED ROUTED-DRY EXCLUSION BUS. Carries each routed osc EXACTLY ONCE (the
    //    union of every device's mask). Main-send devices subtract THIS instead of summing the
    //    per-device buses, which double-counted any osc routed to 2+ devices and handed the next
    //    device that osc phase-INVERTED. Also retires the fb305/fb338 landmine: there is no longer
    //    a per-bus sum a newly added device can forget to join — one bus, one subtraction, forever.
    juce::AudioBuffer<float> routedDryBuf_;
    float exUnionG_[6] { 0, 0, 0, 0, 0, 0 };         // union route mask (A,B,C,D,Sub,Noise)
    bool  exUnionAny_ = false;
    // ── fb348 — one send bus per POOLED instance (Delay 2..6 = 0..4, Distortion 2..6 = 5..9).
    //    NO GLOBAL SEND any more (Max): a device affects ONLY what it is routed to, so a delay on
    //    osc C can never touch osc A. An unrouted device is silent.
    std::array<juce::AudioBuffer<float>, (size_t) kPoolSendCount> poolSendBuf_;
    std::array<bool,  (size_t) kPoolSendCount> poolRouteAny_ {};
    std::array<float, (size_t) kPoolSendCount * 6> poolRouteG_ {};
    float dstG_[6] = { 0,0,0,0,0,0 };                // per-source distortion route gains (A,B,C,D,Sub,Noise)
    bool  dstRouteActive_ = false;                   // any distortion route enabled this block
    float dstEnv_ = 0.0f, dstEnvT_ = 0.0f;           // on/off FADE env (0 = fully bypassed, no click)
    float dstDry_ = 1.0f, dstWet_ = 0.0f;            // equal-power mix — RAMPED per sample
    float dstDryT_ = 1.0f, dstWetT_ = 0.0f;
    float dstBloomEnv_ = 0.0f;
    std::atomic<float> dstBloomViz_ { 0.0f };        // audio-reactive core (fb311 law: viz must be DRAMATIC)

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
