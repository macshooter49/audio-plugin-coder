// SampleKeyDetect.h — automatic sample key / root-note detection for Terrain.
// ─────────────────────────────────────────────────────────────────────────────
// WHAT IT DOES
//   Given a decoded audio buffer (any channel count / sample rate), estimates the
//   sample's true monophonic fundamental, the nearest musical note, and the
//   base-tuning offset (in semitones) needed to SNAP that sample so it plays the
//   SAME C as Terrain's oscillators.
//
// WHY / HOW IT FITS TERRAIN'S TUNING
//   • Terrain's oscillators use A4 = MIDI 69 = 440 Hz  (freq = 440·2^((n-69)/12)),
//     and a loaded sample plays at its NATIVE pitch when the root key (MIDI 60) is
//     pressed (SynthVoice.h renderSampleOsc: noteSemis = glideNote-60+…; the chop
//     sampler: pitchSemitones = midi-rootMidiNote+…). So at the root key the
//     oscillator sounds C4 (261.63 Hz) while a raw sample sounds its recorded
//     fundamental — e.g. an A3 sample is 3 semitones flat of the oscillator C.
//   • The snap offset makes (detectedNote + offset) land on the nearest C, so the
//     sample's root key becomes an in-tune C, matching the oscillators. The offset
//     is the MINIMAL shift to the nearest C (range −6..+5 semitones) so timbre is
//     preserved as much as possible (no octave-jumping resample damage).
//
// ALGORITHM  (research: de Cheveigné & Kawahara, "YIN, a fundamental frequency
//   estimator for speech and music", JASA 2002 — the standard robust monophonic
//   pitch detector). Per analysis frame:
//     1. difference function                d(τ) = Σ (x[j] − x[j+τ])²
//     2. cumulative mean normalised diff    d'(τ) = d(τ)·τ / Σ_{k≤τ} d(k),  d'(0)=1
//     3. absolute threshold: first τ with d'(τ) < thresh, descended to its local
//        min (this picks the smallest true period → avoids sub-octave errors)
//     4. parabolic interpolation around that τ for sub-sample accuracy
//     5. f0 = sr / τ,  confidence = 1 − d'(τ)
//   The steady (post-transient) portion is analysed across several frames and the
//   median is taken for robustness. UN-PITCHED material (noise, phrases, pads with
//   no clear fundamental) never dips below the threshold → reported UNVOICED →
//   caller does NOTHING (offset 0), never mis-snapping.
//
// CPU / THREADING
//   One-time analysis on load. Header-only, JUCE-free (float pointers only) so it
//   is trivially unit-testable offline (see SampleKeyDetect_test.cpp) and is meant
//   to run OFF the audio thread (in the loader worker / on the message thread).
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <string>
#include "TerrainTuning.h"   // tp103

namespace tw
{
    struct SampleKeyResult
    {
        bool   voiced        = false;   // true ONLY if a clear pitched fundamental was found
        double frequencyHz   = 0.0;     // estimated fundamental (0 if unvoiced)
        int    midiNote      = -1;      // nearest MIDI note (A4 = 69 = 440 Hz); -1 if unvoiced
        double cents         = 0.0;     // signed deviation of f0 from midiNote, in cents [-50,50]
        double confidence    = 0.0;     // 0..1 periodicity (median 1−d'); 0 if unvoiced
        int    snapSemitones = 0;       // base-tuning offset to snap the root key to nearest C
                                        // (0 when unvoiced — a guaranteed no-op)
    };

    // Tuning knobs for the detector. Namespace-scope (not nested) so its default
    // member initializers are usable as default arguments below.
    struct SampleKeyParams
    {
        double minFreq         = 25.0;    // lowest fundamental considered (~G0)
        double maxFreq         = 4200.0;  // highest fundamental considered (~C8)
        double yinThreshold    = 0.15;    // absolute threshold for a periodic dip
        double minRms          = 3.0e-4;  // frames quieter than this are treated as silence
        int    maxFrames       = 5;       // analysis frames spread across the steady region
        double transientSkipS  = 0.030;   // skip this much past onset (seconds)
        double onsetRatio      = 0.15;    // onset = first |x| above this fraction of peak
        double maxAnalyseS     = 2.0;     // cap the analysed region to this many seconds
    };

    class SampleKeyDetector
    {
    public:
        using Params = SampleKeyParams;

