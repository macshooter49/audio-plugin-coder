// OrganicEngine.cpp — THE ORGANICS ENGINE runtime DSP (design §2 knobs, §3 day-1 playback, §5 CPU).
//
// Shape of one engine (one oscillator slot of one voice):
//   notes[]   — one per unison PLAYER of each noteOn ("Ensemble": own RR, own Human seed, k·7 ms·detune timing)
//   readers[] — one per sounding sample region (layer / release / noise), pooled, logical cap 48 per engine
//               (kMaxRegionsPerOsc) with a 5 ms steal-fade; 96 physical slots so stolen/handed-over readers can
//               finish their fades (live readers never exceed 48). notes[] = 96 + 16 so a new note never starves.
// Per block: params are smoothed once, each note recomputes its region targets only when Dynamics/Body moved,
// each reader computes ratio + gain endpoints once, then the inner loop is interpolate → gain ramp → accumulate.
// Tone is ONE first-order tilt per voice (pivot max(700 Hz, f0 of the lead note), the lead note's velocity/key/Human
// tone; coefficients glide across the block, identity-bracketed on/off), Image one M/S multiply per engine (skipped at 1.0).
// tp108: on a note the tilt can't move (a pure tone) Tone also runs ToneShape — a Chebyshev exciter on the + side and the
// tilt's shelf morphed into a low-pass on the − side, both sized from the lead note's own spectrum (see ToneShape).
// Memory: ~45 KB per engine (the pools + ToneShape), allocated in the constructor + prepare(); nothing after prepare().
//
// Reused from SampleEngine.h (fb204 law): the 4-point Hermite kernel, the EQUAL-GAIN smoothstep loop crossfade
// (correlated seam reads must not sum to +3 dB) with the adaptive window limited by the lead-in room, and the
// 2.5 ms terminal declick. Layer and Body crossfades are EQUAL-POWER (different recordings = uncorrelated).
#include "OrganicEngine.h"
#include "OrganicsLibrary.h"
#include "../Vibrato.h"          // tp55's JUCE-free per-voice pitch LFO (64-sample grid law) — tp105 Organics vibrato

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#if (defined (__ARM_NEON) || defined (__ARM_NEON__)) && ! defined (ORG_NO_NEON)
 #include <arm_neon.h>
 #define ORG_NEON 1
#endif
#include <cmath>
#include <cstring>
#include <vector>

namespace tw
{
    namespace
    {
        std::atomic<int> gLastReaders { 0 }, gLastRegion { -1 }, gLastLive { 0 }, gSteals { 0 }, gTailRel { 0 };
        std::atomic<int> gNzDecisions { 0 }, gNzHits { 0 }, gNzVar { -1 }, gNzVarN { 0 }, gNzDelay { 0 }; std::atomic<float> gNzDb { 0.f };   // tp107
        std::atomic<bool> gTnEnabled { true };   // tp108 test hook: the Tone stages off = the tp107 tilt alone (CPU A/B)
        std::atomic<float> gTnSwing { 0.f }, gTnSwingR { 0.f }, gTnFlat { 0.f }, gTnGap { 0.f }, gTnDom { 0.f }, gTnExc { 0.f }, gTnLp { 0.f }, gTnGateA { 1.f }, gTnRho { 0.f }; std::atomic<int> gTnStages { 0 };  // tp108

        inline uint32_t mix32 (uint32_t h) noexcept
        {
            h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16; return h;
        }
        struct Rng
        {
            uint32_t s;
            explicit Rng (uint32_t seed) noexcept : s (mix32 (seed) | 1u) {}
            float next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (1.0f / 16777216.0f); }
        };

