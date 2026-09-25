// OrganicEngine.cpp — THE ORGANICS ENGINE runtime DSP (design §2 knobs, §3 day-1 playback, §5 CPU).
//
// Shape of one engine (one oscillator slot of one voice):
//   notes[]   — one per unison PLAYER of each noteOn ("Ensemble": own RR, own Human seed, k·7 ms·detune timing)
//   readers[] — one per sounding sample region (layer / release / noise), pooled, logical cap 48 per engine
//               (kMaxRegionsPerOsc) with a 5 ms steal-fade; 96 physical slots so stolen/handed-over readers can
//               finish their fades (live readers never exceed 48). notes[] = 96 + 16 so a new note never starves.
// Per block: params are smoothed once, each note recomputes its region targets only when Dynamics/Body moved,
// each reader computes ratio + gain endpoints once, then the inner loop is interpolate → gain ramp → accumulate.
// Tone is ONE first-order tilt per voice (pivot 700 Hz, the lead note's velocity/key/Human tone; coefficients
// glide across the block, identity-bracketed on/off), Image one M/S multiply per engine (skipped at 1.0).
// Memory: ~35 KB per engine (the pools), allocated in the constructor + prepare(); nothing after prepare().
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
        std::atomic<int> gLastReaders { 0 }, gLastRegion { -1 }, gLastLive { 0 }, gSteals { 0 };

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
            int rd[kPerNote]; int nrd = 0;
            uint64_t gen = 0;
            int mapKey = 60;                                                  // tp105 NO-SILENCE: the key the lookups use
            float vibPhase = 0.f, vibDepthSm = 0.f, vibRateMul = 1.f;         // tp105: this player's vibrato (phase, depth, rate)
        };

        struct PendingOn { bool on = false; int note = 60; float vel = 0.8f; int players = 1; float det[kMaxPlayers] {}; uint32_t seed = 0; };

        // ── state ──
        double sr = 48000.0; int maxBlock = 0; float toneK = 0.f;
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
        float toneCur = 1.0e9f, leadTone = 0.f, tb0 = 1.f, tb1 = 0.f, ta1 = 0.f, txL = 0.f, txR = 0.f, tyL = 0.f, tyR = 0.f;
        bool  tiltOn = false;
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
            if (role == org::Kind::Attack && atk < 0.f) fin = std::max (fin, fadeFrames (-atk * 0.150));
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
                n.humLvl = dbToLin ((2.f * uLvl - 1.f) * 1.5f * h);
                n.humStartSec = uStart * 0.006f * h;
                n.humToneDb = (2.f * uTone - 1.f) * 1.5f * h;
                n.delay = (int) std::lround (((double) uTime * 0.012 * h + (double) k * 0.007 * D) * sr);
                n.rrIdx = seqBase + (uint32_t) k;
                n.detC = pend.det[k];
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
                    for (int d = 1; d <= 6; ++d)
                    {
                        const int k2 = key0 + c * d;
                        if (k2 < 0 || k2 > 127) break;
                        const int t = topRegion (n, k2, n.vL);
                        if (t >= 0 && t != base && n.key - I.regions[(size_t) t].root <= 7) { n.fake = c * d; break; }
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

        void updateTargets (int idx, float pitchCents, bool atStart) noexcept
        {
            auto& n = notes[idx];
            const float vT = std::clamp ((float) n.vIdx + 63.f * sDyn, 1.f, 127.f);
            n.vL = atStart ? vT : n.vL + aDynNote * (vT - n.vL);
            const int64_t gLen = (int64_t) (0.06 * sr);
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
            (map.json env.r, 30 ms..30 s) at 0.5, ≥ 12 s at 1 (log-interpolated both halves) — and never shorter than
            the voice's amp-envelope release. */
        float relTime (const org::Region& r) const noexcept
        {
            const float A = std::clamp (r.envR, 0.03f, 30.f), rr = sRelease;
            const float T = rr <= 0.5f ? 0.02f * std::pow (A / 0.02f, 2.f * rr)
                                       : A * std::pow (std::max (12.f, A) / A, 2.f * rr - 1.f);
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
                    if (r.decaying() && (sSustain > 0.001f || rd.tailWrapped))
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
            const auto& r = *rd.r;
            const auto& s = *rd.s;
            const int cnt = nEnd - i0;
            rd.ratio = ratioFor (n, r, s, pitchCents);

            if (r.offByIdx >= 0 && ! rd.fading
                && rd.ins->groupEpoch()[r.offByIdx].load (std::memory_order_relaxed) != rd.chokeSeen)
                startFade (rd, fadeFrames (0.005));                       // choke: always a 5 ms fade

            float compDb = 0.f;
            float g1 = targetGain (rd, n, compDb);
            if (rd.relOn)
            {
                // exponential, −60 dB after relTime (re-evaluated per block: the knob / its mod / the amp release are live)
                rd.relG *= std::exp (-6.9077553f * (float) cnt / (relTime (r) * (float) sr));
                g1 *= rd.relG;
            }
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
            if (! looping && ! rd.fading)
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
            rd.gPrev = g1;
            rd.age += cnt;
            if (rd.tailWrapped) rd.tailAge += cnt;

            // retire
            if (ended || finishedFade) { rd.active = false; return; }
            if (rd.relOn && rd.relG < 1.0e-4f && ! rd.fading) startFade (rd, fadeFrames (0.005));   // −80 dB into the release: done
            if (rd.role == org::Kind::Attack && g0 <= 0.f && g1 <= 0.f && rd.layer <= 0.f) { rd.active = false; return; }
            if (! r.looping() && ! (looping && sSustain >= 0.999f))
            {
                const float lin = g1 * 32768.f * std::max (rd.panL, rd.panR);    // g1 already holds the Sustain comp
                const float est = rd.s->envAt (rd.pos) + 20.f * std::log10 (lin * (rd.fading ? rd.fadeStart : 1.f) + 1.0e-9f);
                if (est < -90.f && rd.age > rd.fadeInLen + 4800) rd.active = false;
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
            if (! any) { txL = txR = tyL = tyR = 0.f; tiltOn = false; toneCur = 1.0e9f; updateRetiring(); return 0.f; }

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

            // Tone: ONE first-order tilt per voice (pivot 700 Hz): 9·t + velocity·Velocity + key tracking above
            // C6 + the lead note's Human tone. Coefficients only when the target moved > 0.05 dB (fb441).
            if (lead >= 0)
            {
                const auto& ln = notes[lead];
                const float kt = ln.key > 84 ? -1.5f * (float) (ln.key - 84) / 12.f : 0.f;
                leadTone = std::clamp (9.f * sTone + 4.f * (ln.vel01 - 0.6f) * sVelo + kt + ln.humToneDb, -12.f, 12.f);
            }
            // The IDENTITY coefficients (A = 1: b0 = 1, b1 = a1) bracket every on/off: the filter glides in from
            // identity and glides out to identity for one block before it is bypassed, and while bypassed its
            // state is kept exactly what identity would hold — so neither switch steps the signal.
            const float ident[3] = { 1.f, (toneK - 1.f) / (1.f + toneK), (toneK - 1.f) / (1.f + toneK) };
            const float prev[3] = { tb0, tb1, ta1 };
            if (std::abs (leadTone - toneCur) > 0.05f)
            {
                toneCur = leadTone;
                const float A = dbToLin (toneCur), C = A, K = toneK;
                const float inv = 1.f / (1.f + C * K);
                tb0 = (A + K) * inv; tb1 = (K - A) * inv; ta1 = (C * K - 1.f) * inv;
            }
            const bool onNow = std::abs (toneCur) >= 0.05f;
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
