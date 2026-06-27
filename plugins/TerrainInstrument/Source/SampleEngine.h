// =============================================================================
// SampleEngine.h — Terrain Instrument (Waves Crate)
// Oscillator engine #2 of 5: the Sample engine's per-voice playback core.
//
// Header-only, JUCE-light by design: operates on a raw const float* const*
// view of the loaded buffer (+ length / channels / native rate), so it (a)
// compiles and proves in a bare g++ harness offline, and (b) drops straight
// into SynthVoice's `case Engine::SAMP:` — the voice just hands it the
// SampleBuffer::BufferPtr's read pointers each block.
//
// SCOPE (this increment — the playable spine, the producer essentials):
//   • pitch-accurate playback (native/output rate × note ratio), cubic-Hermite
//   • bipolar SCAN  → signed playback rate (reverse / freeze / forward / fast)
//   • 5 loop modes  → One-Shot · Forward · Reverse · Ping-Pong · Tailed
//   • Start/End region + inner LS/LE loop (hard-clamped inside the region)
//   • equal-power loop crossfade (X-Fade), auto-scaled in-bounds
//   • per-note SPRAY → random start scatter, seeded at note-on
//
// NEXT increment (own pass, see handoff §3/§4): STRETCH (feed read through the
// existing WarpProcessor stream) + FORMANT (spectral-envelope shift). Designed
// so they bolt on without touching this read/loop core.
//
// RT-safety: tick() is allocation-free and branch-light. No JUCE, no STL alloc.
// =============================================================================
#pragma once
#include <cstdint>
#include <cmath>

namespace tw
{
class SampleEngine
{
public:
    enum class LoopMode : int { OneShot = 0, Forward = 1, Reverse = 2, PingPong = 3, Tailed = 4 };

    SampleEngine() = default;

    // ── setup ────────────────────────────────────────────────────────────────
    void prepare (double outputSampleRate) noexcept
    {
        outRate_ = (outputSampleRate > 0.0) ? outputSampleRate : 44100.0;
    }

    /** Point the engine at the loaded buffer. Call whenever the BufferPtr or
     *  native rate changes (cheap — just stores pointers + recomputes region). */
    void setSample (const float* const* channels, int numChannels,
                    int numSamples, double nativeRate) noexcept
    {
        ch0_ = (numChannels > 0 && channels) ? channels[0] : nullptr;
        ch1_ = (numChannels > 1 && channels) ? channels[1] : ch0_;   // mono → dup
        numSamples_ = (numSamples > 0 && ch0_) ? numSamples : 0;
        nativeRate_ = (nativeRate > 0.0) ? nativeRate : outRate_;
        recomputeRegion();
    }

    void clearSample() noexcept { ch0_ = ch1_ = nullptr; numSamples_ = 0; active_ = false; }
    bool hasSample() const noexcept { return numSamples_ > 1 && ch0_ != nullptr; }

    // ── region / loop params (all normalised 0..1 of the whole buffer) ────────
    void setRegion (float start01, float end01) noexcept
    {
        start01_ = clamp01 (start01);
        end01_   = clamp01 (end01);
        if (end01_ < start01_ + kMinSpan01) end01_ = clampHi (start01_ + kMinSpan01);
        recomputeRegion();
    }
    void setLoop (float loopStart01, float loopEnd01) noexcept
    {
        loopStart01_ = clamp01 (loopStart01);
        loopEnd01_   = clamp01 (loopEnd01);
        recomputeRegion();
    }
    void setLoopMode (LoopMode m) noexcept { mode_ = m; }
    /** 0..1 → loop crossfade as a fraction of loop length (capped to half-loop). */
    void setXFade (float x01) noexcept { xfade01_ = clamp01 (x01); recomputeRegion(); }
    /** Region edge amplitude fades (shift-drag Start/End in the UI). 0..1 of region. */
    void setFades (float fadeIn01, float fadeOut01) noexcept
    {
        fadeIn01_  = clamp01 (fadeIn01);
        fadeOut01_ = clamp01 (fadeOut01);
        recomputeRegion();
    }