        inline float clamp01 (float v) noexcept { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
        inline float smooth01 (float u) noexcept { u = clamp01 (u); return u * u * (3.f - 2.f * u); }
        /** C2 smootherstep for region STARTS: a sample that begins at full slope must not splash above 8 kHz. */
        inline float smoother01 (float u) noexcept { u = clamp01 (u); return u * u * u * (u * (u * 6.f - 15.f) + 10.f); }
        inline float dbToLin (float db) noexcept { return std::exp (db * 0.115129255f); }
        /** Release knob (release-trigger sample level): 0 = off, 0.5 = authored (0 dB), 1 = +6 dB. */
        inline float knobLevel (float v) noexcept { v = clamp01 (v); return v <= 0.5f ? 2.f * v : dbToLin (6.f * (2.f * v - 1.f)); }
        /** tp105 Noise knob: 0 = silent, 0.5 = authored (0 dB), 1 = +12 dB (Max: "noise that you can hear"). */
        inline float noiseLevel (float v) noexcept { v = clamp01 (v); return v <= 0.5f ? 2.f * v : dbToLin (12.f * (2.f * v - 1.f)); }

        // ── tp108: the Tone exciter's passes (see OrganicEngine::Impl::ToneShape). NEON on Apple silicon / ARM, the same
        //    arithmetic in scalar elsewhere. ─────────────────────────────────────────────────────────────────────────────────
        constexpr int kTnGroup = 32;                                   // the envelope's node spacing (frames)
        struct TnBand { float m[5] {}; float y1 = 0.f, y2 = 0.f; };   // m[0] = mid₋₁ … m[4] = mid₋₅ · y₋₁, y₋₂
        /** The band's all-pole part: y = u + p1·y₋₁ + p2·y₋₂ (a resonant pole pair at f_dom), four frames at a time from the
            impulse response h (h0 = 1) and the homogeneous responses A (to y₋₁) and B (to y₋₂). */
        struct TnPoles { float p1 = 0.f, p2 = 0.f, h1 = 0.f, h2 = 0.f, h3 = 0.f, A[4] {}, B[4] {}; };
        inline void tnPolesSet (TnPoles& q, double p1, double p2) noexcept
        {
            q.p1 = (float) p1; q.p2 = (float) p2;
            const double h1 = p1, h2 = p1 * h1 + p2, h3 = p1 * h2 + p2 * h1;
            q.h1 = (float) h1; q.h2 = (float) h2; q.h3 = (float) h3;
            double a[6] = { 0.0, 1.0 }, b[6] = { 1.0, 0.0 };   // index 0 = y₋₂, 1 = y₋₁, then y0..y3
            for (int k = 2; k < 6; ++k) { a[k] = p1 * a[k - 1] + p2 * a[k - 2]; b[k] = p1 * b[k - 1] + p2 * b[k - 2]; }
            for (int k = 0; k < 4; ++k) { q.A[k] = (float) a[k + 2]; q.B[k] = (float) b[k + 2]; }
        }
        /** THE SHAPER'S INPUT BAND, one pass: mid = ½(L+R) → u = Σ f_t·mid₋ₜ (t = 0..5, the zeros) → the resonant pole pair;
            y into out[], and the peak |y| of every group into gpk[]. */
        inline void tnBand (const float* L, const float* R, float* out, float* gpk, int n, const float* f, const TnPoles& q, TnBand& s) noexcept
        {
            float m1 = s.m[0], m2 = s.m[1], m3 = s.m[2], m4 = s.m[3], m5 = s.m[4], y1 = s.y1, y2 = s.y2;
            int i = 0;
            for (int g = 0; g < (n + kTnGroup - 1) / kTnGroup; ++g) gpk[g] = 0.f;
           #if ORG_NEON
            {
                const float t0[4] = { 1.f, q.h1, q.h2, q.h3 }, t1[4] = { 0.f, 1.f, q.h1, q.h2 }, t2[4] = { 0.f, 0.f, 1.f, q.h1 }, t3[4] = { 0.f, 0.f, 0.f, 1.f };
                const float32x4_t T0 = vld1q_f32 (t0), T1 = vld1q_f32 (t1), T2 = vld1q_f32 (t2), T3 = vld1q_f32 (t3), VA = vld1q_f32 (q.A), VB = vld1q_f32 (q.B);
                const float32x4_t half = vdupq_n_f32 (0.5f);
                const float pm[4] = { m4, m3, m2, m1 }, pp[4] = { 0.f, 0.f, 0.f, m5 };
                float32x4_t Mp = vld1q_f32 (pm), Mpp = vld1q_f32 (pp), PK = vdupq_n_f32 (0.f);
                for (int g = 0; i + 4 <= n; ++g)
                {
                    const int e = std::min (n & ~3, i + kTnGroup);
                    for (; i < e; i += 4)
                    {
                        const float32x4_t M = vmulq_f32 (half, vaddq_f32 (vld1q_f32 (L + i), vld1q_f32 (R + i)));
                        float32x4_t U = vmulq_n_f32 (M, f[0]);
                        U = vfmaq_n_f32 (U, vextq_f32 (Mp, M, 3), f[1]); U = vfmaq_n_f32 (U, vextq_f32 (Mp, M, 2), f[2]);
                        U = vfmaq_n_f32 (U, vextq_f32 (Mp, M, 1), f[3]); U = vfmaq_n_f32 (U, Mp, f[4]);
                        U = vfmaq_n_f32 (U, vextq_f32 (Mpp, Mp, 3), f[5]);
                        Mpp = Mp; Mp = M;
                        float32x4_t P = vmulq_laneq_f32 (T0, U, 0);
                        P = vfmaq_laneq_f32 (P, T1, U, 1); P = vfmaq_laneq_f32 (P, T2, U, 2); P = vfmaq_laneq_f32 (P, T3, U, 3);
                        const float32x4_t Y = vfmaq_n_f32 (vfmaq_n_f32 (P, VB, y2), VA, y1);
                        y1 = vgetq_lane_f32 (Y, 3); y2 = vgetq_lane_f32 (Y, 2);
                        vst1q_f32 (out + i, Y);
                        PK = vmaxq_f32 (PK, vabsq_f32 (Y));
                    }
                    gpk[g] = vmaxvq_f32 (PK); PK = vdupq_n_f32 (0.f);
                }
                if (i > 0) { m1 = vgetq_lane_f32 (Mp, 3); m2 = vgetq_lane_f32 (Mp, 2); m3 = vgetq_lane_f32 (Mp, 1); m4 = vgetq_lane_f32 (Mp, 0); m5 = vgetq_lane_f32 (Mpp, 3); }
            }
           #endif
            for (; i < n; ++i)
            {
                const float a = 0.5f * (L[i] + R[i]), u = f[0] * a + f[1] * m1 + f[2] * m2 + f[3] * m3 + f[4] * m4 + f[5] * m5;
                m5 = m4; m4 = m3; m3 = m2; m2 = m1; m1 = a;
                const float y = u + q.p1 * y1 + q.p2 * y2;
                y2 = y1; y1 = y; out[i] = y;
                gpk[i / kTnGroup] = std::max (gpk[i / kTnGroup], std::abs (y));
            }
            s.m[0] = m1; s.m[1] = m2; s.m[2] = m3; s.m[3] = m4; s.m[4] = m5;
            s.y1 = std::abs (y1) < 1.0e-15f ? 0.f : y1; s.y2 = std::abs (y2) < 1.0e-15f ? 0.f : y2;
        }

        /** THE ENVELOPE, continuous and step-free: a node at every 32-frame group boundary, E_{g+1} = max(1.08·peak_g,
            1.08·peak_{g+1}, E_g·d) (one group of look-ahead inside the block; d a release of ~30 periods, so it rides a decay
            or a tremolo without tracing the waveform), linear between nodes — both ends of every group are over its peak, so
            |u| = |y|/e < 1. The block's first node IS the last block's last one; the uniform 8 % margin (a breathy flute swings that fast) lets the next block's
            first group (unseen) fit under it, and a faster swell is caught by the next node. Nothing in it is periodic in the
            block: a per-block envelope stepped at every boundary (a 187.5 Hz comb out to Nyquist on a breathy flute), and a
            margin at the last node only bumped e once per block (±187.5 Hz sidebands round every harmonic). nodes[0..ng]. */
        /** The DC of Σ q_j·u^j for u = r·cos θ: Σ_{j even} q_j·r^j·C(j, j/2)/2^j — what the even orders add as an offset on a
            (near-)sine, known in closed form, so it is taken out exactly instead of estimated from the output. */
        inline float tnDc (const float* q, float r) noexcept
        {
            const float r2 = r * r, r4 = r2 * r2;
            (void) r4;
            return q[0] + r2 * (0.5f * q[2] + r2 * (0.375f * q[4] + r2 * (0.3125f * q[6] + r2 * (0.2734375f * q[8]))));
        }
        /** The envelope's memory across blocks: its last node (and that node's DC term) and the last 8 groups' peaks. */
        struct TnEnv { float E = 0.f, DC = 0.f; float past[8] {}; };   // past[0] = 8 groups ago … past[7] = the last one
        /** Node g sits 8 % over the largest group peak within ±W groups of it (W ≈ one period: the crest level, not a
            sawtooth between crests — a ripple at f0 sampled on the 32-frame grid beats into lines between the harmonics),
            never under the release E·d, and the block's first node continues the last block's (only a real attack raises
            it). The groups before the block come from `past`; the ones after it are not known yet, so the margin covers
            them. e·DC(r) rides along with r = 1/1.08 (the tone's amplitude re e on a steady (near-)sine). nodes[0..ng]. */
        inline void tnNodes (float* ext, int ng, int W, TnEnv& st, float d, const float* q, float* nodes, float* dcn) noexcept
        {
            // ext = [the last block's 8 group peaks | this block's ng | 8 zeros]: node g covers groups g − W … g + W − 1
            constexpr float kMargin = 1.08f;
            const float dcR = tnDc (q, 1.f / kMargin);
            for (int k = 0; k < 8; ++k) { ext[k] = st.past[k]; ext[8 + ng + k] = 0.f; }
            // every window's max in O(1): prefix / suffix maxima over runs of the window's length (van Herk / Gil-Werman)
            const int len = 2 * W, tot = 16 + ng;
            float* pre = ext + tot;                     // scratch after ext (the caller sizes it 3 × (16 + groups))
            float* suf = pre + tot;
            for (int i = 0; i < tot; ++i) pre[i] = (i % len) == 0 ? ext[i] : std::max (pre[i - 1], ext[i]);
            for (int i = tot - 1; i >= 0; --i) suf[i] = (i == tot - 1 || (i + 1) % len == 0) ? ext[i] : std::max (suf[i + 1], ext[i]);
            auto win = [&] (int node) { const int a = 8 + node - W, b = a + len - 1; return kMargin * (b < tot ? std::max (suf[a], pre[b]) : suf[a]); };
            // node 0 CONTINUES the last block's (a step in e steps every harmonic, ∝ e^(1−k): a click); from silence it starts
            // at the block's own level. A swell faster than the margin (a vibraphone's motor: ~5 % a block) is covered by the
            // ramp to node 1 — at worst a crest peeks over e for part of the first group and u is held at ±1 there
            float e0 = st.E > 0.f ? st.E : win (0);
            nodes[0] = e0;
            dcn[0] = st.E > 0.f ? st.DC : e0 * dcR;
            for (int g = 1; g <= ng; ++g)
            {
                e0 = std::max (win (g), e0 * d);
                nodes[g] = e0;
                dcn[g] = e0 * dcR;
            }
            st.E = e0; st.DC = dcn[ng];
            for (int k = 0; k < 8; ++k) st.past[k] = ext[ng + k];                  // the last 8 groups (older ones from `past` when ng < 8)
        }

        constexpr int kTnHist = 4096;                                  // the band's history (frames, a power of two): ≥ one period at 20 Hz
        /** PERIODICITY of the band's output over this block: every 8th frame, e = y − y(t − P) (P = one period, linear
            interpolation) against y and y(t − P): Σe² / (Σy² + Σy(t−P)²), the smallest over P·{0.97, 0.985, 1, 1.015, 1.03}
            (a vibrato, a few cents of tuning) — 0 for anything that repeats every period (a tone WITH all its harmonic partials),
            ~1 for a noise, high while the level itself moves fast (an onset) or another mode sounds beside it. The block is written
            into the ring first. */
        inline float tnAperiodicity (const float* y, int n, float* hist, int& head, float P0) noexcept
        {
            constexpr int M = kTnHist - 1;
            for (int i = 0; i < n; ++i) hist[(head + i) & M] = y[i];
            float best = 1.f;
            for (const float m : { 1.f, 0.985f, 1.015f, 0.97f, 1.03f })
            {
                const float P = std::min (P0 * m, (float) (kTnHist - 8));
                const int ip = (int) P; const float fr = P - (float) ip;
                float ee = 0.f, pp = 0.f;
                for (int i = 0; i < n; i += 8)
                {
                    const int t = head + i;
                    const float d = (1.f - fr) * hist[(t - ip) & M] + fr * hist[(t - ip - 1) & M], e = y[i] - d;
                    ee += e * e; pp += y[i] * y[i] + d * d;
                }
                best = std::min (best, pp > 1.0e-24f ? ee / pp : 1.f);
                if (best < 0.005f) break;                                       // periodic at the first guess: done
            }
            head = (head + n) & M;
            return best;
        }

        struct TnShape { const float* q; const float* nodes; const float* dcn; float a0, da, g0, dg, d0, dd; };
        /** THE HARMONICS INTO THE VOICE, one pass: per group, e and 1/e ramp between its nodes (1/e as a chord: never above
            the smaller node's inverse), u = clamp(y/e, ±1), h = a·(e·Σ q_k·u^k − e·DC(r)) (a ramps across the block, e·DC(r)
            between the nodes); L = g·L + h − dc, R likewise (g and the slow residual dc ramp across the block). Returns Σ h. */
        inline float tnShape (const float* x, float* L, float* R, int n, const TnShape& a) noexcept
        {
            const float* q = a.q;
            float sum = 0.f, p8[8] = {};
           #if ORG_NEON
            const float32x4_t Q0 = vdupq_n_f32 (q[0]), Q1 = vdupq_n_f32 (q[1]), Q2 = vdupq_n_f32 (q[2]), Q3 = vdupq_n_f32 (q[3]), Q4 = vdupq_n_f32 (q[4]),
                              Q5 = vdupq_n_f32 (q[5]), Q6 = vdupq_n_f32 (q[6]), Q7 = vdupq_n_f32 (q[7]), Q8 = vdupq_n_f32 (q[8]);
            const float32x4_t one = vdupq_n_f32 (1.f), mone = vdupq_n_f32 (-1.f), four = vdupq_n_f32 (4.f);
            const float jj[4] = { 1.f, 2.f, 3.f, 4.f };
            const float32x4_t J1 = vld1q_f32 (jj);
            float32x4_t S = vdupq_n_f32 (0.f);
           #endif
            for (int s0 = 0, g = 0; s0 < n; s0 += kTnGroup, ++g)
            {
                const int m = std::min (kTnGroup, n - s0);
                const float e0 = a.nodes[g], e1 = a.nodes[g + 1], inv = 1.f / (float) m, c0 = a.dcn[g], dc = (a.dcn[g + 1] - c0) * inv;
                const float iS = e0 > 1.0e-12f ? 1.f / e0 : 0.f, di = ((e1 > 1.0e-12f ? 1.f / e1 : 0.f) - iS) * inv, de = (e1 - e0) * inv;
                int j = 0;
               #if ORG_NEON
                if (m == kTnGroup)
                {
                    const float32x4_t IS = vdupq_n_f32 (iS), E0 = vdupq_n_f32 (e0), C0 = vdupq_n_f32 (c0), A0 = vdupq_n_f32 (a.a0 + a.da * (float) s0), G = vdupq_n_f32 (a.g0 + a.dg * (float) s0),
                                      D = vdupq_n_f32 (a.d0 + a.dd * (float) s0);
                    float32x4_t J = J1;
                    for (; j < kTnGroup; j += 4)
                    {
                        const float32x4_t u = vminq_f32 (one, vmaxq_f32 (mone, vmulq_f32 (vld1q_f32 (x + s0 + j), vfmaq_n_f32 (IS, J, di))));
                        float32x4_t p = vfmaq_f32 (Q7, Q8, u);
                        p = vfmaq_f32 (Q6, p, u); p = vfmaq_f32 (Q5, p, u); p = vfmaq_f32 (Q4, p, u); p = vfmaq_f32 (Q3, p, u);
                        p = vfmaq_f32 (Q2, p, u); p = vfmaq_f32 (Q1, p, u); p = vfmaq_f32 (Q0, p, u);
                        const float32x4_t h = vmulq_f32 (vsubq_f32 (vmulq_f32 (vfmaq_n_f32 (E0, J, de), p), vfmaq_n_f32 (C0, J, dc)), vfmaq_n_f32 (A0, J, a.da));
                        S = vaddq_f32 (S, h);
                        const float32x4_t gg = vfmaq_n_f32 (G, J, a.dg), hd = vsubq_f32 (h, vfmaq_n_f32 (D, J, a.dd));
                        vst1q_f32 (L + s0 + j, vfmaq_f32 (hd, gg, vld1q_f32 (L + s0 + j)));
                        vst1q_f32 (R + s0 + j, vfmaq_f32 (hd, gg, vld1q_f32 (R + s0 + j)));
                        J = vaddq_f32 (J, four);
                    }
                }
               #endif
                for (; j < m; ++j)
                {
                    const float fj = (float) (j + 1), fs = (float) (s0 + j + 1);
                    const float u = std::min (1.f, std::max (-1.f, x[s0 + j] * (iS + di * fj)));
                    const float poly = q[0] + u * (q[1] + u * (q[2] + u * (q[3] + u * (q[4] + u * (q[5] + u * (q[6] + u * (q[7] + u * q[8])))))));
                    const float h = ((e0 + de * fj) * poly - (c0 + dc * fj)) * (a.a0 + a.da * fs), gg = a.g0 + a.dg * fs, hd = h - (a.d0 + a.dd * fs);
                    p8[j & 7] += h;
                    L[s0 + j] = gg * L[s0 + j] + hd; R[s0 + j] = gg * R[s0 + j] + hd;
                }
            }
           #if ORG_NEON
            sum = vaddvq_f32 (S);
           #endif
            return sum + (((p8[0] + p8[1]) + (p8[2] + p8[3])) + ((p8[4] + p8[5]) + (p8[6] + p8[7])));
        }
        /** tp107 ROUND-ROBIN NOISE — the knob is also the CHANCE that a note-on / note-off makes its noise, like a player whose
            thumps and clicks come and go: 0 never · 0.25 one in three · 0.5 two in three (authored level) · 1 nine in ten
            (+12 dB). Linear in each half; every event decides independently. */
        inline float noiseChance (float v) noexcept { v = clamp01 (v); return v <= 0.5f ? (4.f / 3.f) * v : (2.f / 3.f) + (0.9f - 2.f / 3.f) * (2.f * v - 1.f); }
        /** tp107 ATTACK — the swell half's fade: g = 2·knob − 1 (knob 0.5..1) → 10 ms · 300^g, a log taper through the top half
            (the UI's orgFmt ATTACK readout prints this same law): 0.625 → 42 ms, 0.75 → 173 ms, 0.875 → 0.72 s, 1 → 3 s.
            Exactly 0.5 is Natural (no swell: the 2 ms declick, bit-identical). */
        inline float swellSec (float g) noexcept { g = clamp01 (g); return 0.010f * std::pow (300.f, g); }

        /** tp105 Vibrato depth taper: 0..1 → 0..50 cents peak, v^1.6 (0.25 → 5 ¢, 0.5 → 16.5 ¢ musical; 1 → 50 ¢ wild). */
        inline float vibDepthCents (float v) noexcept { v = clamp01 (v); return v <= 0.f ? 0.f : 50.f * std::pow (v, 1.6f); }
        /** The widest rate multiplier a vibrato plan can hold (100 ¢ = Vibrato::kMaxDepthCents): boundary counts use it. */
        constexpr double kVibMaxMul = 1.0600;

        // SampleEngine.h::hermite (verbatim)
        inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
        {
            const float c  = (x1 - xm1) * 0.5f;
            const float v  = x0 - x1;
            const float w  = c + v;
            const float a  = w + v + (x2 - x0) * 0.5f;
            const float bn = w + a;
            return ((((a * t) - bn) * t + c) * t + x0);
        }

        // ── 8-tap windowed sinc (offline bounce): Blackman window, cutoff 0.92·Nyquist, unity DC per phase ──
        constexpr int kSincPhases = 1024;
        struct SincTable
        {
            float t[kSincPhases + 1][8];
            SincTable()
            {
                constexpr double fc = 0.92, pi = 3.14159265358979323846;
                for (int p = 0; p <= kSincPhases; ++p)
                {
                    const double f = (double) p / kSincPhases;
                    double sum = 0.0, h[8];
                    for (int j = 0; j < 8; ++j)
                    {
                        const double x = (double) (j - 3) - f;
                        const double sx = std::abs (x) < 1e-9 ? 1.0 : std::sin (pi * fc * x) / (pi * fc * x);
                        const double wx = std::abs (x) >= 4.0 ? 0.0 : 0.42 + 0.5 * std::cos (pi * x / 4.0) + 0.08 * std::cos (2.0 * pi * x / 4.0);
                        h[j] = sx * wx; sum += h[j];
                    }
                    for (int j = 0; j < 8; ++j) t[p][j] = (float) (h[j] / sum);
                }
            }
        };
        const SincTable& sincTable() { static const SincTable s; return s; }

        template <int CH, bool SINC>
        inline void interp (const int16_t* d, int ip, float f, float& yl, float& yr) noexcept
        {
            if constexpr (! SINC)
            {
                if constexpr (CH == 1)
                {
                    const int16_t* q = d + ip;
                    yl = yr = hermite ((float) q[-1], (float) q[0], (float) q[1], (float) q[2], f);
                }
                else
                {
                    const int16_t* q = d + ip * 2;
                    yl = hermite ((float) q[-2], (float) q[0], (float) q[2], (float) q[4], f);
                    yr = hermite ((float) q[-1], (float) q[1], (float) q[3], (float) q[5], f);
                }
            }
            else
            {
                const auto& T = sincTable();
                const float ph = f * (float) kSincPhases;
                const int   pi = (int) ph;
                const float pf = ph - (float) pi;
                const float* w0 = T.t[pi];
                const float* w1 = T.t[pi < kSincPhases ? pi + 1 : pi];
                float al = 0.f, ar = 0.f;
                for (int j = 0; j < 8; ++j)
                {
                    const float w = w0[j] + pf * (w1[j] - w0[j]);
                    if constexpr (CH == 1) al += w * (float) d[ip - 3 + j];
                    else { al += w * (float) d[(ip - 3 + j) * 2]; ar += w * (float) d[(ip - 3 + j) * 2 + 1]; }
                }
                yl = al; yr = (CH == 1) ? al : ar;
            }
        }

        // Position is 32.32 fixed point inside a run (integer carry chain, exact split into index + fraction);
        // the pan is folded into two per-channel gain ramps.
        constexpr double kFix = 4294967296.0;
        constexpr float  kFrac = 1.0f / 4294967296.0f;

        /** Plain run: no loop seam inside [pos, pos + cnt·ratio). */
        template <int CH, bool SINC, bool ENV>
        void runPlain (const int16_t* d, double& pos, double ratio, int cnt, float& g, float dg,
                       const float* env, float pl, float pr, float* L, float* R) noexcept
        {
            uint64_t P = (uint64_t) (pos * kFix);
            const uint64_t inc = (uint64_t) (ratio * kFix + 0.5);
            float gl = g * pl, gr = g * pr;
            const float dgl = dg * pl, dgr = dg * pr;
           #if ORG_NEON
            if constexpr (CH == 2 && ! SINC)
            {
                // tp105 CPU — the stereo Hermite as ONE 2-lane kernel: the four interleaved frames around the read point
                // are ONE 128-bit load (8 × int16 = xm1 x0 x1 x2 for L and R), widened to two float32x4, and the cubic
                // runs on {L, R} together — half the arithmetic and an eighth of the loads of the scalar pair. Same
                // polynomial as hermite() (SampleEngine.h), same order of operations.
                float32x2_t gv = { gl, gr };
                const float32x2_t dgv = { dgl, dgr };
                for (int i = 0; i < cnt; ++i)
                {
                    const int16_t* q = d + (int) (P >> 32) * 2 - 2;
                    const int16x8_t v = vld1q_s16 (q);
                    const float32x4_t lo = vcvtq_f32_s32 (vmovl_s16 (vget_low_s16 (v)));    // xm1L xm1R x0L x0R
                    const float32x4_t hi = vcvtq_f32_s32 (vmovl_s16 (vget_high_s16 (v)));   // x1L  x1R  x2L x2R
                    const float32x2_t xm1 = vget_low_f32 (lo), x0 = vget_high_f32 (lo), x1 = vget_low_f32 (hi), x2 = vget_high_f32 (hi);
                    const float32x2_t c  = vmul_n_f32 (vsub_f32 (x1, xm1), 0.5f);
                    const float32x2_t vv = vsub_f32 (x0, x1);
                    const float32x2_t w  = vadd_f32 (c, vv);
                    const float32x2_t a  = vadd_f32 (vadd_f32 (w, vv), vmul_n_f32 (vsub_f32 (x2, x0), 0.5f));
                    const float32x2_t bn = vadd_f32 (w, a);
                    const float t = (float) (uint32_t) P * kFrac;
                    float32x2_t y = vsub_f32 (vmul_n_f32 (a, t), bn);
                    y = vadd_f32 (vmul_n_f32 (y, t), c);
                    y = vadd_f32 (vmul_n_f32 (y, t), x0);
                    float32x2_t gg = gv;
                    if constexpr (ENV) gg = vmul_n_f32 (gg, env[i]);
                    const float32x2_t o = vmul_f32 (y, gg);
                    L[i] += vget_lane_f32 (o, 0); R[i] += vget_lane_f32 (o, 1);
                    gv = vadd_f32 (gv, dgv); P += inc;
                }
                pos = (double) P * (1.0 / kFix);
                g += dg * (float) cnt;
                return;
            }
           #endif
            for (int i = 0; i < cnt; ++i)
            {
                float yl, yr;
                interp<CH, SINC> (d, (int) (P >> 32), (float) (uint32_t) P * kFrac, yl, yr);
                if constexpr (ENV) { L[i] += yl * (gl * env[i]); R[i] += yr * (gr * env[i]); }
                else               { L[i] += yl * gl;            R[i] += yr * gr; }
                gl += dgl; gr += dgr; P += inc;
            }
            pos = (double) P * (1.0 / kFix);
            g += dg * (float) cnt;
        }

        /** Seam run: the last xfEff frames before loopE blend with the lead-in (pos − loopLen). */
        template <int CH, bool SINC, bool ENV>
        void runSeam (const int16_t* d, double& pos, double ratio, int cnt, float& g, float dg,
                      const float* env, float pl, float pr, float* L, float* R,
                      double loopE, double loopLen, double invXf) noexcept
        {
            for (int i = 0; i < cnt; ++i)
            {
                const int ip = (int) pos;
                float al, ar, bl, br;
                interp<CH, SINC> (d, ip, (float) (pos - (double) ip), al, ar);
                const double q = pos - loopLen;
                const int iq = (int) q;
                interp<CH, SINC> (d, iq, (float) (q - (double) iq), bl, br);
                const float s = smooth01 ((float) (1.0 - (loopE - pos) * invXf));   // 0 at xfade-in → 1 at loopE
                float gg = g;
                if constexpr (ENV) gg *= env[i];
                L[i] += (al + (bl - al) * s) * (gg * pl);
                R[i] += (ar + (br - ar) * s) * (gg * pr);
                g += dg; pos += ratio;
            }
        }

        /** tp105 VIBRATO run: the playback rate follows the Vibrato plan's piecewise-linear multiplier (per-sample,
            the 64-sample grid law of Vibrato.h), so a vibrato is a true pitch sine, never a staircase. j0 = the chunk
            index of this run's first sample (the plan is indexed from the chunk start). */
        template <int CH, bool SINC, bool ENV>
        void runPlainVib (const int16_t* d, double& pos, double ratio, int cnt, float& g, float dg,
                          const float* env, float pl, float pr, float* L, float* R, const Vibrato::Block& vb, int j0) noexcept
        {
            float gl = g * pl, gr = g * pr;
            const float dgl = dg * pl, dgr = dg * pr;
            const int sh = vb.segShift, mask = (1 << sh) - 1;
            int i = 0;
            while (i < cnt)
            {
                const int j = j0 + i, sgi = j >> sh;
                const int len = std::min (cnt - i, (1 << sh) - (j & mask));
                double m = vb.mul[sgi] + vb.step[sgi] * (double) (j & mask);
                const double st = vb.step[sgi];
                for (int k = 0; k < len; ++k, ++i)
                {
                    const int ip = (int) pos;
                    float yl, yr;
                    interp<CH, SINC> (d, ip, (float) (pos - (double) ip), yl, yr);
                    if constexpr (ENV) { L[i] += yl * (gl * env[i]); R[i] += yr * (gr * env[i]); }
                    else               { L[i] += yl * gl;            R[i] += yr * gr; }
                    gl += dgl; gr += dgr; pos += ratio * m; m += st;
                }
            }
            g += dg * (float) cnt;
        }
        template <int CH, bool SINC, bool ENV>
        void runSeamVib (const int16_t* d, double& pos, double ratio, int cnt, float& g, float dg,
                         const float* env, float pl, float pr, float* L, float* R,
                         double loopE, double loopLen, double invXf, const Vibrato::Block& vb, int j0) noexcept
        {
            const int sh = vb.segShift, mask = (1 << sh) - 1;
            for (int i = 0; i < cnt; ++i)
            {
                const int j = j0 + i;
                const double m = vb.mul[j >> sh] + vb.step[j >> sh] * (double) (j & mask);
                const int ip = (int) pos;
                float al, ar, bl, br;
                interp<CH, SINC> (d, ip, (float) (pos - (double) ip), al, ar);
                const double q = pos - loopLen;
                const int iq = (int) q;
                interp<CH, SINC> (d, iq, (float) (q - (double) iq), bl, br);
                const float s = smooth01 ((float) (1.0 - (loopE - pos) * invXf));
                float gg = g;
                if constexpr (ENV) gg *= env[i];
                L[i] += (al + (bl - al) * s) * (gg * pl);
                R[i] += (ar + (br - ar) * s) * (gg * pr);
                g += dg; pos += ratio * m;
            }
        }

        using PlainFn = void (*) (const int16_t*, double&, double, int, float&, float, const float*, float, float, float*, float*);
        using SeamFn  = void (*) (const int16_t*, double&, double, int, float&, float, const float*, float, float, float*, float*, double, double, double);

        template <int CH, bool SINC> PlainFn plainFor (bool env) { return env ? &runPlain<CH, SINC, true> : &runPlain<CH, SINC, false>; }
        template <int CH, bool SINC> SeamFn  seamFor  (bool env) { return env ? &runSeam<CH, SINC, true>  : &runSeam<CH, SINC, false>; }
        using PlainVFn = void (*) (const int16_t*, double&, double, int, float&, float, const float*, float, float, float*, float*, const Vibrato::Block&, int);
        using SeamVFn  = void (*) (const int16_t*, double&, double, int, float&, float, const float*, float, float, float*, float*, double, double, double, const Vibrato::Block&, int);
        template <int CH, bool SINC> PlainVFn plainVFor (bool env) { return env ? &runPlainVib<CH, SINC, true> : &runPlainVib<CH, SINC, false>; }
        template <int CH, bool SINC> SeamVFn  seamVFor  (bool env) { return env ? &runSeamVib<CH, SINC, true>  : &runSeamVib<CH, SINC, false>; }
    }

    int organics_debug::lastRenderReaders() noexcept { return gLastReaders.load (std::memory_order_relaxed); }
    int organics_debug::lastNoteRegion() noexcept    { return gLastRegion.load (std::memory_order_relaxed); }
    int organics_debug::lastLiveReaders() noexcept   { return gLastLive.load (std::memory_order_relaxed); }
    int organics_debug::steals() noexcept            { return gSteals.load (std::memory_order_relaxed); }
    int organics_debug::tailReleases() noexcept      { return gTailRel.load (std::memory_order_relaxed); }
    int   organics_debug::noiseDecisions() noexcept        { return gNzDecisions.load (std::memory_order_relaxed); }
    int   organics_debug::noiseHits() noexcept             { return gNzHits.load (std::memory_order_relaxed); }
    int   organics_debug::lastNoiseVariant() noexcept      { return gNzVar.load (std::memory_order_relaxed); }
    int   organics_debug::lastNoiseVariantCount() noexcept { return gNzVarN.load (std::memory_order_relaxed); }
    float organics_debug::lastNoiseDb() noexcept           { return gNzDb.load (std::memory_order_relaxed); }
    int   organics_debug::lastNoiseDelay() noexcept        { return gNzDelay.load (std::memory_order_relaxed); }
    void organics_debug::setToneStages (bool on) noexcept { gTnEnabled.store (on, std::memory_order_relaxed); }
    organics_debug::ToneState organics_debug::lastTone() noexcept
    {
        return { gTnSwing.load (std::memory_order_relaxed), gTnSwingR.load (std::memory_order_relaxed), gTnFlat.load (std::memory_order_relaxed), gTnGap.load (std::memory_order_relaxed), gTnDom.load (std::memory_order_relaxed),
                 gTnExc.load (std::memory_order_relaxed), gTnLp.load (std::memory_order_relaxed), gTnStages.load (std::memory_order_relaxed), gTnGateA.load (std::memory_order_relaxed), gTnRho.load (std::memory_order_relaxed) };
    }

