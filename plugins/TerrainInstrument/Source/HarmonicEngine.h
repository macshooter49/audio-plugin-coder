#pragma once
// ════════════════════════════════════════════════════════════════════════════
//  tw::HarmonicEngine — Terrain HARMONIC oscillator (Engine::HARM, slot 5)
//
//  Pure ADDITIVE synthesis: up to 512 partials rendered as a per-voice sine
//  bank. No sample, no wavetable — the spectrum is built procedurally every
//  block from two mode rows and twelve knob laws, then rendered with the same
//  proven machinery as the Resynth engine (per-partial declick ramps, phase
//  accumulators, shared global partial budget, constant-cost unison).
//
//  MAIN row (base spectrum families; HUE = regime morph inside the family):
//    0 BLADE   saw → hollow reed (rolloff steepens, evens fade above 50%)
//    1 NEON    CS-80 lane: saw+pulse blend + PWM wobble + resonant emphasis;
//              above 50% the resonance sharpens and auto-sweeps per note
//    2 CONSOLE drawbar organ: flute → full registration → detuned carillon
//    3 CHANT   vocal: 1/n bed × 3 log-Gaussian formants, oo→ah → scattered choir
//    4 BRONZE  stiff-string/bell stretch + golden-comb modal mask → sparse gong
//    5 HORNET  band-limited pulse-train buzz: 12 partials → the full swarm
//
//  SCULPT row (spectral transforms; CARVE = regime depth, CHURN = motion rate):
//    0 KEEL    pivot tilt: dark warmth → pivot slides up + lows duck (megaphone)
//    1 SPLAY   anchored inharmonic stretch: glass → gong with the root LOCKED
//    2 CULL    sieves: full → odd → primes → Fibonacci survivors (renormed)
//    3 TIDE    traveling amplitude ripple: slow swell → spectral Leslie flutter
//    4 TERRACE dB-quantize the spectrum: lo-fi shelves → dithered sputter
//    5 CLANG   sum/difference intermod lattice on the loudest partials
//
//  Voice knobs:  HUE · PARTIALS(count) · LEAN(tilt) · FAN(stereo) · GRIT(life) · BRAID(ensemble)
//  Sculpt knobs: CARVE · CHURN · ROOT(fund guard+sub) · SHINE(+oct ghost) · WILT(time arrow) · FIZZ(noise fur)
//
//  DSP ground rules honored (house):
//   - every amplitude change rides a per-partial linear ramp (declick)
//   - phase is a running accumulator, never recomputed (freq changes are C0-safe)
//   - Nyquist is a smoothstep TAPER (0.40..0.48 fs), never a hard gate
//   - RMS renorm with a slow-smoothed gain (τ≈200 ms) — knobs change character,
//     never loudness; the partial-count knob fades partials equal-power
//   - global partial budget shared with Resynth: keep-loudest thinning, no walls
//   - exponential-bias curves, zero audio-thread allocation after prepare()
//
//  Offline proof loop (Pattern A — NOT in CMakeLists):
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/HarmonicEngine_test.cpp -o /tmp/hm && /tmp/hm
// ════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace tw {

namespace harm {
    constexpr int   kMaxPartials = 512;   // hard slot ceiling (Pigments-class)
    constexpr int   kMinCount    = 8;     // PARTIALS knob floor
    constexpr int   kBands       = 16;    // GRIT banded-OU resolution
    constexpr int   kBraidMax    = 24;    // max ensemble twin pairs
    constexpr int   kClangSeeds  = 6;     // loudest partials feeding the intermod lattice
    constexpr float kPi          = 3.14159265358979323846f;
    constexpr float kTargetRms   = 0.16f; // renorm target (matches the other engines' loudness lane)
    constexpr int   kDispBins    = 96;    // UI partial-bar resolution
}

// ── knob/mode snapshot pushed from the processor (cheap store, block-rate) ──
struct HarmParams
{
    int   mainMode   = 0;     // 0 Blade 1 Neon 2 Console 3 Chant 4 Bronze 5 Hornet
    int   sculptMode = 0;     // 0 Keel 1 Splay 2 Cull 3 Tide 4 Terrace 5 Clang
    float hue    = 0.35f;     // main-family regime morph
    float count  = 0.5f;      // partial count, log taper 8..512 (continuous — equal-power fade)
    float lean   = 0.5f;      // bipolar spectral tilt (0.5 = flat), ±12 dB/oct
    float fan    = 0.0f;      // stereo: mono → odd/even split → golden-angle orbit
    float grit   = 0.0f;      // analog life: banded OU drift + slow global wander
    float braid  = 0.0f;      // ensemble: detune twin pairs on the loudest partials
    float carve  = 0.0f;      // sculpt regime depth
    float churn  = 0.5f;      // motion-rate scale for the animated regimes
    float root   = 0.0f;      // fundamental guard → half-ratio sub ghost
    float shine  = 0.0f;      // detuned +1 octave ghost spectrum
    float wilt   = 0.5f;      // time arrow: highs decay ↔ highs bloom (0.5 = off)
    float fizz   = 0.0f;      // per-partial noise fur (airy top → tuned waterfall)

    bool operator== (const HarmParams& o) const noexcept
    {
        return mainMode==o.mainMode && sculptMode==o.sculptMode
            && hue==o.hue && count==o.count && lean==o.lean && fan==o.fan
            && grit==o.grit && braid==o.braid && carve==o.carve && churn==o.churn
            && root==o.root && shine==o.shine && wilt==o.wilt && fizz==o.fizz;
    }
    bool operator!= (const HarmParams& o) const noexcept { return ! (*this == o); }
};

class HarmonicEngine
{
public:
    // ── setup ────────────────────────────────────────────────────────────────
    // bankOwner = true for the unison ANCHOR (allocates the spectrum-build arrays);
    // siblings only allocate render state and borrow the anchor's bank via adoptBank().
    void prepare (double sampleRate, bool bankOwner = true) noexcept
    {
        rate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
        phase_.assign ((size_t) harm::kMaxPartials, 0.f);
        ampZL_.assign ((size_t) harm::kMaxPartials, 0.f);
        ampZR_.assign ((size_t) harm::kMaxPartials, 0.f);
        if (bankOwner)
        {
            ratio_.assign ((size_t) harm::kMaxPartials, 0.f);
            amp_.assign   ((size_t) harm::kMaxPartials, 0.f);
            panL_.assign  ((size_t) harm::kMaxPartials, 0.7071f);
            panR_.assign  ((size_t) harm::kMaxPartials, 0.7071f);
            fizzW_.assign ((size_t) harm::kMaxPartials, 0.f);
            baseAmp_.assign ((size_t) harm::kMaxPartials, 0.f);
        }
        scatMul_.assign ((size_t) harm::kMaxPartials, 1.f);
        bankOwner_ = bankOwner;
        bank_ = nullptr;
        gNorm_ = 1.f; tB_ = 0.f; orbit_ = 0.f;
        for (auto& b : ouAmp_)  b = 0.f;
        for (auto& b : ouFrq_)  b = 0.f;
        ouGlobal_ = 0.f;
    }

