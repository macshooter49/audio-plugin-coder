#pragma once
// ════════════════════════════════════════════════════════════════════════════
//  tw::GranularEngine — Terrain granular oscillator DSP (all-original)
//
//  Mirrors tw::SampleEngine's contract: header-only, namespace tw, borrowed
//  const float* const* buffer view, zero-alloc tick(), lifecycle = prepare()
//  once + noteOn()/noteOff() per note + clearSample(). Reads the SAME loaded
//  sample buffer the Sample engine uses; grains are short windowed slices.
//
//  Design: a fixed 64-grain pool + an async, jitter-scheduled spawner. A master
//  read-head (Scan) is DECOUPLED from grain pitch — Scan=0 freezes (grains keep
//  spraying from the held slice = the "living freeze"), Scan<0 reverses. Grains
//  are windowed by construction (window is 0 at both ends), so grain start/stop
//  physically cannot click — even sustained as a pad.
//
//  Controls (front knobs): Scan · Density · Size · Spray · Shape · Key
//                (page 2): Position · Pitch · P.Spray · Width · Dir · Skew
//  Waveform right-click (ported from the Sample osc): Air · Stretch(+ Tones/Beats/Texture)
//
//  Offline proof loop (Pattern A — NOT in CMakeLists):
//    c++ -std=c++17 -Wall -Wextra -ISource Source/GranularEngine_test.cpp -o /tmp/ge && /tmp/ge
// ════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <array>

namespace tw {

// A recent grain birth, for the UI scatter follower.
struct GrainViz { float pos01 = 0.f; float age01 = 0.f; float pan = 0.f; };

// All granular params, gathered per-block by the voice (mirrors SampleEngineParams).
struct GranularEngineParams
{
    float scan       = 0.15f; // -1..+1 read-head rate; 0 = freeze, <0 = reverse
    float position   = 0.f;   // 0..1 grain-birth anchor within the region
    float density    = 0.4f;  // 0..1 -> 1..220 grains/sec (log)
    float size       = 0.25f; // 0..1 -> 2..900 ms grain length (log)
    float spray      = 0.10f; // 0..1 grain-birth position jitter (AMPLIFIED: full = ±half the region)
    float shape      = 0.5f;  // 0..1 window morph Flat-top(0) -> Hann(.5) -> Bell(1)
    float skew       = 0.f;   // -1..+1 window asymmetry (pluck <-> swell)
    float pitch      = 0.f;   // base grain transpose, semitones
    float pitchSpray = 0.f;   // 0..1 per-grain pitch scatter (AMPLIFIED: up to ±24 st)
    float width      = 0.f;   // 0..1 per-grain stereo spread
    float dir        = 1.f;   // -1..+1 per-grain direction bias (-1 rev, 0 rand, +1 fwd)
    int   key        = 0;     // 0=Off 1=Oct 2=5th 3=Chord 4=Maj 5=Min 6=Penta — in-key shimmer

    // ── ported from the Sample osc, exposed on the waveform right-click ──
    float air        = 0.f;   // 0..1 Chebyshev-style high-harmonic exciter (identical to Sample AIR)
    float stretch    = 0.f;   // 0..1 grain-domain time-stretch / smear
    int   stretchMode= 0;     // 0=Tones 1=Beats 2=Texture (stretch character)

    // ── region (start/end handles on the waveform) — travels with the params so the voice
    //    can setRegion() each block; fixes the old hardcoded whole-buffer region ──
    float regStart   = 0.f;   // 0..1
    float regEnd     = 1.f;   // 0..1
    float loopStart  = 0.f;   // 0..1 — the LOOP bracket (from SAMPLE_LOOP_START). Loop modes catch
    float loopEnd    = 1.f;   // 0..1 — then loop INSIDE [loopStart,loopEnd], like the Sample engine.
    int   loopMode   = 1;     // 0=One-Shot 1=Forward 2=Reverse 3=Ping-Pong 4=Tailed (from SAMPLE_LOOP_MODE)
};

class GranularEngine
{
public:
    GranularEngine() = default;

