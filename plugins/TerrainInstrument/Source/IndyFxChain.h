// IndyFxChain.h
// One private FX-chain instance for the per-chop FX-independence routing
// (option 1: shared chain). Owns its own copies of all 7 global FX
// modules + a stereo input bus + per-block ParamTargets.
//
// Shares APVTS parameter VALUES with the global chain (the processor
// passes the same numbers via setParamTargets each block) but has
// INDEPENDENT STATE (own reverb tail, grain history, delay line, etc.).
//
// processInto reads its input bus, runs it through the FX modules whose
// mask bits are set (per currentMask), and writes the result into the
// output bus ADDITIVELY. Order matches the global chain exactly:
//   input → [grain?] → [delay?] → [space?] → [tape?] → [june?] → [eq?] → output
//
// CRITICAL: even when the mask is empty (no chips lit), the per-sample
// loop MUST still run and pass the dry signal through to output. The
// `if (xxxOn)` guards skip individual FX modules, but the final
// `output[i] += dry[i]` line runs unconditionally. Do NOT add an early-
// return on empty mask — that drops the signal entirely.
//
// DO NOT include PluginProcessor.h here; the dependency must go the
// other way.
#pragma once

#include <cstdint>
#include "GrainEngine.h"
#include "TapeProcessor.h"
#include "SpaceReverb.h"
#include "MoogDelay.h"
#include "TerrainChorus.h"
#include "ParametricEQ.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace tw
{
    struct IndyFxChain
    {
        // Per-block parameter snapshot. The processor populates this once
        // per block from APVTS using the same scaling conventions the
        // global chain uses (see snapshotFxParamTargets in PluginProcessor.cpp).
        struct ParamTargets
        {
            // Grain — density / spray are RAW (not pre-scaled); freeze gets
            // pow(*0.01f, 1.5f); wander gets *0.01f; mix is raw.
            float grainSize   = 50.0f;
            float density     = 1.0f;       // grains/sec, range 1..100 (RAW from APVTS)
            float spray       = 0.0f;       // 0..100% (RAW; GrainEngine multiplies by 0.01 internally)
            float pitch       = 0.0f;
            float wander01    = 0.0f;       // (raw * 0.01f)
            float freeze01    = 0.0f;       // pow(raw * 0.01f, 1.5f)
            float mix         = 0.0f;

            // Tape — cassette params (cWow/cSat/cHiss) are normalized 0..1
            // via *0.01f. Studio params (sculpt/weave/tilt) and wire params
            // (wWow/wSat/wHiss) are passed raw (matching the global chain).
            float wowFlutter01  = 0.0f;     // raw * 0.01f
            float saturation01  = 0.0f;     // raw * 0.01f
            float hiss01        = 0.0f;     // raw * 0.01f
            float studioSculpt  = 0.0f;     // raw
            float studioWeave   = 0.0f;     // raw
            float studioTilt    = 0.0f;     // raw
            float wireWow       = 0.0f;     // raw
            float wireSat       = 0.0f;     // raw
            float wireHiss      = 0.0f;     // raw
            bool  wireSpaceNoise = false;
            bool  wireTubeSat    = false;

            // Space
            float spaceSize  = 0.5f;
            float spaceDecay = 0.5f;
            float spaceTone  = 0.5f;
            float spaceMix   = 0.5f;

            // Delay — MoogDelay::Params field names verified from MoogDelay.h
            float dlyTime       = 100.0f;
            float dlyFeedback   = 0.0f;
            float dlyTone       = 0.5f;
            float dlyCharacter  = 0.5f;
            float dlyMod        = 0.0f;     // modDepth
            float dlyModRate    = 1.0f;     // modRateHz
            int   dlyModWave    = 0;
            float dlyMix        = 0.0f;
            float dlyDuck       = 0.0f;
            int   dlyPitch      = 0;
            int   dlyWidth      = 1;
            bool  dlyFreezeHeld = false;

            // June (chorus)
            float chAmount    = 0.0f;
            float chWidth     = 0.7f;
            float chCharacter = 0.3f;

            // EQ — ALL bypass args are bool. Defaults give passthrough.
            bool  eqMasterBypass = false;
            float eqHpFreq = 35.0f;    int eqHpSlope = 1;  bool eqHpBypass = true;
            float eqLpFreq = 16000.0f; int eqLpSlope = 0;  bool eqLpBypass = true;
            float bandFreq[7]   { 50.f, 110.f, 270.f, 630.f, 1500.f, 3500.f, 8500.f };
            float bandGain[7]   { 0.f,   0.f,   0.f,   0.f,    0.f,    0.f,    0.f };
            float bandQ[7]      { 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f };
            bool  bandBypass[7] { true, true, true, true, true, true, true };
        };

        // ── Setup ─────────────────────────────────────────────────────────────
        void prepare (double sr, int blockSize)
        {
            grainL.prepare (sr, blockSize);
            grainR.prepare (sr, blockSize);
            tapeL.prepare (sr, blockSize);
            tapeR.prepare (sr, blockSize);
            spaceReverb.prepare (sr, blockSize);
            moogDelay.prepare (sr, blockSize);
            terrainChorus.prepare (sr, blockSize);
            eqL.prepare (sr, blockSize);
            eqR.prepare (sr, blockSize);
            currentMask = 0;
        }

        void reset() noexcept
        {
            grainL.reset();
            grainR.reset();
            tapeL.reset();
            tapeR.reset();
            spaceReverb.reset();
            moogDelay.reset();
            terrainChorus.reset();
            eqL.reset();
            eqR.reset();
        }

        // ── Per-block wiring ─────────────────────────────────────────────────
        void setMask (std::uint8_t mask) noexcept { currentMask = mask; }
        std::uint8_t getMask() const noexcept     { return currentMask; }

        void setParamTargets (const ParamTargets& t) noexcept { latestTargets = t; }

        // ── Process ──────────────────────────────────────────────────────────
        /** Reads `input` per-sample, runs through enabled FX (per mask),
         *  ADDS the result to `output`. Caller is responsible for clearing
         *  the input buffer at the start of the next block (the processor's
         *  existing indyCaptureBus.clear handles this). Empty mask = dry
         *  pass-through (output[i] += input[i]).
         *
         *  Chain order matches PluginProcessor.cpp's global chain:
         *  grain → delay → space → tape → june → eq. */
        void processInto (const juce::AudioBuffer<float>& input,
                          juce::AudioBuffer<float>& output,
                          int numSamples) noexcept
        {
            const std::uint8_t mask = currentMask;

            // Decode per-module bypass from mask (bit layout from FxMask.h)
            const bool grainOn = (mask & 0b0000001u) != 0;
            const int  tapeM   = (int) ((mask >> 1) & 0b11u);  // 0..3
            const bool tapeOn  = tapeM != 0;
            const bool spaceOn = (mask & 0b0001000u) != 0;
            const bool delayOn = (mask & 0b0010000u) != 0;
            const bool eqOn    = (mask & 0b0100000u) != 0;
            const bool juneOn  = (mask & 0b1000000u) != 0;

            // Per-block tape machine selection
            if (tapeOn)
            {
                tapeL.setMachine (tapeM - 1);  // mask 1/2/3 → machine 0/1/2
                tapeR.setMachine (tapeM - 1);
                tapeL.setWireModes (latestTargets.wireSpaceNoise, latestTargets.wireTubeSat);
                tapeR.setWireModes (latestTargets.wireSpaceNoise, latestTargets.wireTubeSat);
            }

            // Per-block EQ param application (matches global chain pattern)
            if (eqOn)
            {
                eqL.setMasterBypass (latestTargets.eqMasterBypass);
                eqR.setMasterBypass (latestTargets.eqMasterBypass);
                eqL.setHp (latestTargets.eqHpFreq, latestTargets.eqHpSlope, latestTargets.eqHpBypass);
                eqR.setHp (latestTargets.eqHpFreq, latestTargets.eqHpSlope, latestTargets.eqHpBypass);
                eqL.setLp (latestTargets.eqLpFreq, latestTargets.eqLpSlope, latestTargets.eqLpBypass);
                eqR.setLp (latestTargets.eqLpFreq, latestTargets.eqLpSlope, latestTargets.eqLpBypass);
                for (int b = 0; b < 7; ++b)
                {
                    eqL.setBandParams (b, latestTargets.bandFreq[b], latestTargets.bandGain[b],
                                          latestTargets.bandQ[b],    latestTargets.bandBypass[b]);
                    eqR.setBandParams (b, latestTargets.bandFreq[b], latestTargets.bandGain[b],
                                          latestTargets.bandQ[b],    latestTargets.bandBypass[b]);
                }
            }

            if (juneOn)
                terrainChorus.setParams (latestTargets.chAmount, latestTargets.chWidth, latestTargets.chCharacter);

            const float* L  = input.getReadPointer (0);
            const float* R  = input.getNumChannels() > 1 ? input.getReadPointer (1) : L;
            float* outL = output.getWritePointer (0);
            float* outR = output.getNumChannels() > 1 ? output.getWritePointer (1) : outL;

            // Per-sample loop. ALWAYS runs — empty mask = wetL stays equal
            // to L[i] and the final `outL[i] += wetL` does dry pass-through.
            for (int i = 0; i < numSamples; ++i)
            {
                float wetL = L[i];
                float wetR = R[i];

                // 1. Grain
                if (grainOn)
                {
                    wetL = grainL.processSample (wetL,
                              latestTargets.grainSize, latestTargets.density, latestTargets.spray,
                              latestTargets.pitch,     latestTargets.wander01, latestTargets.freeze01,
                              latestTargets.mix);
                    wetR = grainR.processSample (wetR,
                              latestTargets.grainSize, latestTargets.density, latestTargets.spray,
                              latestTargets.pitch,     latestTargets.wander01, latestTargets.freeze01,
                              latestTargets.mix);
                }

                // 2. Delay
                if (delayOn)
                {
                    MoogDelay::Params dp;
                    dp.timeMs      = latestTargets.dlyTime;
                    dp.feedback    = latestTargets.dlyFeedback;
                    dp.tone        = latestTargets.dlyTone;
                    dp.character   = latestTargets.dlyCharacter;
                    dp.modDepth    = latestTargets.dlyMod;
                    dp.modRateHz   = latestTargets.dlyModRate;
                    dp.modWave     = latestTargets.dlyModWave;
                    dp.mix         = latestTargets.dlyMix;
                    dp.duck        = latestTargets.dlyDuck;
                    dp.pitch       = latestTargets.dlyPitch;
                    dp.width       = latestTargets.dlyWidth;
                    dp.freezeHeld  = latestTargets.dlyFreezeHeld;
                    moogDelay.processStereo (wetL, wetR, dp);
                }

                // 3. Space reverb
                if (spaceOn)
                    spaceReverb.processStereo (wetL, wetR,
                        latestTargets.spaceSize, latestTargets.spaceDecay,
                        latestTargets.spaceTone, latestTargets.spaceMix);

                // 4. Tape — signature verified from TapeProcessor.h:
                //   processSample(input, cWow, cSat, cHiss, wWow, wSat, wHiss, sculpt, weave, tilt)
                if (tapeOn)
                {
                    wetL = tapeL.processSample (wetL,
                               latestTargets.wowFlutter01, latestTargets.saturation01, latestTargets.hiss01,
                               latestTargets.wireWow,      latestTargets.wireSat,      latestTargets.wireHiss,
                               latestTargets.studioSculpt, latestTargets.studioWeave,  latestTargets.studioTilt);
                    wetR = tapeR.processSample (wetR,
                               latestTargets.wowFlutter01, latestTargets.saturation01, latestTargets.hiss01,
                               latestTargets.wireWow,      latestTargets.wireSat,      latestTargets.wireHiss,
                               latestTargets.studioSculpt, latestTargets.studioWeave,  latestTargets.studioTilt);
                }

                // 5. June (chorus) — setParams called once per block above
                if (juneOn)
                    terrainChorus.processStereo (wetL, wetR);

                // 6. EQ — setBandParams / setHp / setLp called once per block above
                if (eqOn)
                {
                    wetL = eqL.processSample (wetL);
                    wetR = eqR.processSample (wetR);
                }

                // Additive mix into output. ALWAYS runs — this is the
                // dry-pass-through path when mask is empty.
                outL[i] += wetL;
                outR[i] += wetR;
            }
        }

    private:
        GrainEngine    grainL, grainR;
        TapeProcessor  tapeL,  tapeR;
        SpaceReverb    spaceReverb;
        MoogDelay      moogDelay;
        TerrainChorus  terrainChorus;
        ParametricEQ   eqL, eqR;

        ParamTargets   latestTargets;
        std::uint8_t   currentMask = 0;
    };
}
