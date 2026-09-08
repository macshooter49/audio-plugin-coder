#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// FilterFxEngine — fb377. The FX-rack FILTER device: chain kind 5.
//
// THIS FILE WRITES NO FILTER DSP. `TerrainFilters.h` already ships 94 engines behind one
// stable `FilterSlot` façade, certified and in use by the synth panel since fb165. This is a
// HOSTING + MOTION engine: it puts one FilterSlot on the FX bus, calibrates its gain staging to
// the real bus level, and builds the three things that turn a static filter into an EFFECT —
// an envelope follower, a synced LFO, and key track.
//
// Max, 2026-08-17: "this needs to be JUST like our main 2 filters we have for the synth engine…
// back panel, needs to mirror the exact engines we already have, NO boxes of course. all
// buttons of course 0-100, real engines, real DSP, all different things."
//
// ── THE BOUNDARY vs the synth FILTER panel ───────────────────────────────────
// Per-voice filtering is the synth panel's job: two FilterSlots live inside every voice, so a
// chord's notes each sweep on their own envelope. Whole-bus filtering that MOVES with the
// program is THIS device's job: one stereo instance, and a chord ducks and opens together.
// That is the auto-wah / DJ-sweep sound the per-voice filters structurally cannot make.
//
// ── OVERSAMPLING — what the SHIPPED filter actually does, which is not what the bible assumed
// FILTER-BUILD-BIBLE.md §6.3 specs a fixed internal 2x on the Distortion's 129-tap halfband and
// flags its 64-sample latency as a MUST-RESOLVE (the wet leg combs against the dry at every
// intermediate Mix). Before building that, I read what the voice does with these same cores:
//
//     const double coefSr = oversample ? sr * 2.0 : sr;      // SynthVoice.h:4205
//     filterSlot_.setParams (lastCutHz1_, res1, drvSm1_, coefSr);
//
// and `processStereo` is called exactly ONCE per sample. So the shipped synth filter does NOT
// oversample its signal path at all — `needsOversampling()` only doubles the number handed to
// the coefficient math, which re-prewarps the ZDF cores as if they were running at 2x and takes
// most of the warping error out of the cutoff mapping.
//
// Max asked for "JUST like our main 2 filters", so this device does exactly that. The whole
// halfband goes away with it: no resampler, no 64-sample latency, no comb against the dry leg,
// no MUST-RESOLVE. Zero reported latency and zero lookahead hold by construction, not by
// compensation. (A hand-rolled polyphase halfband was written first and measured -24 dB at
// 1 kHz — the unity gate caught it. Deleted rather than debugged, because it should not exist.)
//
// PURE C++ against TerrainFilters.h + JUCE only for nothing at all — it offline-validates
// standalone, same contract as DelayEngine.h / FxChainTopology.h.
// Harness: Tests/flt_cert.cpp (committed).
// ─────────────────────────────────────────────────────────────────────────────
#include "TerrainFilters.h"

#include <cmath>
#include <algorithm>

namespace tw {

class FilterFxEngine
{
public:
    // ── the 20-entry sync list, cloned WHOLE from PluginProcessor.cpp:3458 including "Free" at
    //    index 0. A 19-entry list starting at "4 bar" is off by one and every saved Rate reads
    //    one division fast (the fb-filter audit caught exactly that in the bible's draft).
    static constexpr int kNumDivs = 20;
    static float divBeats (int i) noexcept
    {
        static const float B[kNumDivs] = { 0.0f, 16.0f, 8.0f, 4.0f, 2.0f, 3.0f, 1.3333f, 1.0f,
                                           1.5f, 0.6667f, 0.5f, 0.75f, 0.3333f, 0.25f, 0.375f,
                                           0.1667f, 0.125f, 0.0625f, 0.03125f, 0.015625f };
        return B[i < 0 ? 0 : (i >= kNumDivs ? kNumDivs - 1 : i)];
    }

    struct Params
    {
        int   engine   = 0;      // 0..93 — index INTO tw::filters::Type. The only engine selector.
        int   charIdx  = 0;      // 0..5  — post drive flavour: Tube/Diode/Fold/Hard/Crush/Fuzz
        float cut      = 0.62f;  // 0..1  -> cutKnobToHz
        float res      = 0.35f;
        float drive    = 0.0f;
        float mix      = 1.0f;
        float env      = 0.70f;  // BIPOLAR about 0.5 — the follower amount
        float track    = 0.0f;   // 0..1 -> 0..200 % keytrack
        float poles    = 1.0f;   // detents to 6/12/18/24 dB
        float sense    = 0.5f;
        float attack   = 0.41f;
        float release  = 0.48f;
        float rate     = 0.368f; // stepped over the 20-entry sync list
        float sweep    = 0.0f;   // LFO depth 0..±60 semis
        bool  wide     = false;  // stereo L/R cutoff split
        bool  punch    = false;  // follower becomes a transient detector
    };

