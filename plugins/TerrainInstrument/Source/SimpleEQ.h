#pragma once

#include <cmath>

//==============================================================================
// SimpleEQ: 3-band biquad EQ (header-only)
//
// Band 1: Low shelf  (freq: 30-500 Hz, gain: -12 to +12 dB)
// Band 2: Parametric peak (freq: 200-8000 Hz, gain: -12 to +12 dB, Q=0.707)
// Band 3: High shelf (freq: 2000-20000 Hz, gain: -12 to +12 dB)
//
// Coefficients from Audio EQ Cookbook (Robert Bristow-Johnson).
// Per-channel — instantiate one per audio channel.
//==============================================================================
class SimpleEQ
{
public:
    SimpleEQ() = default;

    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;
        reset();
    }

    void reset()
    {
        for (int i = 0; i < 3; ++i)
        {
            z1[i] = 0.0;
            z2[i] = 0.0;
        }
    }

    // Update coefficients for all 3 bands
    // lowFreq/midFreq/highFreq in Hz, gains in dB
    void updateCoefficients(float lowFreq, float lowGainDb,
                            float midFreq, float midGainDb,
                            float highFreq, float highGainDb)
    {
        calcLowShelf(0, static_cast<double>(lowFreq), static_cast<double>(lowGainDb));
        calcPeakEQ(1, static_cast<double>(midFreq), static_cast<double>(midGainDb), 0.707);
        calcHighShelf(2, static_cast<double>(highFreq), static_cast<double>(highGainDb));
    }

    // Process one sample through all 3 cascaded bands
    float processSample(float input)
    {
        double x = static_cast<double>(input);

        for (int i = 0; i < 3; ++i)
        {
            double y = b0[i] * x + z1[i];
            z1[i] = b1[i] * x - a1[i] * y + z2[i];
            z2[i] = b2[i] * x - a2[i] * y;
            x = y;
        }

        return static_cast<float>(x);
    }

private:
    void calcLowShelf(int band, double freq, double gainDb)
    {
        if (sr <= 0.0) return;

        const double A = std::sqrt(std::pow(10.0, gainDb / 20.0));
        const double w0 = 2.0 * M_PI * freq / sr;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double S = 1.0; // shelf slope
        const double alpha = sinw0 * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double sqrtA2alpha = 2.0 * std::sqrt(A) * alpha;

        double _a0 =        (A + 1.0) + (A - 1.0) * cosw0 + sqrtA2alpha;
        double _b0 =    A * ((A + 1.0) - (A - 1.0) * cosw0 + sqrtA2alpha);
        double _b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
        double _b2 =    A * ((A + 1.0) - (A - 1.0) * cosw0 - sqrtA2alpha);
        double _a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
        double _a2 =        (A + 1.0) + (A - 1.0) * cosw0 - sqrtA2alpha;

        // Normalize
        b0[band] = _b0 / _a0;
        b1[band] = _b1 / _a0;
        b2[band] = _b2 / _a0;
        a1[band] = _a1 / _a0;
        a2[band] = _a2 / _a0;
    }

    void calcPeakEQ(int band, double freq, double gainDb, double Q)
    {
        if (sr <= 0.0) return;

        const double A = std::sqrt(std::pow(10.0, gainDb / 20.0));
        const double w0 = 2.0 * M_PI * freq / sr;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * Q);

        double _a0 = 1.0 + alpha / A;
        double _b0 = 1.0 + alpha * A;
        double _b1 = -2.0 * cosw0;
        double _b2 = 1.0 - alpha * A;
        double _a1 = -2.0 * cosw0;
        double _a2 = 1.0 - alpha / A;

        b0[band] = _b0 / _a0;
        b1[band] = _b1 / _a0;
        b2[band] = _b2 / _a0;
        a1[band] = _a1 / _a0;
        a2[band] = _a2 / _a0;
    }

    void calcHighShelf(int band, double freq, double gainDb)
    {
        if (sr <= 0.0) return;

        const double A = std::sqrt(std::pow(10.0, gainDb / 20.0));
        const double w0 = 2.0 * M_PI * freq / sr;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double S = 1.0;
        const double alpha = sinw0 * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double sqrtA2alpha = 2.0 * std::sqrt(A) * alpha;

        double _a0 =        (A + 1.0) - (A - 1.0) * cosw0 + sqrtA2alpha;
        double _b0 =    A * ((A + 1.0) + (A - 1.0) * cosw0 + sqrtA2alpha);
        double _b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
        double _b2 =    A * ((A + 1.0) + (A - 1.0) * cosw0 - sqrtA2alpha);
        double _a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
        double _a2 =        (A + 1.0) - (A - 1.0) * cosw0 - sqrtA2alpha;

        b0[band] = _b0 / _a0;
        b1[band] = _b1 / _a0;
        b2[band] = _b2 / _a0;
        a1[band] = _a1 / _a0;
        a2[band] = _a2 / _a0;
    }

    double sr = 44100.0;

    // Biquad coefficients per band (0=low shelf, 1=peak, 2=high shelf)
    double b0[3] = { 1.0, 1.0, 1.0 };
    double b1[3] = { 0.0, 0.0, 0.0 };
    double b2[3] = { 0.0, 0.0, 0.0 };
    double a1[3] = { 0.0, 0.0, 0.0 };
    double a2[3] = { 0.0, 0.0, 0.0 };

    // Transposed Direct Form II state per band
    double z1[3] = { 0.0, 0.0, 0.0 };
    double z2[3] = { 0.0, 0.0, 0.0 };
};