    /** Bipolar SCAN [-1..+1] → signed rate multiplier.
     *  DESIGN CALL (flagged to Max): center 0 = NATURAL FORWARD play (so a
     *  freshly-loaded sample sounds the moment you hit a key), +1 ≈ 2× fast,
     *  −1 = full reverse, ~−0.5 = frozen. This keeps the engine playable on
     *  load and makes the bipolar knob's center read "neutral". If we want the
     *  handoff's literal center-=-frozen scrub model instead, it's just the
     *  body of scanToRate() below — one line. */
    /** Per-block playback ratio = (nativeRate/outputRate) x 2^(semitones/12),
     *  computed by the voice from the MIDI note + osc tuning. */
    void setPitchRatio (double r) noexcept { pitchRatio_ = (r != 0.0) ? r : 1.0; }

    void setScan (float scan) noexcept
    {
        const float s = (scan < -1.f) ? -1.f : (scan > 1.f ? 1.f : scan);
        // Serum notch model (confirmed w/ Max + CC): center 0 = FROZEN (no scan),
        // right = forward & faster, left = reverse & faster, ±1 = ±2x (one octave).
        // The SCAN param DEFAULTS to +0.5 (-> 1x forward) so a freshly-loaded sample
        // plays the instant a key is hit — the knob just sits a notch right of center.
        scanRate_ = 2.f * s;                          // -1 -> -2x (rev) … 0 -> frozen … +1 -> +2x (fwd)
    }

    // ── note lifecycle ────────────────────────────────────────────────────────
    /** pitchRatio = (nativeRate / outputRate) × 2^(semitones/12), computed by
     *  the voice from the MIDI note + osc tuning. sprayAmount 0..1 scatters the
     *  start within the region; spraySeed is the per-note random source. */
    void noteOn (double pitchRatio, float sprayAmount, uint32_t spraySeed) noexcept
    {
        pitchRatio_ = (pitchRatio != 0.0) ? pitchRatio : 1.0;
        releasing_  = false;
        pingSign_   = (scanRate_ < 0.0f) ? -1 : 1;   // reverse scan → ping-pong starts heading backward

        double startPos = regStart_;
        const float spray = clamp01 (sprayAmount);
        if (spray > 0.f && regLen() > 1.0)
        {
            // xorshift32 → [0,1); scatter forward from region start (stays in-bounds)
            uint32_t x = spraySeed ? spraySeed : 0x9E3779B9u;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            const double r = (double) (x & 0x00FFFFFFu) / (double) 0x01000000;
            startPos = regStart_ + r * spray * (regLen() - 1.0);
        }

        pos_ = clampPos (startPos);

        // SCAN reverse: if the note starts with a reverse playback direction (scan < 0),
        // begin at the region END so the sample plays backward (end → start). Otherwise a
        // one-shot / lead-in dead-stops at the Start on frame one (clampRegionEnds kills it
        // the instant pos <= regStart with a negative inc). The UI follower rides
        // position01(), so it visually runs from the end too. The Reverse LOOP mode does
        // its own loop-END snap below, so leave it alone here.
        if (scanRate_ < 0.0f && mode_ != LoopMode::Reverse && regEnd_ > regStart_ + 1.0)
            pos_ = regEnd_ - 1.0;

        // LOOP-CATCH model (confirmed w/ Max): every looping mode plays a one-shot
        // lead-in forward from the region Start and only starts looping once the
        // playhead first reaches the purple loop region [loopStart,loopEnd] ("caught").
        // No lead-in zone (loopStart already at/under the start — e.g. the default
        // full-region loop, or a sprayed start inside the loop) => caught at once.
        // Reverse (A): on catch, snap to the loop END and run backwards from there.
        // Direction-aware catch: a forward playhead enters the loop from loopStart (below); a
        // reverse-scan playhead enters from loopEnd (above), so it leads in BACKWARD from the
        // region END and only catches once it reaches the loop — mirroring the forward feel.
        caught_ = (scanRate_ < 0.0f) ? (pos_ <= loopEnd_) : (pos_ >= loopStart_);
        if (caught_ && mode_ == LoopMode::Reverse && loopEnd_ > loopStart_)
            pos_ = loopEnd_ - 1.0;

        active_ = hasSample();
        onsetPos_ = 0.0;                                  // arm the note-on micro fade-in (declicks the start)
        onsetLen_ = 0.003 * outRate_;                     // ~3 ms equal-power ramp
    }

