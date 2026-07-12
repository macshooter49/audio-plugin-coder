#pragma once
// ════════════════════════════════════════════════════════════════════════════
//  tw::ModalEngine — Terrain MODAL oscillator (Engine::MODAL, slot 6)
//
//  PHYSICAL MODELING. Not a sample player, not a wavetable — the instrument
//  lives in the RESONATOR, and any exciter (dropped one-shot, noise, click, or a
//  continuous bow/breath) is filtered by it into a real acoustic voice. Same
//  principle that made the Annulus a moat, now PER-VOICE, note-tracked, with the
//  round-robin oscillator rotation feeding four independently-aged players.
//
//  THREE cores behind ONE voice API, chosen by FAMILY:
//    • STRING  (waveguide single-delay-loop, dual polarization + dispersion)
//         GRAND · PLUCK           — piano, guitar, lute, harp, sitar, clav
//    • REEDBORE (MSW nonlinear-in-loop: one architecture, swap the nonlinearity)
//         BOW · FLUTE · REED · BRASS — violin/cello, flute, clarinet/sax, trumpet
//    • MODAL   (2D-rotation resonator bank — click-free live retune, Mathews-Smith)
//         BARS · BELLS · SKIN      — marimba/vibes, church/tubular bells, timpani/drums
//
//  Ten controls, each amplitude-maxed, night-and-day, NONE redundant:
//    ESSENTIALS  HARD  exciter hardness / contact time (soft mallet → hard stick)
//                POS   excitation point (bridge ↔ middle — the node comb / mode reweight)
//                DECAY global ring time (staccato → infinite sustain)
//                MTRL  material tilt: frequency-DEPENDENT damping (felt/wood → glass/metal)
//                BREATH the stochastic+continuous lane (plucked → bowed/blown sustain)
//    SCULPT      STRETCH inharmonicity / geometry (harmonic → piano-stiff → bell/gong)
//                BLOOM   post-attack life: energy migrates up, gongs glide (dead → alive)
//                HALO    sympathetic/coupled resonance (dry → beating two-stage halo)
//                AGE     condition + per-note reseed (pristine → worn/buzzy/human = round robin)
//                BODY    resonator body + capture (raw string → full resonant body + stereo)
//
//  House DSP rules honored: allpass-tuned loops (a C is a C), phase-delay-
//  compensated loop lengths, DC-block every feedback path, ring-tail declick at
//  voice-free, denormal-safe tails, state clamps as self-heal, per-note seeded RNG
//  (never per-block reseed), Schelleng/breath auto-clamp so a bow can't scratch,
//  exponential-bias curves, zero audio-thread allocation after prepare().
//
//  Offline proof loop (Pattern A — NOT in CMakeLists):
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/ModalEngine_test.cpp -o /tmp/md && /tmp/md
// ════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>

namespace tw {

namespace modal {
    constexpr int   kMaxModes   = 48;    // per-voice modal bank ceiling (bells/skins)
    constexpr int   kMaxDelay   = 8192;  // waveguide line (down to ~6 Hz @ 48k)
    constexpr int   kDispStages = 6;     // dispersion allpass cascade (piano stiffness)
    constexpr int   kBodyModes  = 6;     // biquad body / soundboard resonators
    constexpr float kPi         = 3.14159265358979323846f;
    constexpr int   kDispBins   = 56;    // UI bar resolution
    constexpr float kTargetRms  = 0.16f; // loudness lane (matches the other engines)

    enum Family : int { GRAND=0, PLUCK, BOW, FLUTE, REED, BRASS, BARS, BELLS, SKIN, kNumFamilies };
    // which core each family routes to
    inline int coreOf (int fam) noexcept
    {
        if (fam <= PLUCK)  return 0;                 // STRING
        if (fam <= BRASS)  return 1;                 // REEDBORE (MSW)
        return 2;                                    // MODAL bank
    }
}

// ── knob/mode snapshot pushed from the processor (cheap store, block-rate) ──
struct ModalParams
{
    int   family = modal::GRAND;   // 0..8
    int   form   = 0;              // 0..4 — the instrument variant inside the family
    int   source = 0;              // 0 Auto · 1 Noise · 2 Click · 3 Sample (exciter select)
    int   loopMode = 0;            // 0 One-Shot · 1 Forward · 2 Reverse · 3 Ping-Pong — exciter read (loop-mode header)
    float loopStart = 0.f;        // exciter loop region start 0..1 (the PURPLE BOX) — looped read is confined here
    float loopEnd   = 1.f;        // exciter loop region end   0..1 (the PURPLE BOX)

    float hard    = 0.5f;   // exciter hardness / contact time
    float pos     = 0.28f;  // excitation position
    float decay   = 0.6f;   // global ring time (freq-independent)
    float material= 0.5f;   // frequency-dependent damping tilt (wood↔metal)
    float breath  = 0.0f;   // stochastic + continuous excitation lane
    float stretch = 0.5f;   // inharmonicity / geometry morph (0.5 = family default)
    float bloom   = 0.0f;   // post-attack energy migration + gong glide
    float halo    = 0.0f;   // sympathetic / coupled resonance
    float age     = 0.0f;   // condition + per-note reseed (round-robin jitter)
    float body    = 0.5f;   // resonator body + capture

    bool operator== (const ModalParams& o) const noexcept
    {
        return family==o.family && form==o.form && source==o.source
            && hard==o.hard && pos==o.pos && decay==o.decay && material==o.material
            && breath==o.breath && stretch==o.stretch && bloom==o.bloom
            && halo==o.halo && age==o.age && body==o.body && loopMode==o.loopMode
            && loopStart==o.loopStart && loopEnd==o.loopEnd;
    }
    bool operator!= (const ModalParams& o) const noexcept { return ! (*this == o); }
};

class ModalEngine
{
public:
    // ═══════════════════════════════ setup ═══════════════════════════════════
    void prepare (double sampleRate, bool /*bankOwner*/ = true) noexcept
    {
        rate_  = sampleRate > 1000.0 ? sampleRate : 48000.0;
        fsInv_ = 1.f / (float) rate_;
        dlA_.prepare (modal::kMaxDelay);
        dlB_.prepare (modal::kMaxDelay);
        combA_.prepare (modal::kMaxDelay);
        combB_.prepare (modal::kMaxDelay);
        boreN_.prepare (modal::kMaxDelay);   // MSW: nut/bridge & jet delays share these lines
        boreB_.prepare (modal::kMaxDelay);
        for (auto& m : modes_)  m = Mode {};
        for (auto& b : body_)   b = Biquad {};
        for (auto& d : disp_)   d = AllpassS {};
        for (auto& d : dispB_)  d = AllpassS {};
        reset();
        gNorm_ = 1.f;
    }

    void reset() noexcept
    {
        dlA_.clear(); dlB_.clear(); combA_.clear(); combB_.clear();
        boreN_.clear(); boreB_.clear();
        lpA_.reset(); lpB_.reset(); ozA_.reset(); ozB_.reset(); dcA_.reset(); dcB_.reset(); dcOut_.reset(); matEq_.reset();
        for (auto& m : modes_) m.reset();
        for (auto& b : body_)  b.reset();
        for (auto& d : disp_)  d.reset();
        for (auto& d : dispB_) d.reset();
        active_ = false; ringDown_ = false; exPos_ = 0; exLen_ = 0;
        env_ = 0.f; envRel_ = 1.f; bloomEnv_ = 0.f; bloomLpZ_ = 0.f; bloomLfo_ = 0.f; driveZ_ = 0.f;
        bowPhase_ = 0.f; lipY1_ = 0.f; ampZ_ = 0.f; silentBlocks_ = 0;
    }

    void setPartialBudget (int* used, int cap) noexcept { budgetUsed_ = used; budgetCap_ = cap; }
    void setUnisonScale (int n) noexcept { uniN_ = n < 1 ? 1 : n; }
    void setParams (const ModalParams& p) noexcept { p_ = p; computeLoopBounds(); }
    void setPitchRatio (double r) noexcept { pitchMul_ = r > 0.0 ? r : 1.0; }
    void setPlayedHz (double hz) noexcept { if (hz > 8.0) playedHz_ = hz; }
    void setDisplayMode (bool on) noexcept { displayMode_ = on; }
    // ── follower: exciter read position 0..1 — drives the white MIDI follower EXACTLY like
    //    Sample/Granular/Resynth. -1 when not reading a sample exciter; one-shot parks when done. ──
    bool  isActive() const noexcept { return active_; }
    float readPos01() const noexcept
    {
        if (! (useSample_ && active_ && exDataLen_ > 1)) return -1.f;
        if (p_.loopMode == 0 && exPos_ >= exLen_) return -1.f;   // one-shot finished → follower parks
        return clamp01 (exRead_ / (float) (exDataLen_ - 1));
    }

    // Exciter sample: the dropped one-shot (mono view). Borrowed pointer — the owner
    // (SynthVoice/SampleBuffer) outlives the note. Used when source == Sample (or Auto
    // for the percussive families) — "noise into guitars" made literal.
    void setExciterSample (const float* data, int len, double srcRate) noexcept
    {
        exData_ = (data && len > 1) ? data : nullptr;
        exDataLen_ = exData_ ? len : 0;
        exSrcStep_ = (srcRate > 1000.0) ? (float) (srcRate / rate_) : 1.f;
        computeLoopBounds();   // purple-box indices depend on the new length
    }