        // ── Main entry: multi-channel buffer (array of channel pointers). ─────────
        static SampleKeyResult detect (const float* const* channels, int numChannels,
                                       int numSamples, double sampleRate,
                                       const Params& p = Params{})
        {
            SampleKeyResult none;
            if (channels == nullptr || numChannels <= 0 || numSamples <= 0 || sampleRate <= 0.0)
                return none;

            // ── locate the onset so we can skip the attack transient (cheap scan). ──
            double peak = 0.0;
            for (int c = 0; c < numChannels; ++c)
            {
                const float* x = channels[c];
                if (x == nullptr) continue;
                for (int i = 0; i < numSamples; ++i)
                    peak = std::max (peak, (double) std::fabs (x[i]));
            }
            if (peak < p.minRms) return none;   // effectively silent → no-op

            const double onsetThresh = p.onsetRatio * peak;
            int onset = 0;
            for (int i = 0; i < numSamples; ++i)
            {
                double a = 0.0;
                for (int c = 0; c < numChannels; ++c)
                    if (channels[c] != nullptr) a = std::max (a, (double) std::fabs (channels[c][i]));
                if (a >= onsetThresh) { onset = i; break; }
            }

            int regionStart = std::min (numSamples - 1,
                                        onset + (int) (p.transientSkipS * sampleRate));
            if (regionStart < 0) regionStart = 0;

            int regionLen = numSamples - regionStart;
            const int maxAnalyse = (int) (p.maxAnalyseS * sampleRate);
            if (maxAnalyse > 0) regionLen = std::min (regionLen, maxAnalyse);
            if (regionLen < 8) { regionStart = 0; regionLen = numSamples; }

            // Build a mono mixdown of just the region we analyse (keeps memory tiny).
            std::vector<float> mono ((size_t) regionLen, 0.0f);
            const double norm = 1.0 / (double) numChannels;
            for (int c = 0; c < numChannels; ++c)
            {
                const float* x = channels[c];
                if (x == nullptr) continue;
                for (int i = 0; i < regionLen; ++i)
                    mono[(size_t) i] += (float) (x[regionStart + i] * norm);
            }

            return detectMono (mono.data(), regionLen, sampleRate, p);
        }

        // ── Convenience: a single mono channel. ──────────────────────────────────
        static SampleKeyResult detect (const float* mono, int numSamples,
                                       double sampleRate, const Params& p = Params{})
        {
            const float* chans[1] = { mono };
            return detect (chans, 1, numSamples, sampleRate, p);
        }

        // ── Core: analyse an already-mono, already-trimmed region. ───────────────
        static SampleKeyResult detectMono (const float* mono, int numSamples,
                                           double sampleRate, const Params& p = Params{})
        {
            SampleKeyResult none;
            if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0) return none;

            int maxTau = (int) (sampleRate / std::max (1.0, p.minFreq));
            int minTau = std::max (2, (int) (sampleRate / std::max (1.0, p.maxFreq)));

            // Fit the frame to the available audio: need frameLen = 2·maxTau samples.
            if (2 * maxTau > numSamples) maxTau = numSamples / 2;
            if (maxTau <= minTau + 2) return none;   // too short for even the highest note
            const int frameLen = 2 * maxTau;
            const int W        = maxTau;             // integration window (lags 0..maxTau)

            const int lastFrameStart = numSamples - frameLen;
            if (lastFrameStart < 0) return none;

            const int nFrames = std::max (1, std::min (p.maxFrames, (lastFrameStart / std::max (1, W)) + 1));

            std::vector<double> freqs;
            std::vector<double> confs;
            freqs.reserve ((size_t) nFrames);
            confs.reserve ((size_t) nFrames);

            std::vector<double> d ((size_t) maxTau + 1, 0.0);
            std::vector<double> dp ((size_t) maxTau + 1, 0.0);

            for (int f = 0; f < nFrames; ++f)
            {
                const int start = (nFrames == 1) ? 0
                                                 : (int) ((long long) lastFrameStart * f / (nFrames - 1));
                const float* x = mono + start;

                // frame energy → skip near-silent frames.
                double e = 0.0;
                for (int i = 0; i < frameLen; ++i) e += (double) x[i] * x[i];
                if (std::sqrt (e / frameLen) < p.minRms) continue;

                double f0 = 0.0, conf = 0.0;
                if (yinFrame (x, W, minTau, maxTau, sampleRate, p.yinThreshold, d, dp, f0, conf))
                {
                    if (f0 >= p.minFreq && f0 <= p.maxFreq)
                    {
                        freqs.push_back (f0);
                        confs.push_back (conf);
                    }
                }
            }