    // ── setup ──
    void prepare (double outputSampleRate) noexcept
    {
        outRate_ = outputSampleRate > 0.0 ? outputSampleRate : 48000.0;
        // AIR high-pass corner ~3 kHz one-pole (matches the Sample engine's exciter feel)
        airCoef_ = 1.f - std::exp (-2.f * kPi * 3000.f / (float) outRate_);
        if (airCoef_ < 0.f) airCoef_ = 0.f; if (airCoef_ > 1.f) airCoef_ = 1.f;
        buildWindows();
        recomputeDerived();
    }

    void setSample (const float* const* ch, int numCh, int numSamples, double nativeRate) noexcept
    {
        (void) nativeRate; // pitch arrives fully-computed via noteOn (like SampleEngine's voice path)
        ch0_        = (numCh > 0) ? ch[0] : nullptr;
        ch1_        = (numCh > 1) ? ch[1] : ch0_;
        numSamples_ = numSamples;
    }

    void clearSample() noexcept { ch0_ = ch1_ = nullptr; numSamples_ = 0; }
    bool hasSample()  const noexcept { return ch0_ != nullptr && numSamples_ > 1; }

    // ── per-block params ──
    void setRegion (float start01, float end01) noexcept
    {
        regStart01_ = clamp01 (start01);
        regEnd01_   = clamp01 (end01);
        if (regEnd01_ < regStart01_) { float t = regStart01_; regStart01_ = regEnd01_; regEnd01_ = t; }
        if (regEnd01_ - regStart01_ < 0.001f) regEnd01_ = clamp01 (regStart01_ + 0.001f);
        refreshLoopBounds();   // bracket ∩ region depends on both — refresh whenever either moves
    }
    void setParams (const GranularEngineParams& p) noexcept { p_ = p; recomputeDerived(); }
    void setPitchRatio (double r) noexcept { basePitch_ = r; }

    // ── note lifecycle ──
    void noteOn (double pitchRatio, uint32_t seed) noexcept
    {
        basePitch_ = pitchRatio > 0.0 ? pitchRatio : 1.0;
        rng_       = seed ? seed : 0x9E3779B9u;
        for (auto& g : pool_) g.active = false;
        scanPos_   = regStart01_ + p_.position * (regEnd01_ - regStart01_);
        countdown_ = 0.0;
        active_    = true;
        releasing_ = false;
        airHpL_ = airHpR_ = 0.f;
        pingDir_   = 1.f;      // Ping-Pong starts moving toward the end
        oneShotDone_ = false;  // One-Shot / Tailed: not yet reached the far edge
        caught_    = false;    // loop modes: head not yet inside the loop bracket (lead-in from the anchor)
    }
    void noteOff() noexcept { releasing_ = true; }

    bool  isActive()   const noexcept { return active_; }
    float scanPos01()  const noexcept { return (float) scanPos_; }

