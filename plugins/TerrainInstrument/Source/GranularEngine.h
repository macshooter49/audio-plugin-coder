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
//  are windowed by construction, so grain start/stop physically cannot click.
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
    float density    = 0.4f;  // 0..1 -> 1..200 grains/sec (log)
    float size       = 0.25f; // 0..1 -> 2..500 ms grain length (log)
    float spray      = 0.10f; // 0..1 grain-birth position jitter
    float shape      = 0.5f;  // 0..1 window morph Tukey(0) -> Gaussian(1)
    float skew       = 0.f;   // -1..+1 window asymmetry (pluck <-> swell)
    float pitch      = 0.f;   // base grain transpose, semitones
    float pitchSpray = 0.f;   // 0..1 per-grain pitch scatter (up to +/-12 st)
    float width      = 0.f;   // 0..1 per-grain stereo spread
    float dir        = 1.f;   // -1..+1 per-grain direction bias (-1 rev, 0 rand, +1 fwd)
    float life       = 0.15f; // 0..1 living-weather macro (Phase 5)
    float jump       = 0.5f;  // 0..1 onset soft-build <-> instant (Phase 5)
    int   key        = 0;     // 0=Off 1=Oct 2=5th 3=Chord 4=Maj 5=Min 6=Penta (Phase 5)
};

class GranularEngine
{
public:
    GranularEngine() = default;

