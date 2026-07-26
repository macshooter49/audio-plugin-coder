#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// fb131 — FLOW MODE CHAIN resolver (pure, no JUCE — provable offline).
// Max: "I click chop first, then glitch — now my chop runs through the glitch."
// FLOW_CHAIN_1..4 hold the click order (values = FLOW_MODE indices, 0 = empty);
// this collapses them into the ordered active set. Rules:
//   · slots resolve in order; a duplicate mode keeps its FIRST sighting only
//     (host automation can momentarily write the same mode twice — the fb122
//     cycleList dedupe lesson)
//   · an all-empty chain falls back to the legacy single FLOW_MODE, so every
//     old save and old automation lane keeps working untouched
//   · a non-empty chain OWNS the truth — FLOW_MODE is then only a mirror
// Audio stages (Chop=2, Glitch=3) process the buffer in chain order; note
// stages (Arp=1, Robin=4) act at the note event wherever they sit. This is the
// patcher's opening act: one ordered list of modes, later one graph of nodes.
// ─────────────────────────────────────────────────────────────────────────────
namespace wc
{

struct FlowChainState
{
    int  order[4] = { 0, 0, 0, 0 };   // resolved mode ids (1..4) in chain order
    int  len      = 0;
    bool arp = false, chop = false, glitch = false, robin = false;
};

inline FlowChainState resolveFlowChain (const int slots[4], int legacyMode) noexcept
{
    FlowChainState s;
    for (int i = 0; i < 4; ++i)
    {
        const int m = slots[i];
        if (m < 1 || m > 4) continue;                       // 0 / out-of-range = empty slot
        bool dup = false;
        for (int j = 0; j < s.len; ++j) if (s.order[j] == m) { dup = true; break; }
        if (! dup) s.order[s.len++] = m;
    }
    if (s.len == 0 && legacyMode >= 1 && legacyMode <= 4)   // legacy single-mode fallback
        s.order[s.len++] = legacyMode;
    for (int j = 0; j < s.len; ++j)
        switch (s.order[j])
        {
            case 1: s.arp = true;    break;   case 2: s.chop  = true; break;
            case 3: s.glitch = true; break;   case 4: s.robin = true; break;
        }
    return s;
}

} // namespace wc