    // ── render one stereo frame; returns false once fully silent/finished ──
    bool tick (float& outL, float& outR) noexcept
    {
        outL = outR = 0.f;
        if (! active_ || ! hasSample()) return active_;

        // 1) async scheduler — spawn on a jittered countdown (async cloud, not periodic)
        countdown_ -= 1.0;
        while (countdown_ <= 0.0)
        {
            if (! releasing_ && ! oneShotDone_) spawnGrain();   // One-Shot: stop spawning once the head hits the far edge
            const double interval = outRate_ / (double) densHz_;
            const double jit      = 0.5 * interval * (nextRand01() * 2.f - 1.f); // regularity jitter
            countdown_ += interval + jit;
            if (countdown_ < 1.0) countdown_ = 1.0;
        }

        // 2) grain mix + retire
        float accL = 0.f, accR = 0.f;
        bool  any  = false;
        const double lo = effLo01() * (numSamples_ - 1);   // caught by the loop bracket → grains live in it
        const double hi = effHi01() * (numSamples_ - 1);
        const double span = (hi - lo) > 1.0 ? (hi - lo) : 1.0;
        for (auto& g : pool_)
        {
            if (! g.active) continue;
            any = true;
            const float ph = (float) g.age / (float) g.len;
            const float w  = windowAt (p_.shape, p_.skew, ph);
            float sl, sr; readHermite (g.readPos, sl, sr);
            accL += w * g.gain * g.panL * sl;
            accR += w * g.gain * g.panR * sr;
            g.readPos += g.readInc;
            ++g.age;
            // wrap the read within the region so long/scanning grains never run off the buffer
            if (g.readPos > hi) g.readPos = lo + std::fmod (g.readPos - lo, span);
            if (g.readPos < lo) g.readPos = hi - std::fmod (hi - g.readPos, span);
            if (g.age >= g.len) g.active = false; // retire at window->0, no click
        }

        // 3) equal-power-ish normalization vs the expected overlap (stable, count-independent).
        //    norm_ is cached per block (recomputeDerived) — no per-sample sqrt/pow.
        outL = accL * norm_;
        outR = accR * norm_;

        // 4) AIR — Chebyshev-style high-harmonic exciter (identical math to the Sample osc's AIR)
        if (p_.air > 0.001f)
        {
            const float drv = 1.f + p_.air * 20.f;
            airHpL_ += airCoef_ * (outL - airHpL_); const float hpL = outL - airHpL_;
            outL += p_.air * 2.f * (std::tanh (hpL * drv) - hpL);
            airHpR_ += airCoef_ * (outR - airHpR_); const float hpR = outR - airHpR_;
            outR += p_.air * 2.f * (std::tanh (hpR * drv) - hpR);
        }

        // 5) advance the read-head per LOOP MODE — Scan=0 stays frozen (grains keep spraying from here).
        //    0=One-Shot (clamp+stop), 1=Forward loop, 2=Reverse loop, 3=Ping-Pong (bounce), 4=Tailed(=One-Shot).
        if (p_.scan != 0.f && ! oneShotDone_)
        {
            const double range = (regEnd01_ - regStart01_) > 0.f ? (regEnd01_ - regStart01_) : 1.0;
            double rate = (double) p_.scan;
            if      (p_.loopMode == 2) rate = -std::fabs (rate);              // Reverse — always toward start
            else if (p_.loopMode == 3) rate =  std::fabs (rate) * pingDir_;   // Ping-Pong — |scan| × bounce dir
            scanPos_ += rate * 2.0 / outRate_ * range;

            if (p_.loopMode == 0 || p_.loopMode == 4)            // One-Shot / Tailed — clamp at the REGION edge, stop spawning
            {
                if (scanPos_ >= regEnd01_)   { scanPos_ = regEnd01_;   oneShotDone_ = true; }
                if (scanPos_ <= regStart01_) { scanPos_ = regStart01_; oneShotDone_ = true; }
            }
            else
            {
                // LOOP modes honour the LOOP BRACKET (loopLo_/loopHi_), Sample-engine style:
                // the head leads in from the Position anchor, is "caught" the moment it enters
                // the bracket (forward from below, reverse from above), then loops INSIDE it.
                // Default bracket = whole region → caught on the first tick → legacy behaviour.
                if (! caught_)
                {
                    caught_ = (rate >= 0.0) ? (scanPos_ >= loopLo_) : (scanPos_ <= loopHi_);
                    if (! caught_)   // still leading in — wrap at the REGION edges so the head can reach the bracket
                    {
                        if (scanPos_ > regEnd01_)   scanPos_ = regStart01_ + (scanPos_ - regEnd01_);
                        if (scanPos_ < regStart01_) scanPos_ = regEnd01_   - (regStart01_ - scanPos_);
                    }
                }
                if (caught_)
                {
                    if (p_.loopMode == 3)                         // Ping-Pong — bounce at the bracket edges
                    {
                        if (scanPos_ >= loopHi_) { scanPos_ = loopHi_; pingDir_ = -1.f; }
                        if (scanPos_ <= loopLo_) { scanPos_ = loopLo_; pingDir_ =  1.f; }
                    }
                    else                                          // Forward (1) / Reverse (2) — wrap inside the bracket
                    {
                        // fmod fold (not a single subtract) so a head caught far outside — or a
                        // bracket dragged away mid-note — lands inside in ONE step, no spiral.
                        const double lspan = loopHi_ - loopLo_;   // refreshLoopBounds guarantees > 0
                        if (scanPos_ > loopHi_) scanPos_ = loopLo_ + std::fmod (scanPos_ - loopLo_, lspan);
                        if (scanPos_ < loopLo_) scanPos_ = loopHi_ - std::fmod (loopHi_ - scanPos_, lspan);
                    }
                }
            }
        }

        if (releasing_ && ! any) active_ = false; // freed once every grain has retired
        return active_;
    }

