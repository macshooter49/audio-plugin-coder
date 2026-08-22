#pragma once
// ════════════════════════════════════════════════════════════════════════════════════════════
//  fb453 — THE FX RACK'S PER-BLOCK MODULATION VALUE MATH.
//
//  Deliberately JUCE-FREE and in its own header so `Tests/fxmod_cert.cpp` can call the SHIPPED
//  function instead of a copy of it. fb393: a harness kinder than reality is worse than no
//  harness — and a second, hand-typed implementation of the ownership crossfade in the cert
//  would be exactly that. `PluginProcessor.cpp` supplies the plugin's pointers/LFOs/envelopes
//  as callbacks; the cert supplies fakes. The DECISIONS — which routes count, how they combine,
//  where the clamp lands — are compiled once and run by both.
//
//  THE LAW (fb184, and it is the FLOW knobs' law verbatim):
//    • an LFO route ADDS to the knob's base value;
//    • an envelope route OWNS the knob — it crossfades the base away in proportion to its depth
//      and drives from ZERO up to the depth-scaled top (fb179, "KNOB-IS-THE-PEAK"). At rest an
//      owned knob reads 0, NOT its base. That is intended; do not "fix" it.
//    • both are summed first and clamped ONCE, at the end.
//
//  Every FX-rack float parameter is declared NormalisableRange<float>(0, 1), so the [0,1] clamp
//  here is the parameter's own range — not an extra restriction.
// ════════════════════════════════════════════════════════════════════════════════════════════

#include "SynthModConfig.h"

namespace wc
{

// The per-block sparse map: one entry per DISTINCT parameter that carries at least one route.
// KEYED BY POINTER, not by (kind, instance, knob) — two destinations that resolve to the SAME
// parameter therefore land in the SAME slot and ACCUMULATE. That is not a special case bolted
// on; it is why the key is a pointer. The Delay's front "Time" dial and its back "Time L" knob
// ARE one parameter (SYN_DLY_TIME, fb306-310's L/R link), so routes on either one sum, exactly
// as two routes on one destination sum. `ptr` is the brief's fxModPtr_, `val` its fxModVal_.
struct FxModAccum
{
    const void* ptr [MAX_ASSIGNMENTS] {};
    float       val [MAX_ASSIGNMENTS] {};   // the additive (LFO) sum, then the final value
    float       ownW[MAX_ASSIGNMENTS] {};   // fb184 ownership weight = sum |depth| over ENV routes
    float       ownV[MAX_ASSIGNMENTS] {};   // fb184 ownership value  = sum |depth| * env level
    int         count = 0;

    void reset() noexcept { count = 0; }

    // Find the slot for this parameter, or open one seeded with the parameter's own value.
    // Returns -1 only if more than MAX_ASSIGNMENTS distinct parameters are routed, which the
    // assignment list's own size makes impossible.
    int slotFor (const void* p, float base) noexcept
    {
        for (int s = 0; s < count; ++s) if (ptr[s] == p) return s;
        if (count >= MAX_ASSIGNMENTS) return -1;
        const int s = count++;
        ptr[s] = p; val[s] = base; ownW[s] = 0.0f; ownV[s] = 0.0f;
        return s;
    }

    void addLfo (int s, const DestInfo& info, float lfoValue, float depth) noexcept
    { val[s] += routeContribution (info, lfoValue, depth); }

    // 🚨 monoEnvLevel is what monoEnvLevelOf() RETURNS, i.e. the envelope's level MINUS ONE
    //    (fb179). `(monoEnvLevel + 1.0f)` is therefore already the 0..1 level — a rack knob's
    //    exact travel — and there is NO further scaling. An earlier draft had a `* 0.5f` here
    //    and it would have quietly halved every envelope in the rack. This expression is
    //    byte-for-byte flowMod()'s own term (PluginProcessor.cpp, `flowKnob`'s helper).
    void addEnv (int s, float monoEnvLevel, float depth) noexcept
    { const float dw = std::abs (depth);
      ownW[s] += dw;
      ownV[s] += dw * (monoEnvLevel + 1.0f); }

    // The ownership crossfade, then clamp ONCE — flowKnob()'s `(base + m) * (1 - w) + oV`.
    void finish() noexcept
    { for (int s = 0; s < count; ++s)
      { const float w = std::min (1.0f, ownW[s]);
        val[s] = clampRange (val[s] * (1.0f - w) + ownV[s], 0.0f, 1.0f); } }

    // The read-site lookup. Pointer identity, so a read site cannot mis-map: there is no knob
    // index at the call site to get wrong.
    float lookup (const void* p, float fallback) const noexcept
    { for (int s = 0; s < count; ++s) if (ptr[s] == p) return val[s];
      return fallback; }
};

// One block's worth of rack modulation, built by walking the <= MAX_ASSIGNMENTS routes ONCE.
// The 1,152 destinations are never iterated: a rack with no routes costs one loop over zero.
//   refOf  (kind, inst, knob) -> const void*  — the parameter behind that dial, or nullptr if
//                                               the device has no such dial (the Filter's 8)
//   baseOf (const void*)      -> float        — that parameter's un-modulated value
//   lfoOf  (int lfoIndex)     -> float        — the global LFO's current output (bipolar)
//   envOf  (int modSource)    -> float        — monoEnvLevelOf(): the level MINUS ONE
template <typename RefOf, typename BaseOf, typename LfoOf, typename EnvOf>
inline void buildFxMod (FxModAccum& acc, const ModConfig& cfg,
                        RefOf refOf, BaseOf baseOf, LfoOf lfoOf, EnvOf envOf)
{
    acc.reset();
    for (int a = 0; a < cfg.numAssignments; ++a)
    {
        const Assignment& as = cfg.assignments[a];
        if (! as.enabled || ! isFxModDest ((int) as.dest)) continue;
        const FxModAddr ad = fxModDecode ((int) as.dest);
        const void* ref = refOf (ad.kind, ad.inst, ad.knob);
        if (ref == nullptr) continue;                   // a hole — that device has no such dial
        const int slot = acc.slotFor (ref, baseOf (ref));
        if (slot < 0) continue;
        if (isEnvModSource ((int) as.source))           // ENV OWNS
            acc.addEnv (slot, envOf ((int) as.source), as.depth);
        else                                            // LFO ADDS
        {
            const int si = (int) as.source - (int) ModSource::L1;
            if (si < 0 || si >= NUM_LFOS) continue;      // only LFO sources have a global value
            acc.addLfo (slot, kDestInfo[(size_t) as.dest], lfoOf (si), as.depth);
        }
    }
    acc.finish();
}

} // namespace wc