    // ════════════════════════════ note lifecycle ════════════════════════════
    void noteOn (double playedHz, std::uint32_t seed, float velocity = 0.9f) noexcept
    {
        playedHz_ = playedHz > 8.0 ? playedHz : 130.8128;
        seed_ = seed ? seed : 0x9E3779B9u;
        rng_  = seed_ ^ 0xA511E9B3u;
        vel_  = velocity < 0.02f ? 0.02f : (velocity > 1.f ? 1.f : velocity);

        buildVoice();                // map params → per-note coefficients (+ AGE reseed)
        primeExcitation();           // load the burst / arm the continuous drive

        active_ = true; ringDown_ = false;
        env_ = 0.f; envRel_ = 1.f; bloomEnv_ = 0.f; driveZ_ = 0.f;
        ampZ_ = 0.f; silentBlocks_ = 0; relGain_ = 1.f;
    }

    void noteOff() noexcept
    {
        ringDown_ = true;            // stop feeding continuous drive; dampers engage per DECAY
    }

    bool isRinging() const noexcept { return active_; }

    // ════════════════════════════ render ════════════════════════════════════
    // Self-contained per-block render — mirrors HarmonicEngine::renderBlockAdd so
    // SynthVoice drives MODAL exactly like HARM (see the bank-style shims below too).
    void renderBlockAdd (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0 || ! active_) return;

        const float pan  = panBody_;                    // BODY→stereo listening spread
        const float gl   = 0.7071f * (1.f - 0.5f * pan);
        const float gr   = 0.7071f * (1.f + 0.5f * pan);
        const float relK = 1.f - std::exp (-1.f / (0.006f * (float) rate_));   // 6 ms release-fade smoother
        const float relDampK = 1.f - std::exp (-1.f / (0.012f * (float) rate_)); // 12 ms damper-close smoother
        float peak = 0.f;

        for (int i = 0; i < n; ++i)
        {
            // amp envelope: percussive families own their decay in the resonator, so the
            // "env" here is only an attack de-click + note-off release-fade (ring-tail law).
            if (ringDown_)
            {
                envRel_ += relK * (0.f - envRel_);      // fade the DRIVE (bow/breath), not the tail
                relGain_ += relDampK * (relGainTarget_ - relGain_);   // GRAND: the damper closes on the string
            }
            const float excite = nextExcitation (i);    // strike burst / continuous bow/breath

            float s = 0.f;
            switch (core_)
            {
                case 0: s = tickString (excite);  break;
                case 1: s = tickReedBore (excite); break;
                default: s = tickModal (excite);  break;
            }

            s = dcOut_.process (s);                     // global DC guard
            s = softClip (s * outGain_);                // own your ceiling (no master limiter after)
            const float a = std::fabs (s);
            if (a > peak) peak = a;

            L[i] += gl * s;
            R[i] += gr * s;
        }

        // voice-free detection: after note-off, once the tail is inaudible, free the voice
        if (peak < 3.0e-5f) { if (++silentBlocks_ > 6) { active_ = false; } }
        else silentBlocks_ = 0;
    }

    // ── bank-style shims: let SynthVoice call MODAL on the same code path as HARM ──
    // (Physical-model voices are stateful and independent — there's no shared bank, so
    //  these map onto the self-contained render. Unison siblings each run their own core.)
    bool prepareBank (int /*n*/) noexcept { return active_; }
    void adoptBank (const ModalEngine& src) noexcept
    {
        p_ = src.p_; core_ = src.core_;                 // sibling shares config, keeps own state
    }
    void reserveBudget() noexcept { if (budgetUsed_) { *budgetUsed_ += activeCost_; reserved_ = activeCost_; } }
    void releaseBudget() noexcept { if (budgetUsed_ && reserved_ > 0) { *budgetUsed_ -= reserved_; reserved_ = 0; } }
    int  preparedActive() const noexcept { return activeCost_; }
    void renderBankAdd (float* L, float* R, int n) noexcept { renderBlockAdd (L, R, n); }
    void postProcess (float* /*L*/, float* /*R*/, int /*n*/) noexcept {}   // no post-sat stage (yet)

    // ── viz: white bars = live resonant spectrum, purple ghost = family template ──
    int liveBins (float* out, int nBins) const noexcept
    {
        if (out == nullptr || nBins <= 0) return 0;
        for (int b = 0; b < nBins; ++b) out[b] = 0.f;
        float mx = 1e-9f;
        if (core_ == 2)                              // modal: one bar per mode by frequency
        {
            for (int k = 0; k < nModes_; ++k)
            {
                const float f = modes_[(size_t) k].freqHz;
                if (f <= 0.f) continue;
                const float x = std::sqrt (std::min (1.f, f / (0.5f * (float) rate_)));
                const int   b = std::min (nBins - 1, (int) (x * (float) nBins));
                const float e = modes_[(size_t) k].energy();
                out[b] = std::max (out[b], e); mx = std::max (mx, out[b]);
            }
        }
        else                                          // waveguide/reedbore: harmonic ladder proxy
        {
            const float f0 = (float) (playedHz_ * pitchMul_);
            const float amp = std::min (1.f, 6.f * std::fabs (ampZ_) + 0.02f);
            for (int h = 1; h <= nBins; ++h)
            {
                const float f = f0 * (float) h;
                if (f >= 0.5f * (float) rate_) break;
                const float x = std::sqrt (std::min (1.f, f / (0.5f * (float) rate_)));
                const int   b = std::min (nBins - 1, (int) (x * (float) nBins));
                const float rolloff = std::pow (1.f / (float) h, 0.7f + 1.3f * (1.f - p_.material));
                out[b] = std::max (out[b], amp * rolloff); mx = std::max (mx, out[b]);
            }
        }
        for (int b = 0; b < nBins; ++b)
        {
            out[b] = std::cbrt (out[b] / mx);
            if (out[b] < 3e-2f) out[b] = 0.f;
        }
        return core_ == 2 ? nModes_ : nBins;
    }

