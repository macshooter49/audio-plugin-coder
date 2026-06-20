#pragma once
// =============================================================================
//  FlowChop.h  —  Terrain Instrument · FLOW mode CHOP   (replaces the SEQ slot)
//  Waves Crate
//
//  Header-only, NO JUCE. RT-safe in process() (allocation only in prepare()).
//  Sibling of FlowArp.h — reuses its rate ladder, ArpRng, clamps, quantizers.
//
//  WHAT IT IS
//  CHOP is a performance mode (sibling to the ARP). It chops the synth's OWN
//  live output into grid-locked slices and re-grooves them in real time —
//  reordered, repitched IN KEY, reversed, rolled — like an MPC/Dilla flip that
//  plays itself. It is NOT a looper (the tape engine owns record/overdub) and
//  NOT a destructive FX (that's GLITCH). It rearranges; you ride the macros.
//
//  TWO SUB-MODES (right-click "glass" menu on the mode badge):
//    • ALWAYS-ON : continuously re-grooves the trailing output, like an instrument
//    • CATCH     : only flips while a key/trigger is held (punch-in performance)
//
//  ── THE NON-NEGOTIABLE: ZERO CLICKS ──────────────────────────────────────────
//  The "silent light-switch" principle, generalized. Every slice plays through a
//  voice in a small POOL; each voice has a COSINE (Hann-edged) envelope whose
//  attack starts at 0 and release ends at 0 with ZERO SLOPE at both ends (C1
//  continuous). Transitions OVERLAP: the outgoing voice releases while the
//  incoming voice attacks. Buffer reads are CONTINUOUS (read forward/back through
//  the ring — never loop a slice, so there is no loop seam to click). Out-of-range
//  reads clamp to the newest written sample (a flat hold, not a jump). Result:
//  a hard discontinuity is mathematically impossible. Proven offline by asserting
//  max |out[n]-out[n-1]| stays tiny under the most aggressive settings.
//
//  ── VARISPEED (the hard part, done right) ─────────────────────────────────────
//  Repitch = resampling: read the buffer at increment = pitch_ratio = 2^(semi/12).
//  Pitch and length couple (the tape/MPC sound). We DON'T fight that — we read
//  CONTINUOUSLY through the buffer for the whole gate (so up-pitched slices pull
//  in newer audio instead of looping/gapping, down-pitched slices stretch), and
//  the step grid is honored by gating each voice to the step with the cosine
//  release. Repitch is QUANTIZED to the held key so flips stay musical.
//  Interpolation: 4-point cubic Hermite (Catmull-Rom) — the sampler standard.
//
//  KNOBS (effective = base + LFO mod, summed upstream):
//    RATE → slice grid (ladder)   GATE → slice length / space (staccato↔legato)
//    VARY → flip intensity: déjà-vu re-roll + reverse/pitch probability
//    TRAJ → flip STYLE {Forward, Reverse, Shuffle, PingPong, StutterRoll, GrainSpray}
//    MORPH→ swing + pitch-spice (in-key repitch range)
//
//  Card depth (extension panel): slices, loop length, scale/key, déjà-vu lock,
//  per-style params, ratchet/roll, pitch range, mix, DIRT (future). Most controls
//  live there — front shows the 5 macros + the glass mode menu.
//
//  Build test: g++ -std=c++17 Source/FlowChop_test.cpp -o /tmp/fc && /tmp/fc
// =============================================================================

#include "FlowArp.h"
#include <vector>
#include <cmath>

namespace wc
{

enum class ChopMode  : int { AlwaysOn = 0, Catch };
enum class ChopStyle : int { Forward = 0, Reverse, Shuffle, PingPong, StutterRoll, GrainSpray };
static constexpr int kChopStyleN  = 6;
static constexpr int kChopVoices  = 8;     // slice-player pool (overlap headroom)
static constexpr int kChopMaxLoop = 32;    // max pattern length (slices in a loop)

inline ChopStyle chopStyle (float traj) noexcept { return (ChopStyle) arpQuantIdx (traj, kChopStyleN); }

class FlowChop
{
public:
    FlowChop() { rng_.seed (0xC40F5EEDu); }