    void setPartialBudget (int* used, int cap) noexcept { budgetUsed_ = used; budgetCap_ = cap; }
    // constant-cost unison divisor (house pattern): N detuned banks pre-thin by ceil(√N)
    void setUnisonScale (int n) noexcept
    {
        int d = 1; const int nn = n > 1 ? n : 1;
        while (d * d < nn) ++d;
        unisonDiv_ = d < 1 ? 1 : d;
        uniScatCents_ = (nn > 1) ? 2.8f : 0.f;   // per-sibling partial decorrelation (hm2)
    }
    void setParams (const HarmParams& p) noexcept { p_ = p; }
    void setPitchRatio (double r) noexcept { pitchMul_ = r > 0.0 ? r : 1.0; }
    // live retune (oct/semi/cents moved mid-note): phase accumulators make this C0-continuous
    void setPlayedHz (double hz) noexcept { if (hz > 8.0) playedHz_ = hz; }
    // Display instances build the full bank regardless of note pitch or budget.
    void setDisplayMode (bool on) noexcept { displayMode_ = on; }

    void noteOn (double playedHz, std::uint32_t seed) noexcept
    {
        playedHz_ = playedHz > 8.0 ? playedHz : 261.6256;
        seed_ = seed ? seed : 0x9E3779B9u;
        rng_  = seed_ ^ 0xA511E9B3u;    // noise lane seeded ONCE per note, then free-runs
                                        // (a per-block reseed would freeze the OU drift solid)
        // phase policy (cookbook): random per partial per note = lush, crest-safe, every note
        // a slightly different waveform. HORNET instead fires NEAR-ALIGNED phases — the
        // band-limited impulse-train buzz is its identity (small jitter keeps headroom sane).
        const bool buzz = (p_.mainMode == 5);
        for (int j = 0; j < harm::kMaxPartials; ++j)
            phase_[(size_t) j] = buzz ? 0.06f * hash01 ((std::uint32_t) j * 2654435761u ^ seed_)
                                      : hash01 ((std::uint32_t) j * 2654435761u ^ seed_);
        std::fill (ampZL_.begin(), ampZL_.end(), 0.f);   // start silent → first block ramps in
        std::fill (ampZR_.begin(), ampZR_.end(), 0.f);
        // per-sibling per-partial static frequency scatter — decorrelates the unison stack so
        // shared-bank animation reads as ENSEMBLE, not coherent vibrato (unison = end of chain)
        if (scatMul_.size() != (size_t) harm::kMaxPartials) scatMul_.assign ((size_t) harm::kMaxPartials, 1.f);
        for (int j = 0; j < harm::kMaxPartials; ++j)
            scatMul_[(size_t) j] = (uniScatCents_ > 0.f)
                ? centsMul (uniScatCents_ * (hash01 ((std::uint32_t) j * 40503u ^ seed_ ^ 0x1F123BB5u) - 0.5f) * 2.f)
                : 1.f;
        if (bankOwner_)
        {
            std::fill (fizzW_.begin(), fizzW_.end(), 0.f);
            for (auto& b : ouAmp_) b = 0.f;
            for (auto& b : ouFrq_) b = 0.f;
            ouGlobal_ = 0.f;
        }
        tB_ = 0.f;
        gNorm_ = -1.f;   // sentinel: snap the renorm gain on the first block (no fade-up swell)
    }

    // ═══ ANCHOR: build this block's spectrum (targets only — render ramps to them) ═══
    bool prepareBank (int n) noexcept
    {
        if (! bankOwner_ || n <= 0) return false;
        const float dt = (float) n / (float) rate_;
        // CPU (hm2): at small host blocks the spectral build ran up to ~750×/s per anchor —
        // rebuild every OTHER block when the knobs are static (declick ramps + ~190Hz updates
        // make the skip inaudible; any knob touch rebuilds immediately).
        skipTick_ = ! skipTick_;
        if (skipTick_ && nP_ > 0 && n < 128 && ! displayMode_ && p_ == lastBuilt_)
        { tB_ += dt; bank_ = this; return true; }
        lastBuilt_ = p_;
        const float baseHz = (float) (playedHz_ * pitchMul_);
        const float churnMul = fastExp2 (lerpf (-3.f, 3.f, clamp01 (p_.churn)));   // ×0.125..×8 (hm2 amplify)

        // partial-count window: log taper 8..512, CONTINUOUS (equal-power 3-partial fade edge)
        const float kf = (float) harm::kMinCount
                       * std::pow ((float) harm::kMaxPartials / (float) harm::kMinCount,
                                   clamp01 (p_.count));
        // per-note Nyquist reach — high notes are cheap, only low notes use deep banks
        int nEff = harm::kMaxPartials;
        if (! displayMode_)
        {
            const float rMax = 0.48f * (float) rate_ / std::max (1.f, baseHz);
            nEff = std::min (harm::kMaxPartials, (int) rMax + 4);
        }
        nEff = std::min (nEff, (int) kf + 4);
        nEff = std::max (nEff, harm::kMinCount);

        buildBase   (nEff, kf, baseHz);            // MAIN family + HUE (fills ratio_/amp_)
        if (displayMode_)                          // ghost layer = the untouched family spectrum
            std::copy (amp_.begin(), amp_.begin() + nEff, baseAmp_.begin());
        applyLean   (nEff);
        shineChurn_ = churnMul;
        nEff = applyShineRoot (nEff);              // ghost deposits may append the sub slot
        applySculpt (nEff, churnMul);
        applyWilt   (nEff);
        applyFizz   (nEff);
        applyGrit   (nEff, dt);
        nEff = applyBraid (nEff);                  // appends twin slots (bounded by room)
        applyFan    (nEff, dt, churnMul);
        applyNyquistTaper (nEff, baseHz);
        thinToBudget (nEff);
        renorm      (nEff, dt);

        nP_ = nEff;
        preparedActive_ = 0;
        for (int j = 0; j < nEff; ++j) if (amp_[(size_t) j] > 1e-6f) ++preparedActive_;
        bank_ = this;
        tB_ += dt;
        return true;
    }

    // Sibling: borrow the anchor's freshly-built bank (pointer — the anchor outlives the
    // block render). Phases/ramps stay per-sibling = real unison width.
    void adoptBank (const HarmonicEngine& src) noexcept
    {
        bank_ = &src;
        nP_ = src.nP_;
        preparedActive_ = src.preparedActive_;
    }

