#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  SubOsc.h — voice-anchored SUB oscillator (universal osc box, 2026-07-09)
//
//  ONE per voice per osc — NOT per unison sibling (anchor law: same as the
//  voice-0-anchored unison / grain-warp anchor). A single band-limited low
//  oscillator that tracks the osc's FINAL pitch (note + Oct + Semi + Cent +
//  Coarse + pitch env) shifted down 1–3 octaves. Mono / centered — sub-bass
//  belongs in the middle; unison spreads the MAIN osc above it.
//
//  Forms: Sine · Triangle · Square · Saw. The discontinuous pair (Square/Saw)
//  is polyBLEP band-limited; Triangle's 1/k² harmonics make naive clean.
//
//  HEAT = light saturation via the house ADAA-tanh pattern (FORGE, hm6):
//  ln·cosh antiderivative, first-order ADAA, state seeded with the FIRST
//  input sample (terrain-dsp-adaa-history-seed gotcha — never seed with 0).
// ═══════════════════════════════════════════════════════════════════════════
#include <cmath>

namespace tw {

struct SubOsc
{
    void noteOn() noexcept { phase_ = 0.0; seeded_ = false; x0_ = lc0_ = 0.f; }

    // one band-limited sample; phase advances by inc (cycles/sample)
    inline float tick (double inc, int form) noexcept
    {
        const double t = phase_;
        phase_ += inc; if (phase_ >= 1.0) phase_ -= 1.0;
        switch (form)
        {
            default:
            case 0: return (float) std::sin (6.283185307179586 * t);     // Sine
            case 1: return 1.f - 4.f * (float) std::fabs (t - 0.5);      // Triangle
            case 2: { float v = (t < 0.5) ? 1.f : -1.f;                  // Square
                      v += pblep (t, inc);
                      v -= pblep (frac (t + 0.5), inc);
                      return v; }
            case 3: { float v = (float) (2.0 * t - 1.0);                 // Saw
                      v -= pblep (t, inc);
                      return v; }
        }
    }

    // HEAT — light ADAA-tanh drive, peak-normalized, wet-faded so 0 = transparent
    inline float heat (float x, float h) noexcept
    {
        if (h < 0.004f) { seeded_ = false; return x; }
        const float g   = 1.f + 7.f * h;
        const float xin = g * x;
        const float lc1 = lncosh (xin);
        float y;
        if (! seeded_) { y = std::tanh (xin); seeded_ = true; }
        else
        {
            const float dx = xin - x0_;
            y = (dx > 1e-4f || dx < -1e-4f) ? (lc1 - lc0_) / dx
                                            : std::tanh (0.5f * (xin + x0_));
        }
        x0_ = xin; lc0_ = lc1;
        const float shaped = y / std::tanh (g);            // soft knee, peaks stay ~±1
        const float w = h < 0.08f ? h * 12.5f : 1.f;       // wet fades in — morph, no switch
        return x + w * (shaped - x);
    }

private:
    static inline float frac (double x) noexcept { return (float) (x - std::floor (x)); }
    static inline float pblep (double t, double dt) noexcept
    {
        if (dt <= 1e-9) return 0.f;
        if (t < dt)       { const double u = t / dt;         return (float) (u + u - u * u - 1.0); }
        if (t > 1.0 - dt) { const double u = (t - 1.0) / dt; return (float) (u * u + u + u + 1.0); }
        return 0.f;
    }
    static inline float lncosh (float x) noexcept
    {
        const float a = x < 0.f ? -x : x;
        return a + std::log1p (std::exp (-2.f * a)) - 0.6931472f;
    }

    double phase_ = 0.0;
    float  x0_ = 0.f, lc0_ = 0.f;
    bool   seeded_ = false;
};

} // namespace tw
