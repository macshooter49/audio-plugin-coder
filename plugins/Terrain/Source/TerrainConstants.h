#pragma once

namespace tw {

/** Maximum number of slice glow slots tracked per layer.
 *  Defines the size of LayerState::sliceGlowLevel and any processor-side
 *  glow buffers. Hard upper bound for the per-block update loop in
 *  PluginProcessor::processBlock; indices >= this value are silently dropped.
 */
constexpr int kMaxGlowSlots = 256;

/** tp101 — the most chops the Slices pill offers (4 / 8 / 16 / 24 / 32 / 64). */
constexpr int kMaxChops = 64;

/** tp101 — SamplerVoices PREALLOCATED per chop layer. LAYER sub-mode fires one voice per chop
 *  on a single key, so 64 chops needs 64 voices or the tail of the list is stolen before it
 *  sounds. Built once in the LayerState constructor (message thread) — the audio thread never
 *  grows the pool. An idle voice costs one early-return per block. */
constexpr int kSamplerVoicesPerLayer = kMaxChops;

} // namespace tw