    // ── UI follower: fill up to maxOut recent grain births ──
    int cloudSnapshot (GrainViz* out, int maxOut) const noexcept
    {
        int n = 0;
        const double denom = numSamples_ > 1 ? (double) (numSamples_ - 1) : 1.0;
        for (const auto& g : pool_)
        {
            if (! g.active) continue;
            if (n >= maxOut) break;
            out[n].pos01 = (float) (g.readPos / denom);
            out[n].age01 = (float) g.age / (float) (g.len > 0 ? g.len : 1);
            out[n].pan   = g.panR - g.panL;
            ++n;
        }
        return n;
    }

    // ── testing accessors (offline harness only) ──
    int  activeGrainsForTesting()   const noexcept { int n = 0; for (auto& g : pool_) n += g.active ? 1 : 0; return n; }
    bool caughtForTesting()         const noexcept { return caught_; }
    double loopLoForTesting()       const noexcept { return loopLo_; }
    double loopHiForTesting()       const noexcept { return loopHi_; }
    bool anyGrainReversedForTesting() const noexcept { for (auto& g : pool_) if (g.active && g.readInc < 0.0) return true; return false; }
    static double snapToKeyForTesting (double semis, int key) noexcept { return snapToKey (semis, key); }
    float windowForTesting (float shape, float skew, float phase) const noexcept { return windowAt (shape, skew, phase); }

private:
    struct Grain
    {
        double readPos = 0.0, readInc = 0.0;
        int    age = 0, len = 0;
        float  gain = 0.f, panL = 0.f, panR = 0.f;
        bool   active = false;
    };

    // ── range maps (research-grounded, AMPLIFIED + Stretch-coupled) ──
    // Stretch elongates grains (and fills the gaps with density) for a smooth time-stretch/freeze
    // pad; the mode biases the character: Tones = long+smooth, Beats = short+dense, Texture = mid+diffuse.
    float stretchLenMul() const noexcept
    {
        const float s = clamp01 (p_.stretch);
        switch (p_.stretchMode) { case 1: return 1.f + s * 0.6f;   // Beats — stay punchy
                                  case 2: return 1.f + s * 2.5f;   // Texture — diffuse
                                  default: return 1.f + s * 4.0f; } // Tones — long & smooth
    }
    float stretchDensMul() const noexcept
    {
        const float s = clamp01 (p_.stretch);
        switch (p_.stretchMode) { case 1: return 1.f + s * 3.0f;   // Beats — more grains = stutter fill
                                  case 2: return 1.f + s * 1.6f;   // Texture
                                  default: return 1.f + s * 2.2f; } // Tones
    }
    // CPU: derive densHz_ / grainLenSamp_ / norm_ ONCE per block (setParams), not per sample.
    // p_ is constant within a block, so these are bit-identical to the old per-sample recompute —
    // this just stops re-evaluating pow()/sqrt() on every tick(). (Was densityHz/grainLenSec/overlap.)
    void recomputeDerived() noexcept
    {
        const float d = clamp01 (p_.density);
        densHz_ = std::pow (220.f, d) * stretchDensMul();                     // 1..220 g/s (+Stretch)
        const float s = clamp01 (p_.size);
        const float glenSec = 0.002f * std::pow (450.f, s) * stretchLenMul(); // 2..900 ms (+Stretch)
        grainLenSamp_ = glenSec * (float) outRate_;
        const float ov = densHz_ * glenSec;
        norm_ = 1.f / std::sqrt (ov < 1.f ? 1.f : ov);
        refreshLoopBounds();
    }

