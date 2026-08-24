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
        HarmLowCut,           // 4th-order HP in harmonic number → hollow / thin / telephone
        HarmHighCut,          // 4th-order LP in harmonic number → dark / soft / muted
        Count
    };

    class SpectralMorph
    {
    public:
        /** Pure transform: base spectrum + mode + amount(0..1) → morphed WavetableSpec
         *  ready for Wavetable::buildFromSpec. amount<=0 or None returns the base
         *  untouched (and the morph is continuous from there — at amount→0 the stretch
         *  factor → 1, so partials land back on the integer grid losslessly). */
        static WavetableSpec apply (const WavetableSpec& base, SpectralMode mode, float amount,
                                    float rangeLo = 1.0f, float rangeHi = (float) FrameSpec::kMaxHarmonics)
        {
            amount = std::clamp (amount, 0.0f, 1.0f);
            if (mode == SpectralMode::None || amount <= 0.0f)
                return base;

            // fb467 — THE BAND TOP, computed ONCE for the whole spec, not per frame. Disperse's
            // coefficient is normalised by it (see the mode), and a per-frame value would make each
            // frame disperse by a different amount — the WT-POS sweep would go lumpy. Frames of one
            // table are one instrument; they share the band.
            const float top = bandTop (base);

            // fb467 — the window is enforced here so every mode gets it for free and no mode can
            // forget it. Hi >= Lo + 4 (a window narrower than the smoothstep edges is not a window).
            const float lo = std::clamp (rangeLo, 1.0f, (float) FrameSpec::kMaxHarmonics);
            const float hi = std::clamp (std::max (rangeHi, lo + 4.0f), 1.0f, (float) FrameSpec::kMaxHarmonics);

            WavetableSpec out;
            for (int f = 0; f < WavetableSpec::kNumFrames; ++f)
                morphFrame (base.frames[(size_t) f], out.frames[(size_t) f], mode, amount, top, lo, hi);
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

        static float smooth01 (float e0, float e1, float x) noexcept
        {
            if (e1 <= e0) return x >= e1 ? 1.0f : 0.0f;
            const float t = std::clamp ((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
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
                                float bandTopR, float rangeLo, float rangeHi) noexcept
        {
            FrameSpec::Partial p[FrameSpec::kMaxPartials];
            const int n = extract (in, p);

            // fb467 — PARTIAL RANGE. Both edges wide open is the DEFAULT and it takes a separate
            // path that copies nothing and blends nothing, so an unwindowed patch is bit-identical
            // to what shipped before this change — the window can never cost an existing preset.
            const bool loOpen = (rangeLo <= 1.0f + 1.0e-6f);
            const bool hiOpen = (rangeHi >= (float) FrameSpec::kMaxHarmonics - 1.0e-6f);
            const bool windowed = ! (loOpen && hiOpen);
            FrameSpec::Partial q[FrameSpec::kMaxPartials];
            if (windowed) for (int i = 0; i < n; ++i) q[(size_t) i] = p[(size_t) i];   // the dry copy

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

                case SpectralMode::HarmLowCut:
                case SpectralMode::HarmHighCut:
                {
                    // ── THE SPECTRAL CUT, and it is NOT the filter you already have ──────────────
                    //  This cuts by HARMONIC NUMBER, not by hertz, so the corner rides the note: play
                    //  an octave up and the timbre is identical instead of getting brighter. Serum 2's
                    //  spectral Lo/Hi markers are in Hz and do not track [M2 p.108]; Vital's kLowPass /
                    //  kHighPass are a brick wall with a one-harmonic taper and no slope at all
                    //  (spectral_morph.h:243-305). Ours is a FOURTH-ORDER BUTTERWORTH — the same order
                    //  Serum uses and the same one fb470 gave the rack EQ's cuts, so a cut is a cut
                    //  wherever you meet one in this plugin.
                    //
                    //  It is also AMPLITUDE-ONLY: no ratio and no phase is touched, so it composes
                    //  cleanly with Disperse (phase-only) and with the fb467 partial window.
                    //
                    //     |H(r)|  =  1 / sqrt(1 + (r/rc)^8)                 High Cut (low-pass)
                    //     |H(r)|  =  (r/rc)^4 / sqrt(1 + (r/rc)^8)          Low Cut  (high-pass)
                    //
                    //  The corners start FAR outside the band so amount 0 is an exact identity, which
                    //  the house law requires and which a corner parked at harmonic 512 would not give
                    //  (a 4th-order response is still -3 dB at its own corner).
                    const bool lowCut = (mode == SpectralMode::HarmLowCut);
                    const float rc = lowCut ? 0.0625f * std::pow (2.0f, 14.0f * amount)
                                            : 8192.0f * std::pow (2.0f, -14.0f * amount);
                    for (int i = 0; i < n; ++i)
                    {
                        const float x  = std::max (1.0e-6f, p[(size_t) i].ratio / rc);
                        const float x4 = x * x * x * x;
                        const float den = std::sqrt (1.0f + x4 * x4);
                        p[(size_t) i].amp *= (lowCut ? x4 : 1.0f) / den;
                    }
                    break;
                }

                case SpectralMode::None:
                case SpectralMode::Count:
                default:
                    break;   // identity: partials unchanged
            }

            // fb467 — blend the morphed partial back toward its dry self by the window weight.
            // AMP, RATIO and PHASE are blended SEPARATELY and phase is blended as an ANGLE through
            // the shortest arc. Blending the PHASOR instead would be a comb filter with an infinite
            // null at w = 0.5 (a partial cancelling against a rotated copy of itself), and lerping
            // the raw angle would take the long way round whenever the two straddle +/-pi.
            if (windowed)
            {
                const float E = 2.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float r  = q[(size_t) i].ratio;                       // the DRY ratio selects
                    const float wl = loOpen ? 1.0f : smooth01 (rangeLo - E, rangeLo, r);
                    const float wh = hiOpen ? 1.0f : 1.0f - smooth01 (rangeHi, rangeHi + E, r);
                    const float w  = wl * wh;
                    if (w >= 1.0f) continue;
                    float dPh = p[(size_t) i].phase - q[(size_t) i].phase;
                    while (dPh >  3.14159265f) dPh -= 6.28318531f;
                    while (dPh < -3.14159265f) dPh += 6.28318531f;
                    p[(size_t) i].amp   = q[(size_t) i].amp   + w * (p[(size_t) i].amp   - q[(size_t) i].amp);
                    p[(size_t) i].ratio = q[(size_t) i].ratio + w * (p[(size_t) i].ratio - q[(size_t) i].ratio);
                    p[(size_t) i].phase = q[(size_t) i].phase + w * dPh;
                }
            }

            writeBack (out, p, n);
        }
    };
} // namespace tw