    void prepare (double sampleRate, int /*maxBlock*/) noexcept
    {
        fs_ = (float) sampleRate;
        slot_.prepare (sampleRate);          // the HOST rate — the signal path is 1x, like the voice
        coefSr_ = fs_;
        reset();
    }

    void reset() noexcept
    {
        slot_.reset();
        env_ = 0.0f; envFast_ = 0.0f; envSlow_ = 0.0f; gate_ = 0.0f;
        cutSm_ = -1.0f; resSm_ = -1.0f; drvSm_ = -1.0f; keySm_ = 0.0f;
        lfoPh_ = 0.0f; dip_ = 1.0f; pendingType_ = -1;
        dcL_ = dcR_ = dcXL_ = dcXR_ = 0.0f;
        lvlSm_ = 0.0f;
        curType_ = -1;
    }

    void setTempo (double bpm, double ppq, bool playing) noexcept
    { bpm_ = (bpm > 1.0 ? (float) bpm : 120.0f); ppq_ = ppq; playing_ = playing; }

    void noteOn (int midiNote, float velocity) noexcept
    {
        lastNote_ = midiNote;
        // Karplus engines are plucked, not swept. ⚠️ FilterSlot::excite() guards on
        // Type::KARPLUS only, so KARPLUS_BRIGHT / KARPLUS_MUTE are silent no-ops here; the
        // guard has to widen in TerrainFilters.h before "Pluck" plucks. Documented, not hidden.
        slot_.excite (velocity);
    }