    // Effective LOOP bracket = [loopStart,loopEnd] sorted, intersected with the region; a
    // degenerate bracket (<0.001 span) falls back to the whole region, so the default 0..1
    // bracket makes every loop mode behave exactly as before this fix.
    void refreshLoopBounds() noexcept
    {
        float ls = clamp01 (p_.loopStart), le = clamp01 (p_.loopEnd);
        if (le < ls) { const float t = ls; ls = le; le = t; }
        loopLo_ = ls > regStart01_ ? (double) ls : (double) regStart01_;
        loopHi_ = le < regEnd01_   ? (double) le : (double) regEnd01_;
        if (loopHi_ - loopLo_ < 0.001) { loopLo_ = (double) regStart01_; loopHi_ = (double) regEnd01_; }
    }

    // Bounds the cloud lives in RIGHT NOW: the region until the head is caught by the
    // bracket, then the bracket — births, grain read-wrap and the scan head all use these.
    double effLo01() const noexcept { return caught_ ? loopLo_ : (double) regStart01_; }
    double effHi01() const noexcept { return caught_ ? loopHi_ : (double) regEnd01_; }

    void spawnGrain() noexcept
    {
        int slot = -1;
        for (int i = 0; i < kPool; ++i) if (! pool_[i].active) { slot = i; break; }
        if (slot < 0) return; // Max-Grains reached -> skip-and-wait (graceful, no glitch)

        Grain& g = pool_[slot];

        // birth position: scan head + sprayed jitter, clamped into the region.
        // AMPLIFIED — full Spray reaches ±half the region (Texture stretch adds extra scatter).
        float sprayAmt = clamp01 (p_.spray);
        if (p_.stretchMode == 2) sprayAmt = clamp01 (sprayAmt + p_.stretch * 0.4f);
        double birth01 = scanPos_ + sprayAmt * (nextRand01() * 2.f - 1.f) * 0.5;
        const double bLo = effLo01(), bHi = effHi01();   // caught by the loop bracket → births stay inside it
        birth01 = birth01 < bLo ? bLo : (birth01 > bHi ? bHi : birth01);
        g.readPos = birth01 * (numSamples_ - 1);

        // per-grain pitch (base + spray), decoupled from the scan head.
        // Key OFF: pitch + wide free spray. Key ON: pitch + an in-key octave/degree draw
        // (the "shimmer" — audible even at pitchSpray=0), then snapped back to the scale.
        double semis = (double) p_.pitch;
        if (p_.key > 0) semis += randKeyOffset (p_.key);
        semis += (double) p_.pitchSpray * (nextRand01() * 2.f - 1.f) * 24.0;   // AMPLIFIED ±24 st
        if (p_.key > 0) semis = snapToKey (semis, p_.key);
        const double pr = basePitch_ * std::pow (2.0, semis / 12.0);

        // per-grain direction bias: -1 all-reverse, +1 all-forward, 0 = coin-flip
        const bool rev = (p_.dir <= -1.f) ? true
                       : (p_.dir >=  1.f) ? false
                       : (nextRand01() > (p_.dir * 0.5f + 0.5f));
        g.readInc = pr * (rev ? -1.0 : 1.0);

        // grain length from Size (+Stretch), clamped so it can't exceed the region span.
        // grainLenSamp_ is cached per block (recomputeDerived) — no per-spawn pow.
        int len = (int) grainLenSamp_;
        const int spanSamp = (int) ((regEnd01_ - regStart01_) * (numSamples_ - 1));
        const int maxLen = spanSamp > 8 ? spanSamp * 4 : 8;   // grains may re-scan the region a few times
        if (len < 4)      len = 4;
        if (len > maxLen) len = maxLen;
        g.len = len;
        g.age = 0;

        // per-grain equal-power pan from Width spray
        const float pan = p_.width * (nextRand01() * 2.f - 1.f); // -w..+w
        const float a   = (pan * 0.5f + 0.5f) * 1.5707963f;       // 0..pi/2
        g.panL = std::cos (a);
        g.panR = std::sin (a);
        g.gain = 1.f;

        g.active = true;
    }

