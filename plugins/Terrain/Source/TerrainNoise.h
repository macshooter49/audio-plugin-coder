#pragma once
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  TerrainNoise.h — tp82. THE INSTRUMENT'S NOISE GENERATOR, LIFTED OUT OF SynthVoice.
//
//  Max wanted a Noise lane on the Shaper. Every other effect the Shaper borrows is a finished,
//  self-contained rack engine, so lending it was a few lines. This one was not: the generator lived
//  INSIDE SynthVoice's render, so the only honest way to give the Shaper one was to lift it into its
//  own file that BOTH use — one implementation, not a second noise engine bolted on beside the real one.
//
//  ⚠️ THE BODY BELOW IS THE SHIPPED CODE, MOVED AND NOT REWRITTEN. The only edit is juce::jlimit ->
//  tnClamp, so this header needs no JUCE and an offline proof can build it. TerrainNoise_test.cpp
//  holds a FROZEN copy of the generator as it was before the move and asserts this one matches it
//  sample for sample, bit for bit, across all thirteen colours — that is the null that lets the
//  instrument's own sound be trusted afterwards.
//
//  Thirteen colours, index-aligned with SYN_NOISE_TYPE:
//    0 White · 1 Pink · 2 Brown · 3 Geiger · 4 Tape Hiss · 5 Tape Hum · 6 Tape Air · 7 Tape Crackle
//    8 Clean Vinyl · 9 Dirty Vinyl · 10 Space Open · 11 Space Helium · 12 Space Wind
//
//  The state does NOT reset per note: the noise free-runs, which is what it always did. reset() is
//  for a fresh owner (the Shaper's own instance), not for a note-on.
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cmath>
#include <cstdint>

namespace tw
{
static inline float tnClamp (float lo, float hi, float v) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

struct TerrainNoise
{
    static constexpr int kTypes = 13;

    void prepare (double sampleRate) noexcept { noiseSR_ = (float) sampleRate; }
    void setType (int t) noexcept { noiseType_ = t < 0 ? 0 : (t >= kTypes ? kTypes - 1 : t); }
    int  type() const noexcept { return noiseType_; }
    /** A fresh owner's silence. NOT called on a note — the generator free-runs, as it always has. */
    void reset() noexcept
    {
        noiseRngL_ = 0x9E3779B9u; noiseRngR_ = 0x85EBCA6Bu;
        for (int i = 0; i < 7; ++i) { pkL_[i] = 0; pkR_[i] = 0; }
        brL_ = brR_ = geValL_ = geValR_ = 0.0f;
        tpL_ = tpR_ = spL_ = spL2_ = spR_ = spR2_ = 0.0f;
        tpL2_ = tpR2_ = humPh_ = windPh_ = windPh2_ = gustL_ = 0.0f;
        rumbL_[0] = rumbL_[1] = rumbR_[0] = rumbR_[1] = 0.0f;
    }
    inline void tick (float& oL, float& oR) noexcept { noiseTick (oL, oR); }