    /** Tailed → drop the loop and play the release tail to End. Others → stop. */
    void noteOff() noexcept
    {
        if (mode_ == LoopMode::Tailed) releasing_ = true;
        else if (mode_ == LoopMode::OneShot) { /* keep playing to End */ }
        else active_ = false;
    }

    bool isActive() const noexcept { return active_; }
    double positionForTesting() const noexcept { return pos_; }
    /** Normalised read position 0..1 of the whole buffer — for the UI MIDI follower.
     *  The voice/processor pushes this (per active voice) to the WebView each frame. */
    double position01() const noexcept { return (numSamples_ > 1) ? (pos_ / (double) (numSamples_ - 1)) : 0.0; }
    bool   caught()     const noexcept { return caught_; }

    // ── render one stereo frame. returns false once finished (one-shot / tail) ──
    bool tick (float& outL, float& outR) noexcept
    {
        if (! active_ || ! hasSample()) { outL = outR = 0.f; return false; }

        readFrame (pos_, outL, outR);

        // Region edge fades (amplitude ramp from Start / into End).
        const float g = edgeGain (pos_);
        outL *= g; outR *= g;

        // DECLICK — loop-seam micro fade (only when a content crossfade can't run, e.g. loop at a
        // buffer edge) + the note-on micro fade-in (kills sprayed random-start clicks).
        if (caught_ && ! loopXfadeContentValid_ && seamFadeLen_ > 1.0)
        {
            const double dE = loopEnd_ - pos_, dS = pos_ - loopStart_;
            const double d  = (dE < dS) ? dE : dS;
            if (d >= 0.0 && d < seamFadeLen_) { const float sg = (float) std::sin ((d / seamFadeLen_) * kHalfPi); outL *= sg; outR *= sg; }
        }
        if (onsetPos_ < onsetLen_)
        {
            const float og = (float) std::sin ((onsetPos_ / onsetLen_) * kHalfPi);
            outL *= og; outR *= og; onsetPos_ += 1.0;
        }

        const double inc = pitchRatio_ * (double) scanRate_;   // signed: + fwd, − rev, 0 frozen
        advance (inc);
        return active_;
    }

    /** Mono convenience for the per-sine voice path (averages the two channels). */
    float tickMono() noexcept
    {
        float l, r;
        tick (l, r);
        return 0.5f * (l + r);
    }

