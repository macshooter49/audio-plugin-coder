#pragma once
// ══ fb636 — TEST-ONLY DETERMINISM ══════════════════════════════════════════════════════════════════
//  Two fresh instances of Terrain never render the same bits: every per-note random draw is seeded from
//  the clock (juce::Random's default constructor) or from an object's heap ADDRESS, and both change from
//  run to run. That is right for the instrument and useless for proving a CPU change bit-identical.
//  With TERRAIN_DETERMINISTIC set in the environment when the plugin loads, those seeds come from stable
//  values instead, so Tests/cpu_pass_bench.cpp can render the bank twice and compare every sample.
//  Unset (every real session) nothing changes: seedAddr() returns the address itself, and every
//  `if (tw::deterministic())` branch is skipped.
#include <cstdint>
#include <cstdlib>
namespace tw
{
    inline bool deterministic() noexcept { static const bool on = std::getenv ("TERRAIN_DETERMINISTIC") != nullptr; return on; }
    inline std::uintptr_t seedAddr (const void* p, std::uintptr_t stable) noexcept
    { return deterministic() ? stable : reinterpret_cast<std::uintptr_t> (p); }
}