    // ── grain windows: Flat-top (bright/dense) <-> Hann (neutral) <-> Bell (soft/sparse) ──
    // All three are exactly 0 at both ends → grain births/deaths physically cannot click,
    // even sustained as a pad. Shape morphs across them for a dramatic, audible timbre sweep.
    void buildWindows() noexcept
    {
        double sf = 0, sh = 0, sb = 0;   // window energies, for the RMS match below
        for (int i = 0; i < kWin; ++i)
        {
            const float x = (float) i / (float) (kWin - 1);
            const float hann = 0.5f - 0.5f * std::cos (2.f * kPi * x);

            // Flat-top = Tukey with a small taper (mostly flat, sharp-ish zero-edges → brighter/buzzier)
            const float alpha = 0.12f;
            float flat;
            if (x < alpha * 0.5f)
                flat = 0.5f * (1.f + std::cos (kPi * (2.f * x / alpha - 1.f)));
            else if (x > 1.f - alpha * 0.5f)
                flat = 0.5f * (1.f + std::cos (kPi * (2.f * x / alpha - 2.f / alpha + 1.f)));
            else
                flat = 1.f;

            // Bell = tight Gaussian, multiplied by Hann so the ends are guaranteed 0 (peak stays 1)
            const float d = (x - 0.5f) / 0.16f;
            const float bell = std::exp (-0.5f * d * d) * hann;

            winFlat_[i] = flat;
            winHann_[i] = hann;
            winBell_[i] = bell;
            sf += (double) flat * flat; sh += (double) hann * hann; sb += (double) bell * bell;
        }
        // RMS-match Flat and Bell to the Hann window so SHAPE morphs TIMBRE, NOT loudness. (This is why
        // Shape=0 felt like it "disappeared": the flat window was simply louder. Now all shapes are equal
        // energy — the difference is purely tonal: flat = brighter/fuller, bell = softer/grainier.)
        const float rf = (sf > 1e-9) ? (float) std::sqrt (sh / sf) : 1.f;
        const float rb = (sb > 1e-9) ? (float) std::sqrt (sh / sb) : 1.f;
        for (int i = 0; i < kWin; ++i) { winFlat_[i] *= rf; winBell_[i] *= rb; }
    }

    float windowAt (float shape01, float skew, float phase01) const noexcept
    {
        float ph = phase01;
        if (skew != 0.f)
        {
            // +skew pushes the peak later (swell), -skew earlier (pluck)
            float k = 1.f + (skew < 0.f ? skew * 0.6f : skew * 1.5f);
            if (k < 0.05f) k = 0.05f;
            ph = std::pow (phase01 < 0.f ? 0.f : phase01, k);
        }
        ph = ph < 0.f ? 0.f : (ph > 1.f ? 1.f : ph);
        const int idx = (int) (ph * (float) (kWin - 1));
        const float s = shape01 < 0.f ? 0.f : (shape01 > 1.f ? 1.f : shape01);
        if (s < 0.5f)  { const float t = s * 2.f;          return winFlat_[idx] + (winHann_[idx] - winFlat_[idx]) * t; }
        else           { const float t = (s - 0.5f) * 2.f; return winHann_[idx] + (winBell_[idx] - winHann_[idx]) * t; }
    }

    // ── interpolation (reused verbatim from SampleEngine) ──
    inline float samp0 (int i) const noexcept { i = i < 0 ? 0 : (i >= numSamples_ ? numSamples_ - 1 : i); return ch0_[i]; }
    inline float samp1 (int i) const noexcept { i = i < 0 ? 0 : (i >= numSamples_ ? numSamples_ - 1 : i); return ch1_[i]; }
    static inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c = (x1 - xm1) * 0.5f, v = x0 - x1, w = c + v, a = w + v + (x2 - x0) * 0.5f, bn = w + a;
        return ((((a * t) - bn) * t + c) * t + x0);
    }
    void readHermite (double p, float& l, float& r) const noexcept
    {
        const int   i1 = (int) std::floor (p);
        const float f  = (float) (p - (double) i1);
        l = hermite (samp0 (i1 - 1), samp0 (i1), samp0 (i1 + 1), samp0 (i1 + 2), f);
        r = hermite (samp1 (i1 - 1), samp1 (i1), samp1 (i1 + 1), samp1 (i1 + 2), f);
    }