    /** Snap a normalised position to the nearest zero-crossing in the buffer
     *  (Snap = Zero-cross). Returns the input unchanged if no sample / no cross
     *  found within the search window. Transient snap is a later add. */
    float snapZeroCross01 (float pos01, int searchSamples = 2048) const noexcept
    {
        if (! hasSample()) return pos01;
        const int center = (int) (clamp01 (pos01) * (float) (numSamples_ - 1));
        int best = center; int bestDist = searchSamples + 1;
        const int lo = (center - searchSamples < 1) ? 1 : center - searchSamples;
        const int hi = (center + searchSamples > numSamples_ - 1) ? numSamples_ - 1 : center + searchSamples;
        for (int i = lo; i <= hi; ++i)
        {
            const float a = ch0_[i - 1], b = ch0_[i];
            if ((a <= 0.f && b > 0.f) || (a >= 0.f && b < 0.f))   // sign change = zero cross
            {
                const int d = (i > center) ? (i - center) : (center - i);
                if (d < bestDist) { bestDist = d; best = i; }
            }
        }
        return (float) best / (float) (numSamples_ - 1);
    }

private:
    // ── derived region/loop in samples (recomputed on any region/buffer change) ─
    void recomputeRegion() noexcept
    {
        if (numSamples_ < 2) { regStart_ = regEnd_ = loopStart_ = loopEnd_ = 0.0; xfadeLen_ = 0.0; return; }
        const double N = (double) (numSamples_ - 1);
        regStart_ = start01_ * N;
        regEnd_   = end01_   * N;
        if (regEnd_ < regStart_ + 1.0) regEnd_ = regStart_ + 1.0;

        // LS/LE clamped strictly inside [regStart, regEnd] with a minimum gap.
        loopStart_ = loopStart01_ * N;
        loopEnd_   = loopEnd01_   * N;
        if (loopStart_ < regStart_) loopStart_ = regStart_;
        if (loopEnd_   > regEnd_)   loopEnd_   = regEnd_;
        if (loopEnd_ < loopStart_ + kMinLoopSamples) loopEnd_ = loopStart_ + kMinLoopSamples;
        if (loopEnd_ > regEnd_) { loopEnd_ = regEnd_; loopStart_ = loopEnd_ - kMinLoopSamples; if (loopStart_ < regStart_) loopStart_ = regStart_; }

        // X-Fade length: fraction of loop, capped to half the loop (so it can't
        // overrun the seam) — this is the "auto-scaled in-bounds" rule.
        const double L = loopEnd_ - loopStart_;
        xfadeLen_ = (double) xfade01_ * L;
        const double cap = 0.5 * L;
        if (xfadeLen_ > cap) xfadeLen_ = cap;
        if (xfadeLen_ < 0.0) xfadeLen_ = 0.0;

        // DECLICK — the content crossfade reads pre/post-loop audio (p ± loopLen); when the loop
        // sits at a buffer edge those reads clamp to DC and THUMP. Only use the content crossfade
        // when that audio genuinely exists; otherwise fall back to a short seam amplitude micro-fade.
        const double Nm1 = (double) (numSamples_ - 1);
        loopXfadeContentValid_ = (loopStart_ >= xfadeLen_) && (loopEnd_ + xfadeLen_ <= Nm1);
        const double seamMax = 0.0015 * outRate_;   // ~1.5 ms declick
        seamFadeLen_ = (xfadeLen_ < seamMax) ? xfadeLen_ : seamMax;

        // Region edge fades — fraction of the region length, each capped to the region.
        const double RL = regEnd_ - regStart_;
        fadeInLen_  = (double) fadeIn01_  * RL;
        fadeOutLen_ = (double) fadeOut01_ * RL;
        if (fadeInLen_  > RL) fadeInLen_  = RL;
        if (fadeOutLen_ > RL) fadeOutLen_ = RL;
        if (fadeInLen_  < 0.0) fadeInLen_  = 0.0;
        if (fadeOutLen_ < 0.0) fadeOutLen_ = 0.0;
    }

    double regLen()  const noexcept { return regEnd_  - regStart_; }
    double loopLen() const noexcept { return loopEnd_ - loopStart_; }