    // ── lifecycle ───────────────────────────────────────────────────────────────
    void prepare (double sampleRate, double captureSeconds = 4.0) noexcept
    {
        sr_  = (sampleRate > 0.0 ? sampleRate : 44100.0);
        cap_ = (int) std::ceil (sr_ * (captureSeconds > 0.5 ? captureSeconds : 0.5));
        if (cap_ < 1024) cap_ = 1024;
        bufL_.assign ((size_t) cap_, 0.0f);
        bufR_.assign ((size_t) cap_, 0.0f);
        fade_ = (int) std::lround (sr_ * 0.0015);          // 1.5 ms cosine edge
        if (fade_ < 2) fade_ = 2;
        reset();
    }

    void reset() noexcept
    {
        wAbs_ = 0; written_ = 0;
        for (auto& v : voice_) v = Voice{};
        cur_ = -1;
        wetEnv_ = 0.f; catchHeld_ = false;
        haveClock_ = false; nextStep_ = 0; nextBoundary_ = 0.0; freePpq_ = 0.0;
        subRemain_ = 0; subTimer_ = 0; subPitchAccum_ = 0.f;
        for (int i = 0; i < kChopMaxLoop; ++i) { backValid_[i] = false; }
        rng_.seed (seed_ ? seed_ : 0xC40F5EEDu);
        regenPattern_ = true;
    }

    // ── configuration (card → engine) ───────────────────────────────────────────
    void setMode (ChopMode m) noexcept { mode_ = m; }
    void setCatchHeld (bool held) noexcept { catchHeld_ = held; }          // CATCH trigger (key/footswitch)
    void setSlices (int n) noexcept { len_ = arpClampi (n, 2, kChopMaxLoop); regenPattern_ = true; }
    void setLoopLen (int n) noexcept { len_ = arpClampi (n, 2, kChopMaxLoop); regenPattern_ = true; }
    void setMix (float wet) noexcept { mix_ = arpClamp01 (wet); }
    void setScale (int rootMidi, uint16_t mask) noexcept { scaleRoot_ = arpClampi (rootMidi, 0, 127); scaleMask_ = mask; buildScale(); }
    void setSeed (uint32_t s) noexcept { seed_ = s; }
    void setDejavu (float d) noexcept { dejavu_ = arpClamp01 (d); }        // 0=re-roll every loop, 1=locked
    void setPitchRangeDeg (int deg) noexcept { pitchRangeDeg_ = arpClampi (deg, 0, 14); }
    void setReverseProb (float p) noexcept { revProb_ = arpClamp01 (p); }
    void setRatchet (int r) noexcept { ratchet_ = arpClampi (r, 1, 16); }   // sub-triggers per step (rolls)
    void setStyleOverride (int s) noexcept { styleOv_ = s; }                 // -1 = follow TRAJ
    void setHostTrack (bool on) noexcept { keyTrack_ = on; }
    void noteOnRoot (int midi) noexcept { lastRoot_ = arpClampi (midi, 0, 127); }

    bool  isActive() const noexcept { return wetEnv_ > 0.001f; }
    int   lastSliceIndex() const noexcept { return lastSlice_; }            // for UI / test
    ChopStyle activeStyle() const noexcept { return curStyle_; }

