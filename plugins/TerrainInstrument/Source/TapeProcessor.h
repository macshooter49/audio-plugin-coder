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
        smoothedSculpt = 0.0;
        smoothedWeave  = 0.0;
        smoothedTilt   = 0.0;
        smoothedWireWow  = 0.0;
        smoothedWireSat  = 0.0;
        smoothedWireHiss = 0.0;
    }

    void reset()
    {
        studioMachine.reset();
        cassetteMachine.reset();
        wireMachine.reset();
        smoothedWow = 0.0;
        smoothedSat = 0.0;
        smoothedHiss = 0.0;
        smoothedSculpt = 0.0;
        smoothedWeave  = 0.0;
        smoothedTilt   = 0.0;
        smoothedWireWow  = 0.0;
        smoothedWireSat  = 0.0;
        smoothedWireHiss = 0.0;

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

    // Process one sample with tape character.
    // Cassette and Wire each have their OWN wow/sat/hiss values so they
    // can be tuned independently (especially with LINK on). Studio ignores
    // both sets — it uses sculpt/weave/tilt instead.
    //   cWow / cSat / cHiss : Cassette wow/sat/hiss (0..1)
    //   wWow / wSat / wHiss : Wire wow/sat/hiss     (0..1)
    //   sculpt / weave      : 0..1 (Studio)
    //   tilt                : -1..+1 (Studio)
    float processSample (float input,
                         float cWow, float cSat, float cHiss,
                         float wWow, float wSat, float wHiss,
                         float sculpt, float weave, float tilt)
    {
        // Smooth all six wow/sat/hiss inputs (Cassette + Wire) and the three
        // Studio sculptor inputs at 5 ms — anti-click on LFO modulation.
        smoothedSat      += amountSmoothCoeff * (static_cast<double>(cSat)  - smoothedSat);
        smoothedWow      += amountSmoothCoeff * (static_cast<double>(cWow)  - smoothedWow);
        smoothedHiss     += amountSmoothCoeff * (static_cast<double>(cHiss) - smoothedHiss);
        smoothedWireWow  += amountSmoothCoeff * (static_cast<double>(wWow)  - smoothedWireWow);
        smoothedWireSat  += amountSmoothCoeff * (static_cast<double>(wSat)  - smoothedWireSat);
        smoothedWireHiss += amountSmoothCoeff * (static_cast<double>(wHiss) - smoothedWireHiss);
        smoothedSculpt   += amountSmoothCoeff * (static_cast<double>(sculpt) - smoothedSculpt);
        smoothedWeave    += amountSmoothCoeff * (static_cast<double>(weave)  - smoothedWeave);
        smoothedTilt     += amountSmoothCoeff * (static_cast<double>(tilt)   - smoothedTilt);

        studioMachine.setSculptorParams (smoothedSculpt, smoothedWeave, smoothedTilt);

        // Pick the right wow/sat/hiss set per machine. Studio ignores both
        // (it uses sculptor params), Cassette uses cassette set, Wire uses
        // wire set — so each machine sees its own independent values.
        auto valuesFor = [this] (int machineIdx, float& outSat, float& outWow, float& outHiss)
        {
            if (machineIdx == 2) // Wire
            {
                outSat  = static_cast<float> (smoothedWireSat);
                outWow  = static_cast<float> (smoothedWireWow);
                outHiss = static_cast<float> (smoothedWireHiss);
            }
            else // Studio (ignores) or Cassette
            {
                outSat  = static_cast<float> (smoothedSat);
                outWow  = static_cast<float> (smoothedWow);
                outHiss = static_cast<float> (smoothedHiss);
            }
        };

        if (! crossfading)
        {
            float aSat, aWow, aHiss; valuesFor (activeMachine, aSat, aWow, aHiss);
            return processOneMachine (machines[activeMachine], input, aSat, aWow, aHiss);
        }

        float aSat, aWow, aHiss; valuesFor (activeMachine, aSat, aWow, aHiss);
        float tSat, tWow, tHiss; valuesFor (targetMachine, tSat, tWow, tHiss);
        float outputOld = processOneMachine (machines[activeMachine], input, aSat, aWow, aHiss);
        float outputNew = processOneMachine (machines[targetMachine], input, tSat, tWow, tHiss);

        const float blend = static_cast<float> (crossfadeCounter) / static_cast<float> (crossfadeSamples);
        float output = outputOld * blend + outputNew * (1.0f - blend);

        crossfadeCounter--;
        if (crossfadeCounter <= 0)
        {
            activeMachine = targetMachine;
            crossfading = false;
        }
        return output;
    }

    // Process one sample with ALL THREE tape machines in series:
    //   input → Studio → Cassette → Wire → output
    //
    // Each machine receives its own knob values from APVTS via the same
    // smoothed parameters processSample uses, so dialing in Studio's
    // SCULPT/DRIVE/TIMBRE then switching to Cassette to dial WOW/SAT/HISS
    // sets up an entire tape chain that all runs at once when Link is on.
    //
    // The series order is the machine-cycle order (0 → 1 → 2). A 0.6
    // gain compensation at the end keeps peaks musical after three
    // saturator stages stack — without it, hot inputs blow into the
    // post-tape soft limiter ugly.
    //
    // No machine-switch crossfade applies here (link mode is "all three,
    // always"). Toggling the link param itself produces a small step in
    // signal energy, but the upstream ~5ms amount-smoothing on wow/sat/
    // hiss/sculpt/weave/tilt absorbs most of it.
    float processSampleLinked (float input,
                               float cWow, float cSat, float cHiss,
                               float wWow, float wSat, float wHiss,
                               float sculpt, float weave, float tilt)
    {
        smoothedSat      += amountSmoothCoeff * (static_cast<double>(cSat)  - smoothedSat);
        smoothedWow      += amountSmoothCoeff * (static_cast<double>(cWow)  - smoothedWow);
        smoothedHiss     += amountSmoothCoeff * (static_cast<double>(cHiss) - smoothedHiss);
        smoothedWireWow  += amountSmoothCoeff * (static_cast<double>(wWow)  - smoothedWireWow);
        smoothedWireSat  += amountSmoothCoeff * (static_cast<double>(wSat)  - smoothedWireSat);
        smoothedWireHiss += amountSmoothCoeff * (static_cast<double>(wHiss) - smoothedWireHiss);
        smoothedSculpt   += amountSmoothCoeff * (static_cast<double>(sculpt) - smoothedSculpt);
        smoothedWeave    += amountSmoothCoeff * (static_cast<double>(weave)  - smoothedWeave);
        smoothedTilt     += amountSmoothCoeff * (static_cast<double>(tilt)   - smoothedTilt);

        studioMachine.setSculptorParams (smoothedSculpt, smoothedWeave, smoothedTilt);

        const float casSat  = static_cast<float> (smoothedSat);
        const float casWow  = static_cast<float> (smoothedWow);
        const float casHiss = static_cast<float> (smoothedHiss);
        const float wirSat  = static_cast<float> (smoothedWireSat);
        const float wirWow  = static_cast<float> (smoothedWireWow);
        const float wirHiss = static_cast<float> (smoothedWireHiss);

        // Cassette wow/sat/hiss for the Cassette stage. Wire wow/sat/hiss
        // for the Wire stage. Studio uses sculptor params (set above).
        float y = processOneMachine (machines[0], input, 0.f,    0.f,    0.f);     // Studio (Harmonic Sculptor)
        y      = processOneMachine (machines[1], y,     casSat, casWow, casHiss); // Cassette
        y      = processOneMachine (machines[2], y,     wirSat, wirWow, wirHiss); // Wire

        return y * 0.6f; // gain comp for three series stages
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
        //    Pass post-saturation/post-wow signal for input-reactive space noise
        float audioGainMultiplier = 1.0f;
        double hissNoise = machine->processHiss(dHiss, audioGainMultiplier, processed);

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
    double smoothedSculpt = 0.0;
    double smoothedWeave  = 0.0;
    double smoothedTilt   = 0.0;
    // Wire-specific smoothed values (independent from Cassette's smoothedWow/Sat/Hiss).
    double smoothedWireWow  = 0.0;
    double smoothedWireSat  = 0.0;
    double smoothedWireHiss = 0.0;
};
