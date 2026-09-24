#pragma once
// harness shim — SamplerVoice.h includes juce_audio_processors only for juce::SynthesiserVoice,
// which actually lives in juce_audio_basics. Lets Source/ChopStretch_test.cpp drive the SHIPPED
// SamplerVoice with the real juce_core + juce_audio_basics and nothing else (no GUI modules).
#include <juce_audio_basics/juce_audio_basics.h>
