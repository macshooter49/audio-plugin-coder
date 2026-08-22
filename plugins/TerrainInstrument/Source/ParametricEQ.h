#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <limits>

//==============================================================================
// ParametricEQ — Terrain v6 EQ
//
// Per-channel header-only DSP. One instance per audio channel.
//
// Topology (serial cascade per channel):
//
//   in → HP (1, 2, or 4 cascaded biquads, 12/24/48 dB/oct)
//      → B1 .. B7 (peaking bell biquads)
//      → LP (1, 2, or 4 cascaded biquads, 12/24/48 dB/oct)
//      → out
//
// Coefficients are recomputed only when their parameters change (fb456 — this line
// described the INTENT for a long time; updateAllCoefficients() rebuilt unconditionally,
// heap-allocating per sample. It is now actually true).
// LinearSmoothedValue<float> ramps changes over 5 ms to avoid zipper noise.
//==============================================================================
class ParametricEQ
{
public:
    static constexpr int NUM_BANDS    = 7;
    static constexpr int MAX_CUT_BIQUADS = 4; // 12/24/48 dB/oct = 1/2/4 stages

    enum SlopeChoice { Slope12 = 0, Slope24 = 1, Slope48 = 2 };

    ParametricEQ() = default;

    void prepare (double sampleRate, int samplesPerBlock)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };

        for (auto& f : hpStages) f.prepare (spec);
        for (auto& f : lpStages) f.prepare (spec);
        for (auto& f : bandFilters) f.prepare (spec);

        // CRITICAL: initialize smoothers to safe defaults BEFORE the first
        // call to updateAllCoefficients(). Default current value is 0, which
        // makes Q=0, which makes alpha = sin/(2*Q) = NaN inside makePeakFilter.
        // The biquad coefficients then become NaN and filter state is poisoned
        // for the lifetime of the instance. Using setCurrentAndTargetValue so
        // the first audio sample sees finite, musically-neutral coefficients.
        for (int b = 0; b < NUM_BANDS; ++b)
        {
            smoothFreq[b].reset (sampleRate, 0.005);
            smoothGain[b].reset (sampleRate, 0.005);
            smoothQ[b]   .reset (sampleRate, 0.005);
            smoothFreq[b].setCurrentAndTargetValue (1000.0f);
            smoothGain[b].setCurrentAndTargetValue (0.0f);
            smoothQ[b]   .setCurrentAndTargetValue (0.707f);
        }
        smoothHpFreq.reset (sampleRate, 0.005);
        smoothLpFreq.reset (sampleRate, 0.005);
        smoothHpFreq.setCurrentAndTargetValue (35.0f);
        smoothLpFreq.setCurrentAndTargetValue (16000.0f);

        // fb456 — invalidate the rebuild cache so the first sample after prepare() builds
        // every coefficient from scratch (sample rate may have changed under us).
        lastF.fill (kInvalid); lastG.fill (kInvalid); lastQ.fill (kInvalid);
        bandDirty.fill (true);
        lastHpF = lastLpF = lastSoloF = lastSoloQ = kInvalid;
        hpDirty = lpDirty = soloDirty = true;

        reset();
    }

    void reset()
    {
        for (auto& f : hpStages) f.reset();
        for (auto& f : lpStages) f.reset();
        for (auto& f : bandFilters) f.reset();
    }

    // Per-block parameter setters (called from processBlock before processSample loop).
    void setBandParams (int idx, float freq, float gainDb, float q, bool bypassed)
    {
        if (idx < 0 || idx >= NUM_BANDS) return;
        smoothFreq[idx].setTargetValue (freq);
        smoothGain[idx].setTargetValue (gainDb);
        smoothQ[idx]   .setTargetValue (q);
        // fb456 — un-bypassing must rebuild: while bypassed we skip the coefficient build,
        // so the cached f/g/q would otherwise look "unchanged" and leave stale coefficients.
        if (bandBypass[idx] != bypassed) bandDirty[idx] = true;
        bandBypass[idx] = bypassed;
    }
    void setHp (float freq, int slopeChoice, bool bypassed)
    {
        smoothHpFreq.setTargetValue (freq);
        const int newSlope = juce::jlimit (0, 2, slopeChoice);
        if (newSlope != hpSlope || hpBypass != bypassed) hpDirty = true;   // fb456 — stage COUNT changes with slope
        hpSlope = newSlope;
        hpBypass = bypassed;
    }
    void setLp (float freq, int slopeChoice, bool bypassed)
    {
        smoothLpFreq.setTargetValue (freq);
        const int newSlope = juce::jlimit (0, 2, slopeChoice);
        if (newSlope != lpSlope || lpBypass != bypassed) lpDirty = true;   // fb456
        lpSlope = newSlope;
        lpBypass = bypassed;
    }
    void setMasterBypass (bool b) noexcept { masterBypass = b; }

    // Solo: -1 = no solo, 0..6 = which band to solo.
    // When soloing, processSample returns a band-pass at the soloed band's freq+Q
    // applied to the EQ's INPUT, ignoring all other bands.
    void setSolo (int band) noexcept
    {
        const int nb = juce::jlimit (-1, NUM_BANDS - 1, band);
        if (nb != soloBand) soloDirty = true;   // fb456
        soloBand = nb;
    }

    float processSample (float x) noexcept
    {
        if (masterBypass) return x;

        // Update smoothed coefficients (cheap if no change targeted)
        updateAllCoefficients();

        if (soloBand >= 0)
            return processSolo (x);

        float y = x;
        if (! hpBypass)
        {
            const int n = stagesForSlope (hpSlope);
            for (int i = 0; i < n; ++i) y = hpStages[i].processSample (y);
        }
        for (int b = 0; b < NUM_BANDS; ++b)
            if (! bandBypass[b])
                y = bandFilters[b].processSample (y);
        if (! lpBypass)
        {
            const int n = stagesForSlope (lpSlope);
            for (int i = 0; i < n; ++i) y = lpStages[i].processSample (y);
        }

        if (! std::isfinite (y)) y = 0.0f;
        return y;
    }