    inline float nextRand01() noexcept
    {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (float) ((rng_ & 0x00FFFFFFu) / (double) 0x01000000);
    }
    static inline float clamp01 (float x) noexcept { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    // Key — snap a semitone offset to the nearest scale degree (0 = Off).
    static double snapToKey (double semis, int key) noexcept
    {
        if (key <= 0) return semis;
        const int* tbl; int n; keyTable (key, tbl, n);
        const double oc = std::floor (semis / 12.0);
        const double pc = semis - oc * 12.0;
        double best = tbl[0], bd = 1.0e9;
        for (int i = 0; i < n; ++i) { const double d = std::fabs (pc - (double) tbl[i]); if (d < bd) { bd = d; best = (double) tbl[i]; } }
        const double dWrap = std::fabs (pc - 12.0); if (dWrap < bd) best = 12.0;
        return oc * 12.0 + best;
    }

    static void keyTable (int key, const int*& tbl, int& n) noexcept
    {
        static const int oct[]   = { 0 };
        static const int fifth[] = { 0, 7 };
        static const int chord[] = { 0, 4, 7 };
        static const int maj[]   = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minr[]  = { 0, 2, 3, 5, 7, 8, 10 };
        static const int pent[]  = { 0, 2, 4, 7, 9 };
        switch (key)
        {
            case 1:  tbl = oct;   n = 1; break;
            case 2:  tbl = fifth; n = 2; break;
            case 3:  tbl = chord; n = 3; break;
            case 4:  tbl = maj;   n = 7; break;
            case 5:  tbl = minr;  n = 7; break;
            default: tbl = pent;  n = 5; break;   // 6 = Penta
        }
    }

    // A random in-key interval spanning a musical range of octaves — THIS is what makes Key
    // audible: it turns the grain cloud into a shimmering in-key chord/shimmer even when
    // pitchSpray is 0. Octave weighting leans on the root/+1 octave so it stays musical.
    double randKeyOffset (int key) noexcept
    {
        const int* tbl; int n; keyTable (key, tbl, n);
        const int deg = tbl[(int) (nextRand01() * (float) n) % (n > 0 ? n : 1)];
        static const int octWeighted[] = { -12, 0, 0, 0, 12, 12, 12, 24 };   // mostly root/+1 oct
        const int oc = octWeighted[(int) (nextRand01() * 8.f) & 7];
        return (double) (oc + deg);
    }

    static constexpr int   kPool = 64;
    static constexpr int   kWin  = 2048;
    static constexpr float kPi   = 3.14159265358979f;

    std::array<Grain, kPool> pool_ {};
    std::array<float, kWin>  winFlat_ {};
    std::array<float, kWin>  winHann_ {};
    std::array<float, kWin>  winBell_ {};

    const float* ch0_ = nullptr;
    const float* ch1_ = nullptr;
    int    numSamples_ = 0;
    double outRate_ = 48000.0;
    double basePitch_ = 1.0;
    double scanPos_ = 0.0;
    double countdown_ = 0.0;
    float  regStart01_ = 0.f, regEnd01_ = 1.f;
    double loopLo_ = 0.0, loopHi_ = 1.0;   // effective LOOP bracket = [loopStart,loopEnd] ∩ region (refreshLoopBounds)
    bool   caught_ = false;                // sample-engine "catch": true once the scan head has entered the bracket
    uint32_t rng_ = 0x9E3779B9u;
    float  airCoef_ = 0.3f, airHpL_ = 0.f, airHpR_ = 0.f;   // AIR exciter high-pass state
    float  densHz_ = 8.f, grainLenSamp_ = 480.f, norm_ = 1.f;   // cached per-block derived values (CPU)
    float  pingDir_ = 1.f;         // Ping-Pong bounce direction (+1 → end, -1 → start)
    bool   oneShotDone_ = false;   // One-Shot/Tailed: head reached the far edge → stop spawning
    bool   active_ = false, releasing_ = false;
    GranularEngineParams p_ {};
};

} // namespace tw