    // ── one stereo sample, in place. Everything above the FilterSlot is this engine's own work.
    inline void processSample (float& l, float& r, const Params& p) noexcept
    {
        // 1. ENGINE SWAP — setType() hard-resets every core (TerrainFilters.h:1426), so a bare
        //    call clicks. Dip to 2 % in ~2 ms, swap at the floor, recover over ~40 ms (fb345).
        if (p.engine != curType_ && pendingType_ != p.engine) pendingType_ = p.engine;
        if (pendingType_ >= 0)
        {
            dip_ += (0.02f - dip_) * dipDown_;
            if (dip_ < 0.05f)
            {
                slot_.setType ((filters::Type) pendingType_);
                curType_ = pendingType_; pendingType_ = -1;
                cutSm_ = -1.0f;                     // force a coefficient re-seat after the reset
                // the core declares its own budget; the voice honours it by doubling the number
                // it hands the coefficient math, and so do we (SynthVoice.h:4205).
                coefSr_ = slot_.needsOversampling() ? fs_ * 2.0f : fs_;
            }
        }
        else dip_ += (1.0f - dip_) * dipUp_;

        const float inL = l, inR = r;

        // 2. DETECTOR — stereo peak rectify, asymmetric one-pole. Punch turns it into a
        //    transient detector (fast minus slow), so the filter blips on attacks only.
        const float rect = std::max (std::fabs (inL), std::fabs (inR));

        // input level, for the card's brightness
        lvlSm_ += (rect > lvlSm_ ? 0.02f : 0.0015f) * (rect - lvlSm_);
        const float tA = 0.0002f * std::pow (1500.0f, p.attack);
        const float tR = 0.020f  * std::pow (100.0f,  p.release);
        const float aA = 1.0f - std::exp (-1.0f / (fs_ * tA));
        const float aR = 1.0f - std::exp (-1.0f / (fs_ * tR));
        env_ += (rect > env_ ? aA : aR) * (rect - env_);

        float det = env_;
        if (p.punch)
        {
            const float af = 1.0f - std::exp (-1.0f / (fs_ * 0.001f));
            const float as = 1.0f - std::exp (-1.0f / (fs_ * 0.050f));
            envFast_ += af * (rect - envFast_);
            envSlow_ += as * (rect - envSlow_);
            det = std::max (0.0f, envFast_ - envSlow_);
        }

        // 3. SENSITIVITY — +6..+48 dB on a soft knee, so it EVOLVES 0->100 and never plateaus.
        //    Calibrated to the measured -26 dBFS bus: at Sense 0.5 that lands at half sweep.
        const float gS  = std::pow (10.0f, (6.0f + 42.0f * p.sense) / 20.0f);
        const float lvl = det * gS;
        const float sweep01 = lvl / (1.0f + lvl);

        // 4. THE SYNCED LFO — phase derived from the host transport, never free-integrated
        //    against a glided rate (the fb345 one-clock law; accumulators integrate glide skew).
        const int   divIdx = (int) std::lround (p.rate * (kNumDivs - 1));
        const float beats  = divBeats (divIdx);
        float lfo = 0.0f;
        if (p.sweep > 0.0001f)
        {
            if (playing_ && beats > 0.0f) lfoPh_ = (float) std::fmod (ppq_ / (double) beats, 1.0);
            else
            {
                const float hz = (beats > 0.0f) ? (bpm_ / 60.0f) / beats : 0.7f;
                lfoPh_ += hz / fs_; if (lfoPh_ >= 1.0f) lfoPh_ -= 1.0f;
            }
            lfo = std::sin (6.2831853f * lfoPh_);
        }

        // 5. SUM IN SEMITONES, exactly the grammar the voice uses (SynthVoice.h:4195), then
        //    convert ONCE and clamp. ⚠️ the ceiling is min(20k, 0.45*fs_eff), never bare
        //    0.45*fs_eff: seven engines re-derive cut01 = log(cutHz/20)/log(1000) internally and
        //    saturate above 20 kHz, which flat-tops the top of the modulation range (a law-5 fail).
        const float keyTarget = p.track * 2.0f * (float) (lastNote_ - 60);
        keySm_ += (keyTarget - keySm_) * kt20ms_;

        const float baseSemis = 12.0f * std::log2 (filters::cutKnobToHz (p.cut) / 20.0f);
        const float envSemis  = ((p.env - 0.5f) * 2.0f) * 84.0f * sweep01;
        const float lfoSemis  = p.sweep * 60.0f * lfo;
        const float fmax = std::min (20000.0f, 0.45f * coefSr_);   // the in-tree law, SynthVoice.h:4311
        float cutHz = 20.0f * std::pow (2.0f, (baseSemis + envSemis + lfoSemis + keySm_) / 12.0f);
        cutHz = std::max (20.0f, std::min (fmax, cutHz));

        // 6. SELF-OSC GATE — nothing free-runs. Above res 0.90 the resonance is available only
        //    while the note feeds the bus; the gate is applied SQUARED (the fb345 grid-leak law),
        //    so 60 ms of one-pole release closes the audible whistle in ~0.4 s.
        const float ga = 1.0f - std::exp (-1.0f / (fs_ * 0.002f));
        const float gr = 1.0f - std::exp (-1.0f / (fs_ * 0.060f));
        gate_ += (rect > gate_ ? ga : gr) * (rect - gate_);
        const float g2  = std::min (1.0f, gate_ * 400.0f); const float gsq = g2 * g2;
        const float res = (p.res <= 0.90f) ? p.res : 0.90f + (p.res - 0.90f) * gsq;

        // 7. glide every continuous term (10-30 ms, the no-clicks law)
        if (cutSm_ < 0.0f) { cutSm_ = cutHz; resSm_ = res; drvSm_ = p.drive; }
        cutSm_ += (cutHz    - cutSm_) * kc10ms_;
        resSm_ += (res      - resSm_) * kc15ms_;
        drvSm_ += (p.drive  - drvSm_) * kc15ms_;

        slot_.setPoles ((int) std::lround (p.poles * 3.0f));
        slot_.setSpread (p.wide ? 0.5f : 0.0f);

        // 8. BUS LIFT — the FX bus program sits at ~-26 dBFS while the cores were voiced at
        //    about -12. +14 dB in, -14 dB out; at Drive 0 the pair nulls exactly.
        float wl = inL * kBusLift_, wr = inR * kBusLift_;

        // 9. the FilterSlot — one call per sample, coefficients prewarped at coefSr_
        slot_.setParams (cutSm_, resSm_, drvSm_, coefSr_);
        slot_.processStereo (wl, wr);

        wl *= kBusDrop_; wr *= kBusDrop_;

        // 10. post drive flavour — the shipped drive-type list, applied AFTER the filter exactly
        //     as SYN_FILTER1_DRIVETYPE does. At Drive 0 every flavour is bit-identical to unity.
        if (drvSm_ > 0.0005f) { wl = shape (wl, p.charIdx, drvSm_); wr = shape (wr, p.charIdx, drvSm_); }

        // 11. DC blocker — fs-aware 10 Hz, NOT the 0.995 hardcode (which is 38 Hz at 48k and is
        //     documented as a bug in DistortionEngine.h:122). Asymmetric drive and ring
        //     modulation both emit DC, and a latched rail boots the next note silent.
        const float rdc = 1.0f - (6.2831853f * 10.0f / fs_);
        const float oL = wl - dcXL_ + rdc * dcL_; dcXL_ = wl; dcL_ = oL;
        const float oR = wr - dcXR_ + rdc * dcR_; dcXR_ = wr; dcR_ = oR;

        // 12. the engine-swap dip, then Mix. Mix 1.0 = FULLY WET, zero dry (the house law).
        const float m = p.mix;
        l = inL * (1.0f - m) + oL * dip_ * m;
        r = inR * (1.0f - m) + oR * dip_ * m;
    }