    // Anchor budget reserve while siblings render (saturation thins siblings, never the core).
    void reserveBudget() noexcept { if (budgetUsed_ != nullptr) { *budgetUsed_ += preparedActive_; reserved_ = preparedActive_; } }
    void releaseBudget() noexcept { if (budgetUsed_ != nullptr && reserved_ > 0) { *budgetUsed_ -= reserved_; reserved_ = 0; } }
    int  preparedActive() const noexcept { return preparedActive_; }

    void renderBankAdd (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0 || bank_ == nullptr) return;
        if (budgetUsed_ != nullptr && budgetCap_ > 0 && *budgetUsed_ >= budgetCap_)
        {   // shared pool saturated — this (sibling) bank sits the block out, ramped silent
            std::fill (ampZL_.begin(), ampZL_.end(), 0.f);
            std::fill (ampZR_.begin(), ampZR_.end(), 0.f);
            return;
        }
        const HarmonicEngine& b = *bank_;
        const float baseHz = (float) (playedHz_ * pitchMul_);
        const float invN   = 1.f / (float) n;
        const float g      = b.gNormS_;
        int voiced = 0;
        for (int j = 0; j < b.nP_; ++j)
        {
            const float prevL = ampZL_[(size_t) j];
            const float prevR = ampZR_[(size_t) j];
            const float rj  = b.ratio_[(size_t) j];
            const float hz  = rj * baseHz * scatMul_[(size_t) j];
            const bool  aud = (rj > 0.f && hz > 0.f && hz < (float) rate_ * 0.49f);
            const float a   = (j < b.nP_ && aud) ? b.amp_[(size_t) j] * g : 0.f;
            const float tgtL = a * b.panL_[(size_t) j];
            const float tgtR = a * b.panR_[(size_t) j];
            if (prevL <= 1e-7f && prevR <= 1e-7f && tgtL <= 1e-7f && tgtR <= 1e-7f)
            { ampZL_[(size_t) j] = 0.f; ampZR_[(size_t) j] = 0.f; continue; }   // silent slot — skip
            const float inc = aud ? hz / (float) rate_ : 0.f;   // a dying partial keeps its freq while fading
            const float dL = (tgtL - prevL) * invN;
            const float dR = (tgtR - prevR) * invN;
            float ph = phase_[(size_t) j];
            float gl = prevL, gr = prevR;
            for (int i = 0; i < n; ++i)
            {
                const float s = sineAt (ph);
                gl += dL; gr += dR;
                L[i] += gl * s; R[i] += gr * s;
                ph += inc; if (ph >= 1.f) ph -= 1.f;
            }
            phase_[(size_t) j] = ph;
            ampZL_[(size_t) j] = tgtL;
            ampZR_[(size_t) j] = tgtR;
            if (tgtL > 1e-7f || tgtR > 1e-7f) ++voiced;
        }
        if (budgetUsed_ != nullptr) *budgetUsed_ += voiced;
    }

    // classic single-engine path (tests/tools and any non-unison caller)
    void renderBlockAdd (float* L, float* R, int n) noexcept
    {
        if (! prepareBank (n)) return;
        renderBankAdd (L, R, n);
    }

    // ── display: white bars (post-sculpt bank) + purple ghost (base family) ──
    // Bars are PARTIAL-indexed with sqrt index compression so the low harmonics get
    // real estate and the 512-tail still reads. Values normalized 0..1 per layer.
    void displayBins (float* white, float* ghost, int nBins) const noexcept
    {
        if (white == nullptr || ghost == nullptr || nBins <= 0) return;
        for (int b = 0; b < nBins; ++b) { white[b] = 0.f; ghost[b] = 0.f; }
        float wMax = 1e-9f, gMax = 1e-9f;
        for (int j = 0; j < nP_; ++j)
        {
            const float x = std::sqrt ((float) j / (float) harm::kMaxPartials);   // 0..~1
            const int   b = std::min (nBins - 1, (int) (x * (float) nBins));
            white[b] = std::max (white[b], amp_[(size_t) j]);
            ghost[b] = std::max (ghost[b], baseAmp_[(size_t) j]);
            wMax = std::max (wMax, white[b]);
            gMax = std::max (gMax, ghost[b]);
        }
        // perceptual lift (cube root ≈ dB-ish) so quiet partials still draw
        for (int b = 0; b < nBins; ++b)
        {
            white[b] = std::cbrt (white[b] / wMax);
            ghost[b] = std::cbrt (ghost[b] / gMax);
            if (white[b] < 3e-2f) white[b] = 0.f;
            if (ghost[b] < 3e-2f) ghost[b] = 0.f;
        }
    }

    // live white bins from the CURRENT bank — voice anchors feed the UI bars while a
    // note sounds (same index compression as displayBins; no ghost layer needed here)
    int liveBins (float* out, int nBins) const noexcept
    {
        if (out == nullptr || nBins <= 0 || nP_ <= 0 || ! bankOwner_) return 0;
        for (int b = 0; b < nBins; ++b) out[b] = 0.f;
        float mx = 1e-9f;
        for (int j = 0; j < nP_; ++j)
        {
            const float x = std::sqrt ((float) j / (float) harm::kMaxPartials);
            const int   b = std::min (nBins - 1, (int) (x * (float) nBins));
            out[b] = std::max (out[b], amp_[(size_t) j]);
            mx = std::max (mx, out[b]);
        }
        for (int b = 0; b < nBins; ++b)
        {
            out[b] = std::cbrt (out[b] / mx);
            if (out[b] < 3e-2f) out[b] = 0.f;
        }
        return nP_;
    }

    // test hooks
    int   partialsForTesting()   const noexcept { return nP_; }
    float ratio1ForTesting()     const noexcept { return nP_ > 0 ? ratio_[0] : 0.f; }
    float rmsTargetForTesting()  const noexcept { return gNormS_; }

