// SignalsmithEngine.h
//
// Wraps signalsmith::stretch::SignalsmithStretch<float> with a sampler-voice
// friendly interface. Configured for TONES character — formants preserved at
// all pitch shifts. One instance owned per voice via WarpProcessor; lazily
// constructed when the voice first dispatches a non-NONE warp mode.
//
// RT-safety: process() is allocation-free. prepare() and reset() may allocate
// — call only from the audio thread at note-on boundaries.
//
// Latency: spectral stretchers introduce inherent latency. Signalsmith reports
// it via inputLatency() / outputLatency(). At note-on we reset() so the engine
// starts cold for each trigger — small bit of "pre-roll" silence is acceptable
// for one-shot sampler use.
//
#pragma once
#include <vector>

#include <juce_core/juce_core.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>
#include <cstring>

namespace tw
{
    class SignalsmithEngine
    {
    public:
        SignalsmithEngine() = default;

        /** Allocates internal buffers. Call once before audio thread touches the instance. */
        void prepare (double sampleRateHz, int numChannels, int /*blockSize*/)
        {
            sampleRate = sampleRateHz;
            channels   = juce::jlimit (1, 2, numChannels);

            // Configure stretcher for TONES character. We use presetCheaper (NOT
            // presetDefault) deliberately (Max's all-4-oscs CPU fix, 2026-07-01):
            //   • smaller FFT block (100 ms vs 120 ms) + larger hop (40 ms vs 30 ms)
            //     → ~40 % less average STFT cost, and
            //   • splitComputation=true → the FFT work is SPREAD across process() calls
            //     instead of dumped on one hop, which FLATTENS the per-block CPU spikes.
            // That's what lets STRETCH/FORMANT be turned up on all 4 oscillators (each
            // owns a phase-vocoder) without a CPU spike. Near-identical on sustained
            // sample material. setFormantFactor(1.0) keeps formants correct at any pitch.
            //
            // 🔇 CHOP-STRETCH (highOverlap) — presetCheaper's 100 ms block / 40 ms hop is a 2.5× overlap,
            // and at any ratio ≠ 1 the synthesis overlap-add is not smooth enough at that density: every
            // hop leaves a faint broadband tick (measured by Source/ChopStretch_test.cpp: a band-limited
            // pad stretched 2× through the chop voice → one HF click event per 40 ms hop, 50 in 2 s, to
            // −18 dB of the local level; the crackle Max hears on stretched chops). Same 100 ms block at a
            // 25 ms hop (4× overlap) removes the hop-rate ticks (49 → 2 events, the 2 being the source's
            // own start/end). Transients need a little more: a drum loop stretched 2× still left a splat
            // on every kick at 4× (11 events); a 20 ms hop (5× overlap) takes it to 1. Cost, measured on
            // an M-series core: 0.56 % → 0.97 % of one core per warped voice. The CHOP voice opts in, and
            // (SYNTH-STRETCH) so does the synth's Sample oscillator — measured on the real processor by
            // Tests/synthstretch_cert.cpp. The cost is only paid while an osc is actually warping.
            //
            // 🔇 SYNTH-STRETCH (longWindow) — the synth's Sample osc runs the same 5× overlap on a 150 ms block / 30 ms hop.
            // The 100 ms / 20 ms pair LOST LOW PARTIALS: Signalsmith's vertical phase smoothing spans fftSize/interval
            // bins, and at 5× on a 100 ms block that reach swallowed partials under ~130 Hz — measured (the same
            // band-limited sources as the harnesses): a 49 Hz vocal stretched 1.25× lost 19.5 dB at 100 Hz (44 dB
            // worst band), a 55 Hz pad at 1.5× lost 15 dB at 100 Hz; notes the synth plays low all the time. The
            // 150 ms block holds those partials (≤ 2.3 dB worst band, most ≤ 0.5) and is as tick-free (> 7.5 kHz
            // artefact −94…−135 dB of the signal vs −52…−63 dB at presetCheaper). Same per-second bin work as 100/20
            // (33 hops/s × 3600 bins vs 50 × 2400); measured on the real processor (synthstretch_cert cpu): 0.54 % →
            // ~1.1 % of one core per warped Tones voice, Texture 0.41 → 0.74 %. Its latency is longer, though, and the
            // CHOP voice pays latency on short slices (Tests/chopstretch_gate.sh at 150/30: TONES/short fidelity 2.1 →
            // 3.4 dB), so the chop keeps 100/20. The synth primes past the latency from the sample's look-ahead (fb642).
            // Formant-only (ratio 1) needs it as much as a stretch: at presetCheaper it ticked too (337 HF events / 4 notes).
            if (highOverlap)
                stretcher.configure (channels, (int) std::round (sampleRate * (longWindow ? 0.150 : 0.100)),
                                     (int) std::round (sampleRate * (longWindow ? 0.030 : 0.020)), /*splitComputation*/ true);
            else
                stretcher.presetCheaper (channels, (float) sampleRate);
            stretcher.setFormantFactor (formantFactor);

            // fb642 — outputSeek() sizes two internal scratch vectors the first time it runs. Run it ONCE here, off the
            // audio thread, so a note-on never allocates; then reset back to silence.
            {
                const int n = juce::jmax (1, stretcher.inputLatency() + stretcher.outputLatency());
                std::vector<float> z ((size_t) n, 0.0f);
                const float* in[2] = { z.data(), z.data() };
                stretcher.outputSeek (in, n);
                stretcher.reset();
            }
            // SYNTH-STRETCH — our outputSeek() renders the output latency into these and discards it (sized here, off
            // the audio thread); then one run of it so any scratch the seek/process path sizes lazily exists too.
            discardL.assign ((size_t) juce::jmax (1, stretcher.outputLatency()), 0.0f);
            discardR.assign ((size_t) juce::jmax (1, stretcher.outputLatency()), 0.0f);
            {
                ready = true;
                const int n = juce::jmax (1, outputSeekLength());
                std::vector<float> z ((size_t) n, 0.0f);
                outputSeek (z.data(), z.data(), n);
                stretcher.reset();
            }

            ready = true;
        }