    // ── the card feed. The card draws its SPECTRUM from the shared analyser FFT (the same one
    //    the synth page's filter uses), so this engine publishes only what the card cannot get
    //    anywhere else: the post-motion cutoff, the resonance, and an input level for brightness.
    //    fb389 added a 12-band follower bank here and fb390 removed it again — once the spectrum
    //    moved to the real FFT nothing read those bands, and ~36 mults a sample x 6 instances of
    //    unread arithmetic is not "spare capacity", it is waste with a comment on it.
    float liveLevel() const noexcept { return lvlSm_; }
    float liveCutHz() const noexcept { return cutSm_ > 0.0f ? cutSm_ : 20.0f; }
    float liveRes()   const noexcept { return resSm_ > 0.0f ? resSm_ : 0.0f; }
    float liveEnv()   const noexcept { return env_; }

private:
    static inline float shape (float x, int flavour, float amt) noexcept
    {
        const float k = 1.0f + amt * 24.0f;                 // 0..+28 dB into the shaper
        const float d = x * k;
        float y;
        switch (flavour)
        {
            case 1:  y = d / (1.0f + std::fabs (d)); break;                       // Diode
            case 2:  { float t = std::fmod (std::fabs (d) + 1.0f, 4.0f);          // Fold
                       y = (t > 2.0f ? 4.0f - t : t) - 1.0f;
                       y = (d < 0.0f) ? -y : y; } break;
            case 3:  y = std::max (-1.0f, std::min (1.0f, d)); break;             // Hard
            case 4:  y = std::floor (std::max (-1.0f, std::min (1.0f, d)) * 12.0f) / 12.0f; break;  // Crush
            case 5:  y = std::tanh (d * 2.2f); break;                             // Fuzz
            default: y = std::tanh (d); break;                                    // Tube
        }
        return y / std::sqrt (k);                            // driveMakeup — the house g^-0.5
    }

    filters::FilterSlot slot_;

    float fs_ = 48000.0f, coefSr_ = 48000.0f;
    float env_ = 0.0f, envFast_ = 0.0f, envSlow_ = 0.0f, gate_ = 0.0f;
    float cutSm_ = -1.0f, resSm_ = -1.0f, drvSm_ = -1.0f, keySm_ = 0.0f;
    float lfoPh_ = 0.0f, dip_ = 1.0f;
    float dcL_ = 0.0f, dcR_ = 0.0f, dcXL_ = 0.0f, dcXR_ = 0.0f;
    float lvlSm_ = 0.0f;
    int   curType_ = -1, pendingType_ = -1, lastNote_ = 60;
    float bpm_ = 120.0f; double ppq_ = 0.0; bool playing_ = false;

    static constexpr float kBusLift_ = 5.0119f;   // +14 dB
    static constexpr float kBusDrop_ = 0.19953f;  // -14 dB, exact reciprocal
    // one-pole glide coefficients at 48k; recomputed nowhere because they are close enough
    // across 44.1-96k and the law only asks for 10-30 ms.
    static constexpr float kc10ms_ = 0.00208f, kc15ms_ = 0.00139f, kt20ms_ = 0.00104f;
    static constexpr float dipDown_ = 0.0104f, dipUp_ = 0.00052f;   // ~2 ms down, ~40 ms up
};

} // namespace tw