private:
    // ── tiny math kit (audio-thread safe, allocation-free) ──────────────────
    static inline float clamp01 (float v) noexcept { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
    static inline float sstep (float a, float b, float x) noexcept
    { const float t = clamp01 ((x - a) / (b - a)); return t * t * (3.f - 2.f * t); }
    static inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }
    // fast log2/exp2 (amp-domain tolerances — max err ≈1%, ≪0.1 dB after scaling)
    static inline float fastLog2 (float x) noexcept
    {
        union { float f; std::uint32_t i; } u; u.f = x;
        const float e = (float) ((int) (u.i >> 23) - 127);
        u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
        const float m = u.f;                        // mantissa in [1,2)
        return e + (m * (2.f - 0.3333333f * m) - 1.6666667f);
    }
    static inline float fastExp2 (float x) noexcept
    {
        x = x < -60.f ? -60.f : (x > 60.f ? 60.f : x);
        const float fl = std::floor (x);
        const float f  = x - fl;
        const float p  = 1.f + f * (0.69583f + f * (0.22606f + f * 0.078024f));
        union { float fv; std::int32_t bits; } u; u.fv = p;
        u.bits += (std::int32_t) fl << 23;
        return u.fv;
    }
    static inline float dbMul  (float dB)    noexcept { return fastExp2 (dB * 0.166096f);  }   // 10^(dB/20)
    static inline float centsMul (float c)   noexcept { return fastExp2 (c * (1.f/1200.f)); }
    static inline std::uint32_t hashU (std::uint32_t x) noexcept
    { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
    static inline float hash01 (std::uint32_t x) noexcept { return (float) (hashU (x) >> 8) * (1.f / 16777216.f); }
    inline float rng01() noexcept { rng_ = rng_ * 1664525u + 1013904223u; return (float) (rng_ >> 8) * (1.f / 16777216.f); }
    inline float rngPm() noexcept { return rng01() * 2.f - 1.f; }
    // unit-ish gaussian from 4 uniforms (CLT — plenty for drift noise)
    inline float rngGauss() noexcept { return (rng01() + rng01() + rng01() + rng01() - 2.f) * 1.7320508f; }

    static const std::array<float, 4097>& sineLUT() noexcept
    {
        static const std::array<float, 4097> t = [] {
            std::array<float, 4097> v {};
            for (int i = 0; i <= 4096; ++i)
                v[(size_t) i] = std::sin (2.f * harm::kPi * (float) i / 4096.f);
            return v;
        }();
        return t;
    }
    static inline float sineAt (float phase01) noexcept
    {
        const float x = phase01 * 4096.f;
        const int   i = (int) x;
        const float f = x - (float) i;
        const auto& t = sineLUT();
        return t[(size_t) i] + (t[(size_t) (i + 1)] - t[(size_t) i]) * f;
    }
    // static sieve masks (computed once, shared by every instance)
    struct Sieves
    {
        std::array<std::uint8_t, harm::kMaxPartials> odd {}, prime {}, fib {};
        Sieves()
        {
            std::array<std::uint8_t, harm::kMaxPartials + 1> comp {};
            for (int i = 2; i * i <= harm::kMaxPartials; ++i)
                if (! comp[(size_t) i])
                    for (int j = i * i; j <= harm::kMaxPartials; j += i) comp[(size_t) j] = 1;
            for (int nn = 1; nn <= harm::kMaxPartials; ++nn)
            {
                odd  [(size_t) (nn - 1)] = (std::uint8_t) (nn & 1);
                prime[(size_t) (nn - 1)] = (std::uint8_t) (nn == 1 || (nn >= 2 && ! comp[(size_t) nn]));
            }
            int a = 1, b = 2;
            fib[0] = 1;
            while (b <= harm::kMaxPartials) { fib[(size_t) (b - 1)] = 1; const int c = a + b; a = b; b = c; }
        }
    };
    static const Sieves& sieves() noexcept { static const Sieves s; return s; }

    // ═══ stage 1 — MAIN family base spectrum (fills ratio_[0..nEff) and amp_) ═══
    void buildBase (int nEff, float kf, float baseHz) noexcept
    {
        const float k = clamp01 (p_.hue);
        // default harmonic ladder + count window (equal-power fade over ~3 partials)
        for (int j = 0; j < nEff; ++j)
        {
            const float nn = (float) (j + 1);
            ratio_[(size_t) j] = nn;
            const float w = clamp01 ((kf - nn) * (1.f / 3.f) + 1.f);
            amp_[(size_t) j] = std::sqrt (w);           // window only — family fills below
            panL_[(size_t) j] = 0.7071f; panR_[(size_t) j] = 0.7071f;
        }

        switch (p_.mainMode)
        {
            default:
            case 0: // ── BLADE — saw → hollow reed ──
            {
                const float t = 0.75f + 1.9f * k;
                const float evenG = 1.f - sstep (0.45f, 0.9f, k);
                for (int j = 0; j < nEff; ++j)
                {
                    const int nn = j + 1;
                    const float base = fastExp2 (-t * fastLog2 ((float) nn));   // 1/n^t
                    amp_[(size_t) j] *= base * ((nn & 1) ? 1.f : evenG);
                }
                break;
            }
            case 1: // ── NEON — the CS-80 lane ──
            {
                const float wob   = 0.035f + 0.12f * sstep (0.45f, 1.f, k);      // PWM depth grows up top
                const float duty  = 0.30f + wob * sineAt (frac (0.4f * tB_));
                const float mix   = 0.55f;
                // parked playable resonance below, self-sweeping brass stab above — CROSSFADED
                // through k∈[0.45,0.6] so the knob morphs, never switches (hm2 de-snap)
                const float swp   = sstep (0.45f, 0.6f, k);
                const float cPark = lerpf (2.f, 14.f, clamp01 (k * 2.f));
                const float cSwp  = 2.5f + 9.5f * fastExp2 (-tB_ * 3.2f);
                const float c     = lerpf (cPark, cSwp, swp);
                const float Q     = lerpf (2.5f, lerpf (4.f, 12.f, clamp01 (k * 2.f - 1.f)), swp);
                for (int j = 0; j < nEff; ++j)
                {
                    const int nn = j + 1;
                    const float saw = 1.f / (float) nn;
                    const float pul = (2.f / ((float) nn * harm::kPi))
                                    * std::fabs (sineAt (frac (0.5f * (float) nn * duty)) );
                    const float r  = (float) nn / c;
                    const float d2 = (1.f - r * r);
                    const float H  = 1.f / std::sqrt (d2 * d2 + (r / Q) * (r / Q));   // resonant LP magnitude
                    amp_[(size_t) j] *= (mix * saw + (1.f - mix) * pul) * std::min (H, Q);
                }
                break;
            }
            case 2: // ── CONSOLE — drawbars → carillon cluster ──
            {
                static const float RH[9] = { 0.5f, 1.5f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 8.f };
                static const float RD[9] = { 0.5f, 1.56f, 1.f, 1.97f, 3.09f, 4.15f, 5.21f, 6.07f, 8.44f }; // 8' stays true
                static const float VA[9] = { 0.f, 0.f, 1.f, 0.3f, 0.f, 0.1f, 0.f, 0.f, 0.f };
                static const float VB[9] = { 0.7f, 0.5f, 1.f, 0.8f, 0.7f, 0.6f, 0.5f, 0.5f, 0.8f };
                const float regK = clamp01 (k * 2.f);          // flute → full
                const float cluK = clamp01 (k * 2.f - 1.f);    // full → cluster
                // faint 1/n^2.5 bed above the bars = cabinet warmth
                for (int j = 0; j < nEff; ++j)
                    amp_[(size_t) j] *= 0.02f * fastExp2 (-2.5f * fastLog2 ((float) (j + 1)));
                for (int bj = 0; bj < 9 && bj < nEff; ++bj)
                {
                    const float bar = (k < 0.5f) ? lerpf (VA[bj], VB[bj], regK)
                                                 : lerpf (VB[bj], 1.f, cluK * 0.6f);
                    ratio_[(size_t) bj] = lerpf (RH[bj], RD[bj], cluK);
                    amp_[(size_t) bj]   = bar;                 // bars own the first 9 slots outright
                }
                break;
            }
            case 3: // ── CHANT — vocal choir ──
            {
                // vowel path oo → ah on k∈[0,0.5]; above: choir scatter + slow formant drift
                static const float VOO[3] = { 300.f,  870.f, 2250.f };
                static const float VAH[3] = { 730.f, 1090.f, 2440.f };
                static const float VEE[3] = { 270.f, 2290.f, 3010.f };
                const float vk = clamp01 (k * 2.f);          // oo → ah on the lower half
                const float wk = clamp01 (k * 2.f - 1.f);    // ah → ee on the upper half (hm2: full walk)
                const float scat = wk;
                float F[3], G[3] = { 1.f, 0.75f, 0.45f };
                for (int fj = 0; fj < 3; ++fj)
                {
                    F[fj] = lerpf (lerpf (VOO[fj], VAH[fj], vk), VEE[fj], wk);
                    F[fj] *= 1.f + 0.045f * scat * sineAt (frac (0.15f * tB_ + 0.33f * (float) fj));
                }
                const float refHz = displayMode_ ? 110.f : baseHz;   // freq-ANCHORED: vowels survive pitch
                for (int j = 0; j < nEff; ++j)
                {
                    const int nn = j + 1;
                    const float f = (float) nn * refHz;
                    float mask = 0.025f;
                    for (int fj = 0; fj < 3; ++fj)
                    {
                        const float x = fastLog2 (f / F[fj]);             // distance in OCTAVES
                        // Gaussian bump, σ = 0.14 oct (vocal-band width); exp(-y) = 2^(-1.4427·y)
                        mask += G[fj] * fastExp2 (-x * x * (1.f / (2.f * 0.14f * 0.14f)) * 1.4427f);
                    }
                    amp_[(size_t) j] *= mask / (float) nn;
                    // choir scatter — frozen per-note random micro-detune (a crowd, not a soloist)
                    ratio_[(size_t) j] *= 1.f + 0.009f * scat * (hash01 ((std::uint32_t) nn * 77u ^ seed_) - 0.5f) * 2.f;
                }
                break;
            }
            case 4: // ── BRONZE — stiff string → temple gong ──
            {
                const float B = 4e-4f * fastExp2 (6.7f * k * 1.4427f);      // e^(6.7k) — gong reach (hm2)
                const float n1 = std::sqrt (1.f + B);                        // root anchor (ratio[0] = 1 exactly)
                const float sp2 = sstep (0.5f, 0.7f, k);                     // FADED sparse masks — no snap (hm2)
                const float sp3 = sstep (0.7f, 1.0f, k);
                for (int j = 0; j < nEff; ++j)
                {
                    const int nn = j + 1;
                    ratio_[(size_t) j] = (float) nn * std::sqrt (1.f + B * (float) nn * (float) nn) / n1;
                    float a = fastExp2 (-0.7f * fastLog2 ((float) nn))
                            * (0.55f + 0.45f * sineAt (frac ((float) nn * 0.618f + 0.25f)));
                    if (nn >= 4)
                    {
                        if ((nn % 2) != 0) a *= lerpf (1.f, 0.07f, sp2);     // odd clang modes thin first…
                        if ((nn % 3) != 0) a *= lerpf (1.f, 0.22f, sp3);     // …then only every 6th survives
                    }
                    amp_[(size_t) j] *= a;
                }
                break;
            }
            case 5: // ── HORNET — pulse-train buzz wall ──
            {
                const int   K = (int) std::lround (12.f * fastExp2 (fastLog2 ((float) nEff / 12.f) * k));   // exp growth
                const float d = 0.5f - 0.44f * sstep (0.4f, 1.f, k);
                for (int j = 0; j < nEff; ++j)
                {
                    const int nn = j + 1;
                    float a = 0.f;
                    if (nn <= K)
                        a = std::fabs (sineAt (frac (0.5f * (float) nn * d))) / ((float) nn * d * harm::kPi) + 0.15f;
                    amp_[(size_t) j] *= a;
                }
                break;
            }
        }
    }

    // stage 2 — LEAN: bipolar tilt ±12 dB/oct anchored at the fundamental
    void applyLean (int nEff) noexcept
    {
        const float T = (p_.lean - 0.5f) * 2.f * 18.f;   // dB/oct (hm2 amplify)
        if (std::fabs (T) < 0.05f) return;
        for (int j = 0; j < nEff; ++j)
            amp_[(size_t) j] *= dbMul (T * fastLog2 (std::max (0.26f, ratio_[(size_t) j])));
    }

    // stage 3 — ROOT (fund guard + sub ghost) and SHINE (+1 oct detuned ghost)
    int applyShineRoot (int nEff) noexcept
    {
        const float s = clamp01 (p_.shine);
        if (s > 0.003f)
        {
            const float det = 0.013f * s * sineAt (frac (0.11f * tB_ * shineChurn_));
            const int   half = std::min (nEff / 2, 128);
            for (int j = 0; j < half; ++j)
            {
                const int dst = (j + 1) * 2 - 1;         // slot of ratio ≈ 2·(j+1)
                if (dst >= nEff) break;
                const float ghost = s * 0.95f * amp_[(size_t) j];
                if (ghost <= 1e-7f) continue;
                const float tot = amp_[(size_t) dst] + ghost;
                // beat the ghost against the host by nudging the slot in proportion to the ghost share
                if (dst > 1) ratio_[(size_t) dst] *= 1.f + det * (ghost / std::max (1e-9f, tot));
                amp_[(size_t) dst] = tot;
            }
        }
        const float r = clamp01 (p_.root);
        if (r > 0.003f)
        {
            rootHold_ = amp_[0];                          // remembered: re-asserted after sculpt
            if (r > 0.5f && nEff < harm::kMaxPartials)
            {   // half-ratio sub ghost in its own appended slot (never disturbs slot 1's tuning)
                const int sub = nEff;
                ratio_[(size_t) sub] = 0.5f;
                amp_  [(size_t) sub] = (r * 2.f - 1.f) * 0.9f * std::max (amp_[0], 0.2f);
                panL_ [(size_t) sub] = 0.7071f; panR_[(size_t) sub] = 0.7071f;
                ++nEff;
            }
        }
        else rootHold_ = -1.f;
        return nEff;
    }

    // stage 4 — SCULPT row
    void applySculpt (int nEff, float churnMul) noexcept
    {
        const float k = clamp01 (p_.carve);
        if (k < 0.004f) { postRoot(); return; }
        switch (p_.sculptMode)
        {
            default:
            case 0: // ── KEEL — pivot tilt: felt blanket → megaphone scream ──
            {
                float slope, pivot;
                if (k < 0.5f) { slope = 9.f * (k * 2.f); pivot = 1.f; }
                else          { slope = 9.f;             pivot = lerpf (1.f, 18.f, k * 2.f - 1.f); }
                for (int j = 0; j < nEff; ++j)
                {
                    const float rr = std::max (0.26f, ratio_[(size_t) j]);
                    float m = dbMul (-slope * 0.96f * fastLog2 (rr / pivot));
                    m = std::min (m, 4.f);                              // +12 dB cap
                    if (pivot > 1.f) m *= sstep (0.f, pivot * 0.5f, rr); // lows below the pivot duck
                    amp_[(size_t) j] *= m;
                }
                break;
            }
            case 1: // ── SPLAY — anchored inharmonic stretch ──
            {
                float B, anchor;
                if (k < 0.5f) { B = 0.002f * (k * 2.f) * (k * 2.f); anchor = 1.f; }
                else { const float u = k * 2.f - 1.f; B = 0.002f + 0.138f * u * u; anchor = 1.f + 3.f * u; }
                for (int j = 0; j < nEff; ++j)
                {
                    const float nn = (float) (j + 1);
                    if (ratio_[(size_t) j] < 0.9f) continue;                   // sub ghost stays put
                    // FRACTIONAL anchor (hm2 de-snap): partials near the anchor blend smoothly
                    // into the stretch instead of re-locking in audible integer steps
                    const float m = sstep (anchor - 0.5f, anchor + 1.5f, nn);
                    if (m <= 0.f) continue;
                    const float str = std::sqrt (1.f + B * std::max (0.f, nn * nn - anchor * anchor));
                    ratio_[(size_t) j] *= lerpf (1.f, str, m);
                }
                break;
            }
            case 2: // ── CULL — sieve chain: full → odd → primes → Fibonacci ──
            {
                const auto& sv = sieves();
                const float z = k * 2.f;
                for (int j = 0; j < nEff; ++j)
                {
                    const float mo = (float) sv.odd  [(size_t) j];
                    const float mp = (float) sv.prime[(size_t) j];
                    const float mf = (float) sv.fib  [(size_t) j];
                    float m;
                    if      (z < 1.0f) m = lerpf (1.f, mo, z);
                    else if (z < 1.5f) m = lerpf (mo, mp, (z - 1.f) * 2.f);
                    else               m = lerpf (mp, mf, (z - 1.5f) * 2.f);
                    amp_[(size_t) j] *= m;
                }
                break;
            }
            case 3: // ── TIDE — traveling amplitude ripple ──
            {
                float d, w, f;
                if (k < 0.5f) { d = std::min (1.f, k * 2.4f); w = 0.06f; f = 0.3f; }
                else { const float u = k * 2.f - 1.f; d = 1.f; w = lerpf (0.06f, 0.7f, u); f = lerpf (0.3f, 10.f, u * u); }
                f *= churnMul;
                const float phT = f * tB_;
                for (int j = 0; j < nEff; ++j)
                    amp_[(size_t) j] *= 1.f - d * 0.5f * (1.f + sineAt (frac ((float) (j + 1) * w - phT)));
                break;
            }
            case 4: // ── TERRACE — dB-quantize the spectrum (the lossy lane) ──
            {
                float step, dith;
                if (k < 0.5f) { step = lerpf (0.75f, 6.f, k * 2.f); dith = 0.f; }
                else          { step = lerpf (6.f, 32.f, k * 2.f - 1.f); dith = k * 2.f - 1.f; }
                const std::uint32_t epoch = (std::uint32_t) (tB_ * 12.f * churnMul);   // re-dither ~12 Hz·churn
                for (int j = 0; j < nEff; ++j)
                {
                    float a = amp_[(size_t) j];
                    if (a <= 1e-6f) continue;
                    const float dB = 6.0206f * fastLog2 (a);
                    const float dj = dith * hash01 ((std::uint32_t) j * 193u ^ epoch * 7919u ^ seed_);
                    const float q  = std::floor (dB / step + dj) * step;
                    amp_[(size_t) j] = (q < lerpf (-80.f, -54.f, k)) ? 0.f : dbMul (q);   // the floor RISES with the knob — real deletion
                }
                break;
            }
            case 5: // ── CLANG — sum/difference lattice on the loudest partials ──
            {
                int   idx[harm::kClangSeeds] = { 0, 0, 0, 0, 0, 0 };
                float val[harm::kClangSeeds] = { -1.f, -1.f, -1.f, -1.f, -1.f, -1.f };
                for (int j = 0; j < nEff; ++j)      // O(N·6) loudest scan — no allocation
                {
                    const float a = amp_[(size_t) j];
                    int slot = -1; float low = a;
                    for (int q = 0; q < harm::kClangSeeds; ++q)
                        if (val[q] < low) { low = val[q]; slot = q; }
                    if (slot >= 0) { val[slot] = a; idx[slot] = j; }
                }
                const float u = clamp01 (k * 2.f - 1.f);
                const float gD = (k < 0.5f) ? k * 2.f * 0.85f : 0.85f;
                const float gS = u * 1.15f;
                int pair = 0;
                for (int a = 0; a < harm::kClangSeeds; ++a)
                    for (int b = a + 1; b < harm::kClangSeeds; ++b, ++pair)
                    {
                        if (val[a] <= 1e-6f || val[b] <= 1e-6f) continue;
                        const float ra = ratio_[(size_t) idx[a]], rb = ratio_[(size_t) idx[b]];
                        const float g  = std::sqrt (val[a] * val[b]);
                        const float nudge = 1.f + 0.02f * u * (hash01 ((std::uint32_t) pair * 511u ^ seed_) - 0.5f) * 2.f;
                        depositAt (std::fabs (ra - rb) * nudge, gD * g, nEff);
                        if (gS > 0.f) depositAt ((ra + rb) * nudge, gS * g, nEff);
                    }
                break;
            }
        }
        postRoot();
    }
    // ROOT's fundamental guard — re-assert the family's own fundamental after the sculpt op
    void postRoot() noexcept
    {
        if (rootHold_ >= 0.f)
        {
            const float r = clamp01 (p_.root);
            const float guard = std::min (1.f, r * 2.f);
            amp_[0] = lerpf (amp_[0], std::max (amp_[0], rootHold_), guard);
        }
    }
    // deposit ring-mod product energy into the nearest harmonic slot (guarding slots 1-2's tuning)
    void depositAt (float r, float g, int nEff) noexcept
    {
        if (g <= 1e-7f || r < 0.3f) return;
        int slot = (int) std::lround (r) - 1;
        if (slot < 0) slot = 0;
        if (slot >= nEff) return;
        const float tot = amp_[(size_t) slot] + g;
        if (slot > 1)   // slots 1-2 keep their exact tuning (pitch identity is sacred)
            ratio_[(size_t) slot] += (r - ratio_[(size_t) slot]) * (g / std::max (1e-9f, tot)) * 0.5f;
        amp_[(size_t) slot] = tot;
    }

    // stage 5 — WILT: bipolar time arrow (auto-pluck ↔ auto-swell)
    // WILT (hm2 rework): the knob is DEPTH into a fixed time law — sweeping it mid-note
    // morphs smoothly between dry and wilted. (The old form scaled the RATE, so mid-note
    // sweeps were non-monotonic: nothing… nothing… SNAP. Max heard it. Never again.)
    void applyWilt (int nEff) noexcept
    {
        const float w = (p_.wilt - 0.5f) * 2.f;
        const float depth = std::pow (std::fabs (w), 0.75f);
        if (depth < 0.01f) return;
        for (int j = 1; j < nEff; ++j)   // fundamental untouched — the note never dies to silence
        {
            const float hi = std::max (1.f, ratio_[(size_t) j]) - 1.f;
            const float e  = std::exp (-tB_ * 7.f * hi * (1.f / ((w < 0.f) ? 32.f : 24.f)));
            const float target = (w < 0.f) ? e : (1.f - e);
            amp_[(size_t) j] *= lerpf (1.f, target, depth);
        }
    }

    // stage 6 — FIZZ: per-partial random-walk ratio fur (airy top → tuned waterfall)
    void applyFizz (int nEff) noexcept
    {
        const float k = clamp01 (p_.fizz);
        if (k < 0.004f) return;
        float bw;
        if (k < 0.5f) { bw = 0.009f * (k * 2.f); }
        else          { const float u = k * 2.f - 1.f; bw = lerpf (0.009f, 0.09f, u * u); }
        const float mask1 = sstep (0.5f, 0.75f, k);   // whole-spectrum fur FADES in (hm2 de-snap)
        for (int j = 0; j < nEff; ++j)
        {
            float& w = fizzW_[(size_t) j];
            w = 0.99f * w + 0.1f * rngPm();
            const float mask = std::max (mask1, sstep (0.3f, 1.f, (float) (j + 1) / (float) std::max (16, nEff)));
            ratio_[(size_t) j] *= 1.f + bw * mask * w;
        }
    }

    // stage 7 — GRIT: banded OU drift (amp dB + freq cents) + slow global wander.
    // Exact OU discretization (cookbook): x' = ρx + σ√(1-ρ²)·randn, ρ = e^(-dt/τ).
    void applyGrit (int nEff, float dt) noexcept
    {
        const float g = clamp01 (p_.grit);
        if (g < 0.004f) return;
        const float hot   = sstep (0.5f, 1.f, g);                    // the haunted-machine regime
        const float sigA  = 1.5f * g + 3.2f * hot;                   // dB (hm2 amplify)
        const float sigF  = 2.6f * g + 12.f * hot;                   // cents
        const float rhoA  = std::exp (-dt / lerpf (0.20f, 0.07f, hot));
        const float rhoF  = std::exp (-dt / lerpf (0.60f, 0.18f, hot));
        const float qA = std::sqrt (1.f - rhoA * rhoA), qF = std::sqrt (1.f - rhoF * rhoF);
        for (int b = 0; b < harm::kBands; ++b)
        {
            ouAmp_[(size_t) b] = rhoA * ouAmp_[(size_t) b] + sigA * qA * rngGauss();
            ouFrq_[(size_t) b] = rhoF * ouFrq_[(size_t) b] + sigF * qF * rngGauss();
        }
        const float rhoG = std::exp (-dt / 8.f);
        ouGlobal_ = rhoG * ouGlobal_ + (4.f * hot + 1.2f * g) * std::sqrt (1.f - rhoG * rhoG) * rngGauss();
        const float bandScale = (float) (harm::kBands - 1) / 9.f;    // partials 1..512 ≈ 9 octaves
        for (int j = 0; j < nEff; ++j)
        {
            const float pos = fastLog2 (std::max (1.f, ratio_[(size_t) j])) * bandScale;
            const int   b0  = std::min (harm::kBands - 2, (int) pos);
            const float fx  = pos - (float) b0;
            const float sc  = 0.5f + hash01 ((std::uint32_t) j * 31u ^ seed_ ^ 0x5F3759DFu);   // static per-note scatter
            const float aDb = lerpf (ouAmp_[(size_t) b0], ouAmp_[(size_t) b0 + 1], fx) * sc;
            const float cts = (lerpf (ouFrq_[(size_t) b0], ouFrq_[(size_t) b0 + 1], fx) * sc + ouGlobal_)
                            * ((j == 0) ? 0.4f : 1.f);               // fundamental wanders less
            amp_  [(size_t) j] *= dbMul (aDb);
            ratio_[(size_t) j] *= centsMul (cts);
        }
    }

    // stage 8 — BRAID: energy-neutral detune twin pairs on the loudest partials (CS-80 ensemble)
    int applyBraid (int nEff) noexcept
    {
        const float b = clamp01 (p_.braid);
        if (b < 0.004f) return nEff;
        const int room = harm::kMaxPartials - nEff;
        const int M = std::min ({ (int) std::lround (lerpf (4.f, (float) harm::kBraidMax, b)), room, nEff });
        if (M <= 0) return nEff;
        // loudest-M threshold from a single pass histogram-free scan (nth largest via partial pass)
        int   idx[harm::kBraidMax];
        float val[harm::kBraidMax];
        for (int q = 0; q < M; ++q) { idx[q] = -1; val[q] = -1.f; }
        for (int j = 0; j < nEff; ++j)
        {
            const float a = amp_[(size_t) j];
            int slot = -1; float low = a;
            for (int q = 0; q < M; ++q) if (val[q] < low) { low = val[q]; slot = q; }
            if (slot >= 0) { val[slot] = a; idx[slot] = j; }
        }
        const float dBase = 4.f + 26.f * b;   // cents (hm2 amplify — the chorus Max liked, bigger)
        int cursor = nEff;
        for (int q = 0; q < M && cursor < harm::kMaxPartials; ++q)
        {
            const int j = idx[q];
            if (j < 0 || val[q] <= 1e-6f) continue;
            const float psi = hash01 ((std::uint32_t) j * 917u ^ seed_);
            const float d = dBase + 7.f * b * sineAt (frac (lerpf (0.4f, 1.4f, psi) * tB_ * shineChurn_ + psi));
            const float half = amp_[(size_t) j] * 0.7071f;
            amp_  [(size_t) cursor] = half;
            ratio_[(size_t) cursor] = ratio_[(size_t) j] * centsMul (-d);
            amp_  [(size_t) j]      = half;
            ratio_[(size_t) j]     *= centsMul (d * ((j == 0) ? 0.5f : 1.f));   // root braids gently
            // opposing pans (Fan may re-spread later; this is the ensemble's own width)
            panL_[(size_t) cursor] = 0.94f; panR_[(size_t) cursor] = 0.34f;
            panL_[(size_t) j]      = 0.34f; panR_[(size_t) j]      = 0.94f;
            fizzW_[(size_t) cursor] = fizzW_[(size_t) j];
            ++cursor;
        }
        return cursor;
    }

    // stage 9 — FAN: stereo partial placement (mono → odd/even split → golden orbit)
    void applyFan (int nEff, float dt, float churnMul) noexcept
    {
        const float f = clamp01 (p_.fan);
        if (f < 0.004f) return;                    // braid pans (if any) survive at fan=0? no — fan=0 leaves braid's own width
        const float u = clamp01 (f * 2.f - 1.f);
        orbit_ += u * 0.05f * 2.f * harm::kPi * churnMul * dt;
        if (orbit_ > 2.f * harm::kPi) orbit_ -= 2.f * harm::kPi;
        for (int j = 0; j < nEff; ++j)
        {
            const float split = (((j + 1) & 1) ? -1.f : 1.f) * std::min (1.f, f * 2.f) * 0.9f;
            const float scatt = sineAt (frac ((float) (j + 1) * 0.381966f + orbit_ * (1.f / (2.f * harm::kPi))));
            float pan = lerpf (split, scatt, sstep (0.45f, 0.55f, f));   // morph, never switch (hm2)
            if (j < 2) pan *= 0.2f;                // fundamental stays centered — mono-safe bass
            const float th = (pan + 1.f) * (harm::kPi * 0.25f);
            panL_[(size_t) j] = std::cos (th);
            panR_[(size_t) j] = std::sin (th);
        }
    }

    // stage 10 — Nyquist smoothstep taper (0.40..0.48 fs) — never a hard gate
    void applyNyquistTaper (int nEff, float baseHz) noexcept
    {
        if (displayMode_) return;
        const float fStart = 0.40f * (float) rate_, fStop = 0.48f * (float) rate_;
        for (int j = 0; j < nEff; ++j)
        {
            const float f = ratio_[(size_t) j] * baseHz;
            if (f > fStart)
                amp_[(size_t) j] *= 1.f - sstep (fStart, fStop, f);
        }
    }

    // stage 11 — keep-loudest thinning against this engine's share of the global pool
    void thinToBudget (int nEff) noexcept
    {
        if (displayMode_ || budgetUsed_ == nullptr || budgetCap_ <= 0) return;
        int active = 0;
        for (int j = 0; j < nEff; ++j) if (amp_[(size_t) j] > 1e-6f) ++active;
        int room = std::max (24, budgetCap_ - *budgetUsed_);   // never starve below a musical floor
        if (unisonDiv_ > 1)                                     // constant-cost unison pre-thin
            room = std::min (room, std::max (24, active / unisonDiv_));
        if (active <= room) return;
        // threshold = the (active-room)-th smallest amp — one nth_element on a stack copy
        static thread_local std::array<float, harm::kMaxPartials> tmp;
        int c = 0;
        for (int j = 0; j < nEff; ++j) if (amp_[(size_t) j] > 1e-6f) tmp[(size_t) c++] = amp_[(size_t) j];
        const int drop = active - room;
        std::nth_element (tmp.begin(), tmp.begin() + drop, tmp.begin() + c);
        const float thr = tmp[(size_t) drop];
        for (int j = 2; j < nEff; ++j)                                // never thin the root or slot 2
            if (amp_[(size_t) j] < thr) amp_[(size_t) j] = 0.f;
    }

    // stage 12 — RMS renorm: slow-smoothed gain (τ≈200 ms) — character, never loudness.
    // Also the AUDIBILITY FLOOR (hm2 CPU win): partials below −72 dB of the bank's peak are
    // masked into silence anyway — zeroing them here lets the render's silent-slot skip do
    // its job on dark patches (Keel/Lean down) instead of burning sines on inaudible tails.
    void renorm (int nEff, float dt) noexcept
    {
        if (! displayMode_)
        {
            float pk = 0.f;
            for (int j = 0; j < nEff; ++j) pk = std::max (pk, amp_[(size_t) j]);
            const float floorA = pk * 2.5e-4f;   // −72 dB relative
            for (int j = 2; j < nEff; ++j)       // root + slot 2 always keep their voice
                if (amp_[(size_t) j] < floorA) amp_[(size_t) j] = 0.f;
        }
        float e = 0.f;
        for (int j = 0; j < nEff; ++j) { const float a = amp_[(size_t) j]; e += a * a; }
        const float rms = std::sqrt (0.5f * e);
        const float g   = harm::kTargetRms / std::max (1e-5f, rms);
        const float gClamped = std::min (g, 24.f);
        if (gNorm_ < 0.f) { gNorm_ = gClamped; gNormS_ = gClamped; return; }   // note-on snap
        const float a = 1.f - std::exp (-dt / 0.2f);
        gNorm_ += a * (gClamped - gNorm_);
        gNormS_ = gNorm_;
    }

    static inline float frac (float x) noexcept { return x - std::floor (x); }

    // ── state ────────────────────────────────────────────────────────────────
    HarmParams p_;
    std::vector<float> ratio_, amp_, panL_, panR_, baseAmp_;   // anchor-owned bank (targets)
    std::vector<float> fizzW_;                                  // anchor-owned per-partial walk
    std::vector<float> phase_, ampZL_, ampZR_;                  // per-instance render state
    std::vector<float> scatMul_;                                // per-sibling static freq scatter (unison decorrelation)
    std::array<float, harm::kBands> ouAmp_ {}, ouFrq_ {};
    const HarmonicEngine* bank_ = nullptr;    // adopted bank (self for anchors after prepareBank)
    double rate_ = 48000.0, playedHz_ = 261.6256, pitchMul_ = 1.0;
    float  tB_ = 0.f, orbit_ = 0.f, ouGlobal_ = 0.f, rootHold_ = -1.f;
    float  gNorm_ = 1.f, gNormS_ = 1.f, uniScatCents_ = 0.f, shineChurn_ = 1.f;
    bool   skipTick_ = false;
    HarmParams lastBuilt_;
    int    nP_ = 0, preparedActive_ = 0, reserved_ = 0;
    int*   budgetUsed_ = nullptr;
    int    budgetCap_  = 0;
    int    unisonDiv_  = 1;   // constant-cost unison divisor = ceil(sqrt(unison count))
    bool   bankOwner_ = true, displayMode_ = false;
    std::uint32_t seed_ = 0x9E3779B9u, rng_ = 0x9E3779B9u;
};

} // namespace tw
