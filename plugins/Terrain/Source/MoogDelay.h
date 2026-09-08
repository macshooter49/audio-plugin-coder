#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

//==============================================================================
struct LPBiquad
{
    void setCutoff (double sampleRate, float cutoffHz)
    {
        if (cutoffHz < 20.0f)    cutoffHz = 20.0f;
        if (cutoffHz > static_cast<float> (sampleRate) * 0.45f)
            cutoffHz = static_cast<float> (sampleRate) * 0.45f;
        const double w0 = 2.0 * 3.14159265358979 * cutoffHz / sampleRate;
        const double cosw = std::cos (w0);
        const double sinw = std::sin (w0);
        const double Q = 0.7071067811865476;
        const double alpha = sinw / (2.0 * Q);

        const double b0 = (1.0 - cosw) * 0.5;
        const double b1 = 1.0 - cosw;
        const double b2 = (1.0 - cosw) * 0.5;
        const double a0 = 1.0 + alpha;
        const double a1 = -2.0 * cosw;
        const double a2 = 1.0 - alpha;

        nb0 = static_cast<float> (b0 / a0);
        nb1 = static_cast<float> (b1 / a0);
        nb2 = static_cast<float> (b2 / a0);
        na1 = static_cast<float> (a1 / a0);
        na2 = static_cast<float> (a2 / a0);
    }

    float process (float x)
    {
        const float y = nb0 * x + nb1 * x1 + nb2 * x2 - na1 * y1 - na2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    void clear() { x1 = x2 = y1 = y2 = 0.0f; }

    float nb0 = 1.0f, nb1 = 0.0f, nb2 = 0.0f, na1 = 0.0f, na2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

//==============================================================================
struct Compander
{
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double attackMs  = 10.0;
        const double releaseMs = 100.0;
        attackCoef  = std::exp (-1.0 / (sr * attackMs  * 0.001));
        releaseCoef = std::exp (-1.0 / (sr * releaseMs * 0.001));
        envIn  = 0.0f;
        envOut = 0.0f;
    }

    void reset() { envIn = envOut = 0.0f; }

    float compressIn (float x)
    {
        const float absX = std::abs (x);
        const float coef = absX > envIn ? static_cast<float> (1.0 - attackCoef)
                                        : static_cast<float> (1.0 - releaseCoef);
        envIn += coef * (absX - envIn);

        const float threshDb = -30.0f;
        const float envDb = 20.0f * std::log10 (std::max (envIn, 1.0e-6f));
        if (envDb <= threshDb) return x;
        const float overDb = envDb - threshDb;
        const float reducedOverDb = overDb * 0.25f;
        const float gainDb = reducedOverDb - overDb;
        const float gain = std::pow (10.0f, gainDb / 20.0f);
        return x * gain;
    }

    float expandOut (float x)
    {
        const float absX = std::abs (x);
        const float coef = absX > envOut ? static_cast<float> (1.0 - attackCoef)
                                         : static_cast<float> (1.0 - releaseCoef);
        envOut += coef * (absX - envOut);

        const float threshDb = -30.0f;
        const float envDb = 20.0f * std::log10 (std::max (envOut, 1.0e-6f));
        if (envDb >= threshDb) return x;
        const float underDb = threshDb - envDb;
        const float expandedUnderDb = underDb * 4.0f;
        const float gainDb = -(expandedUnderDb - underDb);
        const float gain = std::pow (10.0f, gainDb / 20.0f);
        return x * gain;
    }

    double sr = 44100.0;
    double attackCoef = 0.0;
    double releaseCoef = 0.0;
    float envIn = 0.0f;
    float envOut = 0.0f;
};

//==============================================================================
struct TiltShelf
{
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        update (0.0f);
        reset();
    }

    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