        static inline float noiseWhite (std::uint32_t& s) noexcept
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) ((std::int32_t) s) * (1.0f / 2147483648.0f);
        }
        // Cheap phase→sine, phase in [0,1). ~1% THD — plenty for LFOs / hum / SVF sweep. No std::sin per sample.
        static inline float noiseSine (float ph) noexcept
        {
            const float x = 2.0f * ph - 1.0f;                    // [-1,1)
            const float q = 4.0f * x * (1.0f - std::fabs (x));   // parabola
            return q * (0.775f + 0.225f * std::fabs (q));        // devmaster refine
        }
        inline void noiseTick (float& oL, float& oR) noexcept
        {
            const float wl = noiseWhite (noiseRngL_), wr = noiseWhite (noiseRngR_);
            switch (noiseType_)
            {
                case 1: {   // Pink — Paul Kellet economy filter (≈ -3 dB/oct)
                    auto pk = [] (float w, float* b) noexcept {
                        b[0] = 0.99886f*b[0] + w*0.0555179f; b[1] = 0.99332f*b[1] + w*0.0750759f;
                        b[2] = 0.96900f*b[2] + w*0.1538520f; b[3] = 0.86650f*b[3] + w*0.3104856f;
                        b[4] = 0.55000f*b[4] + w*0.5329522f; b[5] = -0.7616f*b[5] - w*0.0168980f;
                        const float o = b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+w*0.5362f;
                        b[6] = w*0.115926f; return o * 0.11f;
                    };
                    oL = pk (wl, pkL_); oR = pk (wr, pkR_); break;
                }
                case 2:     // Brown — leaky integrator (≈ -6 dB/oct)
                    brL_ = (brL_ + 0.02f*wl) * 0.996f; brR_ = (brR_ + 0.02f*wr) * 0.996f;
                    oL = brL_ * 3.5f; oR = brR_ * 3.5f; break;
                case 3: {   // Geiger — dry Poisson clicks, random amplitude, crisp fast decay, NO bed
                    auto click = [] (float w, float w2, float& env) noexcept {
                        if (w > 0.9993f || w < -0.9993f)
                            env = (0.6f + 0.4f*std::fabs (w2)) * (w > 0.0f ? 1.0f : -1.0f);
                        const float o = env; env *= 0.80f; return o;
                    };
                    oL = click (wl, wr, geValL_); oR = click (wr, wl, geValR_); break;
                }
                case 4: {   // Tape Hiss — band-limited upper-mid noise (~1.5–8 kHz), gentle top roll-off
                    tpL_  += 0.21f*(wl - tpL_);   tpR_  += 0.21f*(wr - tpR_);     // HP ~1.5 kHz (remove lows)
                    const float hpL = wl - tpL_,  hpR = wr - tpR_;
                    tpL2_ += 0.55f*(hpL - tpL2_); tpR2_ += 0.55f*(hpR - tpR2_);   // LP ~6 kHz (tame top)
                    oL = tpL2_ * 2.0f; oR = tpR2_ * 2.0f; break;
                }
                case 5: {   // Tape Hum — 60 Hz + 120 + 180 harmonics (low buzz) over faint hiss
                    humPh_ += 60.0f / noiseSR_; if (humPh_ >= 1.0f) humPh_ -= 1.0f;
                    float h2 = humPh_*2.0f; if (h2 >= 1.0f) h2 -= 1.0f;
                    float h3 = humPh_*3.0f; while (h3 >= 1.0f) h3 -= 1.0f;
                    const float hum = noiseSine (humPh_)*0.70f + noiseSine (h2)*0.22f + noiseSine (h3)*0.10f;
                    tpL_ += 0.25f*(wl - tpL_); tpR_ += 0.25f*(wr - tpR_);         // faint hiss bed
                    oL = hum*0.82f + (wl - tpL_)*0.12f;
                    oR = hum*0.82f + (wr - tpR_)*0.12f; break;
                }
                case 6: {   // Tape Air — breathy bright high-shelf, slow "breathing" amplitude
                    tpL_ += 0.38f*(wl - tpL_); tpR_ += 0.38f*(wr - tpR_);         // HP ~2.7 kHz (airy top)
                    const float airL = wl - tpL_, airR = wr - tpR_;
                    windPh_ += 0.25f / noiseSR_; if (windPh_ >= 1.0f) windPh_ -= 1.0f;  // ~0.25 Hz breath
                    const float breath = 0.72f + 0.28f * noiseSine (windPh_);
                    oL = airL * 1.3f * breath; oR = airR * 1.3f * breath; break;
                }
                case 7: {   // Tape Crackle — sparse ASYMMETRIC pops over faint hiss
                    tpL_ += 0.22f*(wl - tpL_); tpR_ += 0.22f*(wr - tpR_);         // faint hiss bed
                    const float hissL = wl - tpL_, hissR = wr - tpR_;
                    auto pop = [] (float w, float w2, float& env) noexcept {
                        if      (w >  0.9995f)  env =  (0.7f + 0.3f*std::fabs (w2)); // positive-biased pop
                        else if (w < -0.99985f) env = -(0.5f + 0.3f*std::fabs (w2)); // rare negative
                        const float o = env; env *= 0.86f; return o;
                    };
                    oL = pop (wl, wr, geValL_) + hissL*0.28f;
                    oR = pop (wr, wl, geValR_) + hissR*0.28f; break;
                }
                case 8: case 9: {   // Vinyl — LF rumble + pink surface + Poisson crackle (Dirty=denser/louder)
                    const bool dirty = (noiseType_ == 9);
                    rumbL_[0] += 0.006f*(wl - rumbL_[0]); rumbL_[1] += 0.006f*(rumbL_[0] - rumbL_[1]);   // ~40 Hz turntable rumble
                    rumbR_[0] += 0.006f*(wr - rumbR_[0]); rumbR_[1] += 0.006f*(rumbR_[0] - rumbR_[1]);
                    const float rmb = dirty ? 24.0f : 18.0f;
                    auto pk = [] (float w, float* b) noexcept {                   // pink surface (reuse Kellet state)
                        b[0]=0.99886f*b[0]+w*0.0555179f; b[1]=0.99332f*b[1]+w*0.0750759f;
                        b[2]=0.96900f*b[2]+w*0.1538520f; b[3]=0.86650f*b[3]+w*0.3104856f;
                        b[4]=0.55000f*b[4]+w*0.5329522f; b[5]=-0.7616f*b[5]-w*0.0168980f;
                        const float o=b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+w*0.5362f; b[6]=w*0.115926f; return o*0.11f;
                    };
                    const float surfL = pk (wl, pkL_) * (dirty ? 0.50f : 0.22f);
                    const float surfR = pk (wr, pkR_) * (dirty ? 0.50f : 0.22f);
                    const float thr = dirty ? 0.9975f : 0.9993f;                  // Poisson crackle
                    auto crk = [thr] (float w, float w2, float& env) noexcept {
                        if (w > thr || w < -thr) env = (0.55f + 0.45f*std::fabs (w2)) * (w > 0.0f ? 1.0f : -1.0f);
                        const float o = env; env *= 0.845f; return o;
                    };
                    const float ckL = crk (wl, wr, geValL_) * (dirty ? 0.9f : 0.7f);
                    const float ckR = crk (wr, wl, geValR_) * (dirty ? 0.9f : 0.7f);
                    oL = rumbL_[1]*rmb + surfL + ckL;
                    oR = rumbR_[1]*rmb + surfR + ckR; break;
                }
                case 10: case 11: case 12: {   // Space — Chamberlin SVF resonant band-pass (tonal wash, not flat noise)
                    const float w0 = 6.2831853f / noiseSR_;
                    float fc, qd, amp;
                    if (noiseType_ == 10) {          // Space Open — broad airy wash, slow drift
                        windPh2_ += 0.07f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                        fc = 1100.0f + 500.0f * noiseSine (windPh2_);
                        qd = 0.90f; amp = 2.6f;
                    } else if (noiseType_ == 11) {   // Space Helium — high, thin, resonant formant
                        windPh2_ += 0.05f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                        fc = 3200.0f + 400.0f * noiseSine (windPh2_);
                        qd = 0.28f; amp = 1.8f;
                    } else {                          // Space Wind — gusting swept band-pass
                        windPh_  += 0.13f / noiseSR_; if (windPh_  >= 1.0f) windPh_  -= 1.0f;
                        windPh2_ += 0.09f / noiseSR_; if (windPh2_ >= 1.0f) windPh2_ -= 1.0f;
                        fc = 700.0f + 450.0f * noiseSine (windPh2_);
                        qd = 0.60f;
                        gustL_ += 0.00035f*(wl - gustL_);
                        amp = 2.6f * tnClamp (0.0f, 1.4f,
                                      0.45f + 0.55f*noiseSine (windPh_) + 2.5f*gustL_);
                    }
                    float f = tnClamp (0.0f, 0.9f, fc * w0);   // f = 2·sin(π·fc/fs) ≈ 2π·fc/fs
                    spL_ += f * spL2_; const float hpL = wl - spL_ - qd*spL2_; spL2_ += f * hpL;
                    spR_ += f * spR2_; const float hpR = wr - spR_ - qd*spR2_; spR2_ += f * hpR;
                    oL = spL2_ * amp; oR = spR2_ * amp; break;
                }
                default:    // White (0)
                    oL = wl; oR = wr; break;
            }
            // (SCAN/speed is applied at the call site as a sample-and-hold + interpolation on this raw output.)
        }

    int   noiseType_ = 0;
    std::uint32_t noiseRngL_ = 0x9E3779B9u, noiseRngR_ = 0x85EBCA6Bu;
    float pkL_[7] = { 0 }, pkR_[7] = { 0 }, brL_ = 0.0f, brR_ = 0.0f, geValL_ = 0.0f, geValR_ = 0.0f;
    float tpL_ = 0.0f, tpR_ = 0.0f, spL_ = 0.0f, spL2_ = 0.0f, spR_ = 0.0f, spR2_ = 0.0f;
    float tpL2_ = 0.0f, tpR2_ = 0.0f, humPh_ = 0.0f, windPh_ = 0.0f, windPh2_ = 0.0f, gustL_ = 0.0f;
    float rumbL_[2] = { 0.0f, 0.0f }, rumbR_[2] = { 0.0f, 0.0f };
    float noiseSR_ = 48000.0f;
};
}   // namespace tw
