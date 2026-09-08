#pragma once

namespace tw {

/** Maximum number of slice glow slots tracked per layer.
 *  Defines the size of LayerState::sliceGlowLevel and any processor-side
 *  glow buffers. Hard upper bound for the per-block update loop in
 *  PluginProcessor::processBlock; indices >= this value are silently dropped.
 */
constexpr int kMaxGlowSlots = 256;

} // namespace tw