    // tone: -1..+1. 0 = flat. +1 = bright (HF lift), -1 = dark (HF cut).
    // High-shelf at 800 Hz with gain = tone * 9 dB. Combined with implicit
    // mirror behavior at low end via shelf shape — produces audible tilt.
    void update (float tone)
    {
        if (tone == lastTone) return;
        lastTone = tone;

        const double w0 = 2.0 * 3.14159265358979 * 800.0 / sr;
        const double cosw = std::cos (w0);
        const double sinw = std::sin (w0);
        const double A = std::pow (10.0, static_cast<double>(tone) * 9.0 / 40.0); // tone*9dB shelf
        const double S = 1.0;
        const double alpha = sinw / 2.0 * std::sqrt ((A + 1.0/A) * (1.0/S - 1.0) + 2.0);
        const double sqA2alpha = 2.0 * std::sqrt (A) * alpha;
        const double b0 =     A * ((A + 1.0) + (A - 1.0) * cosw + sqA2alpha);
        const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
        const double b2 =     A * ((A + 1.0) + (A - 1.0) * cosw - sqA2alpha);
        const double a0 =          (A + 1.0) - (A - 1.0) * cosw + sqA2alpha;
        const double a1 =    2.0 * ((A - 1.0) - (A + 1.0) * cosw);
        const double a2 =          (A + 1.0) - (A - 1.0) * cosw - sqA2alpha;
        nb0 = static_cast<float>(b0 / a0);
        nb1 = static_cast<float>(b1 / a0);
        nb2 = static_cast<float>(b2 / a0);
        na1 = static_cast<float>(a1 / a0);
        na2 = static_cast<float>(a2 / a0);
    }

