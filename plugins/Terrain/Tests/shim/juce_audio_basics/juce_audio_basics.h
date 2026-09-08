#pragma once
// harness shim — TerrainFilters.h uses exactly four juce symbols: jlimit, jmin, jmax,
// MathConstants. Nothing else from JUCE is touched, which is why the 94-engine filter core
// offline-validates standalone. (Verified with: grep -o "juce::[A-Za-z_:]*" TerrainFilters.h)
#include <algorithm>
#include <cmath>
namespace juce {
template <typename T> constexpr T jlimit (T lo, T hi, T v) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> constexpr T jmin (T a, T b) noexcept { return a < b ? a : b; }
template <typename T> constexpr T jmax (T a, T b) noexcept { return a > b ? a : b; }
template <typename T> constexpr T jmin (T a, T b, T c) noexcept { return jmin (jmin (a, b), c); }
template <typename T> constexpr T jmax (T a, T b, T c) noexcept { return jmax (jmax (a, b), c); }
template <typename T> struct MathConstants {
    static constexpr T pi      = static_cast<T> (3.141592653589793238L);
    static constexpr T twoPi   = static_cast<T> (6.283185307179586476L);
    static constexpr T halfPi  = static_cast<T> (1.570796326794896619L);
    static constexpr T euler   = static_cast<T> (2.718281828459045235L);
    static constexpr T sqrt2   = static_cast<T> (1.414213562373095048L);
};
}