            // Voiced only with a majority of usable frames agreeing.
            if ((int) freqs.size() * 2 < nFrames || freqs.empty())
                return none;

            const double f0   = median (freqs);
            const double conf = median (confs);

            SampleKeyResult r;
            r.voiced      = true;
            r.frequencyHz = f0;
            r.confidence  = conf;

            const double midf = 69.0 + 12.0 * std::log2 (f0 / wc::tuningA4HzD());   // tp103 — the nearest note ON THE A4 GRID (440 → unchanged)
            r.midiNote      = (int) std::lround (midf);
            r.cents         = (midf - (double) r.midiNote) * 100.0;
            r.snapSemitones = snapToNearestC (r.midiNote);
            return r;
        }

        // ── Snap a MIDI note to the nearest C: minimal signed shift, range −6..+5. ─
        static int snapToNearestC (int midiNote) noexcept
        {
            int pc = ((midiNote % 12) + 12) % 12;   // 0 = C
            return (pc <= 6) ? -pc : (12 - pc);
        }

        // ── "A3", "C#4", … (scientific naming: MIDI 60 = C4, A4 = 69 = 440 Hz). ──
        static std::string noteName (int midiNote)
        {
            static const char* names[12] =
                { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
            if (midiNote < 0) return "-";
            const int pc  = ((midiNote % 12) + 12) % 12;
            const int oct = midiNote / 12 - 1;
            return std::string (names[pc]) + std::to_string (oct);
        }

    private:
        // One YIN frame. Returns true (voiced) + f0/conf when d' dips below thresh.
        static bool yinFrame (const float* x, int W, int minTau, int maxTau,
                              double sampleRate, double threshold,
                              std::vector<double>& d, std::vector<double>& dp,
                              double& f0Out, double& confOut)
        {
            // 1) difference function d(τ)
            d[0] = 0.0;
            for (int tau = 1; tau <= maxTau; ++tau)
            {
                double sum = 0.0;
                for (int j = 0; j < W; ++j)
                {
                    const double diff = (double) x[j] - (double) x[j + tau];
                    sum += diff * diff;
                }
                d[(size_t) tau] = sum;
            }

            // 2) cumulative mean normalised difference d'(τ)
            dp[0] = 1.0;
            double running = 0.0;
            for (int tau = 1; tau <= maxTau; ++tau)
            {
                running += d[(size_t) tau];
                dp[(size_t) tau] = (running > 0.0) ? d[(size_t) tau] * (double) tau / running : 1.0;
            }

            // 3) absolute threshold → first dip, descended to its local minimum.
            int tauEst = -1;
            for (int tau = std::max (1, minTau); tau <= maxTau; ++tau)
            {
                if (dp[(size_t) tau] < threshold)
                {
                    while (tau + 1 <= maxTau && dp[(size_t) (tau + 1)] < dp[(size_t) tau])
                        ++tau;
                    tauEst = tau;
                    break;
                }
            }
            if (tauEst < 0) return false;   // no periodic dip → unvoiced

            // 4) parabolic interpolation around tauEst (sub-sample period).
            double betterTau = (double) tauEst;
            if (tauEst > minTau && tauEst < maxTau)
            {
                const double s0 = dp[(size_t) (tauEst - 1)];
                const double s1 = dp[(size_t) tauEst];
                const double s2 = dp[(size_t) (tauEst + 1)];
                const double denom = (s0 + s2 - 2.0 * s1);
                if (std::fabs (denom) > 1e-12)
                {
                    double delta = 0.5 * (s0 - s2) / denom;
                    if (delta > 1.0)  delta = 1.0;
                    if (delta < -1.0) delta = -1.0;
                    betterTau = (double) tauEst + delta;
                }
            }
            if (betterTau < 1.0) return false;

            f0Out   = sampleRate / betterTau;
            confOut = std::max (0.0, std::min (1.0, 1.0 - dp[(size_t) tauEst]));
            return true;
        }

        static double median (std::vector<double> v)
        {
            if (v.empty()) return 0.0;
            std::sort (v.begin(), v.end());
            const size_t n = v.size();
            return (n & 1u) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
        }
    };
}