    float process (float x)
    {
        const float y = nb0 * x + nb1 * x1 + nb2 * x2 - na1 * y1 - na2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    double sr = 44100.0;
    float lastTone = -999.0f;
    float nb0 = 1.0f, nb1 = 0.0f, nb2 = 0.0f, na1 = 0.0f, na2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

//==============================================================================
// MoogDelay: MF-104S-inspired stereo BBD delay (header-only).
//
// Signal flow (per channel):
//   in -> Compander_in -> AntiAlias_LP -> FractionalDelay -> ReconLP(time-tracked)
//      -> Compander_out -> Tone -> wet
//   feedback = wet -> PitchShift -> BBD_sat -> CrossFeed -> back to delay input
//
// All controls live in MoogDelay::Params; the host writes them per-sample.
//==============================================================================
class MoogDelay
{
public:
    struct Params
    {
        float timeMs    = 350.0f;
        float feedback  = 0.45f;
        float tone      = 0.0f;
        float character = 0.5f;
        float modDepth  = 0.0f;
        float modRateHz = 0.5f;
        int   modWave   = 0;
        float mix       = 0.30f;
        float duck      = 0.0f;
        int   pitch     = 0;
        int   width     = 1;
        bool  freezeHeld = false;
    };

    MoogDelay() = default;

    void prepare (double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;
        const int maxSamples = static_cast<int> (sr * 2.0) + 8;
        bufferL.assign (static_cast<size_t> (maxSamples), 0.0f);
        bufferR.assign (static_cast<size_t> (maxSamples), 0.0f);
        bufSize = maxSamples;
        writePos = 0;
        pitchReadPosL = 0.0f;
        pitchReadPosR = 0.0f;
        lfoPhase = 0.0f;
        lfoSampleHold = 0.0f;
        lfoChaos = 0.0f;
        const double smoothTimeSec = 0.005;
        delaySmoothCoef = static_cast<float> (1.0 - std::exp (-1.0 / (sr * smoothTimeSec)));
        smoothedDelaySamples = static_cast<float> (sr * 0.350);
        reset();

        reconL1.clear(); reconL2.clear();
        reconR1.clear(); reconR2.clear();
        const float cutoff = computeReconCutoff (350.0f);
        reconL1.setCutoff (sr, cutoff);
        reconL2.setCutoff (sr, cutoff);
        reconR1.setCutoff (sr, cutoff);
        reconR2.setCutoff (sr, cutoff);

        preL.clear(); preR.clear();
        preL.setCutoff (sr, computePreCutoff (350.0f));
        preR.setCutoff (sr, computePreCutoff (350.0f));

        compL.prepare (sr);
        compR.prepare (sr);

        duckAttackCoef  = static_cast<float> (1.0 - std::exp (-1.0 / (sr * 0.005)));
        duckReleaseCoef = static_cast<float> (1.0 - std::exp (-1.0 / (sr * 0.200)));
        duckEnvL = duckEnvR = 0.0f;

        freezeSmoothCoef = static_cast<float> (1.0 - std::exp (-1.0 / (sr * 0.020)));
        freezeGain = 0.0f;

        toneL.prepare (sr);
        toneR.prepare (sr);
        pitchReadInitL = false;
        pitchReadInitR = false;
    }

    void reset()
    {
        std::fill (bufferL.begin(), bufferL.end(), 0.0f);
        std::fill (bufferR.begin(), bufferR.end(), 0.0f);
        writePos = 0;
        pitchReadPosL = 0.0f;
        pitchReadPosR = 0.0f;
        lfoPhase = 0.0f;
        lfoSampleHold = 0.0f;
        lfoChaos = 0.0f;
        reconL1.clear(); reconL2.clear();
        reconR1.clear(); reconR2.clear();
        preL.clear(); preR.clear();
        compL.reset(); compR.reset();
        duckEnvL = duckEnvR = 0.0f;
        freezeGain = 0.0f;
        toneL.reset();
        toneR.reset();
        pitchReadInitL = false;
        pitchReadInitR = false;
    }

    void processStereo (float& left, float& right, const Params& p)
    {
        if (p.mix <= 0.0001f)
            return;

        const float freezeTarget = p.freezeHeld ? 1.0f : 0.0f;
        freezeGain += freezeSmoothCoef * (freezeTarget - freezeGain);

        const float minMs = 5.0f;
        const float maxMs = 1500.0f;
        const float clampedMs = std::max (minMs, std::min (maxMs, p.timeMs));
        const float delaySamples = static_cast<float> (sr) * clampedMs / 1000.0f;

        const float lfoInc = std::max (0.0f, std::min (8.0f, p.modRateHz)) / static_cast<float> (sr);
        const float prevPhase = lfoPhase;
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        const bool wrapped = (lfoPhase < prevPhase);
        if (wrapped) lfoSampleHold = nextRand01 (lfoRng);
        lfoChaos += 0.001f * nextRand01 (lfoRng);
        if (lfoChaos >  1.0f) lfoChaos =  1.0f;
        if (lfoChaos < -1.0f) lfoChaos = -1.0f;

        const float lfoVal = lfoWaveform (std::max (0, std::min (6, p.modWave)),
                                          lfoPhase, lfoSampleHold, lfoChaos, lfoRng);

        const float modOffset = lfoVal * p.modDepth * 0.30f * delaySamples;
        const float targetSamples = std::max (1.0f, delaySamples + modOffset);

        smoothedDelaySamples += delaySmoothCoef * (targetSamples - smoothedDelaySamples);
        const float effectiveDelaySamples = smoothedDelaySamples;

        // === Step 1: Read raw buffer at delay tap (Hermite interpolation) ===
        const float wetL_raw = readHermite (bufferL.data(), bufSize, writePos, effectiveDelaySamples);
        const float wetR_raw = readHermite (bufferR.data(), bufSize, writePos, effectiveDelaySamples);

        // === Step 2: Apply recon LP (bandwidth-tracking 4-pole) ===
        const float cutoff = computeReconCutoff (clampedMs);
        reconL1.setCutoff (sr, cutoff);
        reconL2.setCutoff (sr, cutoff);
        reconR1.setCutoff (sr, cutoff);
        reconR2.setCutoff (sr, cutoff);

        const float wetL_lp = reconL2.process (reconL1.process (wetL_raw));
        const float wetR_lp = reconR2.process (reconR1.process (wetR_raw));

        // === Step 3: Pitch-shift the recon-LP'd signal for feedback ===
        // Replaces the broken old algorithm. Maintains a fractional read
        // pointer initialized at the delay tap; advances at pitchRatio per
        // sample; wraps back by one delay period before overrunning the
        // write head.
        // Auto-bypass at short delay times: pitch-in-feedback compounds the
        // re-sync rate to audible AM frequencies (~200Hz wobble at 5ms).
        // Below 40ms the algorithm just sounds like glitchy ring-mod, so we
        // crossfade to dry-recon between 40-50ms.
        const float pitchRatio = pitchSemitoneRatio (p.pitch);
        const float currentMs = effectiveDelaySamples * 1000.0f / static_cast<float>(sr);
        const float pitchActiveAmount = std::max (0.0f, std::min (1.0f,
            (currentMs - 40.0f) / 10.0f));
        float pitchedFromLpL, pitchedFromLpR;

        if (p.pitch == 0 || pitchActiveAmount <= 0.0f)
        {
            pitchedFromLpL = wetL_lp;
            pitchedFromLpR = wetR_lp;
            pitchReadInitL = false;
            pitchReadInitR = false;
        }
        else
        {
            const float bufSizeF = static_cast<float> (bufSize);
            const float writePosF = static_cast<float> (writePos);

            if (! pitchReadInitL)
            {
                pitchReadPosL = writePosF - effectiveDelaySamples;
                if (pitchReadPosL < 0.0f) pitchReadPosL += bufSizeF;
                pitchReadInitL = true;
            }
            if (! pitchReadInitR)
            {
                pitchReadPosR = writePosF - effectiveDelaySamples;
                if (pitchReadPosR < 0.0f) pitchReadPosR += bufSizeF;
                pitchReadInitR = true;
            }

            // Read the buffer at the pitched position. Buffer content is
            // already pre-LP'd on the write side, so spectral content is
            // appropriately bandlimited for the current delay time.
            pitchedFromLpL = readHermiteAbs (bufferL.data(), bufSize, pitchReadPosL, effectiveDelaySamples);
            pitchedFromLpR = readHermiteAbs (bufferR.data(), bufSize, pitchReadPosR, effectiveDelaySamples);

            // Advance read pointer by pitchRatio per output sample.
            pitchReadPosL += pitchRatio;
            pitchReadPosR += pitchRatio;

            // Distance behind write head, in samples (positive = read is in past).
            auto distBehindWrite = [bufSizeF, writePosF] (float readPos)
            {
                float d = writePosF - readPos;
                while (d <  0.0f)     d += bufSizeF;
                while (d >= bufSizeF) d -= bufSizeF;
                return d;
            };

            if (pitchRatio > 1.0f)
            {
                // Read advances faster than write → at risk of overrunning.
                // Re-sync by jumping back one delay period when too close.
                if (distBehindWrite (pitchReadPosL) < effectiveDelaySamples * 0.5f)
                    pitchReadPosL -= effectiveDelaySamples;
                if (distBehindWrite (pitchReadPosR) < effectiveDelaySamples * 0.5f)
                    pitchReadPosR -= effectiveDelaySamples;
            }
            else if (pitchRatio < 1.0f)
            {
                // Read advances slower than write → falls too far behind.
                if (distBehindWrite (pitchReadPosL) > effectiveDelaySamples * 1.8f)
                    pitchReadPosL += effectiveDelaySamples;
                if (distBehindWrite (pitchReadPosR) > effectiveDelaySamples * 1.8f)
                    pitchReadPosR += effectiveDelaySamples;
            }

            // Bounds-normalize the read pointers.
            while (pitchReadPosL <  0.0f)     pitchReadPosL += bufSizeF;
            while (pitchReadPosL >= bufSizeF) pitchReadPosL -= bufSizeF;
            while (pitchReadPosR <  0.0f)     pitchReadPosR += bufSizeF;
            while (pitchReadPosR >= bufSizeF) pitchReadPosR -= bufSizeF;

            // Crossfade against dry-recon over the 40-50ms band so the
            // threshold isn't a hard click when sweeping the time knob.
            if (pitchActiveAmount < 1.0f)
            {
                pitchedFromLpL = wetL_lp * (1.0f - pitchActiveAmount) + pitchedFromLpL * pitchActiveAmount;
                pitchedFromLpR = wetR_lp * (1.0f - pitchActiveAmount) + pitchedFromLpR * pitchActiveAmount;
            }
        }

        // === Step 4: BBD saturation on feedback (softer drive curve) ===
        // OLD: drive = 1.0 + char*2  → at char=0, gain ~1.31x → mosquito buzz
        // NEW: drive = 0.6 + char*1.8 → at char=0, gain ~0.85x → tails decay
        const float fb = std::max (0.0f, std::min (1.10f, p.feedback));
        const float drive = 0.6f + p.character * 1.8f;
        const float effectiveFb = std::max (fb, freezeGain);
        float fbL = bbdSaturate (pitchedFromLpL * effectiveFb, drive);
        float fbR = bbdSaturate (pitchedFromLpR * effectiveFb, drive);

        if (! std::isfinite (fbL)) fbL = 0.0f;
        if (! std::isfinite (fbR)) fbR = 0.0f;

        const float cap = 1.5f;
        if (fbL >  cap) fbL =  cap;
        if (fbL < -cap) fbL = -cap;
        if (fbR >  cap) fbR =  cap;
        if (fbR < -cap) fbR = -cap;

        // === Step 5: Width cross-feed for feedback path ===
        float fbIntoL = fbL;
        float fbIntoR = fbR;
        if (p.width == 0) // mono — both channels collapse
        {
            const float fbMono = 0.5f * (fbL + fbR);
            fbIntoL = fbIntoR = fbMono;
        }
        else if (p.width == 2) // ping-pong: aggressive 90/10 cross-feed
        {
            fbIntoL = fbR * 0.90f + fbL * 0.10f;
            fbIntoR = fbL * 0.90f + fbR * 0.10f;
        }

        // === Step 6: Compander-in + pre-LP + write to buffer ===
        const float inputScale = 1.0f - freezeGain;
        const float compInL = compL.compressIn (left * inputScale + fbIntoL);
        const float compInR = compR.compressIn (right * inputScale + fbIntoR);

        const float preCut = computePreCutoff (clampedMs);
        preL.setCutoff (sr, preCut);
        preR.setCutoff (sr, preCut);

        bufferL[static_cast<size_t> (writePos)] = preL.process (compInL);
        bufferR[static_cast<size_t> (writePos)] = preR.process (compInR);

        writePos = (writePos + 1) % bufSize;

        // === Step 7: Compander-out (expand) on the wet output ===
        const float wetL_exp = compL.expandOut (wetL_lp);
        const float wetR_exp = compR.expandOut (wetR_lp);

        // === Step 8: TONE shaper (tilt EQ on wet signal) ===
        const float toneClamped = std::max (-1.0f, std::min (1.0f, p.tone));
        toneL.update (toneClamped);
        toneR.update (toneClamped);
        const float wetL_toned = toneL.process (wetL_exp);
        const float wetR_toned = toneR.process (wetR_exp);

        // === Step 9: Width post-processing on wet output ===
        float wetOutL = wetL_toned;
        float wetOutR = wetR_toned;
        if (p.width == 0) // MONO — collapse output to center
        {
            const float wetMono = 0.5f * (wetL_toned + wetR_toned);
            wetOutL = wetOutR = wetMono;
        }
        else if (p.width == 1) // STEREO — M/S widening
        {
            const float mid  = 0.5f * (wetL_toned + wetR_toned);
            const float side = 0.5f * (wetL_toned - wetR_toned);
            const float wideSide = side * 1.6f;
            wetOutL = mid + wideSide;
            wetOutR = mid - wideSide;
        }
        // PING (width==2): cross-routing already applied to feedback; pass through.

        // === Step 10: Sidechain ducker on dry input ===
        const float dryAbsL = std::abs (left);
        const float dryAbsR = std::abs (right);
        duckEnvL += (dryAbsL > duckEnvL ? duckAttackCoef : duckReleaseCoef) * (dryAbsL - duckEnvL);
        duckEnvR += (dryAbsR > duckEnvR ? duckAttackCoef : duckReleaseCoef) * (dryAbsR - duckEnvR);
        const float envMax = std::max (duckEnvL, duckEnvR);
        const float envDb = 20.0f * std::log10 (std::max (envMax, 1.0e-6f));
        const float overFloor = std::max (0.0f, envDb + 40.0f);
        const float duckReductionDb = -overFloor * 0.45f * std::max (0.0f, std::min (1.0f, p.duck));
        const float duckGain = std::pow (10.0f, duckReductionDb / 20.0f);

        const float wetL_final = wetOutL * duckGain;
        const float wetR_final = wetOutR * duckGain;

        // === Step 11: Wet/dry mix ===
        left  = left  * (1.0f - p.mix) + wetL_final * p.mix;
        right = right * (1.0f - p.mix) + wetR_final * p.mix;
    }

private:
    static float computePreCutoff (float delayMs)
    {
        const float recon = computeReconCutoff (delayMs);
        return std::min (15000.0f, recon * 1.4f);
    }

    static float computeReconCutoff (float delayMs)
    {
        if (delayMs < 5.0f) delayMs = 5.0f;
        const float raw = 20000.0f * (50.0f / delayMs);
        return std::max (800.0f, std::min (12000.0f, raw));
    }

    static float bbdSaturate (float x, float drive)
    {
        const float d = std::max (0.4f, std::min (3.0f, drive));
        return std::tanh (x * d) / std::tanh (d);
    }

    static float lfoWaveform (int shape, float phase, float& sampleHold, float& chaos,
                              uint32_t& rng)
    {
        (void) rng;
        switch (shape)
        {
            case 0: return std::sin (phase * 6.2831853f);
            case 1: return 1.0f - 4.0f * std::abs (phase - 0.5f);
            case 2: return phase < 0.5f ? 1.0f : -1.0f;
            case 3: return 2.0f * phase - 1.0f;
            case 4: return 1.0f - 2.0f * phase;
            case 5: return sampleHold;
            case 6: return chaos;
            default: return 0.0f;
        }
    }

    static float nextRand01 (uint32_t& rng)
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<float> (rng & 0x7FFFFFFF) / static_cast<float> (0x7FFFFFFF) * 2.0f - 1.0f;
    }

    static float pitchSemitoneRatio (int pitchEnum)
    {
        switch (pitchEnum)
        {
            case 1: return 0.5f;                  // -12
            case 2: return std::pow (2.0f, -7.0f / 12.0f);
            case 3: return std::pow (2.0f,  7.0f / 12.0f);
            case 4: return 2.0f;                  // +12
            default: return 1.0f;
        }
    }

    static float readHermite (const float* buf, int size, int writePos, float delaySamples)
    {
        if (delaySamples < 1.0f) delaySamples = 1.0f;
        const float readPos = static_cast<float> (writePos) - delaySamples;
        const int   intPart = static_cast<int> (std::floor (readPos));
        const float fracPart = readPos - static_cast<float> (intPart);

        auto wrap = [size] (int idx)
        {
            while (idx < 0)     idx += size;
            while (idx >= size) idx -= size;
            return idx;
        };

        const float y0 = buf[static_cast<size_t> (wrap (intPart - 1))];
        const float y1 = buf[static_cast<size_t> (wrap (intPart))];
        const float y2 = buf[static_cast<size_t> (wrap (intPart + 1))];
        const float y3 = buf[static_cast<size_t> (wrap (intPart + 2))];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fracPart + c2) * fracPart + c1) * fracPart + c0;
    }

    static float readHermiteAbs (const float* buf, int size, float pos, float /*delayHint*/)
    {
        if (pos < 0.0f) pos += static_cast<float> (size) * std::ceil (-pos / static_cast<float> (size));
        const int   intPart = static_cast<int> (std::floor (pos));
        const float fracPart = pos - static_cast<float> (intPart);

        auto wrap = [size] (int idx)
        {
            while (idx < 0)     idx += size;
            while (idx >= size) idx -= size;
            return idx;
        };

        const float y0 = buf[static_cast<size_t> (wrap (intPart - 1))];
        const float y1 = buf[static_cast<size_t> (wrap (intPart))];
        const float y2 = buf[static_cast<size_t> (wrap (intPart + 1))];
        const float y3 = buf[static_cast<size_t> (wrap (intPart + 2))];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fracPart + c2) * fracPart + c1) * fracPart + c0;
    }

    double sr = 44100.0;
    std::vector<float> bufferL, bufferR;
    int bufSize = 0;
    int writePos = 0;
    float pitchReadPosL = 0.0f;
    float pitchReadPosR = 0.0f;
    LPBiquad reconL1, reconL2, reconR1, reconR2;
    LPBiquad preL, preR;
    Compander compL, compR;

    // Internal LFO state.
    float lfoPhase = 0.0f;
    float lfoSampleHold = 0.0f;
    float lfoChaos = 0.0f;
    uint32_t lfoRng = 12345;

    // Smoothed delay-time samples (per-sample one-pole, 5 ms).
    float smoothedDelaySamples = 0.0f;
    float delaySmoothCoef = 0.0f;

    // Sidechain ducker state (peak follower on dry input).
    float duckEnvL = 0.0f;
    float duckEnvR = 0.0f;
    float duckAttackCoef = 0.0f;
    float duckReleaseCoef = 0.0f;

    float freezeGain = 0.0f;
    float freezeSmoothCoef = 0.0f;

    // Tone tilt EQ (per channel).
    TiltShelf toneL, toneR;

    // Pitch-shift read pointer init flags.
    bool pitchReadInitL = false;
    bool pitchReadInitR = false;
};