    //==============================================================================================
    struct OrganicEngine::Impl
    {
        static constexpr int kMaxPlayers = 16, kPool = 96, kMaxNotes = kPool + kMaxPlayers, kCap = organics::kMaxRegionsPerOsc;
        static constexpr int kPerNote = 16, kMaxTargets = 12, kRetire = 4;

        struct Reader
        {
            bool active = false, firstBlock = true;
            int  note = -1, ridx = -1;
            const OrganicInstrument* ins = nullptr;
            const org::Region* r = nullptr;
            const org::Sample* s = nullptr;
            org::Kind role = org::Kind::Attack;
            double pos = 0.0, ratio = 1.0;
            float gPrev = 0.f, layer = 0.f, panL = 1.f, panR = 1.f, rtAtt = 1.f;
            int64_t age = 0;
            int  fadeInLen = 0, liftLen = 0;  float liftExtra = 0.f;
            bool fading = false; int fadeDelay = 0, fadeRemain = 0, fadeLen = 1; float fadeStart = 1.f;
            bool tailWrapped = false; int64_t tailAge = 0;
            uint64_t stamp = 0; uint32_t chokeSeen = 0;
            bool relOn = false; float relG = 1.f;     // tp105: the note-off decay (exponential, −60 dB at max(amp release, knob time))
            int  wait = 0;                            // tp107: frames of silence before this reader starts (the noise round-robin's timing)
            // tp105b — THE TAIL RELEASE: the requested release outlives the recording → the region crosses into its compile-time
            // tail loop and keeps decaying there (level = max(natural, the release curve), both relative to note-off).
            bool relDecided = false, relTail = false;
            float relEnvOff = 0.f, relStepDb = 0.f, relLvlDb = 0.f;   // natural level at note-off · the loop steps removed · level re note-off
            double jumpOff = 0.0; int jumpLen = 0, jumpAt = 0; float jumpKb = 1.f, jumpDb = 0.f;   // a level-matched crossfade into the loop
        };

        struct Note
        {
            bool used = false, started = false, released = false, pedalHeld = false, orphan = false, killed = false, relPending = false;
            int  key = 60, vIdx = 64, k = 0, artic = 0, fake = 0, delay = 0;
            float vel01 = 0.5f, human = 0.f;
            float detC = 0.f, humC = 0.f, humLvl = 1.f, humToneDb = 0.f, humStartSec = 0.f, uRand = 0.f, uFake = 0.f;
            uint32_t rrIdx = 0;
            int64_t age = 0; float heldSec = 0.f;
            float vL = 64.f, vSoft = 64.f, gentle = 0.f, lastVL = -1.f, lastBody = -99.f, attack = 0.f;
            int64_t gLen = 0;                                                 // tp107: the softer-layer blend's length (60 ms .. half the swell)
            uint32_t nzSeed = 0;                                              // tp107: this player's noise round-robin draws
            int rd[kPerNote]; int nrd = 0;
            uint64_t gen = 0;
            int mapKey = 60;                                                  // tp105 NO-SILENCE: the key the lookups use
            float vibPhase = 0.f, vibDepthSm = 0.f, vibRateMul = 1.f;         // tp105: this player's vibrato (phase, depth, rate)
        };

        /** tp108 — TONE ON PURE TONES. The tilt (in renderChunk) can only re-weight partials that exist, so on a near-sine (a
            vibraphone bar, a glockenspiel, tubular bells, a flute's upper register) its ±9 dB moved the centroid ×1.0–1.4 end to
            end. Two stages, both sized PER NOTE by what the tilt itself can do to that note:
              • MEASURE (only once Tone ≠ 0): the lead note's own recording, four 2048-frame FFTs from 20 ms past its onset (one per
                block), each bin at the frequency it plays at, the tilt's ±1 |H|² applied analytically → the tilt's own centroid
                and f_rms SWINGS. SPARSE = 1 where the tilt can't move the note (a sine, a vibraphone, a clarinet's chalumeau), 0
                where it already can (a piano's C4, a guitar, a violin section, a trumpet); tonal spectra only (flatness ≤ 0.2 —
                a noise the tilt can't move is not sparse). Also: f_dom (the spectral peak, the SOUNDING pitch) and the strongest
                partial ≥ 1.5·f_dom.
              • + side, EXCITER (before the tilt): depth a = kExcite·SPARSE³·t² (the first half a sheen, 100 % the whole
                distance; none at all under 0.01, or on a note whose full depth stays under 0.06). The mid, band-passed around
                f_dom (a resonant pole pair, Q 3, + zeros at DC, Nyquist and a notch on an inharmonic partial — or any strong one
                on a high note — so nothing but the fundamental's neighbourhood reaches the polynomial), divided by a CONTINUOUS
                envelope (nodes every 32 frames, one group of look-ahead, release ~30 periods, |u| ≤ 1 by construction), drives
                Σ h_k·T_k(u) — Chebyshev polynomials, so a sine becomes EXACTLY its harmonics 2..8 (k·f_dom past 10→14 kHz
                faded; the few left on a high note scaled up to ×2.5 so it still brightens) — times e·a, the even orders' DC
                removed, the voice level-compensated by 1/√(1 + a²Σh²). tp108b, nothing past 16 kHz on ANY note: (1) a SPILL
                PREDICTION from the note's own spectrum through the band fades the orders its other partials would mix past
                16 kHz (a −50 dB budget, the high orders first); (2) a PERIODICITY GATE (only where the top order can reach
                16 kHz) scales each block's depth by how periodic the band's content is at f_dom's period — a breathy onset,
                a chiff, a second mode beside the fundamental close it (the flute's trimmed C7: −33 → −66 dB over 16 kHz).
              • − side, DARK (no filter of its own): the tilt section's own first-order shelf is morphed (zero up to 0.45·fs,
                pole down to 0.7·f_dom, geometrically by SPARSE·(−t)^1.5, gain at f_dom unchanged) — the −9 dB floor is gone and
                dark keeps going.
            Depths are smoothed (τ 20 ms); every ramp is continuous across blocks: no step, no zipper. Tone 0 never enters here
            (bit-identical to tp107, 0 µs). Cost with the exciter running: ~0.75 µs per 512 frames (NEON), ~16 µs in each of the
            four blocks after a note-on while its spectrum is measured; ~10 KB per engine. */
        struct ToneShape
        {
            static constexpr double kBandQ = 3.0, kSpillBudget = 1.0e-5;   // −50 dB re the note
            static constexpr float kCenLo = 1.3f, kCenHi = 1.55f, kRmsLo = 1.5f, kRmsHi = 1.95f, kExcite = 1.2f;
            static constexpr int   kH = 7;                                          // harmonics 2..8
            static constexpr float kProfile[kH] = { 0.5f, 0.4f, 0.3f, 0.22f, 0.16f, 0.12f, 0.09f };
            static constexpr int   kN = 2048, kM = kN / 2, kSegs = 4;              // 4 × 43 ms at 48 kHz: 20 → 190 ms of the note
            double sr = 48000.0;
            std::vector<float> mono, gpk, nodes, dcn, hist, re, im, spec, pm2;   // spec: the note's power per FFT bin (the measurement)
            double orderFade[kH + 2] { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
            int histHead = 0, kTop = 8;
            float pc0 = 0.f;                                // re/im: the kM-point complex FFT (real kN in pairs)
            // measurement (the lead note)
            uint64_t gen = ~0ull; bool frozen = false; int segs = 0;
            const int16_t* srcD = nullptr; int srcCh = 1; int64_t srcPos = 0, srcEnd = 0; double srcRatio = 1.0;   // the lead region's audio
            double acn[2] {}, acd[2] {}, arn[2] {}, pkP = 0.0, flatAcc = 0.0, upP = 0.0;
            float upF = 0.f;                                                       // the strongest partial ≥ 1.5·f_dom
            float swing = 0.f, swingRms = 0.f, sparse = 0.f, fDom = 0.f, flat = 0.f;
            // exciter
            float aEff = 0.f, gate = 1.f, lastRho = 0.f;                         // the depth applied last block · the tonality gate
            float aCur = 0.f, gPrev = 1.f, dcPrev = 0.f, dcTgt = 0.f, bandDom = -1.f, bandNotch = -1.f, bandF[6] {};
            TnPoles bandP;
            TnEnv envSt;
            TnBand band;
            bool  excOn = false;
            float polyDom = -1.f, qC = 0.f;
            float qf[kH + 2] {};                                                   // Σ w_k·h_k·T_k(u) as a polynomial in u (degree 8)
            // low-pass depth (the tilt section applies it)
            float dCur = 0.f;
            int   stages = 0;

            /** Hann window + e^{−j2πk/kN} (k = 0..kM) for the snapshot FFT, built once (prepare, message thread), read-only after. */
            struct Tables
            {
                float win[kN], cs[kM + 1], sn[kM + 1];
                Tables()
                {
                    for (int i = 0; i < kN; ++i) win[i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (kN - 1)));
                    for (int k = 0; k <= kM; ++k) { cs[k] = (float) std::cos (2.0 * 3.14159265358979 * k / kN); sn[k] = (float) -std::sin (2.0 * 3.14159265358979 * k / kN); }
                }
            };
            static const Tables& tables() { static const Tables t; return t; }

            void prepare (double sampleRate, int block)
            {
                sr = sampleRate;
                mono.assign ((size_t) block, 0.f);
                gpk.assign (3 * ((size_t) block / kTnGroup + 2 + 16), 0.f);   // 8 past groups · this block's · 8 padding, then the window scratch
                nodes.assign ((size_t) block / kTnGroup + 3, 0.f);
                dcn.assign ((size_t) block / kTnGroup + 3, 0.f);
                hist.assign ((size_t) kTnHist, 0.f);
                for (auto* v : { &re, &im, &spec, &pm2 }) v->assign ((size_t) kM, 0.f);
                (void) tables();
                reset();
            }
            void resetMeasure() noexcept
            {
                frozen = false; segs = 0; pkP = 0.0; upP = 0.0; upF = 0.f; srcD = nullptr;
                for (int s = 0; s < 2; ++s) acn[s] = acd[s] = arn[s] = 0.0;
                flatAcc = 0.0;
                std::fill (spec.begin(), spec.end(), 0.f);
            }
            void reset() noexcept
            {
                gen = ~0ull; resetMeasure();
                swing = swingRms = sparse = fDom = flat = 0.f;
                aCur = 0.f; aEff = 0.f; gate = 1.f; lastRho = 0.f; envSt = {}; gPrev = 1.f; dcPrev = dcTgt = 0.f; band = {}; bandDom = -1.f; bandNotch = -1.f; excOn = false; polyDom = -1.f;
                dCur = 0.f; stages = 0;
            }
            bool busy() const noexcept { return excOn || aCur > 0.f || dCur > 0.f; }

            /** In-place radix-2 complex FFT of re/im (kM points; twiddle e^{−j2πi/kM} = the table at 2i). */
            void fft() noexcept
            {
                const auto& T = tables();
                for (int i = 1, j = 0; i < kM; ++i)
                {
                    int bit = kM >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
                    if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
                }
                for (int len = 2, step = kM / 2; len <= kM; len <<= 1, step >>= 1)
                    for (int i = 0; i < kM; i += len)
                        for (int k = 0; k < len / 2; ++k)
                        {
                            const float wr = T.cs[2 * k * step], wi = T.sn[2 * k * step];
                            const size_t a = (size_t) (i + k), b = a + (size_t) (len / 2);
                            const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                            re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                        }
            }
            /** THE NOTE'S SPECTRUM, read from the lead region's own recording (it is in memory the moment the note starts, so
                Tone acts from the first blocks, not after the note has sounded): four 2048-frame Hann segments from 20 ms past
                the region's onset (past the strike — the knob sweep's own 20 → 190 ms window), one real FFT per block (~15 µs),
                each bin moved to the frequency it PLAYS at (× the reader's repitch ratio), and the tilt's own ±1 |H(f)|²
                applied bin by bin — so the stages know how far the tilt alone moves THIS note: the power-weighted CENTROID
                swing C(+1)/C(−1) (what the ear and the knob sweep call brightness; a weak hiss barely moves it) and the f_rms
                swing (which a stack of weak upper harmonics does move). SPARSE = the larger of the two shortfalls: 1 = the tilt
                can't brighten it (a sine, a vibraphone bar, a clarinet's chalumeau), 0 = it already can (a piano's C4, a guitar,
                a violin section). f_dom = the spectral peak (the SOUNDING pitch: a glockenspiel sounds far over its key). The
                estimate refines after every segment (the depths glide). */
            void setSource (uint64_t leadGen, const org::Sample& smp, const org::Region& rg, double ratio, float pitchCents) noexcept
            {
                gen = leadGen; resetMeasure(); pc0 = pitchCents;
                srcD = smp.data(); srcCh = std::max (1, smp.channels); srcRatio = ratio;
                srcEnd = std::min<int64_t> (rg.end, smp.frames);
                srcPos = std::min<int64_t> (srcEnd, std::max<int64_t> (rg.start, rg.onset) + (int64_t) (0.02 * smp.sampleRate));
            }
            /** A lead note with no attack region to read: nothing to size the stages on — they stand down for it. */
            void noSource (uint64_t leadGen) noexcept { gen = leadGen; resetMeasure(); sparse = 0.f; frozen = true; }
            void measure (float pivotHz) noexcept
            {
                if (srcD == nullptr || srcPos + kN > srcEnd) { if (segs == 0) sparse = 0.f; frozen = true; return; }   // too short: leave it
                const auto& T = tables();
                constexpr float k16 = 1.f / 32768.f;
                for (int f = 0; f < kN; ++f)
                {
                    const int16_t* fr = srcD + (size_t) (srcPos + f) * (size_t) srcCh;
                    const float v = (srcCh > 1 ? 0.5f * ((float) fr[0] + (float) fr[1]) : (float) fr[0]) * k16 * T.win[f];
                    if (f & 1) im[(size_t) (f >> 1)] = v; else re[(size_t) (f >> 1)] = v;   // real frames in pairs: z[m] = x[2m] + j·x[2m+1]
                }
                srcPos += kN;
                analyse (pivotHz);
                if (++segs >= kSegs) { frozen = true; refineDom(); }
            }
            /** f_dom to a fraction of a bin (the periodicity gate needs the period to ~0.5 %; a 2048-point bin is 23 Hz): the
                accumulated spectrum's peak, parabolic on its log. */
            void refineDom() noexcept
            {
                int pk = 0; float pv = 0.f;
                for (int k = 2; k < kM - 1; ++k) if (spec[(size_t) k] > pv) { pv = spec[(size_t) k]; pk = k; }
                if (pk < 2) return;
                const double l = std::log (spec[(size_t) pk - 1] + 1.0e-30), c = std::log (spec[(size_t) pk] + 1.0e-30), r = std::log (spec[(size_t) pk + 1] + 1.0e-30);
                const double den = l - 2.0 * c + r, d = den < 0.0 ? std::clamp (0.5 * (l - r) / den, -0.5, 0.5) : 0.0;
                fDom = std::clamp ((float) (((double) pk + d) * sr * srcRatio / kN), 20.f, (float) (0.2 * sr));
            }
            void analyse (float pivotHz) noexcept
            {
                const auto& T = tables();
                fft();
                const double K = std::tan (3.14159265358979 * (double) pivotHz / sr), K2 = K * K, A2 = std::pow (10.0, 0.9);   // (+9 dB)²
                const int k0 = std::max (1, (int) std::ceil (20.0 * kN / sr)), k1 = std::min (kM - 1, (int) (20000.0 * kN / sr));
                double cn[2] {}, cd[2] {}, rn[2] {}, sP = 0.0, sL = 0.0; int nb = 0;
                for (int k = k0; k <= k1; ++k)
                {
                    // unpack bin k of the real kN-point transform from the kM-point complex one
                    const size_t a = (size_t) k, b = (size_t) (kM - k);
                    const float zr = re[a], zi = im[a], cr = re[b], ci = -im[b];                        // Z[k], conj(Z[M−k])
                    const float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci), dr = zr - cr, di = zi - ci;
                    const float orr = 0.5f * di, oi = -0.5f * dr;                                       // (Z[k] − conj Z[M−k]) / 2j
                    const float xr = er + T.cs[k] * orr - T.sn[k] * oi, xi = ei + T.cs[k] * oi + T.sn[k] * orr;
                    const double P = (double) xr * xr + (double) xi * xi;
                    const double f = (double) k * sr * srcRatio / kN;                                   // where this bin PLAYS
                    if (f > 0.45 * sr) break;
                    if (P > pkP) { pkP = P; fDom = (float) f; }
                    spec[(size_t) k] += (float) P;
                    if (f <= 16000.0) { sP += P; sL += std::log (P + 1.0e-30); ++nb; }
                    const double w = std::tan (3.14159265358979 * f / sr), w2 = w * w;
                    for (int s = 0; s < 2; ++s)
                    {
                        const double a2 = s == 0 ? A2 : 1.0 / A2, g = P * (a2 * w2 + K2) / (w2 + a2 * K2);   // the ±1 tilt's |H|²
                        cn[s] += g * f; cd[s] += g; rn[s] += g * f * f;
                    }
                }
                for (int s = 0; s < 2; ++s) { acn[s] += cn[s]; acd[s] += cd[s]; arn[s] += rn[s]; }
                if (nb > 0 && sP > 0.0) flatAcc += std::exp (sL / nb) / (sP / nb);                      // this segment's spectral flatness
                flat = (float) (flatAcc / (double) (segs + 1));
                if (acd[0] <= 0.0 || acd[1] <= 0.0 || acn[1] <= 0.0) return;   // silence so far: leave it alone
                fDom = std::clamp (fDom, 20.f, (float) (0.2 * sr));
                // the strongest partial ≥ 1.5·f_dom (a flute's octave, a bar's 2.76·f, a bell's upper modes): the band's notch may go there
                for (int k = k0; k <= k1; ++k)
                {
                    const double f = (double) k * sr * srcRatio / kN;
                    if (f > 0.45 * sr) break;
                    if (f < 1.5 * fDom) continue;
                    const size_t a = (size_t) k, b = (size_t) (kM - k);
                    const float zr = re[a], zi = im[a], cr = re[b], ci = -im[b];
                    const float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci), dr = zr - cr, di = zi - ci;
                    const float orr = 0.5f * di, oi = -0.5f * dr;
                    const float xr = er + T.cs[k] * orr - T.sn[k] * oi, xi = ei + T.cs[k] * oi + T.sn[k] * orr;
                    const double P = (double) xr * xr + (double) xi * xi;
                    if (P > upP) { upP = P; upF = (float) f; }
                }
                swing    = (float) ((acn[0] / acd[0]) / (acn[1] / acd[1]));
                swingRms = (float) std::sqrt ((arn[0] / acd[0]) / (arn[1] / acd[1]));
                // a NOISE the tilt can't move (all of it over the pivot) is not a sparse spectrum: only tonal ones (spectral
                // flatness ≤ 0.2 — a sine ~0, an instrument ≤ 0.1, white noise 0.56) are handed to the stages
                const float tonal = std::clamp ((0.4f - flat) / 0.2f, 0.f, 1.f);
                sparse = tonal * std::max (std::clamp ((kCenHi - swing) / (kCenHi - kCenLo), 0.f, 1.f),
                                           std::clamp ((kRmsHi - swingRms) / (kRmsHi - kRmsLo), 0.f, 1.f));
            }
            /** The Chebyshev sum Σ w_k·h_k·T_k(u) as monomial coefficients; w_k fades every harmonic whose k·f_dom passes
                10→14 kHz (the tilt lifts what is left up to +9 dB — the top octave stays for the sample's own air). */
            void buildPoly() noexcept
            {
                if (fDom == polyDom) return;
                polyDom = fDom;
                const float f0 = (float) std::min (10000.0, 0.21 * sr), f1 = (float) std::min (14000.0, 0.29 * sr);
                // the weights that fit, and the brightness they give a sine (f_rms ratio, a = 1) against the whole profile's
                double w[kH + 2] {}, Af = 0, Cf = 0, A = 0, C = 0;
                for (int k = 2; k <= kH + 1; ++k)
                {
                    const double h = kProfile[k - 2];
                    w[k] = h * std::clamp ((f1 - (float) k * fDom) / (f1 - f0), 0.f, 1.f);
                    Af += h * h * k * k; Cf += h * h; A += w[k] * w[k] * k * k; C += w[k] * w[k];
                }
                // A high note keeps only the harmonics that fit under 14 kHz — scale them up so Tone +1 still brightens it as
                // much as the whole profile would (a C8 glockenspiel: the octave alone, over its fundamental), capped at the
                // brightness those harmonics can reach at all (the highest one's own frequency) and at ×2.5.
                if (C > 1.0e-9)
                {
                    int kTop = 2; for (int k = 2; k <= kH + 1; ++k) if (w[k] > 0.0) kTop = k;
                    const double Bf2 = (1.0 + Af) / (1.0 + Cf), Bav2 = (1.0 + A) / (1.0 + C);
                    const double Bt2 = std::min (Bf2, 0.9 * (double) (kTop * kTop));
                    if (Bav2 < Bt2)
                    {
                        const double lam = std::min (2.5, std::sqrt ((Bt2 - 1.0) / std::max (1.0e-9, A - Bt2 * C)));
                        if (A - Bt2 * C > 1.0e-9 && lam > 1.0) for (auto& x : w) x *= lam;
                        else if (A - Bt2 * C <= 1.0e-9) for (auto& x : w) x *= 2.5;
                    }
                }
                for (int k = 2; k <= kH + 1; ++k) w[k] *= orderFade[k];   // what the note's other partials would push past 16 kHz
                kTop = 1; for (int k = 2; k <= kH + 1; ++k) if (w[k] > 1.0e-3) kTop = k;
                double t0[kH + 2] {}, t1[kH + 2] {}, t2[kH + 2] {}, q[kH + 2] {};
                t0[0] = 1.0; t1[1] = 1.0;
                double c = 0;
                for (int k = 2; k <= kH + 1; ++k)
                {
                    for (int j = 0; j <= kH + 1; ++j) t2[j] = (j > 0 ? 2.0 * t1[j - 1] : 0.0) - t0[j];     // T_k = 2u·T_{k−1} − T_{k−2}
                    for (int j = 0; j <= kH + 1; ++j) q[j] += w[k] * t2[j];
                    c += w[k] * w[k];
                    std::memcpy (t0, t1, sizeof t0); std::memcpy (t1, t2, sizeof t1);
                }
                for (int j = 0; j <= kH + 1; ++j) qf[j] = (float) q[j];
                qC = (float) c;
                (void) 0;
            }

