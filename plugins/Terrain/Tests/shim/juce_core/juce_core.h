#pragma once
// harness shim — Wavetable.h / WavetableBank.h touch exactly THREE juce symbols: jlimit, jmax,
// jmin (verified: grep -oh "juce::[A-Za-z_:]*" Wavetable.h WavetableBank.h). Those are pure
// clamps with exact semantics, so this shim is as REAL as the backend, not kinder than it
// (fb393). Anything beyond them must fail to compile rather than be faked.
#include "../juce_audio_basics/juce_audio_basics.h"
