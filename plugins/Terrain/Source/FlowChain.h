#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// fb131 — FLOW MODE CHAIN resolver (pure, no JUCE — provable offline).
// Max: "I click chop first, then glitch — now my chop runs through the glitch."
// FLOW_CHAIN_1..16 hold the click order (values = FLOW_MODE indices, 0 = empty);
// this collapses them into the ordered active set. Rules:
//   · slots resolve in order; a duplicate (mode, instance) keeps its FIRST sighting
//     only (host automation can momentarily write the same mode twice — the fb122
//     cycleList dedupe lesson)
//   · an all-empty chain falls back to the legacy single FLOW_MODE, so every
//     old save and old automation lane keeps working untouched
//   · a non-empty chain OWNS the truth — FLOW_MODE is then only a mirror
// Audio stages (Chop=2, Glitch=3) process the buffer in chain order; note
// stages (Arp=1) transform the note stream in chain order; Robin (4) acts at
// the note event wherever it sits.
// tp20 — THE FLOW POOL. Every slot also names an INSTANCE (FLOW_CHAIN_INST_n,
// 0 = instance 1 = the old meaning). Arp / Chop / Glitch have kFlowInstances
// each; Robin is one (it is the voice allocator's brain) — its instance is
// forced to 0 so a saved "Robin 2" can never split the Wheel.
// ─────────────────────────────────────────────────────────────────────────────
namespace wc
{

static constexpr int kFlowChainSlots = 16;
static constexpr int kFlowKindInstances = 4;   // == wc::kFlowInstances (SynthModConfig.h); kept literal here — this header includes nothing

struct FlowChainState
{
    int  order[kFlowChainSlots] = {};   // resolved mode ids (1..4) in chain order
    int  inst [kFlowChainSlots] = {};   // 0-based instance per resolved slot (Robin: always 0)
    int  len      = 0;
    bool arp = false, chop = false, glitch = false, robin = false;          // any instance
    bool arpOn[kFlowKindInstances] = {}, chopOn[kFlowKindInstances] = {}, gliOn[kFlowKindInstances] = {};
};

inline FlowChainState resolveFlowChain (const int* slots, const int* insts, int numSlots, int legacyMode) noexcept
{
    FlowChainState s;
    for (int i = 0; i < numSlots && i < kFlowChainSlots; ++i)
    {
        const int m = slots[i];
        if (m < 1 || m > 4) continue;                       // 0 / out-of-range = empty slot
        int in = (insts != nullptr) ? insts[i] : 0;
        if (in < 0 || in >= kFlowKindInstances || m == 4) in = 0;
        bool dup = false;
        for (int j = 0; j < s.len; ++j) if (s.order[j] == m && s.inst[j] == in) { dup = true; break; }
        if (! dup) { s.order[s.len] = m; s.inst[s.len] = in; ++s.len; }
    }
    if (s.len == 0 && legacyMode >= 1 && legacyMode <= 4)   // legacy single-mode fallback
    { s.order[0] = legacyMode; s.inst[0] = 0; s.len = 1; }
    for (int j = 0; j < s.len; ++j)
        switch (s.order[j])
        {
            case 1: s.arp = true;    s.arpOn [s.inst[j]] = true; break;
            case 2: s.chop  = true;  s.chopOn[s.inst[j]] = true; break;
            case 3: s.glitch = true; s.gliOn [s.inst[j]] = true; break;
            case 4: s.robin = true;  break;
        }
    return s;
}

/** The fb131 shape: four slots, instance 1 everywhere. Kept for the offline tests and old callers. */
inline FlowChainState resolveFlowChain (const int slots[4], int legacyMode) noexcept
{ return resolveFlowChain (slots, nullptr, 4, legacyMode); }

} // namespace wc
