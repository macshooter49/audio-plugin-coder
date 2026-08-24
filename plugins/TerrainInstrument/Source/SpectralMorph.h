#pragma once
//
//  SpectralMorph.h — Terrain Instrument
//  ─────────────────────────────────────────────────────────────────────────
//  Frequency-domain "spectral morph" for the wavetable oscillator. This is the
//  REAL spectral slot (it replaces the old per-OSC FX rack that was mislabeled
//  "spectral"). Each mode reshapes a frame's harmonic spectrum, and the result
//  is handed to Wavetable::buildFromSpec — which band-limits across mips and
//  snaps any inharmonic partials onto the harmonic grid (the Batch-2 foundation).
//
//  DESIGN — built for the modulation future (north star: every knob modulated):
//    • apply() is a PURE, STATELESS, amount-parameterized transform.
//        morphed = SpectralMorph::apply(baseSpec, mode, amount)
//    • v1 ships PER-OSC: one morphed table per oscillator, rebuilt when the
//      shape / mode / amount changes, shared by all voices. That delivers 100%
//      of the knob-turn magic.
//    • PER-VOICE later (with the LFO/mod-matrix phase) is a drop-in: call the
//      SAME apply() per voice with that voice's modulated amount and cache the
//      result per voice. The transform never changes — only where `amount`
//      comes from. Nothing here bakes a knob value in as a constant.
//
//  HONEST single-cycle note: a looped single-cycle wavetable is harmonic by
//  construction, so "sliding a partial" = its energy redistributing across the
//  harmonic grid as amount changes (the snap splits a non-integer ratio across
//  adjacent bins, centroid = the true ratio). That continuous spectral sweep is
//  exactly how a single-cycle wavetable synth (incl. Vital) actually behaves —
//  a timbral morph, not a literal microtonal glide of each partial.
//
#include "Wavetable.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace tw
{
    // Curated set (our own order — not a copy of Vital's menu). Indices are stable and
    // map 1:1 to the dropdown / param. ALL modes are implemented; each transforms a
    // DIFFERENT axis of the spectrum so they stay distinct from one another.
    enum class SpectralMode
    {
        None = 0,
        HarmonicStretch,      // spread + brighten   → bright / airy / screaming
        InharmonicStretch,    // power-law spacing   → bell / metallic / clang
        Vocode,               // formant envelope    → vocal / vowel sweep
        Smear,                // blur + phase scatter→ soft / washy / pad
        RandomAmplitudes,     // seeded gain re-roll → mutant / alien (per-preset signature)
        DataCompress,         // quantize + decimate → digital / lo-fi / destroyed
        SpectralPhaser,       // swept comb notches  → hollow / phaser
        Disperse,             // quadratic phase     → chirped / dense / smeared-in-time
        Count
    };

    class SpectralMorph
    {
    public:
        /** Pure transform: base spectrum + mode + amount(0..1) → morphed WavetableSpec
         *  ready for Wavetable::buildFromSpec. amount<=0 or None returns the base
         *  untouched (and the morph is continuous from there — at amount→0 the stretch
         *  factor → 1, so partials land back on the integer grid losslessly). */
        /** fb472 — Lo/Hi are a LOW CUT and a HIGH CUT on the wavetable, taken in HARMONIC NUMBER.
         *  Max: "wire in the low and the high cut in the back channel where it says low and high,
         *  because I thought that's what it was for." They were a partial WINDOW on the morph
         *  (fb467) — an abstraction nobody asked for, on two knobs whose labels promised a filter.
         *
         *  Both are FOURTH-ORDER BUTTERWORTH, the order Serum 2 uses on its spectral Lo/Hi markers
         *  and the one fb470 gave the rack EQ, so a cut is a cut wherever you meet one here:
         *      |H(r)| = (r/rc)^4 / sqrt(1 + (r/rc)^8)      LOW cut  (high-pass)
         *      |H(r)| = 1        / sqrt(1 + (r/rc)^8)      HIGH cut (low-pass)
         *  In harmonic number, so the corner RIDES THE NOTE — Serum's markers are in hertz and do
         *  not track. Amplitude-only, so it composes with Disperse (phase-only) and with every mode.
         *
         *  🚨 A CUT APPLIES WITH mode = None. That is the whole point of it living on its own knobs
         *     instead of occupying the single mode slot, and it is why this no longer returns `base`
         *     on `mode == None`.
         *
         *  cutLo01 / cutHi01 are the knobs' NORMALISED positions. Both ends are an EXACT identity:
         *  Lo at 0 and Hi at 1 skip the filter entirely rather than parking a corner at harmonic 1,
         *  where a fourth-order response would still be -3 dB on the fundamental. */
        static constexpr float kCutLoBase = 0.25f, kCutLoOct = 11.0f;   // 0 -> h0.25 (off) .. 1 -> h512
        static constexpr float kCutHiBase = 0.5f,  kCutHiOct = 13.0f;   // 0 -> h0.5      .. 1 -> h4096 (off)

        static float cutLoCorner (float t) noexcept { return kCutLoBase * std::pow (2.0f, kCutLoOct * std::clamp (t, 0.0f, 1.0f)); }
        static float cutHiCorner (float t) noexcept { return kCutHiBase * std::pow (2.0f, kCutHiOct * std::clamp (t, 0.0f, 1.0f)); }
        static bool  cutLoOn     (float t) noexcept { return t > 0.0f; }
        static bool  cutHiOn     (float t) noexcept { return t < 1.0f; }

        static WavetableSpec apply (const WavetableSpec& base, SpectralMode mode, float amount,
                                    float cutLo01 = 0.0f, float cutHi01 = 1.0f)
        {
            amount = std::clamp (amount, 0.0f, 1.0f);
            const bool morphing = (mode != SpectralMode::None && amount > 0.0f);
            const bool cutting  = cutLoOn (cutLo01) || cutHiOn (cutHi01);
            if (! morphing && ! cutting)
                return base;

            // fb467 — the band top, computed ONCE for the whole spec. Disperse's coefficient is
            // normalised by it; a per-frame value would make each frame disperse differently and the
            // WT-POS sweep would go lumpy. Frames of one table are one instrument; they share a band.
            const float top = bandTop (base);

            WavetableSpec out;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
                morphFrame (base.frames[(size_t) f], out.frames[(size_t) f],
                            morphing ? mode : SpectralMode::None, amount, top, cutLo01, cutHi01);
            return out;
        }

    private:
        // fb467 — the highest ratio carrying real energy anywhere in the spec (-60 dB of the spec's
        // own peak). Disperse needs it because the SAME musical amount of dispersion is a 21x
        // different coefficient on a 24-harmonic table and a 511-harmonic one — measured, fb467:
        // parameterising by the raw coefficient made the knob a lottery, parameterising by CYCLES OF
        // SPREAD made it behave the same on every table.
        static float bandTop (const WavetableSpec& s) noexcept
        {
            float mx = 0.0f;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const FrameSpec& fs = s.frames[(size_t) f];
                if (fs.numPartials > 0)
                    for (int i = 0; i < fs.numPartials; ++i) mx = std::max (mx, std::abs (fs.partials[(size_t) i].amp));
                else
                    for (int h = 1; h <= fs.numHarmonics; ++h) mx = std::max (mx, std::abs (fs.amplitudes[(size_t) (h - 1)]));
            }
            const float thr = mx * 1.0e-3f;
            float top = 1.0f;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
            {
                const FrameSpec& fs = s.frames[(size_t) f];
                if (fs.numPartials > 0)
                { for (int i = 0; i < fs.numPartials; ++i)
                    if (std::abs (fs.partials[(size_t) i].amp) > thr) top = std::max (top, fs.partials[(size_t) i].ratio); }
                else
                { for (int h = 1; h <= fs.numHarmonics; ++h)
                    if (std::abs (fs.amplitudes[(size_t) (h - 1)]) > thr) top = std::max (top, (float) h); }
            }
            return top;
        }

        // Read a frame's spectrum as a unified (ratio, amp, phase) partial list, whether
        // it was authored as integer harmonics OR inharmonic partials. Every mode then
        // operates on this one representation, and we write it back as partials so
        // buildFromSpec's energy-preserving snap + band-limiting handles the rest.
        static int extract (const FrameSpec& f, FrameSpec::Partial* p) noexcept
        {
            int n = 0;
            if (f.numPartials > 0)
            {
                for (int i = 0; i < f.numPartials && n < FrameSpec::kMaxPartials; ++i)
                    if (f.partials[(size_t) i].amp != 0.0f)
                        p[n++] = f.partials[(size_t) i];
            }
            else
            {
                for (int h = 1; h <= f.numHarmonics && n < FrameSpec::kMaxPartials; ++h)
                {
                    const float a = f.amplitudes[(size_t) (h - 1)];
                    if (a != 0.0f)
                        p[n++] = { (float) h, a, f.phases[(size_t) (h - 1)] };
                }
            }
            return n;
        }

        static void writeBack (FrameSpec& out, const FrameSpec::Partial* p, int n) noexcept
        {
            out.numHarmonics = 0;                       // force the partial path in buildFromSpec
            out.numPartials   = std::min (n, FrameSpec::kMaxPartials);
            for (int i = 0; i < out.numPartials; ++i)
                out.partials[(size_t) i] = p[i];
        }

        static void morphFrame (const FrameSpec& in, FrameSpec& out, SpectralMode mode, float amount,
                                float bandTopR, float cutLo01, float cutHi01) noexcept
        {
            FrameSpec::Partial p[FrameSpec::kMaxPartials];
            const int n = extract (in, p);

            switch (mode)
            {
                case SpectralMode::HarmonicStretch:
                {
                    // Aggressive, WARP/FOLD-level drama. Two things happen as the knob opens:
                    //  (1) SPREAD — even spacing widens hard: ratio r → 1 + (r−1)·s, s:1→6,
                    //      so 1,2,3,4… fans to 1,7,13,19… (energy rockets up the spectrum).
                    //  (2) BRIGHTEN — partials are boosted ∝ ratio^(bright·amount), which
                    //      cancels the natural 1/h rolloff and pushes the spectrum toward a
                    //      flat, buzzing, brilliant tone. Without this the spread just thinned
                    //      out (RMS halved); with it the morph gets LOUDER and brighter — the
                    //      "night and day" turn. amount→0 ⇒ s→1, bright→0 ⇒ exact identity.
                    const float s      = 1.0f + 5.5f * amount;     // fb251 — spread 1 → 6.5 (was 4): harder fan, night-and-day
                    const float bright = 1.15f * amount;            // fb251 — brightness 0 → 1.15 (was 0.9): more brilliance
                    for (int i = 0; i < n; ++i)
                    {
                        const float newR = 1.0f + (p[(size_t) i].ratio - 1.0f) * s;
                        p[(size_t) i].ratio = newR;
                        p[(size_t) i].amp  *= std::pow (newR, bright);
                    }
                    break;
                }

                case SpectralMode::InharmonicStretch:
                {
                    // Power-law spacing → genuinely inharmonic (bell / struck-metal / gong),
                    // now pushed hard: ratio' = ratio^p, p:1→2.6 — the partials smear into a
                    // dense clangorous cluster. A brightness boost ∝ ratio^(bright·amount)
                    // keeps the metal RINGING and present instead of dull. The fundamental
                    // (ratio 1) stays pinned; everything above explodes outward + up.
                    const float pw     = 1.0f + 2.3f * amount;      // fb251 — exponent 1 → 3.3 (was 2.6): denser, clangier
                    const float bright = 1.05f * amount;            // fb251 — brightness 0 → 1.05 (was 0.8): rings brighter
                    for (int i = 0; i < n; ++i)
                    {
                        const float newR = std::pow (p[(size_t) i].ratio, pw);
                        p[(size_t) i].ratio = newR;
                        p[(size_t) i].amp  *= std::pow (newR, bright);
                    }
                    break;
                }

                case SpectralMode::Vocode:
                {
                    // THE TALKER — imposes a vocal-tract FORMANT envelope and sweeps the
                    // vowel as the knob opens: /u/ → /o/ → /a/ → /e/ → /i/ (dark → bright).
                    // Cardinal-vowel formants, male register (Hillenbrand et al. 1995). The
                    // partials keep their pitch; their LOUDNESS is reshaped into resonant
                    // peaks. depth ramps 0→1, so amount 0 = dry (identity).
                    static const float VF[5][3] = {   // F1, F2, F3 (Hz) per vowel
                        { 378.0f,  997.0f, 2343.0f },  // /u/  "oo"
                        { 497.0f,  910.0f, 2459.0f },  // /o/  "oh"
                        { 768.0f, 1333.0f, 2522.0f },  // /a/  "ah"
                        { 580.0f, 1799.0f, 2605.0f },  // /e/  "eh"
                        { 342.0f, 2322.0f, 3000.0f },  // /i/  "ee"
                    };
                    const float F0   = 130.81f;            // C3 reference for the envelope
                    const float vIdx = amount * 4.0f;      // 0..4 across the five vowels
                    const int   v0   = std::min ((int) vIdx, 4);
                    const int   v1   = std::min (v0 + 1, 4);
                    const float vf   = vIdx - (float) v0;
                    const float F1 = VF[v0][0] + vf * (VF[v1][0] - VF[v0][0]);
                    const float F2 = VF[v0][1] + vf * (VF[v1][1] - VF[v0][1]);
                    const float F3 = VF[v0][2] + vf * (VF[v1][2] - VF[v0][2]);
                    const float depth = amount;
                    for (int i = 0; i < n; ++i)
                    {
                        const float freq = p[(size_t) i].ratio * F0;
                        const float env  = 0.012f                                          // fb251 — deeper valleys (was 0.02)
                            + 2.10f * Wavetable::lorentzian (freq, F1,  72.0f)              // fb251 — sharper, taller formants (BW 90→72)
                            + 1.55f * Wavetable::lorentzian (freq, F2,  96.0f)              // fb251 — (1.20→1.55, BW 120→96)
                            + 1.05f * Wavetable::lorentzian (freq, F3, 130.0f);             // fb251 — (0.80→1.05, BW 160→130) → clearer vowels
                        p[(size_t) i].amp *= (1.0f - depth) + depth * env;
                    }
                    break;
                }

                case SpectralMode::Smear:
                {
                    // THE BLUR (anti-stretch) — bleeds each partial's energy into its
                    // neighbours (triangular window that widens with amount) and decorrelates
                    // phases, melting crisp harmonics into a soft, washed-out cloud. amount 0 =
                    // window 0 + no scatter = identity. Seeded scatter is stable per index.
                    // soft diffuse cloud: rolls the top off (cutoff falls hard as the knob
                    // opens), blurs neighbours together, and scatters phases — a bright, hard
                    // tone melts into a soft, dark, washy pad. The rolloff is the big move;
                    // the blur + scatter diffuse what's left. amount 0 = identity.
                    const float cut = 3.0f + (1.0f - amount) * (1.0f - amount) * 92.0f;  // fb251 — ~95 → 3 (darker melt, was →4)
                    for (int i = 0; i < n; ++i)
                    {
                        const float r    = p[(size_t) i].ratio / cut;
                        const float roll = 1.0f / (1.0f + r * r * r * r);      // soft LP in harmonic space
                        p[(size_t) i].amp *= (1.0f - amount) + amount * roll;
                    }
                    const int W = (int) std::round (amount * 11.0f);           // fb251 — ± blur window 0..11 (was 8): wider wash
                    if (W > 0)
                    {
                        float src[FrameSpec::kMaxPartials];
                        for (int i = 0; i < n; ++i) src[(size_t) i] = p[(size_t) i].amp;
                        for (int i = 0; i < n; ++i)
                        {
                            double acc = 0.0, wsum = 0.0;
                            for (int k = -W; k <= W; ++k)
                            {
                                const int j = i + k;
                                if (j < 0 || j >= n) continue;
                                const double w = 1.0 - (double) std::abs (k) / (double) (W + 1);
                                acc  += w * (double) src[(size_t) j];
                                wsum += w;
                            }
                            p[(size_t) i].amp = (float) (wsum > 0.0 ? acc / wsum : (double) src[(size_t) i]);
                        }
                    }
                    unsigned int rng = 0x5EED1234u;
                    auto rnd11 = [&rng]() -> float {
                        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                        return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f; };
                    for (int i = 0; i < n; ++i)
                        p[(size_t) i].phase += amount * 4.1f * rnd11();            // fb251 — deeper phase scatter (was π≈3.14)
                    break;
                }

                case SpectralMode::RandomAmplitudes:
                {
                    // THE MUTATION — re-rolls each partial's gain from a FIXED seed, so the
                    // knob morphs the real spectrum into a scrambled-but-tonal mutant. The
                    // seed is per-partial-index (stable across frames + rebuilds), so every
                    // preset gets its own signature mutation rather than noise. The root is
                    // protected so pitch stays clear. amount 0 = ×1 = identity.
                    unsigned int rng = 0x0A17C0DEu;
                    auto rnd01 = [&rng]() -> float {
                        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                        return (float) rng / (float) 0xFFFFFFFFu; };
                    for (int i = 0; i < n; ++i)
                    {
                        const float r   = rnd01();
                        float       mul = 0.02f + r * r * 5.5f;                  // fb251 — 0.02 .. 5.52 (wilder mutation, was 3.52)
                        if (p[(size_t) i].ratio <= 1.01f) mul = 0.7f + 0.3f * r;  // keep the root present
                        p[(size_t) i].amp *= (1.0f - amount) + amount * mul;
                    }
                    break;
                }

                case SpectralMode::DataCompress:
                {
                    // THE DESTROYER — a frequency-domain bitcrusher: quantizes partial
                    // amplitudes to a shrinking number of levels (stair-stepped spectrum =
                    // digital grit) AND decimates bins as the knob opens (drops every Nth
                    // partial → hollow, reduced). The only mode that REMOVES information.
                    // amount 0 = 64 levels + keep-all ≈ identity; the root is always kept.
                    float maxA = 1.0e-9f;
                    for (int i = 0; i < n; ++i) maxA = std::max (maxA, std::abs (p[(size_t) i].amp));
                    const int   levels    = std::max (2, (int) std::round (64.0f - amount * 62.0f)); // fb251 — 64 → 2 (harder crush)
                    const int   keepEvery = 1 + (int) std::round (amount * 3.0f);                    // fb251 — 1 → 4 (more decimation, was 3)
                    const float invMax    = 1.0f / maxA;
                    for (int i = 0; i < n; ++i)
                    {
                        if (i != 0 && keepEvery > 1 && (i % keepEvery) != 0)
                        {
                            p[(size_t) i].amp = 0.0f;       // decimated bin (skipped by buildFromSpec)
                            continue;
                        }
                        const float norm = p[(size_t) i].amp * invMax;                      // -1..1
                        p[(size_t) i].amp = (std::round (norm * (float) levels) / (float) levels) * maxA;
                    }
                    break;
                }

                case SpectralMode::SpectralPhaser:
                {
                    // THE SWEEP — a comb of notches across the harmonic axis (built from the
                    // partials themselves), deepening AND sliding up as the knob opens — the
                    // classic phaser scoop in the spectral domain. With an LFO on amount the
                    // notches march up/down the spectrum (true phaser motion). amount 0 =
                    // depth 0 = identity.
                    const float depth  = amount;
                    const float period = 1.7f;             // fb251 — tighter spacing → MORE notches (was 2.0)
                    const float sweep  = amount * 4.5f;    // fb251 — notches slide farther up the spectrum (was 3.0)
                    for (int i = 0; i < n; ++i)
                    {
                        const float x     = (p[(size_t) i].ratio + sweep) / period;
                        float       notch = std::abs (std::sin (3.14159265f * x));
                        notch             = std::pow (notch, 2.0f);   // fb251 — deeper, wider troughs (was 1.6)
                        p[(size_t) i].amp *= (1.0f - depth) + depth * notch;
                    }
                    break;
                }

                case SpectralMode::Disperse:
                {
                    // THE CHIRP — a quadratic phase ramp. |X'[h]| == |X[h]| EXACTLY (measured
                    // -146 dBr or better through buildFromSpec's snap, the 34-level mip ladder and
                    // the peak normalisation, fb467), so this is the one mode in the menu that is
                    // constitutionally incapable of dulling anything. What it changes is WHEN in the
                    // cycle each harmonic peaks: the wave stops being a stack of aligned peaks and
                    // becomes a smear across the whole period.
                    //
                    //   phi(r) += c*(r-1)^2      group delay tau(r) = -(N/pi)*c*(r-1) samples
                    //   c = pi*D/(H-1)           D = the spread across the band, in CYCLES
                    //   D(a) = 4*a^2             identity at a=0; the ceiling is D=4
                    //
                    // 🚨 D, NOT c, IS THE UNIT, and that is a measurement not a preference (fb467):
                    //    a fixed c disperses a 511-harmonic table 21x harder than a 24-harmonic one,
                    //    and the crest curve came out NON-MONOTONE — the knob was a lottery. Divided
                    //    by the band top it behaves the same on every table.
                    // 🚨 THE CEILING IS D = 4 because past it the change stops being DIRECTIONAL and
                    //    just re-rolls: cumulative change vs a=0 saturates at D~2-4 on every table
                    //    measured, while each 0.1 of the knob still moves the folded output by
                    //    4.4-17.7 dB — travel everywhere, plateau nowhere.
                    // Audible route: the fold/warp/drive downstream (a crest change IS a magnitude
                    // change once it hits a nonlinearity), and the peak-normalised level.
                    const float D = 4.0f * amount * amount;
                    const float c = (bandTopR > 2.0f) ? (3.14159265f * D / (bandTopR - 1.0f)) : 0.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        const float d = p[(size_t) i].ratio - 1.0f;
                        p[(size_t) i].phase += c * d * d;
                    }
                    break;
                }

                case SpectralMode::None:
                case SpectralMode::Count:
                default:
                    break;   // identity: partials unchanged
            }

            // ── fb472 — THE CUT. Applied AFTER the mode, to whatever the mode left, and applied
            //    even when there is no mode at all. Amplitude only: no ratio and no phase is touched,
            //    so it composes with Disperse and cannot move a partial off its own frequency.
            if (cutLoOn (cutLo01))
            {
                const float rc = cutLoCorner (cutLo01);
                for (int i = 0; i < n; ++i)
                { const float x = std::max (1.0e-6f, p[(size_t) i].ratio / rc);
                  const float x4 = x * x * x * x;
                  p[(size_t) i].amp *= x4 / std::sqrt (1.0f + x4 * x4); }
            }
            if (cutHiOn (cutHi01))
            {
                const float rc = cutHiCorner (cutHi01);
                for (int i = 0; i < n; ++i)
                { const float x = std::max (1.0e-6f, p[(size_t) i].ratio / rc);
                  const float x4 = x * x * x * x;
                  p[(size_t) i].amp *= 1.0f / std::sqrt (1.0f + x4 * x4); }
            }

            writeBack (out, p, n);
        }
    };
} // namespace tw