        /** Resets internal state to silence. Must be called at note-on to prevent voice bleed. */
        void reset()
        {
            if (! ready) return;
            stretcher.reset();
        }

        bool isReady() const noexcept { return ready; }

        /** 5× STFT overlap instead of presetCheaper's 2.5× (see prepare); longWin = the 150 ms block (the synth's).
         *  Takes effect at the next prepare() — set it BEFORE preparing. Default false = presetCheaper. */
        void setHighOverlap (bool b, bool longWin = false) noexcept { highOverlap = b; longWindow = longWin; }

        void setStretchRatio (float r) noexcept
        {
            stretchRatio = juce::jlimit (0.1f, 15.0f, r);
        }

        // SAMPLE-ENGINE-FORMANT — formant shift factor (1.0 = neutral, 2.0 = +1 oct).
        void setFormantFactor (float f) noexcept
        {
            formantFactor = juce::jlimit (0.25f, 4.0f, f);
            if (ready) stretcher.setFormantFactor (formantFactor);
        }
        void setPitchSemitones (float semis) noexcept
        {
            pitchSemitones = juce::jlimit (-24.0f, 24.0f, semis);
        }

        /** Input latency in samples — caller should prime this many input
         *  samples via seek() at note-on so the first process() call has
         *  meaningful output. */
        int inputLatency() const noexcept
        {
            return const_cast<signalsmith::stretch::SignalsmithStretch<float>&>(stretcher).inputLatency();
        }

        /** CHOP-STRETCH — synthesis-side latency (samples). */
        int outputLatency() const noexcept
        {
            return const_cast<signalsmith::stretch::SignalsmithStretch<float>&>(stretcher).outputLatency();
        }

        /** Prime the engine with input samples after reset(). Call this once
         *  at note-on with the FIRST inputLatency() source samples so the
         *  engine's STFT buffer is populated before any process() call. Without
         *  this, the first outputLatency() samples of output() are silent
         *  ramp/garbage.
         *
         *  Buffers must be distinct from any future process() call buffers. */
        void seek (const float* primeL, const float* primeR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;
            const float* inputs[2] = { primeL, channels == 2 ? primeR : primeL };
            stretcher.seek (inputs, numSamples, 1.0f /*playbackRateHint*/);
        }