    // ── playhead advance + loop boundary behaviour (direction-agnostic) ────────
    void advance (double inc) noexcept
    {
        // One-Shot: never loops — play through the region once, then die.
        if (mode_ == LoopMode::OneShot)
        {
            pos_ += inc;
            clampRegionEnds (inc);
            return;
        }

        // ── LEAD-IN (Serum "armed" phase) ─────────────────────────────────────
        // Not yet caught: play forward from the region Start toward the loop. The
        // moment the playhead reaches the loop region we're caught and the loop
        // mode takes over. Selecting a mode never jumps the playhead anymore.
        if (! caught_)
        {
            pos_ += inc;
            // Direction-aware catch: forward enters the loop at loopStart (from below), reverse
            // enters at loopEnd (from above). So reverse scan leads in BACKWARD from the End and
            // only starts looping once the playhead reaches the loop — same feel as forward.
            const bool reached = (inc < 0.0) ? (pos_ <= loopEnd_) : (pos_ >= loopStart_);
            if (reached)
            {
                caught_   = true;
                pingSign_ = (inc < 0.0) ? -1 : 1;                // bounce starts in the travel direction
                if (mode_ == LoopMode::Reverse && loopEnd_ > loopStart_)
                    pos_ = loopEnd_ - 1.0;                       // (A) snap to loop end, run back
                else
                {
                    while (pos_ >= loopEnd_)   pos_ -= loopLen();  // clamp overshoot (forward catch)
                    while (pos_ <  loopStart_) pos_ += loopLen();  // clamp overshoot (reverse catch)
                }
            }
            else
            {
                clampRegionEnds (inc);   // hasn't reached the loop yet — keep leading in
            }
            return;
        }

        // ── CAUGHT: behave per the loop mode inside [loopStart, loopEnd] ───────
        // Tailed in release: stop looping, play the tail out to End, then die.
        if (mode_ == LoopMode::Tailed && releasing_)
        {
            pos_ += inc;
            clampRegionEnds (inc);
            return;
        }

        // Ping-pong travels on its OWN sign (pingSign_) at |inc| speed. Using the raw signed
        // inc breaks under reverse scan (inc<0): at loopStart the bounce sets pingSign_=+1 but
        // inc<0 keeps pushing left, so it sticks/reverses instead of bouncing. |inc| fixes it.
        const double mag = (inc < 0.0) ? -inc : inc;
        pos_ += (mode_ == LoopMode::PingPong) ? (mag * (double) pingSign_)
              : (mode_ == LoopMode::Reverse)  ? -inc
                                              :  inc;

        if (mode_ == LoopMode::PingPong)
        {
            if (pos_ >= loopEnd_)   { pos_ = loopEnd_   - (pos_ - loopEnd_);   pingSign_ = -1; }
            if (pos_ <= loopStart_) { pos_ = loopStart_ + (loopStart_ - pos_); pingSign_ = +1; }
            pos_ = clampLoop (pos_);
            return;
        }

        // Forward / Reverse / Tailed-held: wrap within the loop (scan can reverse a fwd loop).
        while (pos_ >= loopEnd_)   pos_ -= loopLen();
        while (pos_ <  loopStart_) pos_ += loopLen();
    }

    /** One-shot / lead-in / tail boundary: clamp to the region ends, die at the far edge. */
    void clampRegionEnds (double inc) noexcept
    {
        if (pos_ >= regEnd_ - 1.0) { pos_ = regEnd_ - 1.0; if (inc >= 0.0) active_ = false; }
        if (pos_ <= regStart_)     { pos_ = regStart_;     if (inc <  0.0) active_ = false; }
    }

    // ── read one interpolated stereo frame, applying the loop crossfade ────────
    void readFrame (double p, float& l, float& r) noexcept
    {
        readHermite (p, l, r);

        // Equal-power loop crossfade: in the last xfadeLen samples before the
        // loop end, blend the loop tail (here) with the lead-in to loopStart, so
        // the wrap loopEnd→loopStart is seamless. cos²+sin² = 1 (constant power).
        const bool fwdLoop = (mode_ == LoopMode::Forward
                              || (mode_ == LoopMode::Tailed && ! releasing_));
        if (fwdLoop && xfadeLen_ > 1.0 && loopXfadeContentValid_)
        {
            const double dEnd = loopEnd_ - p;                 // distance to loop end
            if (dEnd >= 0.0 && dEnd < xfadeLen_)
            {
                const double t  = 1.0 - (dEnd / xfadeLen_);   // 0 at xfade-in, →1 at loopEnd
                float hl, hr;
                readHermite (p - loopLen(), hl, hr);          // the lead-in to loopStart
                const float gT = (float) std::cos (t * kHalfPi);
                const float gH = (float) std::sin (t * kHalfPi);
                l = l * gT + hl * gH;
                r = r * gT + hr * gH;
            }
        }
        else if (mode_ == LoopMode::Reverse && xfadeLen_ > 1.0 && loopXfadeContentValid_)
        {
            const double dStart = p - loopStart_;             // reverse seam at loopStart
            if (dStart >= 0.0 && dStart < xfadeLen_)
            {
                const double t  = 1.0 - (dStart / xfadeLen_);
                float hl, hr;
                readHermite (p + loopLen(), hl, hr);
                const float gT = (float) std::cos (t * kHalfPi);
                const float gH = (float) std::sin (t * kHalfPi);
                l = l * gT + hl * gH;
                r = r * gT + hr * gH;
            }
        }
    }