    // test hooks
    int   coreForTesting()   const noexcept { return core_; }
    int   modeCountForTesting() const noexcept { return nModes_; }
    float mode0HzForTesting() const noexcept { return nModes_ > 0 ? modes_[0].freqHz : 0.f; }
    bool  activeForTesting() const noexcept { return active_; }

private:
    // ═══════════════════════════ tiny DSP kit ════════════════════════════════
    static constexpr float kPi = 3.14159265358979323846f;   // visible to nested structs (complete-class ctx)
    static inline float clamp01 (float v) noexcept { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
    static inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }
    static inline float sstep (float a, float b, float x) noexcept
    { const float t = clamp01 ((x - a) / (b - a)); return t * t * (3.f - 2.f * t); }
    static inline float softClip (float x) noexcept          // gentle tanh-ish, cheap
    { if (x >  1.6f) return  1.f; if (x < -1.6f) return -1.f; return x * (1.f - 0.1171875f * x * x); }
    static inline float flushDenorm (float x) noexcept
    { return (x < 1e-20f && x > -1e-20f) ? 0.f : x; }
    static inline float centsMul (float c) noexcept { return std::pow (2.f, c * (1.f / 1200.f)); }
    static inline std::uint32_t hashU (std::uint32_t x) noexcept
    { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
    inline float rng01() noexcept { rng_ = rng_ * 1664525u + 1013904223u; return (float) (rng_ >> 8) * (1.f / 16777216.f); }
    inline float rngPm() noexcept { return rng01() * 2.f - 1.f; }

    // one-pole lowpass (loop damping / brightness)
    struct OnePole
    {
        float a = 0.5f, z = 0.f;
        void reset() noexcept { z = 0.f; }
        void setCutoff (float fc, float fs) noexcept
        { const float x = std::exp (-2.f * kPi * (fc < 1.f ? 1.f : fc) / fs); a = x; }
        inline float process (float in) noexcept { z = (1.f - a) * in + a * z; return flushDenorm (z); }
        float phaseDelay() const noexcept { return a / (1.f - a); }   // ~samples at DC
    };
    // one-zero averaging lowpass y = ½(x + x₋₁): a hard zero at Nyquist. This is THE
    // waveguide loop damper — it guarantees the loop can never sustain HF buzz (the
    // one-pole alone leaves ~0.56 gain at Nyquist and a bright string fizzes). +0.5 samp delay.
    struct OneZero
    {
        float x1 = 0.f;
        void reset() noexcept { x1 = 0.f; }
        inline float process (float x) noexcept { const float y = 0.5f * (x + x1); x1 = x; return y; }
    };
    // DC blocker (one-zero one-pole)
    struct DCBlock
    {
        float x1 = 0.f, y1 = 0.f;
        void reset() noexcept { x1 = y1 = 0.f; }
        inline float process (float x) noexcept { const float y = x - x1 + 0.9985f * y1; x1 = x; y1 = flushDenorm (y); return y; }
    };
    // 2nd-order allpass (dispersion / piano stiffness): stretches upper partials sharp
    struct AllpassS
    {
        float a1 = 0.f, a2 = 0.f, x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0.f; }
        void set (float a1_, float a2_) noexcept { a1 = a1_; a2 = a2_; }
        inline float process (float x) noexcept
        {
            const float y = a2 * x + a1 * x1 + x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = flushDenorm (y);
            return y;
        }
    };
    // RBJ biquad (bodies / bandpass tube)
    struct Biquad
    {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0.f; }
        void peaking (float f, float fs, float Q, float gainDb) noexcept
        {
            const float w = 2.f * kPi * (f < 1.f ? 1.f : f) / fs;
            const float A = std::pow (10.f, gainDb / 40.f);
            const float al = std::sin (w) / (2.f * (Q < 0.05f ? 0.05f : Q));
            const float cs = std::cos (w);
            const float a0 = 1.f + al / A;
            b0 = (1.f + al * A) / a0; b1 = (-2.f * cs) / a0; b2 = (1.f - al * A) / a0;
            a1 = (-2.f * cs) / a0;    a2 = (1.f - al / A) / a0;
        }
        void bandpass (float f, float fs, float Q, float gain) noexcept
        {
            const float w = 2.f * kPi * (f < 1.f ? 1.f : f) / fs;
            const float al = std::sin (w) / (2.f * (Q < 0.05f ? 0.05f : Q));
            const float cs = std::cos (w);
            const float a0 = 1.f + al;
            b0 = gain * al / a0; b1 = 0.f; b2 = -gain * al / a0;
            a1 = (-2.f * cs) / a0; a2 = (1.f - al) / a0;
        }
        inline float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = flushDenorm (y);
            return y;
        }
    };
    // allpass-interpolated delay (STK DelayA): loop gain stays exactly 1 → tuning is precise
    struct DelayA
    {
        std::vector<float> buf; int size = 0, inPoint = 0, outPoint = 0;
        float coeff = 0.f, apInput = 0.f, lastOut = 0.f, delay = 1.f;
        void prepare (int maxLen) noexcept { size = std::max (8, maxLen); buf.assign ((size_t) size, 0.f); inPoint = 0; clear(); setDelay (1.f); }
        void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.f); apInput = 0.f; lastOut = 0.f; }
        void setDelay (float d) noexcept
        {
            if (d < 0.5f) d = 0.5f;
            if (d > (float) size - 2.f) d = (float) size - 2.f;
            delay = d;
            float outPointer = (float) inPoint - d;
            while (outPointer < 0.f) outPointer += (float) size;
            outPoint = (int) outPointer;
            float alpha = 1.f + (float) outPoint - outPointer;   // fractional in (0,1]
            if (alpha < 0.5f) { outPoint = (outPoint + 1) % size; alpha += 1.f; }
            if (outPoint >= size) outPoint -= size;
            coeff = (1.f - alpha) / (1.f + alpha);
        }
        inline float peek() const noexcept { return lastOut; }
        inline float tick (float in) noexcept
        {
            buf[(size_t) inPoint] = in; if (++inPoint >= size) inPoint = 0;
            const float rd = buf[(size_t) outPoint]; if (++outPoint >= size) outPoint = 0;
            const float out = -coeff * lastOut + apInput + coeff * rd;
            apInput = rd; lastOut = flushDenorm (out);
            return lastOut;
        }
    };
    // 2-pole resonant mode as a complex rotation (Mathews-Smith): retune per-sample with
    // NO amplitude discontinuity (state magnitude = energy; rotation preserves it).
    struct Mode
    {
        float x = 0.f, y = 0.f, cr = 0.f, ci = 0.f, amp = 0.f, freqHz = 0.f;
        void reset() noexcept { x = y = 0.f; }
        void set (float w, float r) noexcept { cr = r * std::cos (w); ci = r * std::sin (w); }
        inline float tick (float in) noexcept
        {
            const float nx = cr * x - ci * y + in;
            const float ny = ci * x + cr * y;
            x = flushDenorm (nx); y = flushDenorm (ny);
            return y;
        }
        float energy() const noexcept { return std::sqrt (x * x + y * y); }
    };

    // ═══════════════════════════ voice build ═════════════════════════════════
    // Map the 10 knobs (+ family/form) into per-note coefficients. Runs once per
    // note-on. AGE reseeds bounded physical jitter here (the round-robin flavor).
    void buildVoice() noexcept
    {
        core_ = modal::coreOf (p_.family);
        const float f0 = (float) (playedHz_ * pitchMul_);
        keyN_ = std::min (88.f, std::max (0.f, 12.f * std::log2 (std::max (8.f, f0) / 27.5f)));  // 0=A0 · 27=C3 · 39=C4 · 87=C8

        // ── AGE: bounded, physical, per-note (reads as human, not chorus) ──
        const float age = clamp01 (p_.age);
        const float jPitch = age * 2.4f * rngPm();                 // ±cents
        const float jPos   = age * 0.06f * rngPm();                // ±% of length
        agePosJit_  = jPos;
        ageBuzz_    = sstep (0.6f, 1.f, age);                      // sitar/rattle at the top
        ageDetune_  = centsMul (jPitch);
        rollChance_ = age;                                         // multi-strike roll odds
        agePitch_   = jPitch;

        const float pos  = clamp01 (p_.pos + jPos);
        const float mtrl = clamp01 (p_.material);
        const float dec  = clamp01 (p_.decay);
        const float str  = clamp01 (p_.stretch);
        const float body = clamp01 (p_.body);

        // BODY → stereo listening spread (Collision L/R idea) + drive/output trim
        panBody_ = (body - 0.5f) * 1.2f;
        outGain_ = 1.f;

        // family form table. Forms differ by TIMBRE (brightness / body / mode-set), NOT by
        // transposition — you play the pitch you want and the form colours it (a "cello" plays
        // your note with a darker, bigger body). pitchMul is left at 1.0 in the tables.
        const Form fm = formOf (p_.family, p_.form);
        formPitchMul_ = fm.pitchMul;
        const float fHz = f0 * ageDetune_;

        if (core_ == 2)  buildModal (fHz, fm, pos, mtrl, dec, str, body);
        else             buildLoops (fHz, fm, pos, mtrl, dec, str, body);

        // GRAND note-off DAMPER target: on key-up the string is damped to a fast decay (effective per-trip
        // gain ~0.72 → ~120-200 ms, faster in the treble). Only bites when the note rings longer than that.
        relGainTarget_ = (p_.family == modal::GRAND) ? std::min (1.f, 0.72f / std::max (0.1f, loopGain_)) : 1.f;

        // BODY biquads (bowed / brass / plucked radiation) — a few peaking resonances
        buildBody (fm, body);

        // BLOOM prep (post-attack energy migration + gong glide)
        bloomAmt_  = clamp01 (p_.bloom);
        bloomGlide_ = (p_.family == modal::BELLS) ? (fm.gongGlide) : 0.f;

        // HALO: dual-polarization detune + sympathetic amount
        haloAmt_ = clamp01 (p_.halo);

        activeCost_ = (core_ == 2) ? nModes_ : 6;   // budget cost estimate
    }

    struct Form { float pitchMul; float bright; float bodyDb; float gongGlide; int modeSet; };

    // 9 families × 5 forms. pitchMul retunes to the instrument's natural register;
    // bright biases MATERIAL/loop-filter; bodyDb scales the body resonance; modeSet
    // selects a mode-ratio table for the MODAL families.
    static Form formOf (int fam, int form) noexcept
    {
        form = form < 0 ? 0 : (form > 4 ? 4 : form);
        switch (fam)
        {
            case modal::GRAND: { // Concert · Upright · Tack · Felt · Silver
                static const Form F[5] = {
                    {1.0f, 0.60f, 6.f, 0.f, 0}, {0.5f, 0.70f, 4.f, 0.f, 0},
                    {1.0f, 0.90f, 3.f, 0.f, 0}, {1.0f, 0.35f, 5.f, 0.f, 0}, {1.0f, 0.80f, 7.f, 0.f, 0} };
                return F[form]; }
            case modal::PLUCK: { // Steel · Nylon · Lute · Harp · Sitar
                static const Form F[5] = {
                    {1.0f, 0.75f, 6.f, 0.f, 0}, {1.0f, 0.45f, 5.f, 0.f, 0},
                    {1.0f, 0.55f, 6.f, 0.f, 0}, {1.0f, 0.60f, 7.f, 0.f, 0}, {1.0f, 0.85f, 6.f, 0.f, 0} };
                return F[form]; }
            case modal::BOW: {   // Violin · Viola · Cello · Contrabass · Gamba
                static const Form F[5] = {
                    {1.0f, 0.70f, 8.f, 0.f, 0}, {0.667f, 0.62f, 8.f, 0.f, 0},
                    {0.333f, 0.55f, 9.f, 0.f, 0}, {0.167f, 0.48f, 9.f, 0.f, 0}, {0.5f, 0.60f, 8.f, 0.f, 0} };
                return F[form]; }
            case modal::FLUTE: { // Concert · Alto · Pan · Shaku · Bottle
                static const Form F[5] = {
                    {1.0f, 0.55f, 3.f, 0.f, 0}, {0.75f, 0.5f, 3.f, 0.f, 0},
                    {1.0f, 0.65f, 2.f, 0.f, 0}, {1.0f, 0.4f, 2.f, 0.f, 0}, {1.0f, 0.35f, 4.f, 0.f, 0} };
                return F[form]; }
            case modal::REED: {  // Clarinet · Sax · Oboe · Bassoon · Duduk (cyl↔cone in build)
                static const Form F[5] = {
                    {1.0f, 0.55f, 4.f, 0.f, 0}, {1.0f, 0.70f, 5.f, 0.f, 1},
                    {1.0f, 0.75f, 4.f, 0.f, 1}, {0.5f, 0.5f, 5.f, 0.f, 1}, {1.0f, 0.45f, 4.f, 0.f, 0} };
                return F[form]; }
            case modal::BRASS: { // Trumpet · Cornet · Horn · Trombone · Tuba
                static const Form F[5] = {
                    {1.0f, 0.75f, 5.f, 0.f, 0}, {1.0f, 0.65f, 5.f, 0.f, 0},
                    {0.667f, 0.6f, 6.f, 0.f, 0}, {0.5f, 0.6f, 6.f, 0.f, 0}, {0.25f, 0.5f, 7.f, 0.f, 0} };
                return F[form]; }
            case modal::BARS: {  // Marimba · Vibraphone · Xylophone · Glass · Kalimba
                static const Form F[5] = {
                    {1.0f, 0.5f, 6.f, 0.f, 0}, {1.0f, 0.65f, 6.f, 0.f, 1},
                    {2.0f, 0.7f, 4.f, 0.f, 2}, {1.0f, 0.8f, 5.f, 0.f, 3}, {1.0f, 0.55f, 4.f, 0.f, 4} };
                return F[form]; }
            case modal::BELLS: { // Church · Tubular · Handbell · Gong · Tibetan
                static const Form F[5] = {
                    {1.0f, 0.6f, 5.f, 0.f, 0}, {1.0f, 0.55f, 4.f, 0.f, 1},
                    {1.0f, 0.65f, 5.f, 0.f, 0}, {0.5f, 0.7f, 6.f, -3.f, 2}, {1.0f, 0.6f, 6.f, 0.5f, 3} };
                return F[form]; }
            default: {           // SKIN: Timpani · Tom · Snare · Djembe · Tabla
                static const Form F[5] = {
                    {1.0f, 0.4f, 6.f, 0.f, 0}, {1.0f, 0.5f, 5.f, 0.f, 1},
                    {1.0f, 0.7f, 4.f, 0.f, 2}, {1.0f, 0.55f, 5.f, 0.f, 3}, {1.0f, 0.6f, 4.f, 0.f, 4} };
                return F[form]; }
        }
    }

    // ── MODAL bank build (bars/bells/skins): ratios × T60 × strike-position gains ──
    void buildModal (float f0, const Form& fm, float pos, float mtrl, float dec, float str, float /*body*/) noexcept
    {
        const float* ratios; int nr;
        modeTable (p_.family, fm.modeSet, ratios, nr);
        nModes_ = std::min (modal::kMaxModes, nr);
        if (budgetUsed_ && budgetCap_ > 0)              // graceful CPU thinning
        {
            const int room = std::max (6, budgetCap_ - *budgetUsed_);
            nModes_ = std::min (nModes_, std::max (6, room / std::max (1, uniN_)));
            nModes_ = std::min (nModes_, nr);
        }

        // STRETCH morph: <0.5 squeeze toward harmonic, >0.5 stretch inharmonic (extra spread)
        const float sc = (str - 0.5f) * 2.f;            // −1..+1
        // DECAY → T60 of the fundamental mode (seconds), exp mapping ~0.05s → ~35s
        const float t60base  = 0.05f * std::pow (2.f, dec * 9.5f);
        const float alphaBase = 6.9078f / (t60base + 1e-4f);        // −60 dB decay rate (per sec) of mode 0
        // MATERIAL → damping-vs-frequency: T60(f) = T60base·(f/f0)^(−tilt). Metal (mtrl→1)
        // tilt≈0.18 so highs ring almost as long (bell/glass); wood/felt (mtrl→0) tilt≈1.7 so
        // highs die fast (marimba/skin). This one law IS the wood↔metal axis.
        const float tilt = lerpf (1.9f, 0.16f, mtrl);
        const float aG   = lerpf (2.4f, 0.55f, mtrl);             // wood dies fast, metal rings long

        const float posf = clamp01 (pos);
        const float f0safe = std::max (1.f, f0);
        float peakA = 1e-6f;
        for (int k = 0; k < nModes_; ++k)
        {
            float ratio = ratios[(size_t) k];
            // inharmonicity morph: <0.5 pulls partials toward the harmonic ladder, >0.5 stretches
            const float harmk = (float) (k + 1);
            if (sc < 0.f)      ratio = lerpf (ratio, harmk, -sc);
            else if (sc > 0.f) ratio = ratio * (1.f + 0.5f * sc * (float) k / (float) std::max (1, nModes_));
            float fk = f0 * ratio;
            if (fk <= 0.f || fk >= 0.49f * (float) rate_) { modes_[(size_t) k].amp = 0.f; modes_[(size_t) k].freqHz = 0.f; continue; }

            // T60-tilt decay → per-sample pole radius
            const float alpha = alphaBase * aG * std::pow (fk / f0safe, tilt);
            float r = std::exp (-alpha / (float) rate_);
            if (r > 0.99998f) r = 0.99998f;             // hard ceiling (stability)
            const float w = 2.f * kPi * fk * fsInv_;

            // strike-position weighting: g = |sin(k·π·pos)| (nodes suppressed) + AGE jitter
            float g = std::fabs (std::sin (harmk * kPi * posf));
            g = 0.15f + 0.85f * g;                      // never fully null a mode
            g *= 1.f / (0.6f + 0.5f * (float) k);       // gentle 1/k tilt (energy realism)

            modes_[(size_t) k].set (w, r);
            modes_[(size_t) k].amp = g;
            modes_[(size_t) k].freqHz = fk;
            modes_[(size_t) k].reset();
            peakA = std::max (peakA, g);
        }
        // normalize so struck loudness is family-independent
        const float gn = 0.6f / peakA;
        for (int k = 0; k < nModes_; ++k) modes_[(size_t) k].amp *= gn;

        // HALO doublet beating pairs (bell warble): split adjacent modes a hair
        // applied in buildVoice-tail via haloAmt_ during ticks is overkill; bake a small
        // per-mode detune here proportional to halo
        // (kept subtle; the sympathetic send in tickModal adds the rest)
        outGain_ = 1.2f;
    }

    // ── waveguide / MSW build (strings, bow, winds, brass) ──
    void buildLoops (float f0, const Form& fm, float pos, float mtrl, float dec, float str, float /*body*/) noexcept
    {
        const float bright = fm.bright;
        // brightness of the loop damping filter (MATERIAL × form). The one-zero averager in
        // the string loop kills Nyquist, so this one-pole only shapes the audible tilt.
        // GRAND stays a STEEL piano string at BOTH ends of MATERIAL — the floor is RAISED to ~1400 Hz
        // (the old ~600-800 Hz floor is what made MATERIAL<50% sound like a dead nylon guitar). Warm hall
        // piano ↔ brilliant new hammers, steel throughout. Other families keep the wide wood↔metal sweep.
        float cut;
        if (p_.family == modal::GRAND)
            cut = lerpf (1400.f, 7000.f, clamp01 (0.72f * mtrl + 0.18f * clamp01 (p_.hard) + 0.10f * bright));
        else
            cut = 300.f * std::pow (2.f, lerpf (0.5f, 6.0f, clamp01 (mtrl * 0.7f + bright * 0.3f)));
        lpA_.setCutoff (std::min (cut, 0.45f * (float) rate_), (float) rate_);
        lpB_.setCutoff (std::min (cut * 0.96f, 0.45f * (float) rate_), (float) rate_);
        // GRAND MATERIAL also tilts the OUTPUT presence (2.6 kHz): the loop cutoff shapes only the
        // weak top partials, so this bell is what actually sweeps the AUDIBLE warm↔brilliant range.
        if (p_.family == modal::GRAND)
            matEq_.peaking (2000.f, (float) rate_, 0.5f, lerpf (-9.f, 8.f, mtrl));   // wide presence tilt

        // loop gain from DECAY (RT60). g close to 1 = long ring. Strings ride the one-zero
        // (a hair of loss per trip) so cap a touch below unity to stay well-behaved.
        if (p_.family == modal::GRAND)
        {
            // register-scaled RT60: bass rings ~2-3× longer than treble (real piano); DECAY spans
            // staccato (~0.1 s) → pedal-up cathedral. g = the per-trip gain that hits that T60 at f0.
            const float t60 = (0.10f * std::pow (2.f, 8.f * dec)) * lerpf (1.8f, 0.4f, clamp01 (keyN_ / 88.f));
            loopGain_ = std::min (0.99995f, std::exp (-6.9078f / std::max (1e-3f, t60 * f0)));
        }
        else
            loopGain_ = lerpf (0.972f, 0.99965f, std::pow (dec, 0.7f));

        // dispersion (piano stiffness B): allpass cascade stretches upper partials sharp.
        float dispAmt;
        if (p_.family == modal::GRAND)
        {
            // A real piano is ALWAYS stiff — bake key-scaled inharmonicity ON by default (a perfectly
            // harmonic comb is the #1 "synth/organ" tell). Railsback U-curve B(key), min at C3; STRETCH
            // scales it a REALISTIC ×0.33 (near-flat) … ×1 (measured grand) … ×3 (honky-tonk) — never
            // a bell/gong detune sweep. The fundamental is pitch-anchored in buildDispersion (a-C-is-a-C).
            const float kn = keyN_;
            const float log10B = (kn <= 27.f) ? (-3.40f - 0.45f * (kn / 27.f))          // A0≈4e-4 → C3≈1.4e-4
                                              : (-3.85f + 2.25f * ((kn - 27.f) / 60.f)); // C4≈4e-4 → C8≈2.5e-2
            const float Beff = std::pow (10.f, log10B) * std::pow (3.f, (str - 0.5f) * 2.f);
            dispAmt = clamp01 ((std::log10 (std::max (1e-6f, Beff)) + 4.30f) / 2.78f);   // perceptual B → allpass strength
        }
        else
            dispAmt = clamp01 ((str - 0.5f) * 2.f);   // other string families: only STRETCH>0.5 stiffens
        buildDispersion (dispAmt, f0);

        // fundamental period, phase-delay compensated so a C is a C (string loop adds the
        // one-zero's ½-sample delay; MSW loops don't run the averager).
        const float lpPd = lpA_.phaseDelay();
        const float ozPd = (core_ == 0) ? 0.5f : 0.f;
        float period = (float) rate_ / std::max (20.f, f0);
        float dispPd = dispActive_ ? dispPhaseDelay_ : 0.f;   // dispPhaseDelay_ = exact TOTAL allpass delay at f0
        float La = period - lpPd - ozPd - dispPd - 1.f;
        // HALO: dual-polarization detune (beating + two-stage decay) — audible chorus at max
        // GRAND runs a permanent slightly-detuned unison TWIN (piano strings) → coupled beating + a
        // two-stage decay; HALO adds MORE sympathetic detune on top. Other string families detune via HALO only.
        const float baseCents = (p_.family == modal::GRAND) ? 1.0f : 0.f;
        const float haloCents = baseCents + clamp01 (p_.halo) * 14.0f;
        float Lb = ((float) rate_ / std::max (20.f, f0 * centsMul (haloCents))) - lpPd - ozPd - dispPd - 1.f;
        La = std::max (2.f, std::min ((float) modal::kMaxDelay - 3.f, La));
        Lb = std::max (2.f, std::min ((float) modal::kMaxDelay - 3.f, Lb));
        dlA_.setDelay (La); dlB_.setDelay (Lb);

        // position comb (pluck/bow point): out -= comb(out) at pos·period
        combLen_ = std::max (1.f, (0.5f * clamp01 (pos + agePosJit_)) * period);
        combA_.setDelay (combLen_); combB_.setDelay (combLen_);
        combDepth_ = 0.5f + 0.4f * clamp01 (pos);
        combNorm_  = 1.f / std::sqrt (1.f + combDepth_ * combDepth_);   // energy-normalize the strike comb → POS is a TIMBRE knob, not a level/clip one

        // MSW families: set up the bore/nut delays + nonlinearity parameters
        if (core_ == 1)
        {
            // Air columns have a low-frequency SPEAKING FLOOR (a short tube can't sound a low
            // note — the harmonic won't lock). Below the floor, octave-fold up so a sub-range
            // note plays the nearest speaking octave instead of going silent (what a real
            // player does). Bow/reed speak down to ~40 Hz, flute/brass are more limited.
            float fEff = f0;
            const float floorHz = (p_.family == modal::FLUTE) ? 90.f
                                : (p_.family == modal::BRASS) ? 52.f : 34.f;
            while (fEff < floorHz) fEff *= 2.f;

            switch (p_.family)
            {
                case modal::BOW:
                {
                    // two string segments around bow point β
                    const float beta = 0.08f + 0.28f * clamp01 (pos);
                    boreN_.setDelay (std::max (2.f, La * (1.f - beta)));
                    boreB_.setDelay (std::max (2.f, La * beta));
                    bowSlope_ = 5.f - 4.f * clamp01 (p_.hard * 0.5f + 0.25f);   // friction curve width
                    break;
                }
                case modal::FLUTE:
                {
                    const float bore = (float) rate_ / std::max (20.f, fEff * 0.6667f);  // overblow tuning
                    boreN_.setDelay (std::max (4.f, bore - lpPd - 1.f));
                    boreB_.setDelay (std::max (2.f, bore * (0.08f + 0.48f * clamp01 (pos)))); // jet delay ≈ ½ period
                    break;
                }
                case modal::REED:
                {
                    const bool cone = (fm.modeSet == 1);          // sax/oboe = conical (all harmonics + octave)
                    const float bore = cone ? ((float) rate_ / std::max (20.f, fEff))
                                            : (0.5f * (float) rate_ / std::max (20.f, fEff)); // clarinet = ½ (odd)
                    boreN_.setDelay (std::max (4.f, bore - lpPd - 1.f));
                    reedSlope_ = -0.44f + 0.26f * clamp01 (p_.hard);
                    break;
                }
                case modal::BRASS:
                {
                    // A true lip-scattering model won't bootstrap from a DC breath, so brass
                    // rides the proven self-oscillating reed loop (open bore = all harmonics,
                    // like a conical brass tube) with a bright mouthpiece formant + a
                    // level-driven brightness ("brassiness" = nonlinear steepening at ff).
                    const float bore = (float) rate_ / std::max (20.f, fEff);        // full period = f0
                    boreN_.setDelay (std::max (4.f, bore - lpPd - 1.f));
                    reedSlope_ = -0.34f + 0.20f * clamp01 (p_.hard);                 // stiffer "lip"
                    lipBq_.peaking (std::min (850.f, 0.4f * (float) rate_), (float) rate_, 1.2f, 6.f * clamp01 (p_.body) + 2.f);
                    break;
                }
                default: break;
            }
        }
        nModes_ = 0;
    }

    void buildDispersion (float amt, float f0) noexcept
    {
        dispActive_ = (amt > 0.02f);
        dispPhaseDelay_ = 0.f;
        if (! dispActive_) { for (auto& d : disp_) { d.reset(); d.set (0.f, 0.f); } for (auto& d : dispB_) { d.reset(); d.set (0.f, 0.f); } return; }
        // stiffness dispersion: a cascade of FIRST-ORDER allpasses with a NEGATIVE coefficient — the
        // group delay DECREASES with frequency, so upper partials complete the loop sooner and stretch
        // SHARP (piano inharmonicity f_n = n·f0·√(1+B·n²), Jaffe–Smith / Van Duyne / Rauhala–Välimäki).
        // AllpassS with a2=0 realizes z⁻¹·AP1(a=−aMag): the z⁻¹ per stage is constant delay (absorbed by
        // the f0 anchoring below); AP1's a<0 does the frequency-dependent sharpening. |a| grows with B.
        // strength grows with B; bounded on SHORT (high-note) loops so the dispersion delay can't eat the
        // period and detune the note (keeps a-C-is-a-C at every register — treble disperses gently).
        // Coefficient bounded so the f0-anchoring linearization stays accurate (strong dispersion shifts the
        // fundamental — see Rauhala–Välimäki) AND the delay can't eat a short (high-note) loop → a-C-is-a-C
        // holds at every register. This gives moderate, pitch-locked piano stiffness (deepening it further
        // = a closed-form dispersion design, a later refinement). |a| grows with B, tapers with register.
        const float period = (float) rate_ / std::max (20.f, f0);
        const float aMag = std::min (0.85f, 0.40f + 1.5f * amt) * clamp01 (period / 240.f);
        for (int s = 0; s < modal::kDispStages; ++s) { disp_[(size_t) s].set (-aMag, 0.f); dispB_[(size_t) s].set (-aMag, 0.f); }
        // ── PITCH ANCHORING (a-C-is-a-C): the allpass cascade adds a frequency-dependent delay to the
        //    loop — measure its EXACT total phase delay AT f0 and compensate La, so the FUNDAMENTAL stays
        //    locked while the upper partials stretch sharp. (The old `1.5·amt` guess slid the fundamental
        //    → that was the "STRETCH detunes into a synth" bug.) ──
        const float w0 = 2.f * kPi * std::min (f0, 0.45f * (float) rate_) * fsInv_;
        float pd = 0.f;
        if (w0 > 1e-5f)
            for (int s = 0; s < modal::kDispStages; ++s)
            {
                const float A1 = disp_[(size_t) s].a1, A2 = disp_[(size_t) s].a2;
                const float c1 = std::cos (w0), s1 = std::sin (w0);
                const float c2 = std::cos (2.f * w0), s2 = std::sin (2.f * w0);
                const float Nre = A2 + A1 * c1 + c2,     Nim = -(A1 * s1 + s2);        // H = (a2 + a1 z⁻¹ + z⁻²)/…
                const float Dre = 1.f + A1 * c1 + A2 * c2, Dim = -(A1 * s1 + A2 * s2);
                float ph = std::atan2 (Nim, Nre) - std::atan2 (Dim, Dre);
                while (ph >  0.f)            ph -= 2.f * kPi;                           // wrap into (-2π, 0]
                while (ph <= -2.f * kPi)     ph += 2.f * kPi;
                pd += -ph / w0;                                                        // phase delay (samples), ≥ 0
            }
        dispPhaseDelay_ = std::max (0.f, pd);            // exact TOTAL allpass delay at f0 (compensated in La)
    }

    void buildBody (const Form& fm, float bodyAmt) noexcept
    {
        // a few peaking resonances = the instrument body / soundboard (BODY scales depth).
        // Violin-ish for bowed, guitar-ish for plucked, dark cabinet for grand.
        const float depth = fm.bodyDb * clamp01 (bodyAmt) * 1.6f;
        struct BR { float f, q, g; };
        static const BR violin[modal::kBodyModes] = { {275.f,3.f,1.f},{460.f,4.f,1.f},{550.f,4.f,1.f},{800.f,2.f,-0.4f},{2500.f,1.2f,0.7f},{6000.f,1.f,0.4f} };
        static const BR guitar[modal::kBodyModes] = { {100.f,5.f,1.f},{200.f,4.f,1.f},{400.f,3.f,0.7f},{1200.f,2.f,-0.3f},{2600.f,1.5f,0.5f},{4200.f,1.2f,0.3f} };
        static const BR grand [modal::kBodyModes] = { {60.f,6.f,1.f},{120.f,5.f,0.8f},{220.f,4.f,0.7f},{440.f,3.f,0.5f},{1100.f,2.f,-0.2f},{3000.f,1.2f,0.3f} };
        const BR* set = (p_.family == modal::BOW) ? violin
                      : (p_.family == modal::GRAND) ? grand : guitar;
        bodyOn_ = (bodyAmt > 0.02f && depth > 0.1f);
        for (int i = 0; i < modal::kBodyModes; ++i)
            body_[(size_t) i].peaking (std::min (set[i].f, 0.45f * (float) rate_), (float) rate_,
                                        set[i].q, set[i].g * depth);
    }

    // ═══════════════════════════ excitation ══════════════════════════════════
    // Map the purple-box fractions (loopStart/loopEnd) → exciter sample indices [loopLo_,loopHi_].
    // Called at block-rate (setParams / setExciterSample) and at note-on so the looped read tracks
    // the box live, exactly like the Sample engine's setRegionParams. Guards a degenerate/inverted box.
    void computeLoopBounds() noexcept
    {
        if (exDataLen_ > 1)
        {
            const float Nm1 = (float) (exDataLen_ - 1);
            float lo = clamp01 (p_.loopStart) * Nm1;
            float hi = clamp01 (p_.loopEnd)   * Nm1;
            if (hi < lo + 1.f) { hi = lo + 1.f; if (hi > Nm1) { hi = Nm1; lo = hi - 1.f; if (lo < 0.f) lo = 0.f; } }
            loopLo_ = lo; loopHi_ = hi;
        }
        else { loopLo_ = 0.f; loopHi_ = 0.f; }
    }

    void primeExcitation() noexcept
    {
        // choose exciter: Sample if loaded & (source==Sample or Auto), else Click/Noise.
        const bool wantSample = exData_ && (p_.source == 3 || (p_.source == 0));
        useSample_ = wantSample;
        exPos_ = 0; exRead_ = 0.f;
        // burst length from HARD (contact time): soft = long/dark, hard = short/bright
        const float hard = clamp01 (p_.hard);
        burstLen_ = (int) (rate_ * (0.020f - 0.018f * hard));     // 20ms soft → 2ms hard
        burstLen_ = std::max (1, burstLen_);
        // noise burst LP cutoff (velocity → brightness; hard → brightness)
        noiseCut_ = 300.f * std::pow (2.f, lerpf (1.f, 7.f, clamp01 (0.35f * hard + 0.5f * vel_ + 0.15f)));
        noiseZ_ = 0.f;
        // GRAND: a real FELT HAMMER, not filtered-noise-into-a-string (that is a pluck = the nylon sound).
        // Contact time is key- + hardness- + velocity-scaled (Chaigne & Askenfelt): bass ~4 ms → treble
        // <1 ms; harder/louder = shorter = brighter. The exciter is a deterministic force pulse of that width.
        feltK_ = 0.f; knockLvl_ = 0.f;
        if (p_.family == modal::GRAND && ! useSample_)
        {
            const float tc0 = lerpf (3.2e-3f, 0.6e-3f, clamp01 (keyN_ / 88.f));
            float tc = tc0 * lerpf (1.4f, 0.28f, hard) * lerpf (1.15f, 0.85f, vel_);
            tc = std::min (4.0e-3f, std::max (0.25e-3f, tc));   // shorter contact = richer upper partials (less dull)
            burstLen_ = std::max (2, (int) (tc * (float) rate_));   // contact window = the force-pulse width
            feltK_    = 0.6f * hard;                                // felt hardening → upper partials with HARD
            knockLvl_ = 0.04f + 0.10f * hard;                       // contact "thump" (felt knock) amount
            noiseCut_ = 1500.f + 5000.f * hard;                     // knock brightness
            // energy-normalize the pulse so HARD is a pure TIMBRE (brightness) control, not a loudness one:
            // attenuate long (bass) contacts, and compensate the short/bright contacts' higher excitation
            // efficiency so a hard hit is brighter, NOT louder → hard hits stay clean (no output clip).
            feltNorm_ = std::min (1.0f, std::sqrt (110.f / (float) burstLen_)) * (1.f - 0.35f * hard);
        }
        exLen_ = useSample_ ? exDataLen_ : burstLen_;
        // exciter read start/direction. LOOP modes start at the sample BEGINNING (exRead_=0) and play a
        // forward LEAD-IN; once the read first reaches the purple box they "catch" and loop within
        // [loopLo_,loopHi_] — exactly the Sample/Granular loop-catch model. (exRead_ is already 0 above.)
        computeLoopBounds();
        exDir_  = 1;
        caught_ = false;

        // continuous drive (bow/breath) target set from BREATH; sustained families always drive
        const float breath = clamp01 (p_.breath);
        contDrive_ = breath;
        if (core_ == 1)                                  // MSW families breathe by default
            contDrive_ = std::max (contDrive_, 0.55f + 0.35f * vel_);

        // AGE roll: chance of a quick multi-strike (marimba/tabla roll humanization)
        rollLeft_ = 0;
        if (core_ == 2 && rng01() < rollChance_ * 0.5f) rollLeft_ = (rng01() < 0.4f) ? 2 : 1;
        rollTimer_ = 0;
        exciteGain_ = 0.35f + 0.65f * vel_;
    }

    // returns the excitation sample for this frame (burst / sample / continuous)
    inline float nextExcitation (int /*i*/) noexcept
    {
        float e = 0.f;

        // ── impulsive part: burst / sample (strings + modal + attack of winds) ──
        const bool sampleLoop = useSample_ && exData_ && p_.loopMode != 0 && exDataLen_ > 1;
        if (exPos_ < exLen_ || sampleLoop)
        {
            float raw;
            if (useSample_ && exData_)
            {
                int idx = (int) exRead_;
                if (idx < 0) idx = 0; else if (idx > exDataLen_ - 1) idx = exDataLen_ - 1;
                raw = (idx < exDataLen_ - 1)
                    ? lerpf (exData_[(size_t) idx], exData_[(size_t) (idx + 1)], exRead_ - (float) idx)
                    : exData_[(size_t) (exDataLen_ - 1)];
                exRead_ += exSrcStep_ * (float) exDir_;
                // loop-mode boundary handling — Sample/Granular LOOP-CATCH model: play a forward
                // LEAD-IN from the sample start, and only once the read first reaches the purple box
                // [loopLo_,loopHi_] does it "catch" and loop THERE. One-Shot ignores the box (reads once).
                const float span = loopHi_ - loopLo_;
                if (p_.loopMode == 0) { if ((int) exRead_ >= exDataLen_) exPos_ = exLen_; }          // One-Shot: stop at buffer end
                else if (span < 1.f)  { if (exRead_ >= loopLo_) exRead_ = loopLo_; }                  // degenerate box → lead-in then hold
                else
                {
                    // catch on first reaching the box's far edge (= loop end); Rev/Ping then turn back there.
                    if (! caught_ && (exRead_ >= loopHi_ || (int) exRead_ >= exDataLen_ - 1))
                    { caught_ = true; if (exRead_ > loopHi_) exRead_ = loopHi_; if (p_.loopMode != 1) exDir_ = -1; }
                    if (caught_)
                    {
                        if      (p_.loopMode == 1) { if (exRead_ >= loopHi_) exRead_ -= span; }        // Forward loop in box
                        else if (p_.loopMode == 2) { if (exRead_ <= loopLo_) exRead_ += span; }        // Reverse loop in box
                        else { if      (exRead_ >= loopHi_) { exRead_ = loopHi_; exDir_ = -1; }         // Ping-Pong in box
                               else if (exRead_ <= loopLo_) { exRead_ = loopLo_; exDir_ =  1; } }
                    }
                }
                if (p_.loopMode != 0) raw *= 0.5f;   // a looped exciter drives continuously — tame it so the resonator can't run away
            }
            else if (p_.family == modal::GRAND)
            {
                // FELT HAMMER force pulse (deterministic): a raised-cosine contact-force pulse whose WIDTH
                // is the contact time — that is a STRIKE, not filtered-noise-into-a-string (a pluck). The
                // Hann pulse's spectrum rolls off above ~1/(2·tc), so contact time IS the attack brightness.
                const float ph = (float) exPos_ / (float) std::max (1, burstLen_);    // 0..1 across contact
                float F = 0.5f - 0.5f * std::cos (2.f * kPi * ph);                     // Hann force pulse (unipolar; DC-blocked in the loop)
                F = F * (1.f + feltK_ * F) / (1.f + feltK_);                           // felt hardening → upper partials, peak-preserved
                // felt KNOCK: brief filtered-noise contact transient over the first half — contact noise
                // (realism law #1), NOT the whole exciter.
                float knock = 0.f;
                if (ph < 0.5f)
                {
                    float nz = rngPm();
                    noiseZ_ += (1.f - std::exp (-2.f * kPi * noiseCut_ * fsInv_)) * (nz - noiseZ_);
                    knock = noiseZ_ * knockLvl_ * (1.f - 2.f * ph);                    // fades out across the first half
                }
                // a coherent force pulse injects FAR more resonance than the old spread-out noise burst,
                // so scale it well under the output soft-clip ceiling → hard hits stay clean (louder +
                // brighter), never digital-clip. Longer (bass) contacts carry more energy → level ∝ 1/√len
                // keeps the hottest low-note/max-velocity case in the clean zone without squashing dynamics.
                raw = (F + knock) * (0.36f * feltNorm_);
            }
            else
            {
                // shaped noise burst with a raised-cosine window (contact-time envelope)
                const float w = 0.5f - 0.5f * std::cos (2.f * kPi * (float) exPos_ / (float) std::max (1, burstLen_));
                float nz = rngPm();
                noiseZ_ += (1.f - std::exp (-2.f * kPi * noiseCut_ * fsInv_)) * (nz - noiseZ_);
                raw = noiseZ_ * w;
                if (p_.source == 2) raw = (exPos_ == 0 ? 1.f : raw * 0.2f);   // Click = a spike + tail
            }
            e += raw * exciteGain_;
            if (! sampleLoop) ++exPos_;   // looping keeps exPos_ < exLen_ so the impulsive gate stays open; exRead_ drives the loop
        }
        else if (rollLeft_ > 0)                               // AGE multi-strike roll
        {
            if (++rollTimer_ > (int) (rate_ * (0.03f + 0.05f * rng01())))
            { exPos_ = 0; exRead_ = 0.f; --rollLeft_; rollTimer_ = 0; exciteGain_ *= 0.8f; }
        }

        // ── continuous part: bow friction / breath (BREATH lane + MSW families) ──
        if (contDrive_ > 1e-3f && ! ringDown_)
        {
            const float target = contDrive_ * envRel_;
            driveZ_ += (1.f - std::exp (-1.f / (0.004f * (float) rate_))) * (target - driveZ_);
            // turbulence/rosin noise rides the drive (breath is half the instrument)
            const float turb = 0.10f * rngPm();
            e += (driveZ_ * (1.f + turb));
        }
        else if (ringDown_)
        {
            driveZ_ += (1.f - std::exp (-1.f / (0.02f * (float) rate_))) * (0.f - driveZ_);
            if (contDrive_ > 1e-3f) e += driveZ_;
        }
        return e;
    }

    // ═══════════════════════════ core ticks ══════════════════════════════════
    // STRING (waveguide SDL, dual polarization). Excite = injected into the loop.
    inline float tickString (float in) noexcept
    {
        // GRAND: strike-position comb on the EXCITATION (feedforward) instead of the OUTPUT — POS shapes
        // which harmonics the hammer excites (node at the strike point) WITHOUT the output comb's resonant
        // peak boost that pushed high POS into the clip. Other string families keep the output comb below.
        const float exc = (p_.family == modal::GRAND) ? (in - combA_.tick (in) * combDepth_) * combNorm_ : in;

        // polarization A — loop damper = one-zero averager (kills Nyquist) → one-pole tilt
        float fbA = lpA_.process (ozA_.process (dlA_.peek())) * (loopGain_ * relGain_);
        if (dispActive_) { for (int s = 0; s < modal::kDispStages; ++s) fbA = disp_[(size_t) s].process (fbA); }
        float outA = dlA_.tick (exc + fbA);
        outA = dcA_.process (outA);
        // AGE buzz: sitar/biwa asymmetric bridge rectifier (nonlinear at the top of AGE)
        if (ageBuzz_ > 0.f) { const float v = std::fabs (outA) - 0.02f * ageBuzz_; outA = lerpf (outA, (v > 0.f ? v : 0.f) * (outA > 0.f ? 1.f : -1.5f), ageBuzz_ * 0.5f); }

        // polarization B — the detuned twin string. Uses its OWN dispersion state (dispB_) so the two
        // polarizations don't corrupt each other's allpass memory when both run every sample.
        float s;
        if (p_.family == modal::GRAND)
        {
            // GRAND: ALWAYS-ON coupled unison. B rings a hair LONGER than A → Weinreich two-stage decay
            // (a prompt sound over a slow aftersound); the ~0.7¢ detune (buildLoops Lb) makes the pair
            // beat = the "alive" piano sustain. Summed as equal partners, scaled to keep the pair's peak
            // ≈ one string (stays under the soft-clip ceiling — no return of the clipping).
            // B loses ~⅓ as much energy per round-trip as A → rings ~3× longer = a strong prompt→aftersound
            // (Weinreich). Clamped stable. This is what makes the sustain "bloom" instead of dying flat.
            const float gB = std::min (0.99996f, 1.f - (1.f - loopGain_) * 0.32f);
            float fbB = lpB_.process (ozB_.process (dlB_.peek())) * (gB * relGain_);
            if (dispActive_) { for (int s2 = 0; s2 < modal::kDispStages; ++s2) fbB = dispB_[(size_t) s2].process (fbB); }
            float outB = dcB_.process (dlB_.tick (exc + fbB));
            s = (outA + outB) * 0.55f;
        }
        else if (haloAmt_ > 0.02f)
        {
            float fbB = lpB_.process (ozB_.process (dlB_.peek())) * std::min (0.99993f, loopGain_ + 0.0016f * haloAmt_);
            if (dispActive_) { for (int s2 = 0; s2 < modal::kDispStages; ++s2) fbB = dispB_[(size_t) s2].process (fbB); }
            float outB = dcB_.process (dlB_.tick (in + fbB));
            s = outA * (1.f - 0.4f * haloAmt_) + outB * (0.5f + 0.5f * haloAmt_);   // HALO chorus/sympathetic
        }
        else
            s = outA;
        // position comb — OUTPUT comb for non-GRAND families; GRAND combs the excitation instead (above)
        // so POS no longer boosts the resonant peak into the clip.
        if (p_.family != modal::GRAND) s -= combA_.tick (s) * combDepth_;
        // MATERIAL presence tilt (GRAND) — the audible warm↔brilliant sweep
        if (p_.family == modal::GRAND) s = matEq_.process (s);
        // BLOOM: slow high-shelf lift as the note develops (energy migrates up)
        s = applyBloom (s);
        // BODY radiation
        if (bodyOn_) { float bsum = 0.f; for (int i = 0; i < modal::kBodyModes; ++i) bsum += body_[(size_t) i].process (s); s = lerpf (s, s * 0.4f + bsum * 0.5f, 0.8f); }
        ampZ_ = s;
        return s;
    }

    // REEDBORE (MSW): the nonlinearity lives IN the loop. One structure, four instruments.
    inline float tickReedBore (float in) noexcept
    {
        float s = 0.f;
        switch (p_.family)
        {
            case modal::BOW:
            {
                // bowed string: two segments, friction table at the bow point
                const float bridge = -lpA_.process (boreB_.peek());
                const float nut    = -boreN_.peek();
                const float bowVel = (0.03f + 0.20f * in);                // BREATH/drive → bow speed
                const float dv = bowVel - (bridge + nut);
                const float slope = bowSlope_;
                float bt = std::pow (std::fabs (dv * slope) + 0.75f, -4.f); // friction reflection
                bt = bt < 0.01f ? 0.01f : (bt > 0.98f ? 0.98f : bt);
                const float nv = dv * bt;
                boreN_.tick (bridge + nv);
                s = boreB_.tick (nut + nv);
                s = dcA_.process (s);
                break;
            }
            case modal::FLUTE:
            {
                float refl = -lpA_.process (boreN_.peek());
                float pd = in - 0.5f * refl;                              // breath − jetReflection·refl
                pd = boreB_.tick (pd);                                     // jet delay
                pd = pd > 2.f ? 2.f : (pd < -2.f ? -2.f : pd);            // guard the jet input
                float jet = pd * (pd * pd - 1.f);                          // cubic sigmoid nonlinearity
                jet = jet > 1.f ? 1.f : (jet < -1.f ? -1.f : jet);        // STK clamp (stops runaway → NaN)
                float x = jet + 0.5f * refl;
                x = dcA_.process (x);
                s = boreN_.tick (x);
                break;
            }
            case modal::REED:
            {
                float refl = -0.95f * lpA_.process (boreN_.peek());
                float pd = refl - in;                                      // reed differential pressure
                float reed = 0.7f + reedSlope_ * pd;                       // reed table (linear clamp)
                reed = reed > 1.f ? 1.f : (reed < -1.f ? -1.f : reed);
                s = boreN_.tick (in + pd * reed);
                s = dcA_.process (s);
                break;
            }
            default: // BRASS — clean reed loop (oscillates at every register) + brass color on the tap
            {
                // The self-oscillating loop is the SAME proven mechanism as REED — a nonlinearity
                // in the feedback path would kill low-register speech (no harmonic survives with
                // net gain), so brassiness lives OUTSIDE the loop.
                float refl = -0.992f * lpA_.process (boreN_.peek());      // high reflection → speaks low
                float pd = refl - in;                                      // pressure across the "lips"
                float lip = 0.7f + reedSlope_ * pd;                       // valve table
                lip = lip > 1.f ? 1.f : (lip < -1.f ? -1.f : lip);
                s = boreN_.tick (in + pd * lip);
                s = dcA_.process (s);
                // brass character on the OUTPUT tap only: level-driven wave steepening (brassiness at
                // ff) + ~850 Hz mouthpiece formant. Not fed back → cannot destabilize the oscillator.
                const float bz = std::fabs (ampZ_);
                s = std::tanh (s * (1.f + 2.2f * bz));
                s = lipBq_.process (s);
                break;
            }
        }
        s = applyBloom (s);
        if (bodyOn_) { float bsum = 0.f; for (int i = 0; i < modal::kBodyModes; ++i) bsum += body_[(size_t) i].process (s); s = lerpf (s, s * 0.5f + bsum * 0.4f, 0.7f); }
        ampZ_ = s;
        return s * 3.0f;   // MSW loops run hot-quiet internally
    }

    // MODAL bank (bars/bells/skins): excite all modes, sum quadrature outputs.
    inline float tickModal (float in) noexcept
    {
        // BLOOM gong glide: slowly detune modes downward (large gong) or up
        if (bloomGlide_ != 0.f && bloomAmt_ > 0.f)
        {
            bloomEnv_ += (1.f - std::exp (-1.f / (0.4f * (float) rate_))) * (1.f - bloomEnv_);
            // (glide is a slow, subtle per-block retune; applied in renderBlock via retuneModes)
        }
        float s = 0.f;
        for (int k = 0; k < nModes_; ++k)
        {
            Mode& m = modes_[(size_t) k];
            if (m.amp <= 0.f) continue;
            s += m.tick (in * m.amp);                    // strike energy weighted by mode gain (once)
        }
        // BODY: a resonant tube/pipe reinforcing the fundamental (marimba bloom)
        if (bodyOn_) { float bsum = body_[0].process (s); s = lerpf (s, s + 0.6f * bsum, 0.7f); }
        // HALO sympathetic send: feed a little output back into the lowest modes (coupled ring)
        if (haloAmt_ > 0.02f && nModes_ > 0)
        {
            const float fb = s * haloAmt_ * 0.02f;
            modes_[0].tick (fb);
            if (nModes_ > 2) modes_[2].tick (fb * 0.6f);
        }
        s = applyBloom (s);
        ampZ_ = s;
        return s;
    }

    // BLOOM: post-attack life (Pianoteq "blooming"). The note starts slightly MUTED and OPENS
    // up bright over the swell envelope — a rising-cutoff lowpass ("wah open") + a shimmer
    // tremolo. Pure filtering + gain, no feedback → always stable. Dead at 0, alive at 1.
    inline float applyBloom (float s) noexcept
    {
        if (bloomAmt_ < 0.02f) return s;
        bloomEnv_ += (1.f - std::exp (-1.f / (lerpf (3.0f, 0.55f, bloomAmt_) * (float) rate_))) * (1.f - bloomEnv_);
        // openness climbs from "darkened by bloom amount" up to fully open as the note develops
        const float openness = 1.f - bloomAmt_ * (1.f - bloomEnv_);
        const float fc = lerpf (350.f, 15000.f, openness * openness);
        const float a  = std::exp (-2.f * kPi * std::min (fc, 0.45f * (float) rate_) * fsInv_);
        bloomLpZ_ = (1.f - a) * s + a * bloomLpZ_;
        // shimmer tremolo that deepens as it blooms — the unmistakable "alive" movement
        const float trem = 1.f + 0.55f * bloomAmt_ * bloomEnv_ * std::sin (bloomLfo_);
        bloomLfo_ += 2.f * kPi * 5.2f * fsInv_; if (bloomLfo_ > 2.f * kPi) bloomLfo_ -= 2.f * kPi;
        // level swells up as it opens (the "boiiing" bloom) — normalized so 0 is unity
        const float swell = 1.f + 0.5f * bloomAmt_ * bloomEnv_;
        return flushDenorm (bloomLpZ_) * trem * swell;
    }

    // ═══════════════════════════ mode tables ═════════════════════════════════
    static void modeTable (int fam, int set, const float*& out, int& n) noexcept
    {
        // measured ratio sets (from the research — re-derived, energy-realistic)
        static const float freeBar[] = { 1.f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f };            // uniform bar
        static const float marimba[] = { 1.f, 4.f, 10.f, 20.f, 31.f, 44.f };                        // tuned 1:4:10
        static const float vibes[]   = { 1.f, 3.984f, 10.7f, 17.6f, 26.4f, 36.f };                  // vibraphone
        static const float xylo[]    = { 1.f, 3.f, 6.f, 10.f, 15.f, 21.f };                         // xylophone
        static const float glass[]   = { 1.f, 2.32f, 4.25f, 6.63f, 9.38f, 12.5f };                  // glass harmonica
        static const float kalimba[] = { 1.f, 2.65f, 5.2f, 8.9f, 13.3f, 18.5f };                    // tine (clamped-free-ish)
        static const float church[]  = { 0.5f, 1.f, 1.183f, 1.506f, 2.f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f }; // hum:prime:tierce:quint:nominal...
        static const float tubular[] = { 1.f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f };                // free-free bar (chimes: 4:5:6 heard)
        static const float gong[]    = { 1.f, 1.52f, 2.f, 2.6f, 3.3f, 4.1f, 5.2f, 6.6f, 8.1f, 9.8f };// dense inharmonic
        static const float tibetan[] = { 1.f, 2.78f, 5.18f, 8.16f, 11.6f, 15.5f };                  // singing bowl (pairs added via halo)
        static const float timpani[] = { 1.f, 1.504f, 1.742f, 2.f, 2.245f, 2.494f, 2.8f, 2.98f };   // kettle 1:1.5:2:2.5:3
        static const float tom[]     = { 1.f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f };           // ideal membrane Bessel
        static const float snare[]   = { 1.f, 1.68f, 1.83f, 2.22f, 2.44f, 3.1f, 3.8f };             // coupled heads + rattle
        static const float djembe[]  = { 1.f, 1.6f, 2.3f, 2.9f, 3.6f, 4.4f, 5.5f };                 // hand drum
        static const float tabla[]   = { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f };                            // near-harmonic (tuned)

        auto pick = [&] (const float* t, int c) { out = t; n = c; };
        switch (fam)
        {
            case modal::BARS:
                if (set == 1) pick (vibes, 6);
                else if (set == 2) pick (xylo, 6);
                else if (set == 3) pick (glass, 6);
                else if (set == 4) pick (kalimba, 6);
                else pick (marimba, 6);
                break;
            case modal::BELLS:
                if (set == 1) pick (tubular, 6);
                else if (set == 2) pick (gong, 10);
                else if (set == 3) pick (tibetan, 6);
                else pick (church, 10);
                break;
            default: // SKIN
                if (set == 1) pick (tom, 7);
                else if (set == 2) pick (snare, 7);
                else if (set == 3) pick (djembe, 7);
                else if (set == 4) pick (tabla, 6);
                else pick (timpani, 8);
                break;
        }
        (void) freeBar;
    }

    // ═══════════════════════════ state ═══════════════════════════════════════
    ModalParams p_;
    double rate_ = 48000.0, playedHz_ = 130.8128, pitchMul_ = 1.0;
    float  fsInv_ = 1.f / 48000.f;
    int    core_ = 0;                 // 0 string · 1 reedbore · 2 modal
    bool   active_ = false, ringDown_ = false, displayMode_ = false;
    float  vel_ = 0.9f, gNorm_ = 1.f, outGain_ = 1.f, panBody_ = 0.f;
    std::uint32_t seed_ = 0x9E3779B9u, rng_ = 0x9E3779B9u;

    // waveguide / MSW
    DelayA dlA_, dlB_, combA_, combB_, boreN_, boreB_;
    OnePole lpA_, lpB_;
    OneZero ozA_, ozB_;
    DCBlock dcA_, dcB_, dcOut_;
    AllpassS disp_[modal::kDispStages];
    AllpassS dispB_[modal::kDispStages];   // separate dispersion state for polarization B (GRAND twin runs every sample)
    Biquad  body_[modal::kBodyModes], lipBq_, matEq_;   // matEq_ = GRAND MATERIAL presence tilt (warm↔brilliant)
    float  loopGain_ = 0.99f, combLen_ = 1.f, combDepth_ = 0.5f, combNorm_ = 1.f;
    bool   dispActive_ = false, bodyOn_ = false;
    float  dispPhaseDelay_ = 0.f;
    float  bowSlope_ = 3.f, reedSlope_ = -0.3f, lipY1_ = 0.f, bowPhase_ = 0.f;
    float  formPitchMul_ = 1.f;
    float  keyN_ = 39.f;                      // MIDI key index from f0 (0=A0 · 27=C3 · 39=C4 · 87=C8) — piano key-scaling
    float  feltK_ = 0.f, knockLvl_ = 0.f, feltNorm_ = 1.f;   // GRAND felt-hammer: hardening, knock level, energy norm

    // modal bank
    Mode   modes_[modal::kMaxModes];
    int    nModes_ = 0;

    // excitation
    const float* exData_ = nullptr; int exDataLen_ = 0; float exSrcStep_ = 1.f;
    bool   useSample_ = false;
    int    exPos_ = 0, exLen_ = 0, burstLen_ = 64, exDir_ = 1;
    bool   caught_ = false;   // loop-catch: has the forward lead-in reached the purple box yet?
    float  exRead_ = 0.f, loopLo_ = 0.f, loopHi_ = 0.f, noiseCut_ = 2000.f, noiseZ_ = 0.f, exciteGain_ = 1.f;
    float  contDrive_ = 0.f, driveZ_ = 0.f;
    int    rollLeft_ = 0, rollTimer_ = 0;

    // sculpt state
    float  bloomAmt_ = 0.f, bloomEnv_ = 0.f, bloomLpZ_ = 0.f, bloomGlide_ = 0.f, bloomLfo_ = 0.f;
    float  haloAmt_ = 0.f;
    float  agePosJit_ = 0.f, ageBuzz_ = 0.f, ageDetune_ = 1.f, rollChance_ = 0.f, agePitch_ = 0.f;

    // envelopes
    float  env_ = 0.f, envRel_ = 1.f, ampZ_ = 0.f;
    float  relGain_ = 1.f, relGainTarget_ = 1.f;   // GRAND note-off DAMPER: extra loop loss on key-up
    int    silentBlocks_ = 0;

    // budget / unison
    int*   budgetUsed_ = nullptr; int budgetCap_ = 0, reserved_ = 0, activeCost_ = 6, uniN_ = 1;
};

} // namespace tw