        /** fb642 — ZERO-LATENCY START. Max: "the formant adds latency to the sample … we do not want the formant to add
         *  latency." The phase vocoder's latency (inputLatency + outputLatency, ~100 ms of STFT at presetCheaper) is
         *  a property of processing a LIVE stream. A sample is not live: its future is already in memory. So the voice
         *  reads outputSeekLength() source samples AHEAD and hands them to outputSeek(), which (Signalsmith's own API)
         *  resets, seeks and pre-computes the pre-roll — the NEXT process() output is aligned to the FIRST of those
         *  samples. The latency is paid in advance, once, at note-on, instead of being heard. */
        int outputSeekLength() const noexcept
        {
            if (! ready) return 0;
            auto& s = const_cast<signalsmith::stretch::SignalsmithStretch<float>&> (stretcher);
            return (int) std::ceil ((double) s.inputLatency() + (double) s.outputLatency() / (double) stretchRatio);
        }
        void outputSeek (const float* primeL, const float* primeR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;
            stretcher.setTransposeSemitones (pitchSemitones);
            // SYNTH-STRETCH — the SAME alignment as Signalsmith's own outputSeek (the next output = the first primed
            // sample), built the way the chop's render cache does it (tp101, WarpProcessor::renderFullSlice): seek the
            // first inputLatency() samples into the analysis history, then RENDER the output latency from the rest and
            // throw it away. Signalsmith's outputSeek instead synthesises that pre-roll, time-reverses and negates it and
            // adds it under the first outputs — at any rate ≠ 1 that seam left a broadband tick ~9 ms into every warped
            // note and every mid-note engage (Tests/synthstretch_cert.cpp: one HF event per note, to −50 dBFS; tp101
            // measured the same seam as a one-sample d2 spike). Same work: the pre-roll was a process() call too.
            stretcher.reset();
            const int inLat = stretcher.inputLatency();
            const int seekN = juce::jmin (numSamples, inLat);
            const float rateHint = 1.0f / juce::jmax (0.0001f, stretchRatio);
            {
                const float* inputs[2] = { primeL, channels == 2 ? primeR : primeL };
                stretcher.seek (inputs, seekN, rateHint);
            }
            const int rest   = numSamples - seekN;
            const int outLat = juce::jmin (stretcher.outputLatency(), (int) discardL.size());
            if (rest > 0 && outLat > 0)
            {
                const float* inputs[2]  = { primeL + seekN, (channels == 2 ? primeR : primeL) + seekN };
                float*       outputs[2] = { discardL.data(), discardR.data() };
                stretcher.process (inputs, rest, outputs, outLat);
            }
        }

        /** Process numSamples of audio.
         *
         *  Convention: stretchRatio > 1.0 means the chop plays LONGER — output
         *  duration > input duration. So for a given number of output samples,
         *  we consume LESS input. Formula: inputLen = outputLen / stretchRatio.
         *
         *  Examples:
         *    stretchRatio = 2.0 → consume 0.5x input per output sample (slower)
         *    stretchRatio = 0.5 → consume 2.0x input per output sample (faster)
         *    stretchRatio = 1.0 → transparent (input == output rate)
         *
         *  IMPORTANT: input and output buffers MUST be distinct. Per Signalsmith
         *  API contract: "The input/output buffers cannot be the same." Earlier
         *  versions of this wrapper aliased input=output and produced silent
         *  output — that bug is fixed by ensuring SamplerVoice passes separate
         *  scratch buffer pairs.
         */
        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples)
        {
            if (! ready || numSamples <= 0) return;
            jassert (inL != outL && inR != outR && "Signalsmith requires distinct input/output buffers");

            stretcher.setTransposeSemitones (pitchSemitones);

            // Pointer-of-channels arrays for signalsmith's templated API.
            const float* inputs [2]  = { inL, channels == 2 ? inR : inL };
            float*       outputs[2] = { outL, channels == 2 ? outR : outL };

            // inputLen < outputLen when stretchRatio > 1.0 → audio plays longer.
            const int inputLen = (int) std::round ((double) numSamples / (double) stretchRatio);

            stretcher.process (inputs, inputLen, outputs, numSamples);

            // Mono source → duplicate L to R for stereo voice path.
            if (channels == 1 && outR != outL)
                std::memcpy (outR, outL, sizeof (float) * (size_t) numSamples);
        }

    private:
        signalsmith::stretch::SignalsmithStretch<float> stretcher;
        std::vector<float> discardL, discardR;   // SYNTH-STRETCH — the discarded output latency of outputSeek()
        double sampleRate     = 48000.0;
        int    channels       = 2;
        float  stretchRatio   = 1.0f;
        float  pitchSemitones = 0.0f;
        float  formantFactor  = 1.0f;   // SAMPLE-ENGINE-FORMANT
        bool   ready          = false;
        bool   highOverlap    = false;  // CHOP-STRETCH — 5× overlap (chop voices: 100 ms / 20 ms)
        bool   longWindow     = false;  // SYNTH-STRETCH — with highOverlap: 150 ms / 30 ms (the synth's Sample osc)
    };
}