    // ── setup ──
    void prepare (double outputSampleRate) noexcept
    {
        outRate_ = outputSampleRate > 0.0 ? outputSampleRate : 48000.0;
        buildWindows();
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
    }
    void setParams (const GranularEngineParams& p) noexcept { p_ = p; }
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
    }
    void noteOff() noexcept { releasing_ = true; }

    bool  isActive()   const noexcept { return active_; }
    float scanPos01()  const noexcept { return (float) scanPos_; }

    // ── render one stereo frame; returns false once fully silent/finished ──
    bool tick (float& outL, float& outR) noexcept
    {
        outL = outR = 0.f;
        if (! active_ || ! hasSample()) return active_;

        // 0) Life — slow bounded drift on density/size/spray (the "living" weather; ~5 Hz update)
        if (p_.life > 0.f && --lifeTick_ <= 0)
        {
            lifeTick_ = 256;
            lifeD_  = 0.97f * lifeD_  + 0.05f * (ouRand() * 2.f - 1.f);
            lifeSz_ = 0.97f * lifeSz_ + 0.05f * (ouRand() * 2.f - 1.f);
            lifeSp_ = 0.97f * lifeSp_ + 0.05f * (ouRand() * 2.f - 1.f);
        }

        // 1) async scheduler — spawn on a jittered countdown (async cloud, not periodic)
        countdown_ -= 1.0;
        while (countdown_ <= 0.0)
        {
            if (! releasing_) spawnGrain();
            const double interval = outRate_ / densityHz();
            const double jit      = 0.5 * interval * (nextRand01() * 2.f - 1.f); // regularity jitter
            countdown_ += interval + jit;
            if (countdown_ < 1.0) countdown_ = 1.0;
        }

        // 2) grain mix + retire
        float accL = 0.f, accR = 0.f;
        bool  any  = false;
        const double lo = regStart01_ * (numSamples_ - 1);
        const double hi = regEnd01_   * (numSamples_ - 1);
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

        // 3) equal-power-ish normalization vs the expected overlap (stable, count-independent)
        const float norm = 1.f / std::sqrt (overlapEstimate());
        outL = accL * norm;
        outR = accR * norm;

        // 4) advance the read-head — Scan=0 stays frozen (grains keep spraying from here)
        if (p_.scan != 0.f)
        {
            const double range = (regEnd01_ - regStart01_) > 0.f ? (regEnd01_ - regStart01_) : 1.0;
            scanPos_ += (double) p_.scan * 2.0 / outRate_ * range;
            if (scanPos_ > regEnd01_) scanPos_ = regStart01_ + (scanPos_ - regEnd01_);
            if (scanPos_ < regStart01_) scanPos_ = regEnd01_ - (regStart01_ - scanPos_);
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
    bool anyGrainReversedForTesting() const noexcept { for (auto& g : pool_) if (g.active && g.readInc < 0.0) return true; return false; }
    static double snapToKeyForTesting (double semis, int key) noexcept { return snapToKey (semis, key); }

private:
    struct Grain
    {
        double readPos = 0.0, readInc = 0.0;
        int    age = 0, len = 0;
        float  gain = 0.f, panL = 0.f, panR = 0.f;
        bool   active = false;
    };

    // ── range maps (research-grounded) ──
    float densityHz()  const noexcept { const float d = clamp01 (p_.density + p_.life * lifeD_  * 0.35f); return 1.f * std::pow (200.f, d); }   // 1..200 g/s (+Life)
    float grainLenSec() const noexcept { const float s = clamp01 (p_.size    + p_.life * lifeSz_ * 0.35f); return 0.002f * std::pow (250.f, s); } // 2..500 ms (+Life)
    float overlapEstimate() const noexcept
    {
        const float o = densityHz() * grainLenSec();
        return o < 1.f ? 1.f : o;
    }

    void spawnGrain() noexcept
    {
        int slot = -1;
        for (int i = 0; i < kPool; ++i) if (! pool_[i].active) { slot = i; break; }
        if (slot < 0) return; // Max-Grains reached -> skip-and-wait (graceful, no glitch)

        Grain& g = pool_[slot];

        // birth position: scan head + sprayed jitter (+Life drift), clamped into the region
        const float sprayAmt = clamp01 (p_.spray + p_.life * lifeSp_ * 0.35f);
        double birth01 = scanPos_ + sprayAmt * (nextRand01() * 2.f - 1.f) * 0.25;
        birth01 = birth01 < regStart01_ ? regStart01_ : (birth01 > regEnd01_ ? regEnd01_ : birth01);
        g.readPos = birth01 * (numSamples_ - 1);

        // per-grain pitch (base + spray), decoupled from the scan head; Key snaps it to a scale
        double semis = (double) p_.pitch + (double) p_.pitchSpray * (nextRand01() * 2.f - 1.f) * 12.0;
        semis = snapToKey (semis, p_.key);
        const double pr = basePitch_ * std::pow (2.0, semis / 12.0);

        // per-grain direction bias: -1 all-reverse, +1 all-forward, 0 = coin-flip
        const bool rev = (p_.dir <= -1.f) ? true
                       : (p_.dir >=  1.f) ? false
                       : (nextRand01() > (p_.dir * 0.5f + 0.5f));
        g.readInc = pr * (rev ? -1.0 : 1.0);

        // grain length from Size
        g.len = (int) (grainLenSec() * outRate_);
        if (g.len < 4) g.len = 4;
        g.age = 0;

        // per-grain equal-power pan from Width spray
        const float pan = p_.width * (nextRand01() * 2.f - 1.f); // -w..+w
        const float a   = (pan * 0.5f + 0.5f) * 1.5707963f;       // 0..pi/2
        g.panL = std::cos (a);
        g.panR = std::sin (a);
        g.gain = 1.f;

        g.active = true;
    }

    // ── grain window: Tukey (flat-top) <-> Gaussian (bell), with phase-skew ──
    void buildWindows() noexcept
    {
        const float alpha = 0.5f; // Tukey taper fraction
        for (int i = 0; i < kWin; ++i)
        {
            const float x = (float) i / (float) (kWin - 1);
            float t;
            if (x < alpha * 0.5f)
                t = 0.5f * (1.f + std::cos (kPi * (2.f * x / alpha - 1.f)));
            else if (x > 1.f - alpha * 0.5f)
                t = 0.5f * (1.f + std::cos (kPi * (2.f * x / alpha - 2.f / alpha + 1.f)));
            else
                t = 1.f;
            tukey_[i] = t;

            const float d = (x - 0.5f) / 0.20f;
            gauss_[i] = std::exp (-0.5f * d * d);
        }
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
        const float a = tukey_[idx];
        const float b = gauss_[idx];
        return a + (b - a) * shape01;
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
    inline float ouRand() noexcept   // separate stream for Life — leaves grain determinism untouched
    {
        ouRng_ ^= ouRng_ << 13; ouRng_ ^= ouRng_ >> 17; ouRng_ ^= ouRng_ << 5;
        return (float) ((ouRng_ & 0x00FFFFFFu) / (double) 0x01000000);
    }
    static inline float clamp01 (float x) noexcept { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    // Key — snap a per-grain semitone offset to the nearest scale degree (0 = Off).
    static double snapToKey (double semis, int key) noexcept
    {
        if (key <= 0) return semis;
        static const int oct[]   = { 0 };
        static const int fifth[] = { 0, 7 };
        static const int chord[] = { 0, 4, 7 };
        static const int maj[]   = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minr[]  = { 0, 2, 3, 5, 7, 8, 10 };
        static const int pent[]  = { 0, 2, 4, 7, 9 };
        const int* tbl; int n;
        switch (key)
        {
            case 1:  tbl = oct;   n = 1; break;
            case 2:  tbl = fifth; n = 2; break;
            case 3:  tbl = chord; n = 3; break;
            case 4:  tbl = maj;   n = 7; break;
            case 5:  tbl = minr;  n = 7; break;
            default: tbl = pent;  n = 5; break;   // 6 = Penta
        }
        const double oc = std::floor (semis / 12.0);
        const double pc = semis - oc * 12.0;
        double best = tbl[0], bd = 1.0e9;
        for (int i = 0; i < n; ++i) { const double d = std::fabs (pc - (double) tbl[i]); if (d < bd) { bd = d; best = (double) tbl[i]; } }
        const double dWrap = std::fabs (pc - 12.0); if (dWrap < bd) best = 12.0;
        return oc * 12.0 + best;
    }

    static constexpr int   kPool = 64;
    static constexpr int   kWin  = 2048;
    static constexpr float kPi   = 3.14159265358979f;

    std::array<Grain, kPool> pool_ {};
    std::array<float, kWin>  tukey_ {};
    std::array<float, kWin>  gauss_ {};

    const float* ch0_ = nullptr;
    const float* ch1_ = nullptr;
    int    numSamples_ = 0;
    double outRate_ = 48000.0;
    double basePitch_ = 1.0;
    double scanPos_ = 0.0;
    double countdown_ = 0.0;
    float  regStart01_ = 0.f, regEnd01_ = 1.f;
    uint32_t rng_ = 0x9E3779B9u;
    uint32_t ouRng_ = 0x2545F491u;                       // Life drift RNG
    float  lifeD_ = 0.f, lifeSz_ = 0.f, lifeSp_ = 0.f;   // bounded OU drift state (density/size/spray)
    int    lifeTick_ = 0;
    bool   active_ = false, releasing_ = false;
    GranularEngineParams p_ {};
};

} // namespace tw