    // 4-point cubic Hermite (Catmull-Rom) interpolation, indices clamped to buffer.
    void readHermite (double p, float& l, float& r) noexcept
    {
        const int i1 = (int) std::floor (p);
        const double f = p - (double) i1;
        l = hermite (sampL (i1 - 1), sampL (i1), sampL (i1 + 1), sampL (i1 + 2), (float) f);
        r = hermite (sampR (i1 - 1), sampR (i1), sampR (i1 + 1), sampR (i1 + 2), (float) f);
    }

    inline float sampL (int i) const noexcept { i = clampIdx (i); return ch0_[i]; }
    inline float sampR (int i) const noexcept { i = clampIdx (i); return ch1_[i]; }
    inline int   clampIdx (int i) const noexcept { return (i < 0) ? 0 : (i >= numSamples_ ? numSamples_ - 1 : i); }

    static inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c  = (x1 - xm1) * 0.5f;
        const float v  = x0 - x1;
        const float w  = c + v;
        const float a  = w + v + (x2 - x0) * 0.5f;
        const float bn = w + a;
        return ((((a * t) - bn) * t + c) * t + x0);
    }

    double clampPos  (double p) const noexcept { return (p < regStart_) ? regStart_ : (p > regEnd_ - 1.0 ? regEnd_ - 1.0 : p); }
    double clampLoop (double p) const noexcept { return (p < loopStart_) ? loopStart_ : (p > loopEnd_ ? loopEnd_ : p); }

    /** Region-edge amplitude fade: 1.0 in the middle, ramps from 0 at Start over
     *  fadeInLen, and down to 0 at End over fadeOutLen. Equal-power (sin) curve. */
    float edgeGain (double p) const noexcept
    {
        float g = 1.f;
        if (fadeInLen_ > 1.0)
        {
            const double d = p - regStart_;
            if (d >= 0.0 && d < fadeInLen_) g *= (float) std::sin ((d / fadeInLen_) * kHalfPi);
        }
        if (fadeOutLen_ > 1.0)
        {
            const double d = regEnd_ - p;
            if (d >= 0.0 && d < fadeOutLen_) g *= (float) std::sin ((d / fadeOutLen_) * kHalfPi);
        }
        return g;
    }

    static inline float clamp01 (float v) noexcept { return (v < 0.f) ? 0.f : (v > 1.f ? 1.f : v); }
    static inline float clampHi (float v) noexcept { return (v > 1.f) ? 1.f : v; }

    // ── data view ──
    const float* ch0_ = nullptr;
    const float* ch1_ = nullptr;
    int    numSamples_ = 0;
    double nativeRate_ = 44100.0;
    double outRate_    = 44100.0;

    // ── params ──
    float    start01_ = 0.f, end01_ = 1.f;
    float    loopStart01_ = 0.f, loopEnd01_ = 1.f;
    float    xfade01_ = 0.12f;
    float    fadeIn01_ = 0.f, fadeOut01_ = 0.f;
    float    scanRate_ = 1.f;                 // signed rate from SCAN (default natural fwd)
    LoopMode mode_ = LoopMode::Forward;

    // ── derived (samples) ──
    double regStart_ = 0.0, regEnd_ = 0.0;
    double loopStart_ = 0.0, loopEnd_ = 0.0;
    double xfadeLen_ = 0.0;
    double fadeInLen_ = 0.0, fadeOutLen_ = 0.0;

    // ── playhead state ──
    double  pos_ = 0.0;
    double  pitchRatio_ = 1.0;
    int     pingSign_ = 1;
    bool    active_ = false;
    bool    releasing_ = false;
    bool    caught_ = false;     // LOOP-CATCH — false during the one-shot lead-in, true once in the loop
    // ── declick state ──
    double  onsetLen_ = 0.0, onsetPos_ = 1.0e18;   // note-on micro fade-in (kills sprayed random-start clicks)
    double  seamFadeLen_ = 0.0;                    // loop-seam micro fade half-width (samples), fallback declick
    bool    loopXfadeContentValid_ = true;         // pre/post-loop audio exists → use the content crossfade

    static constexpr double kHalfPi = 1.57079632679489661923;
    static constexpr double kMinLoopSamples = 8.0;
    static constexpr float  kMinSpan01 = 0.001f;
};
} // namespace tw
