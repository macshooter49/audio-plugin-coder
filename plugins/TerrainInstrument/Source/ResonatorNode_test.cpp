// g++ -std=c++17 Source/ResonatorNode_test.cpp -o /tmp/rn && /tmp/rn
#include "ResonatorNode.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>

static int failures = 0;
static void check (bool ok, const char* what) {
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok) ++failures;
}
static float hz (int note) { return 440.0f * std::pow (2.0f, (note - 69) / 12.0f); }
static float goertzel (const std::vector<float>& x, float f, float sr) {
    const float w = 2.0f * 3.14159265f * f / sr; const float c = 2.0f * std::cos (w);
    float s1 = 0, s2 = 0;
    for (float v : x) { const float s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

int main()
{
    const float sr = 48000.0f; const int N = 256;
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> noise (-0.5f, 0.5f);

    // 1) Mix=0 exact passthrough.
    {
        wc::ResonatorNode r; r.prepare (sr); const int held[] = { 57 };
        bool id = true;
        for (int blk = 0; blk < 20; ++blk) {
            std::vector<float> L (N), Rr (N), L0 (N), R0 (N);
            for (int i = 0; i < N; ++i) { L[i] = L0[i] = noise (rng); Rr[i] = R0[i] = noise (rng); }
            r.process (0.3f, 0.55f, 0.45f, 0.2f, 0, 0.0f, 1.0f, held, 1, sr, L.data(), Rr.data(), N);
            for (int i = 0; i < N; ++i) if (L[i] != L0[i] || Rr[i] != R0[i]) id = false;
        }
        check (id, "Mix=0 is exact passthrough");
    }

    // 2) IT RINGS: excite briefly, then SILENCE → output keeps ringing long after.
    auto ringEnergyAfterStop = [&] (float damping) {
        wc::ResonatorNode r; r.prepare (sr); const int held[] = { 45 };
        float tail = 0.0f;
        for (int blk = 0; blk < 140; ++blk) {
            std::vector<float> L (N), Rr (N);
            const bool exciting = blk < 8;                 // ~43 ms of excitation, then silence
            for (int i = 0; i < N; ++i) { float s = exciting ? noise (rng) : 0.0f; L[i] = s; Rr[i] = s; }
            const int nh = (blk < 8) ? 1 : 0;
            r.process (0.0f, 0.6f, damping, 0.2f, 0, 1.0f, 1.0f, held, nh, sr, L.data(), Rr.data(), N);
            if (blk >= 80 && blk < 100) for (int i = 0; i < N; ++i) tail += L[i] * L[i];  // ~430 ms after stop
        }
        return tail;
    };
    {
        const float lo = ringEnergyAfterStop (0.05f);   // long ring
        const float hi = ringEnergyAfterStop (0.95f);   // short ring
        std::printf ("    (ring tail @430ms: lowDamp=%.4f  highDamp=%.6f)\n", lo, hi);
        check (lo > 1.0e-3f, "RINGS: audible energy long after the input stops (a real resonant tail)");
        check (lo > hi * 4.0f, "Damping controls ring length (low rings far longer)");
    }

    // 3) IT RESONATES (×100): a small steady sine at the note's pitch comes out much louder.
    {
        wc::ResonatorNode r; r.prepare (sr); const int note = 45; const int held[] = { note };
        const float f0 = hz (note); double ph = 0.0; const double dph = 2.0 * 3.14159265 * f0 / sr;
        float inSq = 0, outSq = 0; int cnt = 0;
        for (int blk = 0; blk < 200; ++blk) {
            std::vector<float> L (N), Rr (N);
            for (int i = 0; i < N; ++i) { float s = 0.05f * (float) std::sin (ph); ph += dph; L[i] = s; Rr[i] = s; }
            r.process (0.0f, 0.5f, 0.3f, 0.2f, 0, 1.0f, 1.0f, held, 1, sr, L.data(), Rr.data(), N);
            if (blk >= 150) { for (int i = 0; i < N; ++i) { outSq += L[i] * L[i]; ++cnt; } }
        }
        inSq = 0.05f * 0.05f * 0.5f;                       // input sine power
        const float outRms = std::sqrt (outSq / cnt);
        std::printf ("    (input rms=%.3f  output rms=%.3f  gain=%.1fx)\n", std::sqrt (inSq), outRms, outRms / std::sqrt (inSq));
        check (outRms > std::sqrt (inSq) * 3.0f, "RESONATES: output far exceeds input at the resonant pitch (not unity EQ)");
    }

    // 4) Key-track from NOISE → output peaks at the played pitch.
    {
        wc::ResonatorNode r; r.prepare (sr); const int note = 57; const int held[] = { note };
        std::vector<float> out;
        for (int blk = 0; blk < 260; ++blk) {
            std::vector<float> L (N), Rr (N);
            for (int i = 0; i < N; ++i) { L[i] = noise (rng); Rr[i] = noise (rng); }
            r.process (0.0f, 0.45f, 0.25f, 0.15f, 0, 1.0f, 1.0f, held, 1, sr, L.data(), Rr.data(), N);
            if (blk >= 140) for (int i = 0; i < N; ++i) out.push_back (L[i]);
        }
        const float ratio = goertzel (out, hz (note), sr) / (goertzel (out, hz (note) * 1.335f, sr) + 1.0f);
        std::printf ("    (on-pitch / off-pitch = %.2f)\n", ratio);
        check (ratio > 2.0f, "key-track: output emphasizes the played pitch");
    }

    // 5) POLYPHONY: two held notes → BOTH pitches ring.
    {
        wc::ResonatorNode r; r.prepare (sr); const int a = 45, b = 52; const int held[] = { a, b };
        std::vector<float> out;
        for (int blk = 0; blk < 260; ++blk) {
            std::vector<float> L (N), Rr (N);
            for (int i = 0; i < N; ++i) { L[i] = noise (rng); Rr[i] = noise (rng); }
            r.process (0.0f, 0.45f, 0.3f, 0.15f, 0, 1.0f, 1.0f, held, 2, sr, L.data(), Rr.data(), N);
            if (blk >= 140) for (int i = 0; i < N; ++i) out.push_back (L[i]);
        }
        const float pa = goertzel (out, hz (a), sr), pb = goertzel (out, hz (b), sr);
        const float off = goertzel (out, hz (a) * 1.335f, sr) + 1.0f;
        std::printf ("    (A=%.0f B=%.0f off=%.0f)\n", pa, pb, off);
        check (pa > off * 1.5f && pb > off * 1.5f, "polyphony: BOTH held pitches resonate at once");
    }

    // 6) MATERIAL differs: String 2nd partial sits at 2·f0; Metal disperses it away.
    {
        auto secondPartialRatio = [&] (int mat) {
            wc::ResonatorNode r; r.prepare (sr); const int note = 45; const int held[] = { note };
            std::vector<float> out;
            for (int blk = 0; blk < 220; ++blk) {
                std::vector<float> L (N), Rr (N);
                for (int i = 0; i < N; ++i) { L[i] = noise (rng); Rr[i] = noise (rng); }
                r.process (0.4f, 0.6f, 0.3f, 0.2f, mat, 1.0f, 1.0f, held, 1, sr, L.data(), Rr.data(), N);
                if (blk >= 130) for (int i = 0; i < N; ++i) out.push_back (L[i]);
            }
            return goertzel (out, hz (note) * 2.0f, sr);     // energy exactly at 2·f0
        };
        const float strHarm = secondPartialRatio (0);   // String — should have energy at 2f0
        const float metHarm = secondPartialRatio (3);   // Metal — 2nd partial dispersed away from 2f0
        std::printf ("    (String@2f0=%.0f  Metal@2f0=%.0f)\n", strHarm, metHarm);
        check (strHarm > metHarm, "materials differ: String is harmonic at 2f0, Metal is inharmonic");
    }

    // 7) Stability: 4 s sustained full-drive Metal chord → finite, bounded.
    {
        wc::ResonatorNode r; r.prepare (sr); const int held[] = { 33, 40, 45, 52 };
        bool finite = true, bounded = true; const int blocks = (int) (4.0f * sr / N);
        for (int blk = 0; blk < blocks; ++blk) {
            std::vector<float> L (N), Rr (N);
            for (int i = 0; i < N; ++i) { L[i] = noise (rng) * 2.0f; Rr[i] = noise (rng) * 2.0f; }
            r.process (0.8f, 1.0f, 0.02f, 0.5f, 3, 1.0f, 1.0f, held, 4, sr, L.data(), Rr.data(), N);
            for (int i = 0; i < N; ++i) {
                if (! std::isfinite (L[i]) || ! std::isfinite (Rr[i])) finite = false;
                if (std::fabs (L[i]) > 1.0001f || std::fabs (Rr[i]) > 1.0001f) bounded = false;
            }
        }
        check (finite,  "no NaN/inf under 4 s sustained full-drive Metal chord");
        check (bounded, "output bounded to [-1,1]");
    }

    std::printf ("\n%s — %d failure(s)\n", failures == 0 ? "ALL GOOD" : "PROBLEMS", failures);
    return failures == 0 ? 0 : 1;
}
