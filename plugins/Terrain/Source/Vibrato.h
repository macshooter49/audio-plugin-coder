#pragma once
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  Vibrato.h — tp55 · Terrain · the per-voice pitch LFO that replaced the note-on JITTER.
//
//  Max: "jitter isn't broken, it's the wrong feature — I want the wobble, like a Casio SK-1."
//  The old LayerState::pitchJitterCents sampled ONE random detune at note-on and held it for the
//  voice's life. That is a DE-PHASER for stacked layers, which is exactly why it did nothing
//  audible on a lone one-shot. This is a real periodic vibrato on the PLAYBACK RATE.
//
//  ══ WHY SEGMENTS, AND NOT A PER-SAMPLE sin() ══
//  A per-sample sin + exp2 across 4 layers x 32 voices is ~65 k transcendental PAIRS per block,
//  and this plugin just paid for a -26 % DSP pass (fb636). So the rate multiplier is evaluated on
//  a grid and LINEARLY RAMPED between the grid points; the voice's inner loop pays a shift, a
//  mask, two loads and a multiply-add, and only while vibrato is actually on.
//
//  🚨 THE GRID IS NOT THE BLOCK, AND THAT DISTINCTION IS THE WHOLE DESIGN. The first cut ramped
//  once across the WHOLE block and the cert caught it: a straight line across an arc of A radians
//  misses the sine by about A^2/8, so at the 12 Hz / 100-cent corner that is 8 CENTS on a 512
//  block and 31 CENTS on a 1024 — a vibrato visibly flattened towards a triangle. The error is
//  QUADRATIC in the segment, so cutting the segment to 64 samples divides it by 64: under a tenth
//  of a cent at the same corner, at a cost of ceil(N/64) transcendental pairs per block instead of
//  N. Both numbers are measured in Tests/vibrato_cert.cpp [3], across 64..4096-sample blocks.
//
//  The phase advances on WALL TIME (numSamples), never on the playhead, so a stretched, reversed
//  or scanned voice still wobbles at the rate the knob says.
//
//  Build the gate:  clang++ -std=c++17 -O2 -I Source Tests/vibrato_cert.cpp -o /tmp/vibcert
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <cmath>

namespace tw
{
    struct Vibrato
    {
        // Hard limits — the UI's knobs live inside these, and a preset from anywhere is clamped.
        static constexpr float kMaxDepthCents = 100.0f;
        static constexpr float kMinRateHz     = 0.05f;
        static constexpr float kMaxRateHz     = 12.0f;

        // The grid. kSegSamples is the target; a block longer than kMaxSegs * kSegSamples uses a
        // proportionally longer (still power-of-two) segment rather than allocating, so an 8192
        // sample block costs the same 64 evaluations as a 4096 one and stays well inside a cent.
        static constexpr int kSegSamples = 64;
        static constexpr int kSegShift0  = 6;      // log2(kSegSamples)
        static constexpr int kMaxSegs    = 64;

        /** What one block needs. When `active` is false the voice must take its untouched path —
         *  a parked vibrato has to be BIT-IDENTICAL to no vibrato at all. */
        struct Block
        {
            bool   active   = false;
            int    segShift = kSegShift0;            // segment length = 1 << segShift
            float  semisMid = 0.0f;                  // block centre as a pitch offset (warp path)
            double mul  [kMaxSegs + 1] = { 1.0 };    // rate multiplier at each segment's first sample
            double step [kMaxSegs + 1] = { 0.0 };    // its per-sample slope inside that segment
        };

        /** Note-on. Phase starts at ZERO — the sine's zero crossing — so a note begins at its true
         *  pitch and swings from there: arming vibrato can never put a step on a note-on. Starting
         *  at zero is also what keeps the render DETERMINISTIC; the old jitter pulled a random
         *  number here, and a null test cannot reproduce that. */
        void reset() noexcept { phase = 0.0; depthSmoothed = 0.0f; }

        /** One block's worth. `depthCents` is the PEAK swing, `rateHz` its speed.
         *  Returns a reference to this Vibrato's own plan — valid until the next advance(). */
        const Block& advance (float depthCents, float rateHz, double sampleRate, int numSamples) noexcept
        {
            blk.active   = false;
            blk.segShift = kSegShift0;
            blk.semisMid = 0.0f;
            blk.mul[0]   = 1.0;
            blk.step[0]  = 0.0;

            if (! (sampleRate > 0.0) || numSamples <= 0) return blk;

            const float d = depthCents < 0.0f ? 0.0f
                          : (depthCents > kMaxDepthCents ? kMaxDepthCents : depthCents);

            // One-pole on DEPTH so turning the knob during a held note slides the swing in
            // instead of stepping the pitch.
            depthSmoothed += (d - depthSmoothed) * kDepthSmoothing;

            if (! (depthSmoothed > kOffThresholdCents))
            {
                // Parked. Snap the smoother so re-arming starts from silence, and rewind the
                // phase so the next note begins at the zero crossing.
                depthSmoothed = d;
                phase         = 0.0;
                return blk;                     // the exact identity — see Block's defaults
            }

            const float r = rateHz < kMinRateHz ? kMinRateHz
                          : (rateHz > kMaxRateHz ? kMaxRateHz : rateHz);

            // Pick the grid: the smallest power-of-two segment >= kSegSamples that keeps the
            // segment count within kMaxSegs. Power of two so the voice indexes with a shift.
            int shift = kSegShift0;
            while (((numSamples + (1 << shift) - 1) >> shift) > kMaxSegs) ++shift;
            blk.segShift = shift;

            const int segLen = 1 << shift;
            const int nSegs  = (numSamples + segLen - 1) >> shift;   // <= kMaxSegs

            const double inc   = (double) r / sampleRate;            // cycles per sample
            const double semis = (double) depthSmoothed * 0.01;      // cents -> semitones (peak)
            const double k     = semis / 12.0;

            double p = phase;
            double m = std::exp2 (k * std::sin (p * kTwoPi));
            for (int sgi = 0; sgi < nSegs; ++sgi)
            {
                const double pNext = p + inc * (double) segLen;
                const double mNext = std::exp2 (k * std::sin (pNext * kTwoPi));
                blk.mul [sgi] = m;
                blk.step[sgi] = (mNext - m) / (double) segLen;
                p = pNext; m = mNext;
            }
            // One spare entry so a boundary index can never read past the plan.
            blk.mul [nSegs] = m;
            blk.step[nSegs] = 0.0;

            const double pEnd = phase + inc * (double) numSamples;
            blk.semisMid = (float) (semis * std::sin ((phase + pEnd) * 0.5 * kTwoPi));
            phase        = pEnd - std::floor (pEnd);   // keep the accumulator in [0, 1)
            blk.active   = true;
            return blk;
        }

        double phase         = 0.0;   // 0..1, advances on wall time
        float  depthSmoothed = 0.0f;  // one-pole on the depth knob, in cents
        Block  blk;

        static constexpr double kTwoPi             = 6.283185307179586476925286766559;
        static constexpr float  kDepthSmoothing    = 0.25f;   // per block
        static constexpr float  kOffThresholdCents = 0.01f;
    };
}