    // ── main: process the synth output in place ───────────────────────────────────
    void process (float rate, float gate, float vary, float traj, float morph,
                  double hostPpq, double bpm, double sampleRate,
                  float* L, float* R, int numSamples, bool playing) noexcept
    {
        if (cap_ <= 0 || numSamples <= 0) return;
        const double SR  = (sampleRate > 0.0 ? sampleRate : sr_);
        const double BP  = (bpm > 0.0 ? bpm : 120.0);
        const double pps = (BP / 60.0) / SR;
        const float  beats = arpBeatsPerStep (rate);
        const double stepSamp = (double) beats / pps;
        const double sw = (double) arpClamp01 (morph > 0.9f ? 0.9f : morph) * ((double) beats * 0.5);

        curStyle_ = (styleOv_ >= 0 && styleOv_ < kChopStyleN) ? (ChopStyle) styleOv_ : chopStyle (traj);
        const float gFrac   = arpClamp01 (gate < 0.05f ? 0.05f : gate);
        const float pitchAmt = arpClamp01 (morph);                          // MORPH adds pitch spice
        const float flipAmt  = arpClamp01 (vary);

        double p = playing ? hostPpq : freePpq_;
        // Re-anchor the step clock to the transport whenever the grid changes (RATE knob) or the
        // step counter drifts out of a sane window (transport jump). A raw step counter is only
        // valid for the `beats` it was built with: moving RATE UP makes the next boundary fall far
        // BEHIND p (the boundary loop rapid-fires, guard-capped → glitch/silence at 1/256); moving
        // RATE DOWN makes it fall far AHEAD of p (no boundary ever fires → dead/silent, can't
        // recover). Re-anchoring keeps the grid locked to ppq across any rate move.
        {
            const double    curFloor = std::floor (p / (double) beats);
            const long long curStep  = (long long) curFloor;
            if (! haveClock_ || beats != lastBeats_ || nextStep_ > curStep + 2 || nextStep_ < curStep - 2)
            {
                nextStep_     = curStep;
                nextBoundary_ = boundaryTime (nextStep_, beats, sw);
                lastBeats_    = beats;
                haveClock_    = true;
            }
        }

        // target wet gate: ALWAYS-ON open whenever playing; CATCH open only while held
        const bool wantWet = (mode_ == ChopMode::AlwaysOn) ? true : catchHeld_;

        for (int i = 0; i < numSamples; ++i, p += pps)
        {
            const float dryL = L[i], dryR = R[i];
            // record dry into ring (absolute index)
            bufL_[(size_t) (wAbs_ % cap_)] = dryL;
            bufR_[(size_t) (wAbs_ % cap_)] = dryR;

            // step boundaries (swung) → schedule slice triggers
            int guard = 0;
            while (p >= nextBoundary_ && guard++ < 64)
            {
                onBoundary (nextStep_, stepSamp, gFrac, flipAmt, pitchAmt);
                ++nextStep_;
                nextBoundary_ = boundaryTime (nextStep_, beats, sw);
            }

            // sub-triggers (ratchets / StutterRoll rolls) inside a step
            if (subRemain_ > 0)
            {
                if (--subTimer_ <= 0)
                {
                    subPitchAccum_ += subPitchStep_;
                    triggerVoice (pendBack_, pendRev_, pendPitch_ + subPitchAccum_, pendGate_, stepSamp);
                    --subRemain_;
                    subTimer_ = subInterval_;
                }
            }

            // render voice pool (sum) — each voice cosine-enveloped, continuous read
            float wetL = 0.f, wetR = 0.f;
            for (int v = 0; v < kChopVoices; ++v) if (voice_[v].active) renderVoice (voice_[v], wetL, wetR);

            // wet gate (cosine) for mode enter/exit click-free
            const float target = wantWet ? 1.f : 0.f;
            const float stepInc = 1.f / (float) fade_;
            if (wetEnv_ < target)      wetEnv_ = std::min (target, wetEnv_ + stepInc);
            else if (wetEnv_ > target) wetEnv_ = std::max (target, wetEnv_ - stepInc);
            const float wenv = 0.5f - 0.5f * std::cos (3.14159265358979f * arpClamp01 (wetEnv_)); // smooth the gate

            const float a = mix_ * wenv;
            L[i] = flush (dryL + (wetL - dryL) * a);
            R[i] = flush (dryR + (wetR - dryR) * a);

            ++wAbs_;
            if (written_ < cap_) ++written_;
        }

        freePpq_ = (playing ? hostPpq : freePpq_) + pps * (double) numSamples;
    }

private:
    struct Voice
    {
        bool   active = false;
        double srcStart = 0.0;     // absolute start index of the slice in the ring
        double sliceLen = 0.0;     // source samples spanned by the slice (for reverse origin)
        double readPos = 0.0;      // samples advanced (always >= 0)
        double rate = 1.0;         // pitch ratio
        bool   reverse = false;
        float  level = 1.0f;
        int    phase = 0;          // 0 attack, 1 sustain, 2 release, 3 done
        int    aLen = 1, rLen = 1, aPos = 0, rPos = 0;
        int    gate = 0;           // output samples until release
        float  relStart = 1.0f;    // envelope shape value when release began
    };

