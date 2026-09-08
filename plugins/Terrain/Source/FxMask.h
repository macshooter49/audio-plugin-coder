// FxMask.h
// Pure-function helpers for packing the 5 bool FX chips + 4-state TAPE
// machine selector into a uint8_t mask used by the per-chop FX-
// independence routing (see spec 2026-05-30-terrain-per-chop-fx-
// independence-routing-design.md, option 1: shared chain).
//
// Bit layout:
//   bit 0    : fxGrain
//   bits 1-2 : fxTapeMachine (0=off, 1=Studio, 2=Cassette, 3=Wire)
//   bit 3    : fxSpace
//   bit 4    : fxDelay
//   bit 5    : fxEq
//   bit 6    : fxJune
//   bit 7    : reserved
//
// Two functions, that's it. No Hamming distance / resolver helpers —
// option 1 has one shared chain, so no per-mask routing is needed.
#pragma once

#include <cstdint>

namespace tw::fxmask
{
    inline std::uint8_t packMask (bool fxGrain,
                                  int  fxTapeMachine,
                                  bool fxSpace,
                                  bool fxDelay,
                                  bool fxEq,
                                  bool fxJune) noexcept
    {
        const std::uint8_t tape = (std::uint8_t) (fxTapeMachine & 0b11);
        return (std::uint8_t) ((fxGrain ? 1u : 0u) << 0)
             | (std::uint8_t) (tape << 1)
             | (std::uint8_t) ((fxSpace ? 1u : 0u) << 3)
             | (std::uint8_t) ((fxDelay ? 1u : 0u) << 4)
             | (std::uint8_t) ((fxEq    ? 1u : 0u) << 5)
             | (std::uint8_t) ((fxJune  ? 1u : 0u) << 6);
    }

    /** True if no FX bits are set (bits 0..6 all zero). Bit 7 is reserved
     *  and ignored. */
    inline bool isEmpty (std::uint8_t mask) noexcept
    {
        return (mask & 0b01111111u) == 0u;
    }
}
