#pragma once

#include "TapeMachines.h"

//==============================================================================
// TapeProcessor: Delegating wrapper that owns all three tape machines
// and routes audio to the active one with 75ms crossfade on switch.
//
// Machines:
//   0 = StudioMachine   (warm reel-to-reel)
//   1 = CassetteMachine (lo-fi degraded cassette)
//   2 = WireMachine     (experimental broken wire recorder)
//
// When setMachine() changes the active machine, both old and new machines
// process audio simultaneously during a 75ms linear crossfade. Inactive
// machines consume zero CPU outside of crossfade transitions.
//==============================================================================
class TapeProcessor
{
public:
    TapeProcessor()
    {
        machines[0] = &studioMachine;
        machines[1] = &cassetteMachine;
        machines[2] = &wireMachine;
    }

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;

        // All three machines need valid sample rates even when inactive,
        // in case a crossfade starts at any moment
        studioMachine.prepare(sampleRate);
        cassetteMachine.prepare(sampleRate);
        wireMachine.prepare(sampleRate);

        // 75ms crossfade duration
        crossfadeSamples = static_cast<int>(sr * 0.075);
        crossfadeCounter = 0;
        crossfading = false;

        // One-pole smoothing for amount parameters (~5ms time constant)
        // Prevents clicks when LFO modulation crosses zero boundary
        double tau = 0.005; // 5ms
        amountSmoothCoeff = 1.0 - std::exp(-1.0 / (sampleRate * tau));
        smoothedWow = 0.0;
        smoothedSat = 0.0;
        smoothedHiss = 0.0;
    }

    void reset()
    {
        studioMachine.reset();
        cassetteMachine.reset();
        wireMachine.reset();
        smoothedWow = 0.0;
        smoothedSat = 0.0;
        smoothedHiss = 0.0;

        crossfadeCounter = 0;
        crossfading = false;
    }

    //==========================================================================
    // setMachine: called from processBlock when TAPE_MACHINE param changes.
    // If the index differs from activeMachine, initiates a 75ms crossfade.
    void setMachine(int index)
    {
        // Clamp to valid range
        index = std::max(0, std::min(index, 2));

        if (index == activeMachine && !crossfading)
            return;

        // If already crossfading to the same target, ignore
        if (crossfading && index == targetMachine)
            return;

        // If already crossfading to a different target, snap the current
        // crossfade and start a new one from the current target
        if (crossfading)
        {
            activeMachine = targetMachine;
        }

        targetMachine = index;
        crossfading = true;
        crossfadeCounter = crossfadeSamples;
    }

    int getActiveMachine() const
    {
        return crossfading ? targetMachine : activeMachine;
    }

    // Wire-only mode toggles (space noise hiss, tube saturator)
    void setWireModes(bool spaceNoise, bool tubeSat)
    {
        wireMachine.setSpaceNoiseEnabled(spaceNoise);
        wireMachine.setTubeSatEnabled(tubeSat);
    }

    //==========================================================================
    // Process one sample with tape character
    // wowFlutter: 0-1, saturation: 0-1, hiss: 0-1
    float processSample(float input, float wowFlutter, float saturation, float hiss)
    {
        // Smooth amount params ONCE per sample to prevent clicks when LFO modulation crosses zero
        smoothedSat  += amountSmoothCoeff * (static_cast<double>(saturation) - smoothedSat);
        smoothedWow  += amountSmoothCoeff * (static_cast<double>(wowFlutter) - smoothedWow);
        smoothedHiss += amountSmoothCoeff * (static_cast<double>(hiss) - smoothedHiss);
        const float sSat  = static_cast<float>(smoothedSat);
        const float sWow  = static_cast<float>(smoothedWow);
        const float sHiss = static_cast<float>(smoothedHiss);

        if (!crossfading)
        {
            // Single machine path — zero CPU for inactive machines
            return processOneMachine(machines[activeMachine], input, sSat, sWow, sHiss);
        }

        // Crossfading: process through BOTH old and new machines
        float outputOld = processOneMachine(machines[activeMachine], input, sSat, sWow, sHiss);
        float outputNew = processOneMachine(machines[targetMachine], input, sSat, sWow, sHiss);

        // Linear blend: 1.0 at start (all old) -> 0.0 at end (all new)
        const float blend = static_cast<float>(crossfadeCounter) / static_cast<float>(crossfadeSamples);
        float output = outputOld * blend + outputNew * (1.0f - blend);

        crossfadeCounter--;
        if (crossfadeCounter <= 0)
        {
            activeMachine = targetMachine;
            crossfading = false;
        }

        return output;
    }

private:
    //==========================================================================
    // Process a single sample through one machine's full signal chain:
    //   saturation -> wow -> hiss (additive) + gain modulation
    float processOneMachine(TapeMachineBase* machine, float input, float saturation, float wowFlutter, float hiss)
    {
        const double dInput = static_cast<double>(input);
        const double dSat = static_cast<double>(saturation);
        const double dWow = static_cast<double>(wowFlutter);
        const double dHiss = static_cast<double>(hiss);

        // 1. Saturation
        double processed = machine->processSaturation(dInput, dSat);

        // 2. Wow/Flutter
        processed = machine->processWow(processed, dWow);

        // 3. Hiss — returns additive noise; audioGainMultiplier set by reference
        //    (Wire machine uses this for micro-dropouts, others always set 1.0)
        float audioGainMultiplier = 1.0f;
        double hissNoise = machine->processHiss(dHiss, audioGainMultiplier);

        // Combine: signal * gain multiplier + hiss noise
        double output = processed * static_cast<double>(audioGainMultiplier) + hissNoise;

        return static_cast<float>(output);
    }

    //==========================================================================
    // Machine instances
    StudioMachine   studioMachine;
    CassetteMachine cassetteMachine;
    WireMachine     wireMachine;

    // Pointer array for indexed access
    TapeMachineBase* machines[3];

    // Active machine tracking
    int activeMachine = 0;   // Currently active (0=Studio, 1=Cassette, 2=Wire)
    int targetMachine = 0;   // Target during crossfade

    // Crossfade state
    bool crossfading = false;
    int crossfadeSamples = 0; // Total samples for 75ms crossfade
    int crossfadeCounter = 0; // Counts down from crossfadeSamples to 0

    double sr = 44100.0;

    // Per-sample one-pole smoothing for amount params (anti-click)
    double amountSmoothCoeff = 0.0;
    double smoothedWow = 0.0;
    double smoothedSat = 0.0;
    double smoothedHiss = 0.0;
};