            /** SPILL PREDICTION (once per note, from the measured spectrum). The Chebyshev weights fade every k·f_dom past 14 kHz —
                exact for a lone sine at f_dom. Anything else the band lets through (a horn's 2nd and 3rd harmonics on a high note,
                a bar's upper mode, hiss) mixes into order k: first-order products land at f + (k−1)·f_dom, second-order at
                2f + (k−2)·f_dom. Through the band's actual response, the fraction of the band's power that would land past 16 kHz
                that way is E1_k (first-order) and E2_k (second); order k's predicted spill ≈ w_k²·k²·(E1_k + k²·E2_k²/4). The
                whole note gets a budget of −50 dB, spent from the lowest order up (the high orders are faded first). A pure tone:
                nothing past, every fade 1 — the sound unchanged. */
            void predictSpill (double p1, double p2) noexcept
            {
                const double pi = 3.14159265358979, lim = std::min (16000.0, 0.45 * sr);
                std::vector<float>& Pb = pm2;                                                   // the band's output power per bin
                double tot = 0.0;
                for (int k = 1; k < kM; ++k)
                {
                    const double f = (double) k * sr * srcRatio / kN;
                    if (f > 0.45 * sr) { Pb[(size_t) k] = 0.f; continue; }
                    const double w = 2.0 * pi * f / sr;
                    double nr = 0.0, ni = 0.0;
                    for (int t = 0; t < 6; ++t) { nr += bandF[t] * std::cos (w * t); ni -= bandF[t] * std::sin (w * t); }
                    const double dr = 1.0 - p1 * std::cos (w) - p2 * std::cos (2.0 * w), di = p1 * std::sin (w) + p2 * std::sin (2.0 * w);
                    const double g = (nr * nr + ni * ni) / std::max (1.0e-30, dr * dr + di * di);
                    Pb[(size_t) k] = (float) (spec[(size_t) k] * g);
                    tot += Pb[(size_t) k];
                }
                for (int k = 2; k <= kH + 1; ++k) orderFade[k] = 1.0;
                if (tot <= 0.0) return;
                // a BUDGET for the whole note (−50 dB re the note), spent from the lowest order up: the low orders give the most
                // brightness per unit of spill, the high ones are faded first until the sum fits
                double spill[kH + 2] {};
                for (int k = 2; k <= kH + 1; ++k)
                {
                    const double c1 = lim - (double) (k - 1) * fDom, c2 = 0.5 * (lim - (double) (k - 2) * fDom);
                    double e1 = 0.0, e2 = 0.0;
                    for (int b = 1; b < kM; ++b)
                    {
                        const double f = (double) b * sr * srcRatio / kN;
                        if (std::abs (f - fDom) < 0.03 * fDom) continue;                        // the tone itself
                        if (f > c1) e1 += Pb[(size_t) b];
                        if (f > c2) e2 += Pb[(size_t) b];
                    }
                    e1 /= tot; e2 /= tot;
                    const double h = kProfile[k - 2] * 2.5;                                     // the largest weight order k can get
                    spill[k] = h * h * k * k * (e1 + 0.25 * k * k * e2 * e2);
                }
                double left = kSpillBudget;
                for (int k = 2; k <= kH + 1; ++k)
                {
                    if (spill[k] <= left) { left -= spill[k]; continue; }
                    orderFade[k] = left > 0.0 ? std::sqrt (left / spill[k]) : 0.0;
                    left = 0.0;
                }
            }