private:
    static int stagesForSlope (int slopeChoice) noexcept
    {
        // 12 dB/oct = 1 biquad, 24 = 2, 48 = 4
        return slopeChoice == Slope12 ? 1 : slopeChoice == Slope24 ? 2 : 4;
    }

    // fb456 — CPU: this is called ONCE PER SAMPLE, PER CHANNEL, from processSample.
    // It used to rebuild all 7 bells + both cut cascades EVERY sample, and each
    // `*coeffs = *make...()` heap-ALLOCATES a juce Coefficients object, copies 6 floats
    // out of it and frees it again. At the shipped defaults that is ~22 malloc/free pairs
    // per sample = ~1.06 MILLION per second, burning ~9% of a core with NO notes playing —
    // and malloc in an audio callback is a real-time violation regardless of its cost.
    //
    // A LinearSmoothedValue that has reached its target returns the identical float every
    // call, so the overwhelmingly common case is "nothing moved". Rebuild only when a value
    // ACTUALLY changed (or a dirty flag says the topology did). The smoothers are still
    // advanced every sample, unconditionally, so ramp timing is bit-for-bit what it was.
    void updateAllCoefficients() noexcept
    {
        // Bands: peaking EQ
        for (int b = 0; b < NUM_BANDS; ++b)
        {
            // ALWAYS advance — the ramp must stay sample-locked to the old behaviour.
            const float f = smoothFreq[b].getNextValue();
            const float g = smoothGain[b].getNextValue();
            const float q = smoothQ[b].getNextValue();

            // A bypassed band's coefficients are never read by processSample/processSolo.
            if (bandBypass[b]) continue;
            if (! bandDirty[b] && f == lastF[b] && g == lastG[b] && q == lastQ[b]) continue;

            bandDirty[b] = false; lastF[b] = f; lastG[b] = g; lastQ[b] = q;
            *bandFilters[b].coefficients
                = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, f, q, juce::Decibels::decibelsToGain (g));
        }
        // HP / LP cascades — Butterworth Q per stage
        const float hpF = smoothHpFreq.getNextValue();
        if (! hpBypass && (hpDirty || hpF != lastHpF))
        {
            hpDirty = false; lastHpF = hpF;
            const int hpN = stagesForSlope (hpSlope);
            for (int i = 0; i < hpN; ++i)
                *hpStages[i].coefficients
                    = *juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, hpF, butterworthQ (hpN, i));
        }

        const float lpF = smoothLpFreq.getNextValue();
        if (! lpBypass && (lpDirty || lpF != lastLpF))
        {
            lpDirty = false; lastLpF = lpF;
            const int lpN = stagesForSlope (lpSlope);
            for (int i = 0; i < lpN; ++i)
                *lpStages[i].coefficients
                    = *juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, lpF, butterworthQ (lpN, i));
        }
    }

    // Butterworth Q for the i-th biquad in a 2N-order cascade.
    // For N=1 (one biquad, 12 dB/oct), Q = 1/sqrt(2) = 0.707.
    // For N=2 (24 dB/oct), Qs = 0.541, 1.307.
    // For N=4 (48 dB/oct), Qs = 0.510, 0.601, 0.900, 2.563.
    static float butterworthQ (int totalStages, int stageIdx) noexcept
    {
        // Pre-computed from cos((2k+N-1)*pi / (4N)) ^ -1 for k = 0..N-1, here N=stages.
        if (totalStages == 1) return 0.707106781f;
        if (totalStages == 2) return stageIdx == 0 ? 0.541196100f : 1.306562965f;
        // 4 stages
        static constexpr float q4[4] = { 0.509795579f, 0.601344887f, 0.899976223f, 2.562915448f };
        return q4[juce::jlimit (0, 3, stageIdx)];
    }

    float processSolo (float x) noexcept
    {
        // Build a transient band-pass at the soloed band's freq+Q — return only that.
        const float f = smoothFreq[soloBand].getNextValue();
        const float q = smoothQ[soloBand].getNextValue();
        // Burn the gain smoother to keep it advancing at audio rate.
        (void) smoothGain[soloBand].getNextValue();
        // Also advance HP/LP smoothers so they stay in sync when solo is released.
        (void) smoothHpFreq.getNextValue();
        (void) smoothLpFreq.getNextValue();
        // Advance other bands' smoothers too.
        for (int b = 0; b < NUM_BANDS; ++b)
            if (b != soloBand) { (void) smoothFreq[b].getNextValue(); (void) smoothGain[b].getNextValue(); (void) smoothQ[b].getNextValue(); }

        if (soloDirty || f != lastSoloF || q != lastSoloQ)   // fb456 — was allocating every sample
        {
            soloDirty = false; lastSoloF = f; lastSoloQ = q;
            *soloFilter.coefficients = *juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, f, q);
        }
        const float y = soloFilter.processSample (x);
        return std::isfinite (y) ? y : 0.0f;
    }

    double sr = 44100.0;

    std::array<juce::dsp::IIR::Filter<float>, MAX_CUT_BIQUADS> hpStages, lpStages;
    std::array<juce::dsp::IIR::Filter<float>, NUM_BANDS> bandFilters;
    juce::dsp::IIR::Filter<float> soloFilter;

    std::array<juce::LinearSmoothedValue<float>, NUM_BANDS> smoothFreq, smoothGain, smoothQ;
    juce::LinearSmoothedValue<float> smoothHpFreq, smoothLpFreq;

    std::array<bool, NUM_BANDS> bandBypass { false, false, false, false, false, false, false };
    bool hpBypass = true, lpBypass = true, masterBypass = false;
    int  hpSlope = Slope24, lpSlope = Slope24;
    int  soloBand = -1;

    // fb456 — coefficient-rebuild cache. NaN seeds force a build on the first sample
    // (NaN compares unequal to everything, including itself).
    static constexpr float kInvalid = std::numeric_limits<float>::quiet_NaN();
    std::array<float, NUM_BANDS> lastF { }, lastG { }, lastQ { };
    std::array<bool,  NUM_BANDS> bandDirty { };
    float lastHpF = kInvalid, lastLpF = kInvalid, lastSoloF = kInvalid, lastSoloQ = kInvalid;
    bool  hpDirty = true, lpDirty = true, soloDirty = true;
};