    static double boundaryTime (long long step, float beats, double sw) noexcept
    { return (double) step * (double) beats + ((step & 1LL) ? sw : 0.0); }

    // schedule the slice(s) for a step
    void onBoundary (long long step, double stepSamp, float gFrac, float flipAmt, float pitchAmt) noexcept
    {
        if (regenPattern_ || (step % len_) == 0) { regeneratePattern(); regenPattern_ = false; }
        const int li = (int) (((step % len_) + len_) % len_);

        // back-offset to replay captured slice order_[li] live-anchored:
        //   Forward (slice==li) => back 0 (≈1-slice latency, feels live).
        //   A pattern that wants a not-yet-recorded slice (slice>li) borrows last loop's copy.
        const int slice = order_[li];
        int back = li - slice;
        if (back < 0) back += len_;
        lastSlice_ = slice;

        // reverse + pitch spice (probabilities scale with VARY / MORPH), déjà-vu locked
        bool rev = (rng_.unit() < revProb_ * flipAmt);
        int  semi = 0;
        if (pitchRangeDeg_ > 0 && rng_.unit() < flipAmt)
        {
            const int deg = (int) rng_.below (pitchRangeDeg_ + 1) * (rng_.unit() < 0.5f ? 1 : -1);
            semi = scaleSemis (deg);
            semi = (int) std::lround (semi * (double) pitchAmt);   // MORPH scales the spice depth
        }
        const float gateFull = (float) (stepSamp * (double) gFrac);

        // StutterRoll / GrainSpray / ratchet → sub-triggers across the step
        int rolls = ratchet_;
        float pitchStep = 0.f;
        if (curStyle_ == ChopStyle::StutterRoll) { rolls = (rolls < 4) ? 4 : rolls; pitchStep = (float) scaleSemis (1); } // ascending in-key
        if (curStyle_ == ChopStyle::GrainSpray)  { rolls = (rolls < 6) ? 6 : rolls; }

        subInterval_ = (rolls > 1) ? (int) std::lround (stepSamp / (double) rolls) : 0;
        if (rolls > 1 && subInterval_ < 1) subInterval_ = 1;
        // per-hit gate: rolls last one sub-slot (so voices never pile past the pool); else fill the step
        const float perGate = (rolls > 1) ? (float) std::max (2 * fade_, subInterval_) : gateFull;

        // fire the first hit now; queue the remaining as timed sub-triggers
        triggerVoice (back, rev, (float) semi, perGate, stepSamp);
        subRemain_ = rolls - 1;
        if (subRemain_ > 0)
        {
            subTimer_      = subInterval_;
            subPitchAccum_ = 0.f;
            subPitchStep_  = pitchStep;
            pendBack_      = back;  pendRev_ = rev; pendPitch_ = (float) semi; pendGate_ = perGate;
            if (curStyle_ == ChopStyle::GrainSpray) { pendBack_ = -1; }     // -1 = random per sub (set in trigger)
        }
    }

    // allocate a voice, release the current, attack the new (overlap = click-free)
    void triggerVoice (int back, bool rev, float semi, float gateSamples, double stepSamp) noexcept
    {
        if (back < 0) back = (int) rng_.below (len_);                    // GrainSpray random pick
        const double sliceLen = stepSamp;
        const double srcStart = (double) wAbs_ - (double) (back + 1) * sliceLen;

        // release whoever is current
        if (cur_ >= 0 && voice_[cur_].active && voice_[cur_].phase < 2) startRelease (voice_[cur_]);

        const int slot = allocVoice();
        Voice& v = voice_[slot];
        v.active = true; v.phase = 0; v.aPos = 0; v.rPos = 0;
        v.srcStart = srcStart; v.sliceLen = sliceLen; v.readPos = 0.0;
        v.reverse = rev;
        v.rate = std::pow (2.0, (double) semi / 12.0);
        v.level = 1.0f;
        v.aLen = clampFade ((int) gateSamples); v.rLen = v.aLen;
        v.gate = (int) gateSamples < 1 ? 1 : (int) gateSamples;
        v.relStart = 1.0f;
        cur_ = slot;
    }