            void excite (float* L, float* R, int n, float t, float pitchCents, float pivotHz) noexcept
            {
                stages = 0;
                if (! frozen && t != 0.f) { measure (pivotHz); stages |= 4; }

                const float blk = 1.f - std::exp (-(float) n / (float) (0.02 * sr));
                // depth: kExcite·SPARSE³·t². A note whose FULL depth stays under 0.06 (its harmonics ≤ −30 dB: the tilt was
                // nearly enough) and any target under 0.01 run no exciter at all — it costs nothing where it would do nothing.
                const float full = kExcite * sparse * sparse * sparse;
                float tgt = t > 0.f && frozen && fDom > 0.f && full >= 0.06f ? full * t * t : 0.f;
                if (tgt < 0.01f) tgt = 0.f;
                const float a0 = aCur;
                aCur += blk * (tgt - aCur);
                if (std::abs (tgt - aCur) < 1.0e-5f) aCur = tgt;
                if (aCur <= 0.f && a0 <= 0.f)
                {
                    if (excOn) { excOn = false; band = {}; gPrev = 1.f; envSt = {}; dcPrev = dcTgt = 0.f; aEff = 0.f; gate = 1.f; std::fill (hist.begin(), hist.end(), 0.f); }
                    return;
                }
                buildPoly();
                // the shaper hears the fundamental's neighbourhood only (upper partials, a stray low component and the hiss stay
                // out of the polynomial). The band's notch pair takes an INHARMONIC partial (a bar's 2.76·f, a bell's modes)
                // within 30 dB of the fundamental — its sum and difference tones would land off the harmonic grid — and, on a
                // note at or over 1.5 kHz, any partial within 20 dB (a flute's octave: its products would fold past Nyquist).
                // A low note's harmonic partials are left alone (their products land ON the grid: consonant).
                const float upR = fDom > 0.f ? upF / fDom : 0.f;
                const bool inharm = std::abs (upR - std::round (upR)) > 0.06f;
                const float notchF = upF > 0.f && upF < 0.45 * sr && ((inharm && upP >= 1.0e-3 * pkP) || (fDom >= 1500.f && upP >= 1.0e-2 * pkP)) ? upF : 0.f;
                if (fDom != bandDom || notchF != bandNotch)
                {
                    bandDom = fDom; bandNotch = notchF;
                    const double pi = 3.14159265358979, w = 2.0 * pi * (double) std::min (fDom, (float) (0.4 * sr)) / sr, K = std::tan (0.5 * w);
                    // no inharmonic partial: the pair sits at fs/4 (12 kHz) — the hiss up there, crossed with the harmonics, is what
                    // would land over 16 kHz (or at Nyquist for a fundamental that is itself up there)
                    const double fz = notchF > 0.f ? (double) notchF : (fDom < 0.15 * sr ? 0.25 * sr : 0.5 * sr);
                    const double cn = std::cos (2.0 * pi * fz / sr);
                    (void) K;
                    // the poles: a resonant pair at f_dom, Q = kBandQ (the RBJ band-pass denominator)
                    const double al = std::sin (w) / (2.0 * kBandQ), a0 = 1.0 + al, p1 = 2.0 * std::cos (w) / a0, p2 = -(1.0 - al) / a0;
                    tnPolesSet (bandP, p1, p2);
                    // (1 − z⁻¹)(1 + z⁻¹)² = 1 + z⁻¹ − z⁻² − z⁻³, times (1 − 2·cos θ·z⁻¹ + z⁻²)
                    const double z3[4] = { 1.0, 1.0, -1.0, -1.0 }, z2[3] = { 1.0, -2.0 * cn, 1.0 };
                    double fir[6] {};
                    for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) fir[i + j] += z3[i] * z2[j];
                    double hr = 0.0, hi = 0.0;
                    for (int t = 0; t < 6; ++t) { hr += fir[t] * std::cos (w * t); hi -= fir[t] * std::sin (w * t); }
                    const double dr = 1.0 - p1 * std::cos (w) - p2 * std::cos (2.0 * w), di = p1 * std::sin (w) + p2 * std::sin (2.0 * w);
                    const double gain = std::sqrt (dr * dr + di * di) / std::max (1.0e-12, std::sqrt (hr * hr + hi * hi));   // unity at f_dom
                    for (int t = 0; t < 6; ++t) bandF[t] = (float) (fir[t] * gain);
                    predictSpill (p1, p2);
                    polyDom = -1.f;                                                   // rebuild with the new fades
                }
                // PERIODICITY GATE. The Chebyshev sum turns a PERIODIC input into harmonics of its period — and anything else into
                // intermodulation: a breathy onset or a chiff that the band lets through (±f/4 wide) spreads k-fold, ×6 of 2.5 kHz
                // noise is 15 kHz of hash (the flute's C7 after the tp108 library trim: −33 dB over 16 kHz, all of it in the first
                // 110 ms). So this block's depth is scaled by how periodic the band's content is at f_dom's period (0 = exactly,
                // 1 = a noise): ≤ 0.02 → full depth, ≥ 0.06 → none (a sustained tone 0.00–0.01, a ±50-cent vibrato ≈ 0.018; the flute's
                // onset, a 0.73·f mode at −10 dB beside the fundamental, 0.1–0.9: every order would mix it off the grid). It drops at once and recovers with τ 20 ms; the applied depth
                // ramps block to block (no step). A sustained tone (its harmonics, its vibrato): gate 1, the sound unchanged.
                // It only matters where it can reach 16 kHz: the top order's products span up to ~kTop·1.5·f (the band is ±f/4
                // wide, its skirts wider) — a low note's aperiodic neighbours (a bell's modes, a bassoon's buzz) land far below
                // (the gate stands aside and costs nothing: RISK 0 under kTop·1.5·f = 12 kHz, full at 16 kHz).
                tnBand (L, R, mono.data(), gpk.data() + 8, n, bandF, bandP, band);
                {
                    const double fNow = (double) fDom * std::exp2 ((double) (pitchCents - pc0) / 1200.0);
                    const float risk = std::clamp ((float) ((kTop * 1.5 * fNow - 12000.0) / 4000.0), 0.f, 1.f);
                    float gNow = 1.f;
                    if (risk > 0.f)
                    {
                        const float P = (float) std::clamp ((double) sr / std::max (20.0, fNow), 2.0, (double) kTnHist - 8.0);
                        const float rho = tnAperiodicity (mono.data(), n, hist.data(), histHead, P);
                        gNow = 1.f - risk * (1.f - std::clamp ((0.06f - rho) / 0.04f, 0.f, 1.f));
                        lastRho = rho;
                    }
                    else lastRho = 0.f;
                    gate = gNow < gate ? gNow : gate + blk * (gNow - gate);
                }
                const float aApplied0 = excOn ? aEff : 0.f, aApplied1 = aCur * gate;
                aEff = aApplied1;
                // H = e·a·Q(y/e): e the continuous group-peak envelope (release ~30 periods: rides a fast decay or a tremolo
                // without tracing the waveform), a the smoothed depth
                const float d16 = std::exp (-(float) kTnGroup / (float) (std::clamp (30.0 / (double) fDom, 0.02, 0.3) * sr));
                const int ng = (n + kTnGroup - 1) / kTnGroup;
                const int W = std::clamp ((int) std::ceil (sr / std::max (20.0, (double) fDom) / kTnGroup), 2, 8);   // ≈ one period, ≥ 2 groups (a crest's sub-sample level varies group to group)
                tnNodes (gpk.data(), ng, W, envSt, d16, qf, nodes.data(), dcn.data());
                const float g1 = 1.f / std::sqrt (1.f + aApplied1 * aApplied1 * qC), g0 = excOn ? gPrev : 1.f;
                const float inv = 1.f / (float) n;
                // into L and R with the level compensation and the even orders' DC removed: the DC (it follows the envelope) is
                // the block mean smoothed (τ 10 ms, block-size independent), subtracted as a ramp to the newest estimate — one
                // block late, continuous at every boundary, nothing above ~16 Hz touched
                TnShape a;
                a.q = qf; a.nodes = nodes.data(); a.dcn = dcn.data();
                a.a0 = aApplied0; a.da = (aApplied1 - aApplied0) * inv;
                a.g0 = g0; a.dg = (g1 - g0) * inv;
                // the slow residual DC (a band that is not a pure sine), tracked PER UNIT OF DEPTH so it glides out with a:
                // subtracted as dcPrev·a0 → dcTgt·a1 across the block (one block late, τ 200 ms)
                a.d0 = excOn ? dcPrev * aApplied0 : 0.f; a.dd = ((excOn ? dcTgt * aApplied1 : 0.f) - a.d0) * inv;
                const float mean = tnShape (mono.data(), L, R, n, a) * inv, aAvg = 0.5f * (aApplied0 + aApplied1);
                const float unit = aAvg > 1.0e-4f ? mean / aAvg : dcTgt;
                dcPrev = excOn ? dcTgt : 0.f;
                dcTgt = excOn ? dcTgt + (1.f - std::exp (-(float) n / (float) (0.2 * sr))) * (unit - dcTgt) : 0.f;
                gPrev = g1; excOn = true; stages |= 1;
            }

            /** The − side's depth now (0..1, smoothed τ 20 ms): SPARSE · (−t)^1.5. The engine's own tilt section turns into a
                low-pass by it (see renderChunk) — no filter of its own, so dark costs nothing extra. */
            float darkDepth (int n, float t) noexcept
            {
                const float blk = 1.f - std::exp (-(float) n / (float) (0.02 * sr));
                const float s = t < 0.f ? -t : 0.f;
                const float tgt = fDom > 0.f ? sparse * s * std::sqrt (s) : 0.f;
                dCur += blk * (tgt - dCur);
                if (std::abs (tgt - dCur) < 1.0e-4f) dCur = tgt;
                if (dCur > 0.f) stages |= 2;
                return dCur;
            }
        };

        struct PendingOn { bool on = false; int note = 60; float vel = 0.8f; int players = 1; float det[kMaxPlayers] {}; uint32_t seed = 0; };

        // ── state ──
        double sr = 48000.0; int maxBlock = 0; float toneK = 0.f, tonePivotHz = 700.f;
        bool nonRealtime = false;
        std::shared_ptr<const OrganicInstrument> inst;
        std::shared_ptr<const OrganicInstrument> retiring[kRetire], graveyard[kRetire];
        Note   notes[kMaxNotes];
        Reader readers[kPool];
        PendingOn pend;
        bool offPending = false, offPedal = false, pedalIsDown = false, pedalUpPending = false;
        uint64_t stampCounter = 0, genCounter = 0;
        std::vector<float> sumL, sumR, envBuf;
        float level = 0.f;
        bool  snap = true;
        float sDyn = 0, sBody = 0, sTone = 0, sRelease = 0.5f, sNoise = 0.5f, sVelo = 0.75f, sImage = 1.f, sSustain = 0.f;
        float toneDark = 0.f;                            // tp108: the low-pass depth the tilt coefficients were made with
        float toneCur = 1.0e9f, leadTone = 0.f, tb0 = 1.f, tb1 = 0.f, ta1 = 0.f, txL = 0.f, txR = 0.f, tyL = 0.f, tyR = 0.f;
        bool  tiltOn = false;
        ToneShape tn;                                    // tp108: the spectrum-aware exciter (+) and low-pass (−) around the tilt
        float attackP = 0.f;
        // tp105
        Vibrato vib;                                     // ONE plan builder; each note's phase/depth is swapped in and out
        float vibNorm = 0.f, vibRateHz = 5.5f, vibDelaySec = 0.35f, ampRelSec = 0.f, ampAttSec = 0.f;
        int   velCurveMode = 1; bool tuneEq = true;

        //------------------------------------------------------------------------------------------
        void prepare (double sampleRate, int block)
        {
            sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
            maxBlock = std::max (16, block);
            for (auto* v : { &sumL, &sumR, &envBuf }) v->assign ((size_t) maxBlock, 0.f);
            tn.prepare (sr, maxBlock);
            tonePivotHz = 700.f;
            toneK = (float) std::tan (3.14159265358979 * 700.0 / sr);
            (void) sincTable();
            reset();
        }

        void reset() noexcept
        {
            for (auto& n : notes) { n.used = false; n.nrd = 0; }
            for (auto& r : readers) r.active = false;
            pend.on = false; offPending = false; pedalUpPending = false; pedalIsDown = false;
            level = 0.f; snap = true; toneCur = 1.0e9f; leadTone = 0.f; txL = txR = tyL = tyR = 0.f; tiltOn = false;
            tn.reset(); toneDark = 0.f;
            for (auto& p : retiring) if (p != nullptr && ! org::deferRelease (p)) { for (auto& g : graveyard) if (g == nullptr) { g = std::move (p); break; } }
        }

        bool busy() const noexcept
        {
            if (pend.on) return true;
            for (auto& n : notes) if (n.used) return true;
            return false;
        }

        //------------------------------------------------------------------------------------------
        void setInstrument (std::shared_ptr<const OrganicInstrument> ni) noexcept
        {
            if (ni == inst) return;
            const OrganicInstrument* old = inst.get();
            bool referenced = false;
            for (auto& n : notes)
            {
                if (! n.used) continue;
                if (! n.started) continue;                          // not sounding yet → it starts on the new instrument
                n.orphan = true;
                for (int i = 0; i < n.nrd; ++i) { auto& rd = readers[n.rd[i]]; startFade (rd, fadeFrames (0.005)); if (rd.ins == old) referenced = true; }
            }
            if (old != nullptr)
            {
                if (referenced)
                {
                    int slot = -1;
                    for (int i = 0; i < kRetire; ++i) if (retiring[i] == nullptr) { slot = i; break; }
                    if (slot < 0)
                    {
                        // 5 swaps inside one 5 ms fade: hard-stop the oldest retiring instrument's readers.
                        slot = 0;
                        for (auto& rd : readers) if (rd.active && rd.ins == retiring[0].get()) { rd.active = false; detach (rd); }
                        pushRelease (retiring[0]);
                    }
                    retiring[slot] = std::move (inst);
                }
                else pushRelease (inst);
            }
            inst = std::move (ni);
        }

        void pushRelease (std::shared_ptr<const OrganicInstrument>& p) noexcept
        {
            if (p == nullptr) return;
            if (org::deferRelease (p)) return;
            for (auto& g : graveyard) if (g == nullptr) { g = std::move (p); return; }
            // Every fallback full (never seen): keep it on the engine rather than free on the audio thread.
            for (auto& g : retiring) if (g == nullptr) { g = std::move (p); return; }
        }

        void updateRetiring() noexcept
        {
            for (auto& p : retiring)
            {
                if (p == nullptr) continue;
                bool used = false;
                for (auto& rd : readers) if (rd.active && rd.ins == p.get()) { used = true; break; }
                if (! used) pushRelease (p);
            }
            for (auto& g : graveyard) if (g != nullptr && org::deferRelease (g)) {}
        }

        //------------------------------------------------------------------------------------------
        void noteOn (int note, float vel, int players, const float* det, uint32_t seed) noexcept
        {
            pend.on = true;
            pend.note = std::clamp (note, 0, 127);
            pend.vel = clamp01 (vel);
            pend.players = std::clamp (players, 1, kMaxPlayers);
            for (int k = 0; k < kMaxPlayers; ++k) pend.det[k] = (det != nullptr && k < pend.players) ? det[k] : 0.f;
            pend.seed = seed;
            offPending = false;
        }
        void noteOff (bool pedalDown) noexcept { offPending = true; offPedal = pedalDown; }
        void pedal (bool down) noexcept { pedalIsDown = down; if (! down) pedalUpPending = true; }

        void kill() noexcept
        {
            pend.on = false; offPending = false;
            for (auto& n : notes)
            {
                if (! n.used) continue;
                n.killed = true;
                if (! n.started) { freeNote (n); continue; }
                for (int i = 0; i < n.nrd; ++i) startFade (readers[n.rd[i]], fadeFrames (0.005));
            }
        }

        int fadeFrames (double sec) const noexcept { return std::max (1, (int) std::lround (sec * sr)); }

        void startFade (Reader& rd, int frames, int delay = 0) noexcept
        {
            frames = std::max (1, frames);
            if (rd.fading)
            {
                const int endsIn = rd.fadeDelay + rd.fadeRemain;
                if (endsIn <= delay + frames) return;             // the running fade already ends sooner
                float cur = rd.fadeStart;
                if (rd.fadeDelay == 0) cur *= smooth01 ((float) rd.fadeRemain / (float) rd.fadeLen);
                if (delay > 0 && rd.fadeDelay == 0) delay = 0;    // already descending: shorten from here
                rd.fadeStart = cur;
            }
            else rd.fadeStart = 1.f;
            rd.fading = true; rd.fadeDelay = delay; rd.fadeRemain = frames; rd.fadeLen = frames;
        }

        void detach (Reader& rd) noexcept
        {
            if (rd.note < 0) return;
            auto& n = notes[rd.note];
            const int self = (int) (&rd - readers);
            for (int i = 0; i < n.nrd; ++i) if (n.rd[i] == self) { n.rd[i] = n.rd[--n.nrd]; break; }
            rd.note = -1;
        }

        void freeNote (Note& n) noexcept
        {
            for (int i = 0; i < n.nrd; ++i) { readers[n.rd[i]].active = false; readers[n.rd[i]].note = -1; }
            n.nrd = 0; n.used = false;
        }

        //------------------------------------------------------------------------------------------
        static float layerGain (const org::Region& r, float v) noexcept
        {
            if (v < r.fiLo || v > r.foHi) return 0.f;
            float g = 1.f;
            if (r.fiHi > r.fiLo && v < r.fiHi) g *= std::sin (1.5707963f * (v - r.fiLo) / (r.fiHi - r.fiLo));
            if (r.foHi > r.foLo && v > r.foLo) g *= std::cos (1.5707963f * (v - r.foLo) / (r.foHi - r.foLo));
            return g;
        }

        static bool rrPass (const org::Region& r, const Note& n) noexcept
        {
            if (r.rrLen > 1 && (int) (n.rrIdx % (uint32_t) r.rrLen) != r.rrPos) return false;
            if (r.randLo > 0.f || r.randHi < 1.f)
                if (! (n.uRand >= r.randLo && (n.uRand < r.randHi || r.randHi >= 1.f))) return false;
            return true;
        }

        /** The loudest attack region of one (key, velocity) cell; strict = honour this note's RR / random slot. */
        int bestIn (const Note& n, int key, int vi, float v, bool strict) const noexcept
        {
            const auto& I = *inst;
            const auto& sp = I.span (n.artic, org::Kind::Attack, key, vi);
            const uint16_t* L = I.list (sp);
            int best = -1; float bg = 0.f;
            for (uint32_t i = 0; i < sp.count; ++i)
            {
                const auto& r = I.regions[L[i]];
                if (strict && ! rrPass (r, n)) continue;
                const float g = layerGain (r, v);
                if (g > bg) { bg = g; best = L[i]; }
            }
            return best;
        }

        /** Top (loudest) attack region at key/vel for this note. tp105 NO-SILENCE law (Max: "it's round-robinning to
            a silence"): a key outside the authored range plays its nearest mapped key; a slot this note's RR pick does
            not cover plays another RR of the same cell; a velocity hole plays the nearest layer (lower first).
            −1 only when the articulation has no attack region at all. */
        int topRegion (const Note& n, int key, float v) const noexcept
        {
            const int k  = inst->mappedKey (n.artic, key);
            const int vi = std::clamp ((int) std::lround (v), 1, 127);
            int t = bestIn (n, k, vi, v, true);
            if (t < 0) t = bestIn (n, k, vi, v, false);
            for (int d = 1; t < 0 && d < 127; ++d)
                for (int w : { vi - d, vi + d })
                {
                    if (w < 1 || w > 127) continue;
                    t = bestIn (n, k, w, (float) w, true);
                    if (t < 0) t = bestIn (n, k, w, (float) w, false);
                    if (t >= 0) break;
                }
            return t;
        }

        struct Targets { int idx[kMaxTargets]; float pw[kMaxTargets]; int n = 0;
                         void add (int i, float p) noexcept { for (int k = 0; k < n; ++k) if (idx[k] == i) { pw[k] += p; return; }
                                                               if (n < kMaxTargets) { idx[n] = i; pw[n] = p; ++n; } } };

        /** ADD this velocity's layer set (× the two Body shifts) to T as POWER, scaled by wPow. The caller
            takes the square root once every contribution is in (so a region reached twice sums in power). */
        void addTargets (const Note& n, float vEff, float body, float wPow, Targets& T) const noexcept
        {
            const auto& I = *inst;
            const float bs = std::clamp (6.f * body, -6.f, 6.f);
            const float s0 = std::floor (bs), w = bs - s0;
            const int   ns = w > 1.0e-4f ? 2 : 1;
            const int   vi = std::clamp ((int) std::lround (vEff), 1, 127);
            for (int si = 0; si < ns; ++si)
            {
                const int   sh = (int) s0 + si;
                const float ws = ns == 1 ? 1.f : (si == 0 ? std::cos (1.5707963f * w) : std::sin (1.5707963f * w));
                const int anchor = inst->mappedKey (n.artic, n.key);
                int key2 = inst->mappedKey (n.artic, n.key + sh + n.fake);
                // Upward repitch clamp (+7 st total): walk the borrowed key back toward the played (mapped) key.
                for (int guard = 0; guard < 24; ++guard)
                {
                    const int t = topRegion (n, key2, vEff);
                    if (t < 0 || key2 == anchor || n.key - I.regions[(size_t) t].root <= 7) break;
                    key2 = inst->mappedKey (n.artic, key2 + (key2 < anchor ? 1 : -1));
                }
                const auto& sp = I.span (n.artic, org::Kind::Attack, key2, vi);
                const uint16_t* L = I.list (sp);
                bool added = false;
                for (uint32_t i = 0; i < sp.count; ++i)
                {
                    const auto& r = I.regions[L[i]];
                    if (! rrPass (r, n)) continue;
                    const float g = layerGain (r, vEff) * ws;
                    if (g > 1.0e-5f) { T.add (L[i], g * g * wPow); added = true; }
                }
                // NO-SILENCE fallback: nothing in this cell for this note's RR pick / velocity → the nearest that is
                if (! added && ws > 1.0e-5f)
                    if (const int t = topRegion (n, key2, vEff); t >= 0) T.add (t, ws * ws * wPow);
            }
        }

        //------------------------------------------------------------------------------------------
        int allocReader() noexcept
        {
            int live = 0;
            for (auto& r : readers) if (r.active && ! r.fading) ++live;
            if (live >= kCap)
            {
                // steal: the oldest RELEASING reader first, else the oldest
                int v = -1; uint64_t best = ~0ull;
                for (int pass = 0; pass < 2 && v < 0; ++pass)
                    for (int i = 0; i < kPool; ++i)
                    {
                        auto& r = readers[i];
                        if (! r.active || r.fading) continue;
                        const bool rel = r.role == org::Kind::Release || (r.note >= 0 && notes[r.note].released);
                        if (pass == 0 && ! rel) continue;
                        if (r.stamp < best) { best = r.stamp; v = i; }
                    }
                if (v >= 0) { startFade (readers[v], fadeFrames (0.005)); gSteals.fetch_add (1, std::memory_order_relaxed); }
            }
            for (int i = 0; i < kPool; ++i) if (! readers[i].active) return i;
            // every physical slot is busy fading (never seen with 48 spare slots): recycle the QUIETEST fade
            int v = -1; float best = 1.0e30f;
            for (int i = 0; i < kPool; ++i)
            {
                const auto& r = readers[i];
                if (! r.fading) continue;
                const float lvl = r.gPrev * (r.fadeDelay > 0 ? r.fadeStart : r.fadeStart * smooth01 ((float) r.fadeRemain / (float) r.fadeLen));
                if (lvl < best) { best = lvl; v = i; }
            }
            if (v < 0) return -1;
            detach (readers[v]); readers[v].active = false;
            return v;
        }

        double ratioFor (const Note& n, const org::Region& r, const org::Sample& s, float pitchCents) const noexcept
        {
            const double semis = (double) (n.key - r.root) + ((double) r.cents + (tuneEq ? (double) r.tfix : 0.0) + (double) pitchCents + n.detC + n.humC) * 0.01;
            return std::clamp ((s.sampleRate / sr) * std::exp2 (semis * (1.0 / 12.0)), 1.0e-4, 64.0);
        }

        void spawn (int noteIdx, int ridx, org::Kind role, bool atStart, float layerG, float pitchCents) noexcept
        {
            auto& n = notes[noteIdx];
            if (n.nrd >= kPerNote || inst == nullptr) return;
            const auto& I = *inst;
            const auto& r = I.regions[(size_t) ridx];
            const auto& s = I.samples[(size_t) r.smp];
            const double ratio = ratioFor (n, r, s, pitchCents);
            const float atk = n.attack;                             // the note's Attack, fixed at its start
            double p = (double) r.start;
            if (role == org::Kind::Attack)
            {
                if (atk > 0.f) p += (double) atk * std::max (0.0, (double) r.onset - 0.0015 * s.sampleRate - (double) r.start);
                p += (double) n.humStartSec * s.sampleRate;
            }
            const double pStart = p;
            if (role == org::Kind::Attack && ! atStart)
            {
                p += (double) n.age * ratio;                           // time-aligned join (live Dynamics / Body)
                if (r.looping() && p >= (double) r.le)
                    p = (double) r.ls + std::fmod (p - (double) r.ls, (double) (r.le - r.ls));
            }
            if (p >= (double) r.end - 2.0) return;
            const int slot = allocReader();
            if (slot < 0) return;
            auto& rd = readers[slot];
            rd = Reader();
            rd.active = true; rd.note = noteIdx; rd.ins = &I; rd.r = &r; rd.s = &s; rd.ridx = ridx; rd.role = role;
            rd.pos = p; rd.ratio = ratio; rd.layer = layerG;
            const float pan = r.pan * 0.01f;
            rd.panL = std::min (1.f, 1.f - pan); rd.panR = std::min (1.f, 1.f + pan);
            // start envelope: 2 ms C2 declick (Tight: ends before the onset; Gentle: 0-150 ms raised fade) + Tight lift
            int fin = fadeFrames (0.002);
            if (role == org::Kind::Attack && atk > 0.f)
                fin = std::clamp ((int) (((double) r.onset - pStart) / ratio), fadeFrames (0.0005), fin);
            // tp107 THE SWELL: knob 0.5..1 → a log-tapered fade 2 ms … 3 s. LINKED TO THE AMP ENVELOPE like Release: the onset
            // is max(amp-env attack, the knob's fade) — while the amp attack is the longer one the voice's VCA ramp is the
            // onset and the engine keeps its 2 ms declick (no double fade); past it, the engine's fade is the onset.
            if (role == org::Kind::Attack && atk < 0.f)
            {
                const float kf = swellSec (-atk);
                if (kf > ampAttSec) fin = std::max (fin, fadeFrames (kf));
            }
            rd.fadeInLen = fin;
            if (role == org::Kind::Attack && atk > 0.f) { rd.liftExtra = dbToLin (4.f * atk) - 1.f; rd.liftLen = fadeFrames (0.012); }
            if (atStart) rd.firstBlock = true;
            else
            {
                // mid-note join: the layer ramps in from 0 over the block AND continues the note's attack envelope
                // (a layer joining during Gentle's 150 ms fade must not arrive at full level)
                rd.firstBlock = false; rd.gPrev = 0.f;
                if (role == org::Kind::Attack) rd.age = n.age;
            }
            if (r.offByIdx >= 0) rd.chokeSeen = I.groupEpoch()[r.offByIdx].load (std::memory_order_relaxed);
            rd.stamp = ++stampCounter;
            n.rd[n.nrd++] = slot;
        }

        //------------------------------------------------------------------------------------------
        void resolveNoteOn (const OrganicParams& P) noexcept
        {
            pend.on = false;
            // A busy engine retriggered: sounding notes hand over (loops 5 ms, decays their natural handoff).
            for (auto& n : notes)
            {
                if (! n.used) continue;
                if (! n.started) { freeNote (n); continue; }
                n.killed = true; n.released = true;
                for (int i = 0; i < n.nrd; ++i)
                {
                    auto& rd = readers[n.rd[i]];
                    // release + noise tails ring on (the steal-fade takes the oldest when the cap is hit)
                    if (rd.role != org::Kind::Attack || rd.r->loop == org::Loop::OneShot) continue;
                    startFade (rd, rd.r->decaying() ? fadeFrames (0.04 + 0.4 * sRelease) : fadeFrames (0.005));
                }
            }
            const uint64_t gen = ++genCounter;
            // tp105 Velocity Curve (back panel): Soft / Linear (authored) / Hard, applied to the played velocity before
            // the authored layer map + velCurve see it (Linear is the identity: bit-identical to round 1).
            velCurveMode = std::clamp (P.velCurve, 0, 2);
            const float vPlayed = velCurveMode == 1 ? pend.vel : std::pow (pend.vel, velCurveMode == 0 ? 0.55f : 1.8f);
            const int key = pend.note, vIdx = std::clamp ((int) std::lround (vPlayed * 127.f), 1, 127);
            const int artic = inst != nullptr ? std::clamp (P.artic, 0, inst->numArtics - 1) : 0;
            float maxDet = 0.f;
            for (int k = 0; k < pend.players; ++k) maxDet = std::max (maxDet, std::abs (pend.det[k]));
            const float D = clamp01 (maxDet / 25.f);
            uint32_t seqBase = 0;
            if (inst != nullptr)
            {
                seqBase = inst->rrSeq()[(size_t) artic * 128 + (size_t) key].fetch_add (1, std::memory_order_relaxed);
                // choke: this note's groups tick ONCE (so a note's own players never choke each other)
                {
                    const auto& sp = inst->span (artic, org::Kind::Attack, inst->mappedKey (artic, key), vIdx);
                    const uint16_t* L = inst->list (sp);
                    int done[8]; int nd = 0;
                    for (uint32_t i = 0; i < sp.count; ++i)
                    {
                        const int g = inst->regions[L[i]].grpIdx;
                        if (g < 0) continue;
                        bool seen = false; for (int q = 0; q < nd; ++q) seen |= done[q] == g;
                        if (seen) continue;
                        if (nd < 8) done[nd++] = g;
                        inst->groupEpoch()[g].fetch_add (1, std::memory_order_relaxed);
                    }
                }
            }
            const float h = clamp01 (P.human);

            for (int k = 0; k < pend.players; ++k)
            {
                int slot = -1;
                for (int i = 0; i < kMaxNotes; ++i) if (! notes[i].used) { slot = i; break; }
                if (slot < 0) break;
                auto& n = notes[slot];
                n = Note();
                n.used = true; n.gen = gen; n.key = key; n.vIdx = vIdx; n.vel01 = vPlayed; n.k = k; n.artic = artic; n.human = h;
                n.mapKey = inst != nullptr ? inst->mappedKey (artic, key) : key;
                Rng rng (pend.seed ^ (0x9E3779B1u * (uint32_t) (k + 1)));
                const float uDet = rng.next(), uLvl = rng.next(), uStart = rng.next(), uTone = rng.next(), uTime = rng.next();
                n.uRand = rng.next(); n.uFake = rng.next();
                n.humC = (2.f * uDet - 1.f) * 4.f * h;
                // tp107b — Human's level spread (the same 3 dB·h wide) never makes a press QUIETER than its Human-0 self: 0 … +3 dB·h.
                //  Max: "a press never goes quiet because of randomisation" — the vibraphone's Bowed soft layer sits right at the
                //  −60 dBFS audibility line for its first 50 ms, and the old ±1.5 dB·h draw dropped presses under it (the --sweep's
                //  21 at Human 0.41 / 1). Human 0 → 0 dB, bit-identical.
                n.humLvl = dbToLin (uLvl * 3.f * h);
                n.humStartSec = uStart * 0.006f * h;
                n.humToneDb = (2.f * uTone - 1.f) * 1.5f * h;
                n.delay = (int) std::lround (((double) uTime * 0.012 * h + (double) k * 0.007 * D) * sr);
                n.rrIdx = seqBase + (uint32_t) k;
                n.detC = pend.det[k];
                n.nzSeed = mix32 (pend.seed ^ 0x6E6F6973u ^ (0x85EBCA6Bu * (uint32_t) (k + 1)));   // tp107: its own stream (the draws above are untouched)
                // tp105 Ensemble vibrato: every player its own rate (±3 %, golden-angle spread) and phase (golden-ratio
                // spread), plus Human-scaled randomness — a section shimmers instead of beating in lockstep. Player 0
                // starts at the zero crossing. Deterministic at Human 0 (the spread is k-indexed, not random).
                {
                    const float uVr = rng.next(), uVp = rng.next();   // drawn AFTER the round-1 draws: those stay identical
                    n.vibRateMul = 1.f + 0.03f * std::sin (2.39996f * (float) k) + 0.04f * h * (uVr - 0.5f);
                    const float ph = 0.618034f * (float) k + 0.25f * h * uVp;
                    n.vibPhase = k == 0 && h <= 0.f ? 0.f : ph - std::floor (ph);
                }
            }
        }

        void startNote (int idx, const OrganicParams& P, float pitchCents) noexcept
        {
            auto& n = notes[idx];
            n.started = true;
            if (inst == nullptr || n.killed) { freeNote (n); return; }
            const auto& I = *inst;
            n.artic = std::clamp (n.artic, 0, I.numArtics - 1);
            n.vL = std::clamp ((float) n.vIdx + 63.f * sDyn, 1.f, 127.f);
            attackP = std::clamp (P.attack, -1.f, 1.f);
            n.attack = attackP;
            const int key0 = std::clamp (n.key + (int) std::lround (6.f * sBody), 0, 127);

            // random RR: no immediate repeat (remap the draw into the complement of the last pick's range)
            {
                auto& last = I.rrLast()[(size_t) n.artic * 128 + (size_t) n.key];
                const int prev = last.load (std::memory_order_relaxed);
                if (prev >= 0 && prev < (int) I.regions.size())
                {
                    const auto& pr = I.regions[(size_t) prev];
                    if ((pr.randLo > 0.f || pr.randHi < 1.f) && topRegion (n, key0, n.vL) == prev)
                    {
                        const float lo = pr.randLo, len = std::min (1.f, pr.randHi) - pr.randLo;
                        float u = n.uRand * (1.f - len);
                        if (u >= lo) u += len;
                        n.uRand = std::min (u, 0.99999f);
                    }
                }
            }
            // fake RR (the set has no RR at this key): no-repeat choice of {0, −1, +1} → borrow a neighbour zone
            if (n.human > 0.f && ! I.keyHasRR (n.artic, I.mappedKey (n.artic, n.key)))
            {
                auto& last = I.fakeLast()[n.key];
                const int prev = std::clamp ((int) last.load (std::memory_order_relaxed), -1, 1);
                int opts[2], no = 0;
                for (int c : { 0, -1, 1 }) if (c != prev && no < 2) opts[no++] = c;
                const int c = opts[n.uFake < 0.5f ? 0 : 1];
                last.store (c, std::memory_order_relaxed);
                n.fake = 0;
                if (c != 0)
                {
                    const int base = topRegion (n, key0, n.vL);
                    // tp107b — A BORROW MUST SOUND AS PROMPTLY AS THE NOTE'S OWN TAKE (Max: "a press never goes quiet because of
                    //  randomisation"). The onset in OUTPUT time (lead-in frames ÷ the repitch ratio) of the neighbour must be within
                    //  15 ms of the own region's; otherwise the note plays its own region. The --sweep found the vibraphone's Bowed
                    //  keys 46/47 (mapped to the 57 zone, 4.5 ms lead-in) borrowing the 64 zone (238 ms of bow before the onset,
                    //  repitched down 18 st → 0.68 s late: silent in the window). Human 0 never borrows → bit-identical there.
                    auto onsetOut = [&] (int ri) {
                        const auto& rg = I.regions[(size_t) ri];
                        const double ratio = (I.samples[(size_t) rg.smp].sampleRate / sr) * std::exp2 ((double) (n.key - rg.root) / 12.0);
                        return (double) std::max<int64_t> (0, rg.onset - rg.start) / std::max (1.0e-4, ratio) / sr;
                    };
                    const double own = base >= 0 ? onsetOut (base) : 0.0;
                    for (int d = 1; d <= 6; ++d)
                    {
                        const int k2 = key0 + c * d;
                        if (k2 < 0 || k2 > 127) break;
                        const int t = topRegion (n, k2, n.vL);
                        if (t >= 0 && t != base && n.key - I.regions[(size_t) t].root <= 7 && std::abs (onsetOut (t) - own) <= 0.015)
                        { n.fake = c * d; break; }
                    }
                }
            }
            // Gentle: the first 60 ms lean toward the next-softer layer
            if (attackP < 0.f)
            {
                const int t = topRegion (n, key0, n.vL);
                if (t >= 0 && I.regions[(size_t) t].fiLo > 1.5f)
                {
                    n.gentle = -attackP;
                    n.vSoft = std::max (1.f, std::min (I.regions[(size_t) t].fiLo, (float) I.regions[(size_t) t].lv) - 1.f);
                    // tp107: the softer-layer blend rides along with the swell — 60 ms, or half the swell when that is longer
                    n.gLen = (int64_t) (std::max (0.06, 0.5 * (double) swellSec (-attackP)) * sr);
                }
            }
            updateTargets (idx, pitchCents, true);
            if (const int t = topRegion (n, key0 + n.fake, n.vL); t >= 0)
            {
                gLastRegion.store (t, std::memory_order_relaxed);
                const auto& tr = I.regions[(size_t) t];
                if (tr.randLo > 0.f || tr.randHi < 1.f) I.rrLast()[(size_t) n.artic * 128 + (size_t) n.key].store (t, std::memory_order_relaxed);
            }
            // tp105: "trig":"on" mechanical noise (hammer / key-down thump, breath onset, pick) starts WITH the note —
            // the first two players only (a section's sixteen thumps would be a drum roll, not a section).
            if (I.hasNoise && n.k < 2) spawnKind (idx, org::Kind::Noise, true, pitchCents);
            if (n.relPending) { n.relPending = false; n.released = false; releaseNote (idx, pitchCents); }
        }

        /** Spawn every region of `kind` for this note's cell (mapped key when the played key has none). onNoise: only
            "trig":"on" noise; otherwise "on" noise is skipped (it already sounded at the note's start). */
        void spawnKind (int idx, org::Kind kind, bool onNoise, float pitchCents) noexcept
        {
            auto& n = notes[idx];
            const auto& I = *inst;
            // CPU: a knob at 0 is OFF — its regions would render at gain 0 for their whole length (Salamander: a hammer
            // and a damper reader per note). Not spawned; a knob raised later affects the next notes.
            if ((kind == org::Kind::Noise && sNoise <= 0.f) || (kind == org::Kind::Release && sRelease <= 0.f)) return;
            const auto* spp = &I.span (n.artic, kind, n.key, n.vIdx);
            if (spp->count == 0) spp = &I.span (n.artic, kind, n.mapKey, n.vIdx);   // out of range → the edge zone's
            const auto& sp = *spp;
            const uint16_t* L = I.list (sp);
            if (kind == org::Kind::Noise) { spawnNoise (idx, sp, L, onNoise, pitchCents); return; }
            for (uint32_t i = 0; i < sp.count; ++i)
            {
                const auto& r = I.regions[L[i]];
                if (kind == org::Kind::Noise && r.trigOn != onNoise) continue;
                if (! rrPass (r, n)) continue;
                const int before = n.nrd;
                spawn (idx, L[i], kind, true, 1.f, pitchCents);
                if (n.nrd > before && ! onNoise) readers[n.rd[n.nrd - 1]].rtAtt = dbToLin (-r.rtDecay * n.heldSec);
            }
        }

        /** tp107 THE NOISE ROUND-ROBIN (contract tp107: "like a player"). One DECISION per note-on and per note-off:
              · chance = noiseChance(knob) (0 never · 0.5 two notes in three · 1 nine in ten), drawn from this player's own
                stream (nzSeed → deterministic at a fixed seed; independent of every other draw of the note);
              · the variants = the cell's noise regions grouped by their RR slot / random range (the compiler's takes);
                regions with neither play with every variant. The variant is a no-repeat draw (the last one per artic ×
                trig × key lives on the instrument, like rrLast, so a repeated key never thumps the same take twice running);
              · level ±3 dB, start 0–8 ms late; Human adds ±2·h dB and up to 4·h ms more. */
        void spawnNoise (int idx, const org::Span& sp, const uint16_t* L, bool onNoise, float pitchCents) noexcept
        {
            auto& n = notes[idx];
            const auto& I = *inst;
            int cand[kPerNote]; int nc = 0;
            for (uint32_t i = 0; i < sp.count && nc < kPerNote; ++i)
                if (I.regions[L[i]].trigOn == onNoise) cand[nc++] = L[i];
            if (nc == 0) return;
            gNzDecisions.fetch_add (1, std::memory_order_relaxed);
            Rng rng (n.nzSeed ^ (onNoise ? 0x0Fu : 0xF0u));
            const float uHit = rng.next(), uVar = rng.next(), uLvl = rng.next(), uTime = rng.next();
            if (uHit >= noiseChance (sNoise)) return;
            // variants: distinct (RR slot, random range) keys among the conditional regions, in a stable order
            auto keyOf = [] (const org::Region& r) -> int {
                const bool rr = r.rrLen > 1, rnd = r.randLo > 0.f || r.randHi < 1.f;
                if (! rr && ! rnd) return -1;                                           // plays with every variant
                return (rr ? r.rrPos + 1 : 0) * 4096 + (rnd ? 1 + (int) std::lround (r.randLo * 4000.f) : 0);
            };
            int keys[kPerNote]; int nk = 0;
            for (int c = 0; c < nc; ++c)
            {
                const int k = keyOf (I.regions[(size_t) cand[c]]); if (k < 0) continue;
                bool seen = false; for (int q = 0; q < nk; ++q) seen |= keys[q] == k;
                if (! seen) keys[nk++] = k;
            }
            std::sort (keys, keys + nk);
            int pick = -1;
            if (nk == 1) pick = 0;
            else if (nk > 1)
            {
                auto& last = I.noiseLast()[((size_t) n.artic * 2 + (onNoise ? 1u : 0u)) * 128 + (size_t) n.key];
                const int prev = last.load (std::memory_order_relaxed);
                pick = std::min (nk - 2, (int) (uVar * (float) (nk - 1)));
                if (prev >= 0 && prev < nk && pick >= prev) ++pick;
                if (prev < 0 || prev >= nk) pick = std::min (nk - 1, (int) (uVar * (float) nk));   // the first event: any take
                last.store (pick, std::memory_order_relaxed);
            }
            const float h = n.human;
            const float db = (2.f * uLvl - 1.f) * (3.f + 2.f * h);
            const int wait = (int) std::lround ((double) uTime * (0.008 + 0.004 * (double) h) * sr);
            bool any = false;
            for (int c = 0; c < nc; ++c)
            {
                const auto& r = I.regions[(size_t) cand[c]];
                const int k = keyOf (r);
                if (k >= 0 && (pick < 0 || k != keys[pick])) continue;
                const int before = n.nrd;
                spawn (idx, cand[c], org::Kind::Noise, true, 1.f, pitchCents);
                if (n.nrd > before)
                {
                    auto& rd = readers[n.rd[n.nrd - 1]];
                    rd.rtAtt = (onNoise ? 1.f : dbToLin (-r.rtDecay * n.heldSec)) * dbToLin (db);
                    rd.wait = wait;
                    any = true;
                }
            }
            if (any)
            {
                gNzHits.fetch_add (1, std::memory_order_relaxed);
                gNzVar.store (pick, std::memory_order_relaxed); gNzVarN.store (nk, std::memory_order_relaxed);
                gNzDb.store (db, std::memory_order_relaxed); gNzDelay.store (wait, std::memory_order_relaxed);
            }
        }

        void updateTargets (int idx, float pitchCents, bool atStart) noexcept
        {
            auto& n = notes[idx];
            const float vT = std::clamp ((float) n.vIdx + 63.f * sDyn, 1.f, 127.f);
            n.vL = atStart ? vT : n.vL + aDynNote * (vT - n.vL);
            const int64_t gLen = n.gLen > 0 ? n.gLen : (int64_t) (0.06 * sr);
            const bool gentleLive = n.gentle > 0.f && n.age < gLen;
            if (! atStart && ! gentleLive && std::abs (n.vL - n.lastVL) < 0.02f && std::abs (sBody - n.lastBody) < 1.0e-4f) return;
            n.lastVL = n.vL; n.lastBody = sBody;
            Targets T;
            if (gentleLive)
            {
                // Gentle: an equal-power 60 ms crossfade FROM the next-softer layer set TO the played one
                const float th = 1.5707963f * n.gentle * (1.f - smooth01 ((float) n.age / (float) gLen));
                const float c = std::cos (th), s = std::sin (th);
                addTargets (n, n.vL, sBody, c * c, T);
                addTargets (n, n.vSoft, sBody, s * s, T);
            }
            else addTargets (n, n.vL, sBody, 1.f, T);
            for (int k = 0; k < T.n; ++k) T.pw[k] = std::sqrt (T.pw[k]);   // power-summed → gain
            bool used[kMaxTargets] = {};
            for (int i = 0; i < n.nrd; ++i)
            {
                auto& rd = readers[n.rd[i]];
                if (rd.role != org::Kind::Attack || rd.ins != inst.get()) continue;
                float g = 0.f;
                for (int k = 0; k < T.n; ++k) if (T.idx[k] == rd.ridx && ! used[k]) { g = T.pw[k]; used[k] = true; break; }
                rd.layer = g;
            }
            for (int k = 0; k < T.n; ++k)
                if (! used[k] && T.pw[k] > 1.0e-4f) spawn (idx, T.idx[k], org::Kind::Attack, atStart, T.pw[k], pitchCents);
        }

        void releaseNote (int idx, float pitchCents) noexcept
        {
            auto& n = notes[idx];
            if (n.released) return;
            n.released = true; n.pedalHeld = false;
            if (! n.started) { n.relPending = true; return; }
            n.heldSec = (float) ((double) n.age / sr);
            // tp105 A REAL RELEASE (Max: "linked to the envelope… at 100 % make it at least 10 seconds"): every sounding
            // attack region — decaying or looped — now decays exponentially to −60 dB over max(amp-env release, the
            // Release knob's time) (relTime). One-shots play out, as authored. The voice holds its amp VCA for Organics
            // oscillators while this runs, so the amp envelope's release lengthens EVERY instrument, the piano included.
            for (int i = 0; i < n.nrd; ++i)
            {
                auto& rd = readers[n.rd[i]];
                if (rd.role == org::Kind::Attack && rd.r->loop != org::Loop::OneShot) rd.relOn = true;
            }
            if (n.orphan || n.killed || inst == nullptr || ! (inst->hasRelease || inst->hasNoise)) return;
            // release (Release knob) and key-off mechanical noise (Noise knob) trigger here
            if (inst->hasRelease) spawnKind (idx, org::Kind::Release, false, pitchCents);
            if (inst->hasNoise)   spawnKind (idx, org::Kind::Noise,   false, pitchCents);
        }

        /** tp105 note-off decay time (s): the Release knob's taper — 20 ms at 0, the instrument's AUTHORED release
            (map.json env.r, 30 ms..30 s) at 0.5, max(12 s, 2 × authored) at 1 (≤ 30 s; log-interpolated both halves) —
            and never shorter than the voice's amp-envelope release. tp106: the top was max(12 s, authored), which left
            the upper half of the knob DEAD on every instrument authored at ≥ 12 s (the glockenspiel's 15 s: 50 → 100 %
            changed nothing) — the exposure law: 100 % is always twice the authored ring, at least 12 s. */
        float relTime (const org::Region& r) const noexcept
        {
            const float A = std::clamp (r.envR, 0.03f, 30.f), rr = sRelease;
            const float top = std::min (30.f, std::max (12.f, 2.f * A));
            const float T = rr <= 0.5f ? 0.02f * std::pow (A / 0.02f, 2.f * rr)
                                       : A * std::pow (top / A, 2.f * rr - 1.f);
            return std::max (T, ampRelSec);
        }

        //------------------------------------------------------------------------------------------
        float targetGain (const Reader& rd, const Note& n, float& compDbOut) const noexcept
        {
            const auto& r = *rd.r;
            const float velAmp = 1.f - sVelo * (1.f - r.velCurve[n.vIdx]);
            float g = r.staticGain * n.humLvl * velAmp * (1.f / 32768.f);
            compDbOut = 0.f;
            switch (rd.role)
            {
                case org::Kind::Attack:
                    g *= rd.layer;
                    if (rd.relTail)
                    {
                        // tp105b: the release curve itself, from the note-off level — the recording's own (faster, or truncated)
                        // decay is compensated away by its envelope table, so −60 dB lands exactly at the requested time
                        const float es = rd.s->envAt (rd.pos);
                        const float want = rd.relEnvOff + 20.f * std::log10 (std::max (rd.relG, 1.0e-9f));
                        compDbOut = std::clamp (want - es, -120.f, 48.f);
                        g *= dbToLin (compDbOut);
                    }
                    else if (r.decaying() && (sSustain > 0.001f || rd.tailWrapped))
                    {
                        // Sustain: the level the note WOULD have (natural decay, continued virtually once the
                        // tail loop has wrapped) with its dB distance below refDb scaled by (1 − s).
                        const float es = rd.s->envAt (rd.pos);
                        const float ev = es - (rd.tailWrapped ? r.tailSlopeDb * (float) ((double) rd.tailAge / sr) : 0.f);
                        const float want = ev < r.refDb ? r.refDb + (1.f - sSustain) * (ev - r.refDb) : ev;
                        compDbOut = std::clamp (want - es, -120.f, 48.f);
                        g *= dbToLin (compDbOut);
                    }
                    break;
                case org::Kind::Release: g *= knobLevel (sRelease) * rd.rtAtt; break;
                case org::Kind::Noise:   g *= noiseLevel (sNoise) * rd.rtAtt; break;
            }
            return g;
        }

        void renderReader (Reader& rd, const Note& n, float pitchCents, float* oL, float* oR, int i0, int nEnd,
                           const Vibrato::Block* vb = nullptr) noexcept
        {
            if (rd.wait > 0)   // tp107: a late noise — silent (and not advancing) until its start
            {
                const int skip = std::min (rd.wait, nEnd - i0);
                rd.wait -= skip; i0 += skip;
                if (i0 >= nEnd) return;
            }
            const auto& r = *rd.r;
            const auto& s = *rd.s;
            const int cnt = nEnd - i0;
            rd.ratio = ratioFor (n, r, s, pitchCents);

            if (r.offByIdx >= 0 && ! rd.fading
                && rd.ins->groupEpoch()[r.offByIdx].load (std::memory_order_relaxed) != rd.chokeSeen)
                startFade (rd, fadeFrames (0.005));                       // choke: always a 5 ms fade

            // tp105b — at the note-off (the first released block) decide: does the requested release outlive the audio left?
            if (rd.relOn && ! rd.relDecided)
            {
                rd.relDecided = true;
                const double xfS = std::min (0.25 * (double) (r.tailLe - r.tailLs), 0.02 * s.sampleRate);
                const double leftSec = ((double) r.end - rd.pos) / std::max (1.0e-6, rd.ratio) / sr;
                if (rd.role == org::Kind::Attack && r.decaying() && r.hasTail() && sSustain <= 0.001f && ! rd.tailWrapped
                    && ! rd.fading && rd.pos + xfS + 16.0 < (double) r.end && (double) relTime (r) > leftSec)
                {
                    rd.relTail = true;
                    gTailRel.fetch_add (1, std::memory_order_relaxed);
                    rd.relEnvOff = s.envAt (rd.pos);
                    rd.relStepDb = 0.f;
                }
            }
            float compDb = 0.f;
            if (rd.relOn)   // exponential, −60 dB after relTime (re-evaluated per block: the knob / its mod / the amp release are live)
                rd.relG *= std::exp (-6.9077553f * (float) cnt / (relTime (r) * (float) sr));
            float g1 = targetGain (rd, n, compDb);
            if (rd.relOn && ! rd.relTail) g1 *= rd.relG;
            const float g0 = rd.firstBlock ? g1 : rd.gPrev;
            rd.firstBlock = false;

            // loop setup: authored loop, or the Sustain tail loop on a decaying region
            bool looping = r.looping();
            double loopS = (double) r.ls, loopE = (double) r.le, xfLen = (double) r.xf;
            if (! looping && rd.role == org::Kind::Attack && r.decaying() && r.hasTail() && sSustain > 0.001f)
            {
                looping = true; loopS = (double) r.tailLs; loopE = (double) r.tailLe;
                xfLen = std::min (0.25 * (loopE - loopS), 0.02 * s.sampleRate);
            }
            const double loopLen = loopE - loopS;
            const double xfEff = std::min (xfLen, loopS);                // adaptive: the lead-in must exist
            const bool   seam = looping && xfEff >= 2.0;

            // terminal declick (2.5 ms) for regions that play out
            const double endPos = (double) r.end;
            if (! looping && ! rd.fading && ! rd.relTail)
            {
                const double remOut = (endPos - 1.0 - rd.pos) / rd.ratio;
                const int endFade = fadeFrames (0.0025);
                if (remOut < (double) (cnt + endFade))
                {
                    const int len = std::max (1, std::min (endFade, (int) remOut));
                    startFade (rd, len, std::max (0, (int) remOut - len));
                }
            }

            // per-sample envelope only while one is running (attack fade/lift, a fade-out)
            const bool needEnv = rd.age < (int64_t) std::max (rd.fadeInLen, rd.liftLen) || rd.fading;
            float* env = envBuf.data();
            bool finishedFade = false;
            if (needEnv)
            {
                int64_t a = rd.age;
                for (int i = 0; i < cnt; ++i, ++a)
                {
                    float e = 1.f;
                    if (a < rd.fadeInLen) e = smoother01 ((float) a / (float) rd.fadeInLen);
                    if (a < rd.liftLen)   e *= 1.f + rd.liftExtra * (1.f - (float) a / (float) rd.liftLen);
                    if (rd.fading)
                    {
                        if (rd.fadeDelay > 0) { --rd.fadeDelay; e *= rd.fadeStart; }
                        else if (rd.fadeRemain > 0) { e *= rd.fadeStart * smooth01 ((float) rd.fadeRemain / (float) rd.fadeLen); --rd.fadeRemain; }
                        else { e = 0.f; finishedFade = true; }
                    }
                    env[i] = e;
                }
                if (rd.fading && rd.fadeDelay == 0 && rd.fadeRemain == 0) finishedFade = true;
            }

            const int16_t* d = s.data();
            const bool sinc = nonRealtime;
            PlainFn plain; SeamFn seamFn; PlainVFn plainV = nullptr; SeamVFn seamV = nullptr;
            if (s.channels == 2) { plain = sinc ? plainFor<2, true> (needEnv) : plainFor<2, false> (needEnv); seamFn = sinc ? seamFor<2, true> (needEnv) : seamFor<2, false> (needEnv); }
            else                 { plain = sinc ? plainFor<1, true> (needEnv) : plainFor<1, false> (needEnv); seamFn = sinc ? seamFor<1, true> (needEnv) : seamFor<1, false> (needEnv); }
            if (vb != nullptr)
            {
                if (s.channels == 2) { plainV = sinc ? plainVFor<2, true> (needEnv) : plainVFor<2, false> (needEnv); seamV = sinc ? seamVFor<2, true> (needEnv) : seamVFor<2, false> (needEnv); }
                else                 { plainV = sinc ? plainVFor<1, true> (needEnv) : plainVFor<1, false> (needEnv); seamV = sinc ? seamVFor<1, true> (needEnv) : seamVFor<1, false> (needEnv); }
            }

            float g = g0;
            const float dg = (g1 - g0) / (float) std::max (1, cnt);
            double pos = rd.pos;
            const double ratio = rd.ratio;
            const double ratioB = vb != nullptr ? ratio * kVibMaxMul : ratio;   // boundary counts: the plan's fastest rate
            int i = 0;
            bool ended = false;
            float kbRest = 1.f, dgc = dg;
            while (i < cnt && rd.relTail)
            {
                // ── tp105b TAIL RELEASE: every pass back into the tail loop is a LEVEL-MATCHED crossfade (the lead-in read is
                //    louder by the decay across the jump; it is scaled down by jumpKb so the sum never steps), equal-gain
                //    smoothstep (the jump is a whole number of loop lengths: correlated material, the fb204 law) ──
                const double tLs = (double) r.tailLs, tLe = (double) r.tailLe, tLen = tLe - tLs;
                const double X = std::max (2.0, std::min (0.25 * tLen, 0.02 * s.sampleRate));
                if (rd.jumpLen == 0)
                {
                    const double jStart = tLe - X;
                    if (pos < jStart)
                    {
                        int m = (int) std::ceil ((jStart - pos) / ratioB);
                        m = std::clamp (m, 1, cnt - i);
                        if (vb != nullptr) plainV (d, pos, ratio, m, g, dgc, env + i, rd.panL, rd.panR, oL + i0 + i, oR + i0 + i, *vb, i0 + i);
                        else plain (d, pos, ratio, m, g, dgc, env + i, rd.panL, rd.panR, oL + i0 + i, oR + i0 + i);
                        i += m;
                        continue;
                    }
                    const double k = std::max (1.0, std::floor ((pos - tLs) / tLen));
                    rd.jumpOff = k * tLen;
                    rd.jumpLen = std::max (1, (int) std::ceil (X / ratio));
                    rd.jumpAt = 0;
                    const double mid = pos + 0.5 * X;
                    rd.jumpDb = std::max (0.f, s.envAt (mid - rd.jumpOff) - s.envAt (mid));
                    rd.jumpKb = dbToLin (-rd.jumpDb);
                }
                // the crossfade (scalar; a few percent of the loop period)
                const int m = std::min (cnt - i, rd.jumpLen - rd.jumpAt);
                const int sh = vb != nullptr ? vb->segShift : 0, msk = (1 << sh) - 1;
                for (int k = 0; k < m; ++k)
                {
                    const int j = i + k;
                    float al, ar, bl, br;
                    const int ip = (int) pos; const double q = pos - rd.jumpOff; const int iq = (int) q;
                    if (s.channels == 2)
                    {
                        if (sinc) { interp<2, true> (d, ip, (float) (pos - ip), al, ar); interp<2, true> (d, iq, (float) (q - iq), bl, br); }
                        else      { interp<2, false> (d, ip, (float) (pos - ip), al, ar); interp<2, false> (d, iq, (float) (q - iq), bl, br); }
                    }
                    else
                    {
                        if (sinc) { interp<1, true> (d, ip, (float) (pos - ip), al, ar); interp<1, true> (d, iq, (float) (q - iq), bl, br); }
                        else      { interp<1, false> (d, ip, (float) (pos - ip), al, ar); interp<1, false> (d, iq, (float) (q - iq), bl, br); }
                    }
                    const float w = smooth01 ((float) (rd.jumpAt + k + 1) / (float) rd.jumpLen);
                    float gg = g; if (needEnv) gg *= env[j];
                    oL[i0 + j] += (al + (bl * rd.jumpKb - al) * w) * (gg * rd.panL);
                    oR[i0 + j] += (ar + (br * rd.jumpKb - ar) * w) * (gg * rd.panR);
                    g += dgc;
                    double mul = 1.0;
                    if (vb != nullptr) { const int jj = i0 + j; mul = vb->mul[jj >> sh] + vb->step[jj >> sh] * (double) (jj & msk); }
                    pos += ratio * mul;
                }
                i += m; rd.jumpAt += m;
                if (rd.jumpAt >= rd.jumpLen)
                {
                    // landed in the loop: the lead-in's level is the level now — the gain carries jumpKb from here (this
                    // block's ramp, and gPrev), and the step leaves the natural level for good (relStepDb), so the next
                    // block's gain (computed at the new position) continues exactly
                    pos -= rd.jumpOff;
                    rd.relStepDb += rd.jumpDb;
                    rd.jumpLen = 0; rd.tailWrapped = true;
                    g *= rd.jumpKb; dgc *= rd.jumpKb; kbRest *= rd.jumpKb;
                }
            }
            while (i < cnt)
            {
                double boundary;
                if (looping)
                {
                    if (pos >= loopE)
                    {
                        pos -= loopLen;
                        if (pos >= loopE) pos = loopS + std::fmod (pos - loopS, loopLen);
                        if (rd.role == org::Kind::Attack && ! r.looping()) rd.tailWrapped = true;
                    }
                    const double xfStart = loopE - xfEff;
                    if (seam && pos >= xfStart)
                    {
                        int m = (int) std::ceil ((loopE - pos) / ratioB);
                        m = std::clamp (m, 1, cnt - i);
                        if (vb != nullptr) seamV (d, pos, ratio, m, g, dg, env + i, rd.panL, rd.panR, oL + i0 + i, oR + i0 + i, loopE, loopLen, 1.0 / xfEff, *vb, i0 + i);
                        else seamFn (d, pos, ratio, m, g, dg, env + i, rd.panL, rd.panR, oL + i0 + i, oR + i0 + i, loopE, loopLen, 1.0 / xfEff);
                        i += m;
                        continue;
                    }
                    boundary = seam ? xfStart : loopE;
                }
                else boundary = endPos;
                int m = (int) std::ceil ((boundary - pos) / ratioB);
                if (m < 1)
                {
                    if (! looping) { ended = true; break; }
                    m = 1;
                }
                m = std::min (m, cnt - i);
                if (vb != nullptr) plainV (d, pos, ratio, m, g, dg, env + i, rd.panL, rd.panR, oL + i0 + i, oR + i0 + i, *vb, i0 + i);
                else plain (d, pos, ratio, m, g, dg, env + i, rd.panL, rd.panR, oL + i0 + i, oR + i0 + i);
                i += m;
            }
            rd.pos = pos;
            rd.gPrev = g1 * kbRest;   // tp105b — a landed tail crossfade carries its level match into the next block's ramp
            rd.age += cnt;
            if (rd.tailWrapped) rd.tailAge += cnt;

            // retire
            if (ended || finishedFade) { rd.active = false; return; }
            if (rd.relTail)
            {
                // the tail release is done when its level re the note-off is under −80 dB (whichever curve carried it)
                const float lvl = s.envAt (rd.pos) + compDb - rd.relEnvOff;
                if (lvl < -80.f && ! rd.fading) startFade (rd, fadeFrames (0.005));
            }
            else if (rd.relOn && rd.relG < 1.0e-4f && ! rd.fading) startFade (rd, fadeFrames (0.005));   // −80 dB into the release: done
            if (rd.role == org::Kind::Attack && g0 <= 0.f && g1 <= 0.f && rd.layer <= 0.f) { rd.active = false; return; }
            if (! r.looping() && ! (looping && sSustain >= 0.999f) && ! rd.relTail)   // (a tail release retires on its own curve, above)
            {
                const float lin = g1 * 32768.f * std::max (rd.panL, rd.panR);    // g1 already holds the Sustain comp
                const float est = rd.s->envAt (rd.pos) + 20.f * std::log10 (lin * (rd.fading ? rd.fadeStart : 1.f) + 1.0e-9f);
                // under −90 dBFS: retire with the house 5 ms fade (a hard stop here stepped the output by up to −83 dBFS)
                if (est < -90.f && rd.age > rd.fadeInLen + 4800 && ! rd.fading) startFade (rd, fadeFrames (0.005));
            }
        }

        //------------------------------------------------------------------------------------------
        /** tp105 VIBRATO for one note (player) this chunk — Vibrato.h's plan (tp55: a sine on the playback rate, rate
            multiplier on a 64-sample grid, linear inside; depth one-pole per block). Depth = the knob's taper × the
            onset envelope (vibDelay of nothing, then a 250 ms smooth fade-in). Rate = the back panel's 3..9 Hz + 0.6 Hz
            × depth (players push faster as they dig in) × this player's own ±3 %. nullptr while parked: the render then
            takes the untouched constant-rate path (depth 0 is bit-identical to no vibrato at all). */
        const Vibrato::Block* vibratoFor (Note& nt, int n) noexcept
        {
            if (! nt.started) return nullptr;
            const float t = (float) ((double) nt.age / sr) - vibDelaySec;
            const float onset = t <= 0.f ? 0.f : smooth01 (t / 0.25f);
            const float depth = vibDepthCents (vibNorm) * onset;
            vib.phase = (double) nt.vibPhase; vib.depthSmoothed = nt.vibDepthSm;
            const auto& b = vib.advance (depth, (vibRateHz + 0.6f * vibNorm) * nt.vibRateMul, sr, n);
            nt.vibDepthSm = vib.depthSmoothed;
            if (! b.active) { if (depth <= 0.f) nt.vibDepthSm = 0.f; return nullptr; }   // parked: keep this player's phase
            nt.vibPhase = (float) vib.phase;
            return &b;
        }

        float aDynNote = 1.f;

        void render (const OrganicParams& P, float pitchCents, float* L, float* R, int n) noexcept
        {
            if (maxBlock <= 0 || L == nullptr || R == nullptr) return;
            juce::ScopedNoDenormals noDenormals;
            int done = 0;
            float peak = 0.f;
            int rendered = 0;
            while (done < n)
            {
                const int m = std::min (maxBlock, n - done);
                peak = std::max (peak, renderChunk (P, pitchCents, L + done, R + done, m, rendered));
                done += m;
            }
            level = peak;
            gLastReaders.store (rendered, std::memory_order_relaxed);
        }

        /** The tilt as direct form I: u = b0·x + b1·x₋₁ (parallel), then y = u − a1·y₋₁ solved 4 samples at a
            time from y₋₁ alone (the serial chain is one FMA per 4 samples instead of two per sample).
            When the target moved, the coefficients glide from→to in 16-sample steps across the block (a DF1
            output steps by Δcoef·signal at a switch: 16–32 small steps instead of one keeps Tone/Velocity
            sweeps free of block-rate zipper). */
        static void tiltBlock (float* x, int n, float& x1, float& y1, const float* from, const float* to) noexcept
        {
            constexpr int kSub = 16;
            const bool glide = from[0] != to[0] || from[1] != to[1] || from[2] != to[2];
            const int nSub = (n + kSub - 1) / kSub;
            float xp = x1, yp = y1;
            for (int sb = 0; sb < nSub; ++sb)
            {
                const int i0 = sb * kSub, i1 = std::min (n, i0 + kSub);
                const float t = glide ? (float) (sb + 1) / (float) nSub : 1.f;
                const float b0 = from[0] + t * (to[0] - from[0]), b1 = from[1] + t * (to[1] - from[1]);
                const float c = -(from[2] + t * (to[2] - from[2])), c2 = c * c, c3 = c2 * c, c4 = c2 * c2;
                for (int i = i0; i < i1; ++i) { const float xi = x[i]; x[i] = b0 * xi + b1 * xp; xp = xi; }
                int i = i0;
                for (; i + 4 <= i1; i += 4)
                {
                    const float u0 = x[i], u1 = x[i + 1], u2 = x[i + 2], u3 = x[i + 3];
                    const float p1 = u1 + c * u0, p2 = u2 + c * u1 + c2 * u0, p3 = u3 + c * u2 + c2 * u1 + c3 * u0;
                    x[i]     = u0 + c  * yp;
                    x[i + 1] = p1 + c2 * yp;
                    x[i + 2] = p2 + c3 * yp;
                    yp = x[i + 3] = p3 + c4 * yp;
                }
                for (; i < i1; ++i) { yp = x[i] + c * yp; x[i] = yp; }
            }
            x1 = xp;
            y1 = std::abs (yp) < 1.0e-15f ? 0.f : yp;
        }

        float renderChunk (const OrganicParams& P, float pitchCents, float* L, float* R, int n, int& rendered) noexcept
        {
            // ── params, smoothed once per block (τ 15 ms; Dynamics per note τ 25 ms) ──
            const float a = snap ? 1.f : 1.f - std::exp (-(float) n / (float) (0.015 * sr));
            aDynNote = 1.f - std::exp (-(float) n / (float) (0.025 * sr));
            auto sm = [a] (float& s, float t) { s += a * (t - s); if (std::abs (t - s) < 1.0e-6f) s = t; };
            sm (sDyn, std::clamp (P.dyn, -1.f, 1.f));   sm (sBody, std::clamp (P.body, -1.f, 1.f));
            sm (sTone, std::clamp (P.tone, -1.f, 1.f)); sm (sRelease, clamp01 (P.release)); sm (sNoise, clamp01 (P.noise));
            sm (sVelo, clamp01 (P.velo));               sm (sSustain, clamp01 (P.sustain));
            const float img0 = sImage;
            sm (sImage, std::clamp (P.image, 0.f, 1.5f));
            snap = false;
            attackP = std::clamp (P.attack, -1.f, 1.f);
            vibNorm     = clamp01 (P.vibrato);
            vibRateHz   = std::clamp (P.vibRate, 0.5f, 12.f);
            vibDelaySec = std::clamp (P.vibDelay, 0.f, 4.f);
            ampRelSec   = std::clamp (P.ampRelease, 0.f, 60.f);
            ampAttSec   = std::clamp (P.ampAttack, 0.f, 60.f);
            tuneEq      = P.tuning != 0;

            // ── events (they land at the start of this block) ──
            if (pend.on) resolveNoteOn (P);
            if (offPending)
            {
                offPending = false;
                for (int i = 0; i < kMaxNotes; ++i)
                {
                    auto& nt = notes[i];
                    if (! nt.used || nt.released || nt.killed) continue;
                    if (offPedal) nt.pedalHeld = true;
                    else releaseNote (i, pitchCents);
                }
            }
            if (pedalUpPending && ! pedalIsDown)
            {
                pedalUpPending = false;
                for (int i = 0; i < kMaxNotes; ++i) if (notes[i].used && notes[i].pedalHeld) releaseNote (i, pitchCents);
            }

           #ifdef ORG_DEBUG_CHECKS
            {
                int seen[kPool] = {};
                for (int ni = 0; ni < kMaxNotes; ++ni) if (notes[ni].used) for (int k = 0; k < notes[ni].nrd; ++k) seen[notes[ni].rd[k]]++;
                for (int i = 0; i < kPool; ++i) if (seen[i] > 1 || (seen[i] == 1 && ! readers[i].active) || (seen[i] == 0 && readers[i].active))
                    std::fprintf (stderr, "reader %d listed %d active %d note %d\n", i, seen[i], (int) readers[i].active, readers[i].note);
            }
           #endif
            bool any = false;
            for (auto& nt : notes) if (nt.used) { any = true; break; }
            if (! any) { txL = txR = tyL = tyR = 0.f; tiltOn = false; toneCur = 1.0e9f; tn.reset(); toneDark = 0.f; updateRetiring(); return 0.f; }

            std::fill (sumL.begin(), sumL.begin() + n, 0.f);
            std::fill (sumR.begin(), sumR.begin() + n, 0.f);
            int lead = -1, live = 0; uint64_t leadGen = 0;

            for (int ni = 0; ni < kMaxNotes; ++ni)
            {
                auto& nt = notes[ni];
                if (! nt.used) continue;
                int i0 = 0;
                if (! nt.started)
                {
                    if (nt.delay >= n) { nt.delay -= n; continue; }
                    i0 = nt.delay; nt.delay = 0;
                    startNote (ni, P, pitchCents);
                    if (! nt.used) continue;
                }
                else if (! nt.released && ! nt.orphan && ! nt.killed && inst != nullptr)
                    updateTargets (ni, pitchCents, false);

                const Vibrato::Block* vb = (vibNorm > 0.f || nt.vibDepthSm > 0.f) ? vibratoFor (nt, n) : nullptr;
                for (int k = 0; k < nt.nrd; ++k)
                {
                    auto& rd = readers[nt.rd[k]];
                    if (! rd.active) continue;
                    renderReader (rd, nt, pitchCents, sumL.data(), sumR.data(), i0, n, vb);
                    ++rendered;
                }
                if (nt.started && nt.k == 0 && nt.gen >= leadGen) { leadGen = nt.gen; lead = ni; }
                nt.age += n - i0;
                // compact dead readers
                for (int k = 0; k < nt.nrd;)
                {
                    auto& rd = readers[nt.rd[k]];
                    if (! rd.active) { rd.note = -1; nt.rd[k] = nt.rd[--nt.nrd]; }
                    else ++k;
                }
                if (nt.started && nt.nrd == 0) nt.used = false;
            }

            // Tone: ONE first-order tilt per voice (pivot 700 Hz, or the lead note's f0 when that is higher): 9·t
            // + velocity·Velocity + key tracking above C6 + the lead note's Human tone. Coefficients only when the
            // target moved > 0.05 dB (fb441) or the pivot moved.
            // tp106: a FIXED 700 Hz pivot sits below every partial of a note above ~F5 — the tilt then only changes the
            // level (glockenspiel C6: Tone 0 → 100 % moved the centroid 0.7 %). Tracking f0 (the fundamental at the unity
            // point, the partials above it tilted) keeps Tone a brightness control on every key; up to F5 the pivot is
            // 700 Hz as before.
            // tp108: the + side's EXCITER runs on the summed voice BEFORE the tilt (so the tilt shapes the partials it adds).
            //   Tone exactly 0 with both stages at rest → never entered: bit-identical to tp107 and 0 µs.
            const bool tnOn = gTnEnabled.load (std::memory_order_relaxed);
            if (tnOn && (sTone != 0.f || tn.busy()))
            {
                if (lead >= 0 && notes[lead].gen != tn.gen)
                {
                    // the lead note's loudest attack reader is the recording the stages size themselves on
                    const Reader* best = nullptr;
                    for (int k = 0; k < notes[lead].nrd; ++k)
                    {
                        const auto& rd = readers[notes[lead].rd[k]];
                        if (rd.active && rd.role == org::Kind::Attack && rd.s != nullptr && rd.r != nullptr && (best == nullptr || rd.layer > best->layer)) best = &rd;
                    }
                    if (best != nullptr) tn.setSource (notes[lead].gen, *best->s, *best->r, best->ratio, pitchCents);
                    else tn.noSource (notes[lead].gen);
                }
                tn.excite (sumL.data(), sumR.data(), n, sTone, pitchCents,
                           lead >= 0 ? std::clamp (440.f * std::exp2 ((float) (notes[lead].key - 69) / 12.f), 700.f, (float) (0.2 * sr)) : tonePivotHz);
            }
            bool pivotMoved = false;
            if (lead >= 0)
            {
                const auto& ln = notes[lead];
                const float kt = ln.key > 84 ? -1.5f * (float) (ln.key - 84) / 12.f : 0.f;
                leadTone = std::clamp (9.f * sTone + 4.f * (ln.vel01 - 0.6f) * sVelo + kt + ln.humToneDb, -12.f, 12.f);
                const float piv = std::clamp (440.f * std::exp2 ((float) (ln.key - 69) / 12.f), 700.f, (float) (0.2 * sr));
                if (std::abs (piv - tonePivotHz) > 0.01f * tonePivotHz)
                {
                    tonePivotHz = piv;
                    toneK = (float) std::tan (3.14159265358979 * (double) piv / sr);
                    pivotMoved = true;
                }
            }
            // The IDENTITY coefficients (A = 1: b0 = 1, b1 = a1) bracket every on/off: the filter glides in from
            // identity and glides out to identity for one block before it is bypassed, and while bypassed its
            // state is kept exactly what identity would hold — so neither switch steps the signal.
            const float ident[3] = { 1.f, (toneK - 1.f) / (1.f + toneK), (toneK - 1.f) / (1.f + toneK) };
            const float prev[3] = { tb0, tb1, ta1 };
            // tp108: on a SPARSE note (a pure tone the tilt can't darken) the − side turns this same section into a first-order
            // LOW-PASS — corner 32·f_dom → f_dom/√2 as the depth dk goes 0 → 1, the loss at f_dom made up — by gliding its
            // coefficients from the tilt's toward the low-pass's: the shelf's −9 dB floor is gone, dark keeps going, and it costs
            // nothing (dk = 0 everywhere else: the tp107 tilt, bit for bit).
            const float dk = tnOn && (sTone != 0.f || tn.busy()) ? tn.darkDepth (n, sTone) : 0.f;
            if (std::abs (leadTone - toneCur) > 0.05f || (pivotMoved && (std::abs (toneCur) >= 0.05f || toneDark > 0.f))
                || std::abs (dk - toneDark) > 1.0e-3f || (dk == 0.f && toneDark != 0.f))
            {
                toneCur = leadTone; toneDark = dk;
                const float A = dbToLin (toneCur), C = A, K = toneK;
                const float inv = 1.f / (1.f + C * K);
                tb0 = (A + K) * inv; tb1 = (K - A) * inv; ta1 = (C * K - 1.f) * inv;
                if (dk > 0.f && A < 1.f)
                {
                    // the tilt as a shelf: g·(1 + s/wz)/(1 + s/wp) with g = 1/A, wz = K/A, wp = A·K (prewarped). Darker = its zero
                    // slides up to 0.45·fs (the −9 dB floor is gone, the 6 dB/oct slope keeps going) and its pole down to
                    // 0.7·f_dom, both geometrically by dk; the gain at f_dom stays exactly today's (no level jump).
                    const double fd = tn.fDom > 0.f ? (double) tn.fDom : 1000.0, pi = 3.14159265358979;
                    const double wz = (double) K / A, wp = (double) A * K, wd = std::tan (pi * std::min (fd, 0.45 * sr) / sr);
                    const double W = std::tan (pi * 0.45), wpMin = std::tan (pi * std::min (0.7 * fd, 0.4 * sr) / sr);
                    const double wz2 = wz < W ? wz * std::pow (W / wz, (double) dk) : wz;
                    const double wp2 = wp > wpMin ? wp * std::pow (wpMin / wp, (double) dk) : wp;
                    auto mag2 = [wd] (double z, double p) { return (1.0 + (wd / z) * (wd / z)) / (1.0 + (wd / p) * (wd / p)); };
                    const double g2 = (1.0 / A) * std::sqrt (mag2 (wz, wp) / mag2 (wz2, wp2));
                    const double k = g2 * (wp2 / wz2) / (wp2 + 1.0);
                    tb0 = (float) (k * (wz2 + 1.0)); tb1 = (float) (k * (wz2 - 1.0)); ta1 = (float) ((wp2 - 1.0) / (wp2 + 1.0));
                }
            }
            const bool onNow = std::abs (toneCur) >= 0.05f || toneDark > 0.f;
            if (onNow || tiltOn)
            {
                const float cur[3] = { tb0, tb1, ta1 };
                const float* f = tiltOn ? prev : ident;
                const float* t = onNow ? cur : ident;
                tiltBlock (sumL.data(), n, txL, tyL, f, t);
                tiltBlock (sumR.data(), n, txR, tyR, f, t);
            }
            else { txL = tyL = sumL[(size_t) n - 1]; txR = tyR = sumR[(size_t) n - 1]; }   // identity's exact state
            tiltOn = onNow;
            if (tnOn && (sTone != 0.f || tn.busy()))
            {
                gTnSwing.store (tn.swing, std::memory_order_relaxed); gTnGap.store (tn.sparse, std::memory_order_relaxed); gTnSwingR.store (tn.swingRms, std::memory_order_relaxed); gTnFlat.store (tn.flat, std::memory_order_relaxed);
                gTnDom.store (tn.fDom, std::memory_order_relaxed);    gTnExc.store (tn.aCur, std::memory_order_relaxed);
                gTnLp.store (tn.dCur, std::memory_order_relaxed); gTnGateA.store (tn.gate, std::memory_order_relaxed); gTnRho.store (tn.lastRho, std::memory_order_relaxed);     gTnStages.store (tn.stages, std::memory_order_relaxed);
            }
            for (const auto& rd : readers) live += (rd.active && ! rd.fading) ? 1 : 0;
            gLastLive.store (live, std::memory_order_relaxed);

            // Image: mid/side width of the sample (skipped at 1.0)
            if (std::abs (img0 - 1.f) > 1.0e-4f || std::abs (sImage - 1.f) > 1.0e-4f)
            {
                float w = img0; const float dw = (sImage - img0) / (float) n;
                for (int i = 0; i < n; ++i, w += dw)
                {
                    const float m = 0.5f * (sumL[(size_t) i] + sumR[(size_t) i]);
                    const float sd = 0.5f * (sumL[(size_t) i] - sumR[(size_t) i]) * w;
                    sumL[(size_t) i] = m + sd; sumR[(size_t) i] = m - sd;
                }
            }
            float peak = 0.f;
            for (int i = 0; i < n; ++i)
            {
                L[i] += sumL[(size_t) i]; R[i] += sumR[(size_t) i];
                peak = std::max (peak, std::max (std::abs (sumL[(size_t) i]), std::abs (sumR[(size_t) i])));
            }
            updateRetiring();
            return peak;
        }

        bool isActive() const noexcept { return busy(); }
    };

    //==============================================================================================
    OrganicEngine::OrganicEngine() : impl (std::make_unique<Impl>()) {}
    OrganicEngine::~OrganicEngine() = default;

    void  OrganicEngine::prepare (double sampleRate, int maxBlock)     { impl->prepare (sampleRate, maxBlock); }
    void  OrganicEngine::reset() noexcept                              { impl->reset(); }
    void  OrganicEngine::setInstrument (std::shared_ptr<const OrganicInstrument> inst) noexcept { impl->setInstrument (std::move (inst)); }
    void  OrganicEngine::noteOn (int note, float vel, int players, const float* detuneCents, uint32_t seed) noexcept
                                                                       { impl->noteOn (note, vel, players, detuneCents, seed); }
    void  OrganicEngine::noteOff (bool pedalDown) noexcept             { impl->noteOff (pedalDown); }
    void  OrganicEngine::pedal (bool down) noexcept                    { impl->pedal (down); }
    void  OrganicEngine::kill() noexcept                               { impl->kill(); }
    void  OrganicEngine::render (const OrganicParams& p, float pitchCents, float* L, float* R, int n) noexcept
                                                                       { impl->render (p, pitchCents, L, R, n); }
    bool  OrganicEngine::isActive() const noexcept                     { return impl->isActive(); }
    float OrganicEngine::readLevel() const noexcept                    { return impl->level; }
    void  OrganicEngine::setNonRealtime (bool b) noexcept              { impl->nonRealtime = b; }
}
