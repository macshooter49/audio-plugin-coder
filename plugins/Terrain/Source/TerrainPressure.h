#pragma once
// ══ tp109 — PRESSURE SENSITIVITY (Settings → MIDI & Controllers → Expression: Pressure curve · start · ceiling) ═══
//  Max, on a PolyBrute in Ableton: "when I press down it goes all the way up to 100 %… when I actually press down
//  HARD it should hit 100 %, not just pressing down." An attenuation (a smaller route amount) only lowers the top;
//  this reshapes the TRAVEL, for any MPE or poly-aftertouch controller:
//    in    = the raw pressure, 0..127 / 127 (MPE per-note channel pressure, poly key pressure, channel pressure)
//    u     = (in − start) / (ceiling − start), clamped 0..1      start: resting fingers do nothing below it
//                                                                ceiling: 100 % is reached before the physical end
//    out   = u ^ 2^(2.6·curve)                                   the velocity curve's (and the mod matrix's) CRV law:
//                                                                curve > 0 = HARD (press harder to reach the top)
//  Applied where the processor turns MIDI into the pressure SOURCES (before the 10 ms smoothing), so every route —
//  per-note, per-key, global, the rack's view — gets the same shaped value.
//  ONE value per process, like A4 (Settings says "every instance"): the processor's setPressureShape writes it and
//  MidiSettings.json. The defaults (curve 0, start 0, ceiling 1) take the identity branch: bit-identical output.
//  JUCE-free on purpose (a unit test can include it).
#include <atomic>
#include <cmath>

namespace wc
{
    struct PressureShape
    {
        float curve = 0.0f, start = 0.0f, ceiling = 1.0f;
        bool neutral() const noexcept { return curve == 0.0f && start <= 0.0f && ceiling >= 1.0f; }
        // the audio thread's per-value map. Neutral returns its input untouched (the pre-tp109 value, bit for bit).
        float apply (float in) const noexcept
        {
            if (neutral()) return in;
            float u = in;
            if (start > 0.0f || ceiling < 1.0f)
                u = (in - start) / (ceiling - start);
            u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            if (curve != 0.0f && u > 0.0f && u < 1.0f) u = std::pow (u, std::exp2 (2.6f * curve));
            return u;
        }
        // the ranges the page and the file are held to: curve −1..1, start 0..50 %, ceiling 50..100 %, ≥ 10 % apart
        static PressureShape clamped (float c, float s, float e) noexcept
        {
            auto cl = [] (float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); };
            PressureShape p; p.curve = cl (c, -1.0f, 1.0f); p.start = cl (s, 0.0f, 0.5f); p.ceiling = cl (e, 0.5f, 1.0f);
            if (p.ceiling - p.start < 0.1f) p.ceiling = p.start + 0.1f;   // start ≤ 0.5 → ceiling ≤ 0.6, never past 1
            return p;
        }
    };

    struct PressureShapeStore
    {
        std::atomic<float> curve { 0.0f }, start { 0.0f }, ceiling { 1.0f };
        PressureShape load() const noexcept
        {
            PressureShape p; p.curve = curve.load (std::memory_order_relaxed); p.start = start.load (std::memory_order_relaxed);
            p.ceiling = ceiling.load (std::memory_order_relaxed); return p;
        }
        void store (const PressureShape& p) noexcept
        {
            curve.store (p.curve, std::memory_order_relaxed); start.store (p.start, std::memory_order_relaxed);
            ceiling.store (p.ceiling, std::memory_order_relaxed);
        }
    };
    inline PressureShapeStore& pressureShape() noexcept { static PressureShapeStore s; return s; }
}