    int allocVoice() noexcept
    {
        for (int v = 0; v < kChopVoices; ++v) if (! voice_[v].active) return v;
        // steal the one closest to done (smallest gate+release remaining)
        int best = 0; int bestRem = 1 << 30;
        for (int v = 0; v < kChopVoices; ++v) { const int rem = voice_[v].phase >= 2 ? (voice_[v].rLen - voice_[v].rPos) : (voice_[v].gate + voice_[v].rLen); if (rem < bestRem) { bestRem = rem; best = v; } }
        return best;
    }

    int clampFade (int gateSamples) const noexcept
    {
        int f = fade_;
        if (f > gateSamples / 2) f = gateSamples / 2;       // fade must fit inside the gate
        if (f < 1) f = 1;
        return f;
    }

    static void startRelease (Voice& v) noexcept
    {
        if (v.phase >= 2) return;
        // current envelope shape value -> release continues from it (continuous)
        float shape = (v.phase == 0) ? (0.5f - 0.5f * std::cos (3.14159265f * (float) v.aPos / (float) v.aLen)) : 1.0f;
        v.relStart = shape; v.phase = 2; v.rPos = 0;
    }

    void renderVoice (Voice& v, float& wetL, float& wetR) noexcept
    {
        // ---- envelope (cosine, zero-slope endpoints) ----
        float shape;
        if (v.phase == 0)            // attack
        {
            shape = 0.5f - 0.5f * std::cos (3.14159265f * (float) v.aPos / (float) v.aLen);
            if (++v.aPos >= v.aLen) v.phase = 1;
        }
        else if (v.phase == 1)       // sustain
        {
            shape = 1.0f;
        }
        else if (v.phase == 2)       // release
        {
            shape = v.relStart * (0.5f + 0.5f * std::cos (3.14159265f * (float) v.rPos / (float) v.rLen));
            if (++v.rPos >= v.rLen) { v.phase = 3; v.active = false; }
        }
        else { v.active = false; return; }

        const float g = shape * v.level;

        // ---- continuous read (forward, or backward for reverse) ----
        const double idx = v.reverse ? (v.srcStart + v.sliceLen - 1.0 - v.readPos)
                                     : (v.srcStart + v.readPos);
        const float sL = sampleAt (bufL_, idx);
        const float sR = sampleAt (bufR_, idx);
        wetL += g * sL; wetR += g * sR;

        v.readPos += v.rate;

        // gate countdown → release at step end
        if (v.phase < 2) { if (--v.gate <= 0) startRelease (v); }
    }

    // read ring at absolute fractional index, clamped to the valid written window (Hermite)
    float sampleAt (const std::vector<float>& b, double absIdx) const noexcept
    {
        const double newest = (double) (wAbs_ - 1);
        const double oldest = (double) (wAbs_ - (long long) (written_ < cap_ ? written_ : cap_));
        if (absIdx > newest) absIdx = newest;               // flat hold (no jump) if reading ahead
        if (absIdx < oldest) absIdx = oldest;
        const double fp = std::floor (absIdx);
        const float t = (float) (absIdx - fp);
        const long long i = (long long) fp;
        const float xm1 = ringAt (b, i - 1, oldest, newest);
        const float x0  = ringAt (b, i,     oldest, newest);
        const float x1  = ringAt (b, i + 1, oldest, newest);
        const float x2  = ringAt (b, i + 2, oldest, newest);
        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
    float ringAt (const std::vector<float>& b, long long i, double oldest, double newest) const noexcept
    {
        if ((double) i < oldest) i = (long long) oldest;
        if ((double) i > newest) i = (long long) newest;
        long long m = i % cap_; if (m < 0) m += cap_;
        return b[(size_t) m];
    }

    // ── flip-pattern generation (slice playback ORDER) + déjà-vu lock ─────────────
    void regeneratePattern() noexcept
    {
        const int L = len_;
        switch (curStyle_)
        {
            case ChopStyle::Forward:   for (int i = 0; i < L; ++i) order_[i] = i; break;             // recorded order (delayed passthrough)
            case ChopStyle::Reverse:   for (int i = 0; i < L; ++i) order_[i] = L - 1 - i; break;      // newest→oldest (time-reversed order)
            case ChopStyle::PingPong:  { const int P = (L > 1) ? 2 * L - 2 : 1; for (int i = 0; i < L; ++i) { int m = i % P; order_[i] = (m < L) ? L - 1 - m : L - 1 - (P - m); } } break;
            case ChopStyle::Shuffle:
            case ChopStyle::GrainSpray:
            case ChopStyle::StutterRoll:
            default:
            {
                for (int i = 0; i < L; ++i)
                {
                    if (! (backValid_[i] && rng_.unit() < dejavu_))          // déjà-vu: keep cached order when locked
                    { order_[i] = (int) rng_.below (L); backValid_[i] = true; }
                }
                break;
            }
        }
    }

    // scale tables: semitone offset for a scale-degree (in-key repitch)
    void buildScale() noexcept
    {
        scaleN_ = 0;
        for (int i = 0; i < 12; ++i) if (scaleMask_ & (1u << i)) { if (scaleN_ < 12) scaleSemi_[scaleN_++] = i; }
        if (scaleN_ == 0) { for (int i = 0; i < 12; ++i) scaleSemi_[i] = i; scaleN_ = 12; }
    }
    int scaleSemis (int degree) const noexcept
    {
        if (scaleN_ <= 0) return degree;
        const int oct = (degree >= 0) ? degree / scaleN_ : -(((-degree) + scaleN_ - 1) / scaleN_);
        const int idx = ((degree % scaleN_) + scaleN_) % scaleN_;
        return 12 * oct + scaleSemi_[idx];
    }

    static float flush (float x) noexcept
    {
        if (! (x == x)) return 0.f;
        if (x >  8.f) return 8.f;
        if (x < -8.f) return -8.f;
        if (x < 1.0e-20f && x > -1.0e-20f) return 0.f;
        return x;
    }

    // ── state ─────────────────────────────────────────────────────────────────────
    std::vector<float> bufL_, bufR_;
    int      cap_ = 0, fade_ = 2;
    long long wAbs_ = 0; int written_ = 0;
    double   sr_ = 44100.0;

    // config
    ChopMode mode_ = ChopMode::AlwaysOn;
    bool     catchHeld_ = false;
    int      len_ = 8;
    float    mix_ = 1.0f, dejavu_ = 0.0f, revProb_ = 0.35f;
    int      pitchRangeDeg_ = 4, ratchet_ = 1, styleOv_ = -1;
    bool     keyTrack_ = true;
    uint32_t seed_ = 0xC40F5EEDu;
    int      scaleRoot_ = 60, lastRoot_ = 60; uint16_t scaleMask_ = 0x0AB5; // major default
    int      scaleSemi_[12] = {0,2,4,5,7,9,11,0,0,0,0,0}; int scaleN_ = 7;

    // runtime
    Voice    voice_[kChopVoices];
    int      cur_ = -1, lastSlice_ = 0;
    ChopStyle curStyle_ = ChopStyle::Forward;
    float    wetEnv_ = 0.f;
    int      order_[kChopMaxLoop] = {0}; bool backValid_[kChopMaxLoop];
    bool     regenPattern_ = true;

    // sub-trigger (ratchet / roll) state
    int      subRemain_ = 0, subTimer_ = 0, subInterval_ = 1;
    float    subPitchAccum_ = 0.f, subPitchStep_ = 0.f;
    int      pendBack_ = 0; bool pendRev_ = false; float pendPitch_ = 0.f; float pendGate_ = 0.f;

    // clock
    bool     haveClock_ = false; long long nextStep_ = 0; double nextBoundary_ = 0.0, freePpq_ = 0.0, lastBeats_ = 0.0;

    mutable ArpRng rng_;
};

} // namespace wc
