#pragma once
// ════════════════════════════════════════════════════════════════════════════
//  tw::BlendEngine — Terrain one-shot BLEND / spectral-morph baker (all-original)
//
//  Combines TWO one-shots into a NEW one-shot. OFFLINE by design: analyze() runs
//  once per source pair (STFT, decomposition, peaks — the heavy part, cached);
//  render() re-mixes per knob move in ~ms. The audio thread NEVER sees this —
//  the baked buffer is published as a normal sample the SampleEngine (or the
//  Granular engine — same shared buffer) just plays. Per-voice CPU delta: ZERO.
//
//  Pipeline (research-grounded — see memory project-terrain-blend-engine-scope):
//   1. B is resampled to A's rate, both are onset-trimmed (attacks co-locate),
//      and B is PITCH-LOCKED to A when both are tonal (autocorrelation f0; the
//      "no detuned shit" rule) via clean resampling.
//   2. Each shot splits into three layers by soft spectral masking, summing to
//      identity: ATTACK (transients — vertical/horizontal median HPSS masks),
//      BODY (tonal peaks), BREATH (tonal residual noise).
//   3. Layers combine per the knobs: ATTACK/BREATH = equal-gain spectral mixes
//      (identity-safe); BODY = 1-D optimal-transport morph (Audio Transport,
//      Henderson/Solomon DAFx'19): spectral peaks MOVE between the sounds
//      instead of crossfading, with the spectral ENVELOPE separated (cepstral-
//      style smoothing) and interpolated independently = the SCULPT knob
//      ("shape of A, guts of B" — Caetano/IRCAM descriptor morphing).
//      DICE = deterministic per-harmonic ownership shuffle (FactorSynth-style
//      recombination) + layer-position jitter, seeded from the knob value.
//   4. ISTFT overlap-add, peak-normalize to −1 dBFS, edge declick.
//
//  Knob law (night-and-day by design): layerPos = clamp01(MORPH + 2·(knob−.5)).
//  MORPH is the master A↔B ride; each layer knob can fully override its layer.
//  All layer mixes are EQUAL-GAIN so blend(A,A) == A (harness identity).
//
//  Offline proof loop (Pattern A — NOT in CMakeLists):
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/BlendEngine_test.cpp -o /tmp/be && /tmp/be
// ════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>

namespace tw {

struct BlendParams
{
    float morph  = 0.5f;   // master A<->B position
    float attack = 0.5f;   // transient-layer balance (0=A .. 1=B), offsets morph
    float body   = 0.5f;   // tonal/harmonic-layer transport position
    float breath = 0.5f;   // noise-layer balance
    float sculpt = 0.5f;   // spectral-envelope (formant) source A<->B
    float dice   = 0.0f;   // 0=off; >0 = deterministic harmonic recombination depth (value = seed)
};

class BlendEngine
{
public:
    static constexpr int kWin  = 2048;
    static constexpr int kHop  = 512;              // 75% overlap Hann — smooth OLA resynthesis
    static constexpr int kBins = kWin / 2 + 1;

    BlendEngine() = default;

    bool isAnalyzed() const noexcept { return analyzed_; }
    double outRate() const noexcept { return rate_; }

    // ── heavy pass: align + pitch-lock + STFT + decompose. Call once per source pair. ──
    // Buffers are caller-owned raw views (mono ptr may repeat for both channels).
    void analyze (const float* aL, const float* aR, int lenA, double rateA,
                  const float* bL, const float* bR, int lenB, double rateB,
                  bool tuneLock = true)
    {
        analyzed_ = false;
        if (aL == nullptr || bL == nullptr || lenA < kWin / 2 || lenB < kWin / 2 || rateA <= 0.0) return;
        if (aR == nullptr) aR = aL;
        if (bR == nullptr) bR = bL;
        rate_ = rateA;

        // 1) copy A; resample B to A's rate
        std::vector<float> AL (aL, aL + lenA), AR (aR, aR + lenA), BL, BR;
        resampleTo (bL, bR, lenB, (rateB > 0.0 ? rateB : rateA) / rateA, BL, BR);

        // 2) onset trim (attacks co-locate at sample 0)
        trimOnset (AL, AR);
        trimOnset (BL, BR);
        if ((int) AL.size() < kWin / 2 || (int) BL.size() < kWin / 2) return;

        // 3) pitch-lock B -> A ("no detuned shit"): both tonal => clean-resample B by f0A/f0B.
        f0A_ = detectF0 (AL, AR); f0B_ = detectF0 (BL, BR);
        tuned_ = false;
        if (tuneLock && f0A_ > 0.f && f0B_ > 0.f)
        {
            // OCTAVE-FOLDED lock: octaves are already in tune — tune B to the NEAREST
            // octave-equivalent of A (±6 st max). A 165 Hz pad under a 330 Hz pluck stays
            // put; a 261.6 Hz shot over a 220 Hz shot comes down 3 st. Repitching a pad a
            // whole octave would gut its character (and halve its length) for nothing.
            const double semis = 12.0 * std::log2 ((double) f0A_ / (double) f0B_);
            const double fold  = semis - 12.0 * std::round (semis / 12.0);
            const double ratio = std::pow (2.0, fold / 12.0);
            if (std::fabs (1.0 - ratio) > 0.003)
            {
                std::vector<float> tl, tr;
                resampleTo (BL.data(), BR.data(), (int) BL.size(), ratio, tl, tr);
                BL.swap (tl); BR.swap (tr);
                tuned_ = true;
            }
        }

        // 4) common length = max (a short pluck blended with a long pad keeps the pad's tail)
        outLen_ = (int) std::max (AL.size(), BL.size());
        AL.resize ((size_t) outLen_, 0.f); AR.resize ((size_t) outLen_, 0.f);
        BL.resize ((size_t) outLen_, 0.f); BR.resize ((size_t) outLen_, 0.f);

        // 5) STFT + decompose each shot into Attack/Body/Breath (masks sum to 1 → identity)
        decompose (AL, AR, shotA_);
        decompose (BL, BR, shotB_);
        frames_ = std::min (shotA_.frames, shotB_.frames);
        analyzed_ = true;
    }

    // ── fast pass: combine layers per the knobs and synthesize. ~ms per call. ──
    void render (const BlendParams& p, std::vector<float>& outL, std::vector<float>& outR)
    {
        outL.assign ((size_t) std::max (outLen_, 1), 0.f);
        outR.assign ((size_t) std::max (outLen_, 1), 0.f);
        if (! analyzed_) return;

        const uint32_t seed = (uint32_t) std::lround ((double) clamp01 (p.dice) * 999.0);
        const float dice = clamp01 (p.dice);
        // night-and-day law: each knob can fully override its layer around the MORPH ride
        const float tAtt = layerPos (p.morph, p.attack, seed, 11u, dice);
        const float tBod = layerPos (p.morph, p.body,   seed, 23u, dice);
        const float tBre = layerPos (p.morph, p.breath, seed, 37u, dice);
        const float tScu = layerPos (p.morph, p.sculpt, seed, 53u, dice);

        std::vector<std::complex<float>> outSpecL ((size_t) frames_ * kBins), outSpecR ((size_t) frames_ * kBins);
        std::vector<float> envA (kBins), envB (kBins), whiteA (kBins), whiteB (kBins);
        std::vector<std::complex<float>> bodyScrL ((size_t) kBins), bodyScrR ((size_t) kBins);

        for (int f = 0; f < frames_; ++f)
        {
            const size_t off = (size_t) f * kBins;
            const std::complex<float>* taL = &shotA_.trans[off];  const std::complex<float>* tbL = &shotB_.trans[off];
            const std::complex<float>* naL = &shotA_.breath[off]; const std::complex<float>* nbL = &shotB_.breath[off];
            const std::complex<float>* taR = &shotA_.transR[off];  const std::complex<float>* tbR = &shotB_.transR[off];
            const std::complex<float>* naR = &shotA_.breathR[off]; const std::complex<float>* nbR = &shotB_.breathR[off];

            // ATTACK + BREATH: equal-gain complex mixes (identity-safe; bake renormalizes level)
            for (int b = 0; b < kBins; ++b)
            {
                outSpecL[off + (size_t) b] = taL[b] * (1.f - tAtt) + tbL[b] * tAtt
                                           + naL[b] * (1.f - tBre) + nbL[b] * tBre;
                outSpecR[off + (size_t) b] = taR[b] * (1.f - tAtt) + tbR[b] * tAtt
                                           + naR[b] * (1.f - tBre) + nbR[b] * tBre;
            }

            // BODY: optimal-transport morph of the harmonic layer, envelope handled by SCULPT
            bodyTransport (f, tBod, tScu, seed, dice,
                           envA, envB, whiteA, whiteB, bodyScrL, bodyScrR,
                           &outSpecL[off], &outSpecR[off]);
        }

        istft (outSpecL, frames_, outL);
        istft (outSpecR, frames_, outR);
        outL.resize ((size_t) outLen_); outR.resize ((size_t) outLen_);

        // normalize to −1 dBFS ("amplitude has to be up") + edge declick
        float peak = 1e-9f;
        for (int i = 0; i < outLen_; ++i) peak = std::max (peak, std::max (std::fabs (outL[(size_t) i]), std::fabs (outR[(size_t) i])));
        const float g = 0.891f / peak;
        for (int i = 0; i < outLen_; ++i) { outL[(size_t) i] *= g; outR[(size_t) i] *= g; }
        const int fade = std::min ((int) (0.005 * rate_), outLen_ / 8);
        for (int i = 0; i < fade; ++i)
        {
            const float w = (float) i / (float) fade;
            outL[(size_t) i] *= w; outR[(size_t) i] *= w;
            outL[(size_t) (outLen_ - 1 - i)] *= w; outR[(size_t) (outLen_ - 1 - i)] *= w;
        }
    }

    // ── testing accessors ──
    float f0AForTesting() const noexcept { return f0A_; }
    float f0BForTesting() const noexcept { return f0B_; }
    bool  tunedForTesting() const noexcept { return tuned_; }
    int   outLenForTesting() const noexcept { return outLen_; }

private:
    // One analyzed shot: three complex layers per channel (they sum to the original STFT).
    struct Shot
    {
        int frames = 0;
        std::vector<std::complex<float>> trans, body, breath;      // left
        std::vector<std::complex<float>> transR, bodyR, breathR;   // right
        std::vector<float> bodyMagM;                               // mid |body| (peaks/transport source)
    };

    // ═══ small numeric helpers ══════════════════════════════════════════════
    static inline float clamp01 (float x) noexcept { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    static inline uint32_t hash32 (uint32_t x) noexcept
    { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
    static inline float hash01 (uint32_t a, uint32_t b) noexcept
    { return (float) ((hash32 (a * 0x9E3779B9u + b) & 0x00FFFFFFu) / (double) 0x01000000); }

    // layer position = MORPH + full-range knob offset (+ DICE jitter), clamped
    static float layerPos (float morph, float knob, uint32_t seed, uint32_t salt, float dice) noexcept
    {
        float t = clamp01 (clamp01 (morph) + 2.f * (clamp01 (knob) - 0.5f));
        if (dice > 0.f) t = clamp01 (t + (hash01 (seed, salt) - 0.5f) * 0.4f * dice);
        return t;
    }

    // 4-point Hermite resample by ratio (read step = ratio). Also the pitch-lock shifter:
    // clean classic-sampler repitch — pitch is EXACT, length changes (fine for one-shots).
    static void resampleTo (const float* l, const float* r, int n, double step,
                            std::vector<float>& outL, std::vector<float>& outR)
    {
        if (step <= 0.0) step = 1.0;
        if (std::fabs (step - 1.0) < 1e-9)
        { outL.assign (l, l + n); outR.assign (r, r + n); return; }
        const int outN = std::max (8, (int) std::floor ((double) (n - 1) / step));
        outL.resize ((size_t) outN); outR.resize ((size_t) outN);
        auto at = [n] (const float* s, int i) { return s[i < 0 ? 0 : (i >= n ? n - 1 : i)]; };
        for (int i = 0; i < outN; ++i)
        {
            const double p = (double) i * step;
            const int i1 = (int) p; const float t = (float) (p - i1);
            for (int c = 0; c < 2; ++c)
            {
                const float* s = c ? r : l;
                const float xm1 = at (s, i1 - 1), x0 = at (s, i1), x1 = at (s, i1 + 1), x2 = at (s, i1 + 2);
                const float cc = (x1 - xm1) * 0.5f, v = x0 - x1, w = cc + v, a2 = w + v + (x2 - x0) * 0.5f, bn = w + a2;
                (c ? outR : outL)[(size_t) i] = ((((a2 * t) - bn) * t + cc) * t + x0);
            }
        }
    }

    // trim leading silence so both attacks sit at sample 0 (the auto-align)
    static void trimOnset (std::vector<float>& l, std::vector<float>& r)
    {
        const int n = (int) l.size();
        float peak = 1e-9f;
        for (int i = 0; i < n; ++i) peak = std::max (peak, std::fabs (l[(size_t) i]) + std::fabs (r[(size_t) i]));
        const float thr = peak * 0.02f;
        int on = 0;
        while (on < n && std::fabs (l[(size_t) on]) + std::fabs (r[(size_t) on]) < thr) ++on;
        on = std::max (0, on - 32);   // keep a hair of pre-attack
        if (on > 0) { l.erase (l.begin(), l.begin() + on); r.erase (r.begin(), r.begin() + on); }
    }

    // autocorrelation f0 (mid), post-attack window; 0 = unvoiced (drums pass through untouched)
    float detectF0 (const std::vector<float>& l, const std::vector<float>& r) const
    {
        const int n = (int) l.size();
        const int start = std::min (n / 4, (int) (0.010 * rate_));
        const int win = std::min (n - start, 8192);
        if (win < 2048) return 0.f;
        std::vector<float> m ((size_t) win);
        double e0 = 0.0;
        for (int i = 0; i < win; ++i) { m[(size_t) i] = 0.5f * (l[(size_t) (start + i)] + r[(size_t) (start + i)]); e0 += (double) m[(size_t) i] * m[(size_t) i]; }
        if (e0 < 1e-8) return 0.f;
        const int minLag = std::max (2, (int) (rate_ / 1200.0));   // <=1200 Hz
        const int maxLag = std::min (win / 2, (int) (rate_ / 35.0));  // >=35 Hz
        if (maxLag <= minLag + 2) return 0.f;
        int bestLag = 0; double bestV = 0.0;
        std::vector<double> nsdf ((size_t) maxLag + 1, 0.0);
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double ac = 0.0, e1 = 0.0, e2 = 0.0;
            const int lim = win - lag;
            for (int i = 0; i < lim; ++i)
            {
                const double a = m[(size_t) i], b = m[(size_t) (i + lag)];
                ac += a * b; e1 += a * a; e2 += b * b;
            }
            const double v = (e1 + e2 > 1e-12) ? 2.0 * ac / (e1 + e2) : 0.0;   // NSDF-style
            nsdf[(size_t) lag] = v;
            if (v > bestV) { bestV = v; bestLag = lag; }
        }
        // 0.72: clean harmonic decays score ~0.85+; inharmonic bells/drums score below.
        // Erring UNVOICED is the safe side — a wrong "tune" is worse than no tune.
        if (bestV < 0.72 || bestLag <= minLag || bestLag >= maxLag) return 0.f;   // unvoiced
        // prefer the SHORTEST lag whose value is close to the best (octave-error guard)
        for (int lag = minLag; lag < bestLag; ++lag)
            if (nsdf[(size_t) lag] > bestV * 0.92
                && lag > minLag && lag < maxLag
                && nsdf[(size_t) lag] >= nsdf[(size_t) (lag - 1)] && nsdf[(size_t) lag] >= nsdf[(size_t) (lag + 1)])
            { bestLag = lag; bestV = nsdf[(size_t) lag]; break; }
        // parabolic refine
        double lagF = bestLag;
        const double ym1 = nsdf[(size_t) (bestLag - 1)], y0 = nsdf[(size_t) bestLag], y1 = nsdf[(size_t) (bestLag + 1)];
        const double den = ym1 - 2.0 * y0 + y1;
        if (std::fabs (den) > 1e-12) lagF += 0.5 * (ym1 - y1) / den;
        return (float) (rate_ / lagF);
    }

    // ═══ FFT (iterative radix-2, offline-only) ══════════════════════════════
    static void fft (std::complex<float>* x, int n, bool inverse)
    {
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (x[i], x[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const double ang = (inverse ? 2.0 : -2.0) * 3.14159265358979323846 / len;
            const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
            for (int i = 0; i < n; i += len)
            {
                std::complex<float> w (1.f, 0.f);
                for (int k = 0; k < len / 2; ++k)
                {
                    const std::complex<float> u = x[i + k], v = x[i + k + len / 2] * w;
                    x[i + k] = u + v;
                    x[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse) { const float inv = 1.f / (float) n; for (int i = 0; i < n; ++i) x[i] *= inv; }
    }

    const std::vector<float>& hann() const
    {
        static const std::vector<float> w = [] {
            std::vector<float> v ((size_t) kWin);
            for (int i = 0; i < kWin; ++i) v[(size_t) i] = 0.5f - 0.5f * std::cos (2.f * 3.14159265f * (float) i / (float) (kWin - 1));
            return v;
        }();
        return w;
    }

    int frameCount (int n) const noexcept { return std::max (1, (n + kHop - 1) / kHop); }

    void stft (const std::vector<float>& x, std::vector<std::complex<float>>& spec, int frames) const
    {
        const auto& w = hann();
        spec.assign ((size_t) frames * kBins, {});
        std::vector<std::complex<float>> buf ((size_t) kWin);
        const int n = (int) x.size();
        for (int f = 0; f < frames; ++f)
        {
            const int start = f * kHop;
            for (int i = 0; i < kWin; ++i)
            {
                const int idx = start + i - kWin / 2;   // center-aligned frames
                const float s = (idx >= 0 && idx < n) ? x[(size_t) idx] : 0.f;
                buf[(size_t) i] = { s * w[(size_t) i], 0.f };
            }
            fft (buf.data(), kWin, false);
            for (int b = 0; b < kBins; ++b) spec[(size_t) f * kBins + (size_t) b] = buf[(size_t) b];
        }
    }

    void istft (const std::vector<std::complex<float>>& spec, int frames, std::vector<float>& out) const
    {
        const auto& w = hann();
        const int n = frames * kHop + kWin;
        out.assign ((size_t) n, 0.f);
        std::vector<float> wsum ((size_t) n, 1e-9f);
        std::vector<std::complex<float>> buf ((size_t) kWin);
        for (int f = 0; f < frames; ++f)
        {
            for (int b = 0; b < kBins; ++b)          buf[(size_t) b] = spec[(size_t) f * kBins + (size_t) b];
            for (int b = kBins; b < kWin; ++b)       buf[(size_t) b] = std::conj (spec[(size_t) f * kBins + (size_t) (kWin - b)]);
            fft (buf.data(), kWin, true);
            const int start = f * kHop;
            for (int i = 0; i < kWin; ++i)
            {
                const int idx = start + i - kWin / 2;
                if (idx < 0 || idx >= n) continue;
                out[(size_t) idx]  += buf[(size_t) i].real() * w[(size_t) i];
                wsum[(size_t) idx] += w[(size_t) i] * w[(size_t) i];
            }
        }
        for (int i = 0; i < n; ++i) out[(size_t) i] /= wsum[(size_t) i];
    }

    // ═══ decomposition: HPSS median masks + tonal peak split ════════════════
    void decompose (const std::vector<float>& l, const std::vector<float>& r, Shot& s) const
    {
        const int frames = frameCount ((int) l.size());
        std::vector<std::complex<float>> SL, SR;
        stft (l, SL, frames); stft (r, SR, frames);

        // mid magnitude
        std::vector<float> mag ((size_t) frames * kBins);
        for (size_t i = 0; i < mag.size(); ++i) mag[i] = 0.5f * (std::abs (SL[i]) + std::abs (SR[i]));

        // horizontal (time) median = tonal-ness; vertical (freq) median = transient-ness
        constexpr int MT = 8, MF = 8;   // ±8 → 17-tap medians (~180 ms / ~190 Hz)
        std::vector<float> medT ((size_t) frames * kBins), medF ((size_t) frames * kBins);
        std::vector<float> tmp (17);
        for (int b = 0; b < kBins; ++b)
            for (int f = 0; f < frames; ++f)
            {
                int c = 0;
                for (int k = -MT; k <= MT; ++k)
                { int ff = f + k; if (ff < 0) ff = 0; if (ff >= frames) ff = frames - 1; tmp[(size_t) c++] = mag[(size_t) ff * kBins + (size_t) b]; }
                std::nth_element (tmp.begin(), tmp.begin() + c / 2, tmp.begin() + c);
                medT[(size_t) f * kBins + (size_t) b] = tmp[(size_t) (c / 2)];
            }
        for (int f = 0; f < frames; ++f)
            for (int b = 0; b < kBins; ++b)
            {
                int c = 0;
                for (int k = -MF; k <= MF; ++k)
                { int bb = b + k; if (bb < 0) bb = 0; if (bb >= kBins) bb = kBins - 1; tmp[(size_t) c++] = mag[(size_t) f * kBins + (size_t) bb]; }
                std::nth_element (tmp.begin(), tmp.begin() + c / 2, tmp.begin() + c);
                medF[(size_t) f * kBins + (size_t) b] = tmp[(size_t) (c / 2)];
            }

        s.frames = frames;
        s.trans.resize (mag.size());  s.body.resize (mag.size());  s.breath.resize (mag.size());
        s.transR.resize (mag.size()); s.bodyR.resize (mag.size()); s.breathR.resize (mag.size());
        s.bodyMagM.assign (mag.size(), 0.f);

        std::vector<float> tonalMag (kBins);
        for (int f = 0; f < frames; ++f)
        {
            const size_t off = (size_t) f * kBins;
            // soft Wiener-ish masks (sum to 1 → the three layers reconstruct exactly)
            for (int b = 0; b < kBins; ++b)
            {
                const float t2 = medT[off + (size_t) b] * medT[off + (size_t) b];
                const float p2 = medF[off + (size_t) b] * medF[off + (size_t) b];
                const float wt = t2 / (t2 + p2 + 1e-18f);      // tonal share
                tonalMag[(size_t) b] = mag[off + (size_t) b] * wt;
                s.trans[off + (size_t) b]  = SL[off + (size_t) b] * (1.f - wt);
                s.transR[off + (size_t) b] = SR[off + (size_t) b] * (1.f - wt);
                // stash tonal complex temporarily in body; split below
                s.body[off + (size_t) b]  = SL[off + (size_t) b] * wt;
                s.bodyR[off + (size_t) b] = SR[off + (size_t) b] * wt;
            }
            // tonal → harmonic peaks (±3 bins) vs breath residual
            float med = frameMedian (tonalMag);
            for (int b = 0; b < kBins; ++b)
            {
                bool nearPeak = false;
                for (int k = std::max (0, b - 3); k <= std::min (kBins - 1, b + 3) && ! nearPeak; ++k)
                    nearPeak = isPeak (tonalMag.data(), k, med);
                const std::complex<float> cl = s.body[off + (size_t) b], cr = s.bodyR[off + (size_t) b];
                if (nearPeak)
                {
                    s.breath[off + (size_t) b] = s.breathR[off + (size_t) b] = { 0.f, 0.f };
                    s.bodyMagM[off + (size_t) b] = tonalMag[(size_t) b];
                }
                else
                {
                    s.breath[off + (size_t) b]  = cl;
                    s.breathR[off + (size_t) b] = cr;
                    s.body[off + (size_t) b] = s.bodyR[off + (size_t) b] = { 0.f, 0.f };
                }
            }
        }
    }

    static float frameMedian (std::vector<float>& v)
    {
        static thread_local std::vector<float> scratch;
        scratch = v;
        std::nth_element (scratch.begin(), scratch.begin() + (long) scratch.size() / 2, scratch.end());
        return scratch[scratch.size() / 2];
    }

    static bool isPeak (const float* m, int b, float med) noexcept
    {
        if (b < 2 || b > kBins - 3) return false;
        const float v = m[b];
        return v > 3.f * med + 1e-12f
            && v >= m[b - 1] && v >= m[b + 1] && v > m[b - 2] && v > m[b + 2];
    }

    // ═══ BODY: peak-grouped 1-D optimal transport + SCULPT envelope morph ═══
    struct Peak { float centroid; float mass; int lo, hi; };

    static void findPeaks (const float* mag, std::vector<Peak>& out)
    {
        out.clear();
        float med = 0.f; { static thread_local std::vector<float> v; v.assign (mag, mag + kBins);
            std::nth_element (v.begin(), v.begin() + kBins / 2, v.end()); med = v[kBins / 2]; }
        for (int b = 2; b < kBins - 2; ++b)
        {
            if (! isPeak (mag, b, med)) continue;
            Peak p; p.lo = std::max (0, b - 3); p.hi = std::min (kBins - 1, b + 3);
            double m = 0.0, c = 0.0;
            for (int k = p.lo; k <= p.hi; ++k) { m += mag[k]; c += (double) mag[k] * k; }
            if (m <= 1e-12) continue;
            p.mass = (float) m; p.centroid = (float) (c / m);
            out.push_back (p);
            if ((int) out.size() >= 96) break;
        }
    }

    void bodyTransport (int f, float t, float tSculpt, uint32_t seed, float dice,
                        std::vector<float>& envA, std::vector<float>& envB,
                        std::vector<float>& whiteA, std::vector<float>& whiteB,
                        std::vector<std::complex<float>>& scrL, std::vector<std::complex<float>>& scrR,
                        std::complex<float>* outL, std::complex<float>* outR)
    {
        const size_t off = (size_t) f * kBins;
        const float* magA = &shotA_.bodyMagM[off];
        const float* magB = &shotB_.bodyMagM[off];
        double eA = 0.0, eB = 0.0;
        for (int b = 0; b < kBins; ++b) { eA += magA[b]; eB += magB[b]; }

        // one side (near-)silent → plain equal-gain mix, no degenerate transport
        if (eA < 1e-5 || eB < 1e-5)
        {
            for (int b = 0; b < kBins; ++b)
            {
                outL[b] += shotA_.body[off + (size_t) b] * (1.f - t) + shotB_.body[off + (size_t) b] * t;
                outR[b] += shotA_.bodyR[off + (size_t) b] * (1.f - t) + shotB_.bodyR[off + (size_t) b] * t;
            }
            return;
        }

        // spectral envelopes (smoothed |body|) — SCULPT interpolates them in log domain
        smoothEnv (magA, envA); smoothEnv (magB, envB);
        for (int b = 0; b < kBins; ++b)
        {
            whiteA[(size_t) b] = magA[b] / (envA[(size_t) b] + 1e-12f);   // excitation (whitened)
            whiteB[(size_t) b] = magB[b] / (envB[(size_t) b] + 1e-12f);
        }

        static thread_local std::vector<Peak> pkA, pkB;
        findPeaks (whiteA.data(), pkA);
        findPeaks (whiteB.data(), pkB);
        if (pkA.empty() || pkB.empty())
        {
            for (int b = 0; b < kBins; ++b)
            {
                outL[b] += shotA_.body[off + (size_t) b] * (1.f - t) + shotB_.body[off + (size_t) b] * t;
                outR[b] += shotA_.bodyR[off + (size_t) b] * (1.f - t) + shotB_.bodyR[off + (size_t) b] * t;
            }
            return;
        }

        double mA = 0.0, mB = 0.0;
        for (auto& p : pkA) mA += p.mass;
        for (auto& p : pkB) mB += p.mass;

        // 1-D optimal transport pairing: merge-walk the two normalized peak-mass CDFs.
        // Each chunk moves mass w from A-peak i toward B-peak j; its landing spot is the
        // displacement-interpolated centroid. Peak SHAPES ride along (partials stay sharp).
        // Body accumulates into the per-frame scratch so the SCULPT envelope pass below
        // touches ONLY body content (attack/breath already sit in out).
        std::fill (scrL.begin(), scrL.end(), std::complex<float> {});
        std::fill (scrR.begin(), scrR.end(), std::complex<float> {});
        size_t i = 0, j = 0;
        double remA = pkA[0].mass / mA, remB = pkB[0].mass / mB;
        while (i < pkA.size() && j < pkB.size())
        {
            const double w = std::min (remA, remB);
            float tPair = t;
            if (dice > 0.f)   // FactorSynth-style ownership shuffle: some harmonic pairs go pure A/B
            {
                const uint32_t key = ((uint32_t) (pkA[i].centroid * 0.25f) << 10) ^ (uint32_t) (pkB[j].centroid * 0.25f);
                const float h = hash01 (seed, key);
                if (h < dice * 0.6f) tPair = (h < dice * 0.3f) ? 0.f : 1.f;
            }
            const float cOut = pkA[i].centroid * (1.f - tPair) + pkB[j].centroid * tPair;
            placePeak (shotA_.body.data() + off, shotA_.bodyR.data() + off, pkA[i],
                       cOut, (float) (w * mA / pkA[i].mass) * (1.f - tPair), scrL.data(), scrR.data());
            placePeak (shotB_.body.data() + off, shotB_.bodyR.data() + off, pkB[j],
                       cOut, (float) (w * mB / pkB[j].mass) * tPair, scrL.data(), scrR.data());
            remA -= w; remB -= w;
            if (remA <= 1e-9) { ++i; if (i < pkA.size()) remA = pkA[i].mass / mA; }
            if (remB <= 1e-9) { ++j; if (j < pkB.size()) remB = pkB[j].mass / mB; }
        }

        // SCULPT: re-shape the transported body with the interpolated spectral envelope.
        // The placed shapes carry ≈ (1-t)·envA + t·envB; envOut is the log-domain (formant)
        // interpolation at the SCULPT position. tSculpt == t → g ≈ 1 (neutral); A == B →
        // exactly 1 (identity preserved).
        for (int b = 0; b < kBins; ++b)
        {
            const float srcEnv = envA[(size_t) b] * (1.f - t) + envB[(size_t) b] * t + 1e-12f;
            const float envOut = std::exp ((1.f - tSculpt) * std::log (envA[(size_t) b] + 1e-12f)
                                          + tSculpt        * std::log (envB[(size_t) b] + 1e-12f));
            const float g = envOut / srcEnv;
            outL[b] += scrL[(size_t) b] * g;
            outR[b] += scrR[(size_t) b] * g;
        }
    }

    // add a peak's complex bins shifted so its centroid lands at cOut (integer-bin shift —
    // exact at the knob extremes where delta==0; a new in-between timbre mid-morph)
    static void placePeak (const std::complex<float>* srcL, const std::complex<float>* srcR,
                           const Peak& p, float cOut, float gain,
                           std::complex<float>* outL, std::complex<float>* outR)
    {
        if (gain <= 1e-9f) return;
        const int shift = (int) std::lround (cOut - p.centroid);
        for (int b = p.lo; b <= p.hi; ++b)
        {
            const int d = b + shift;
            if (d < 0 || d >= kBins) continue;
            outL[d] += srcL[b] * gain;
            outR[d] += srcR[b] * gain;
        }
    }

    static void smoothEnv (const float* mag, std::vector<float>& env)
    {
        constexpr int R = 20;   // ±20 bins ≈ ±470 Hz @48k/2048 — formant-scale smoothing
        env.resize (kBins);
        double acc = 0.0; int cnt = 0;
        for (int b = 0; b <= R && b < kBins; ++b) { acc += mag[b]; ++cnt; }
        for (int b = 0; b < kBins; ++b)
        {
            env[(size_t) b] = (float) (acc / std::max (1, cnt));
            const int add = b + R + 1, rem = b - R;
            if (add < kBins) { acc += mag[add]; ++cnt; }
            if (rem >= 0)    { acc -= mag[rem]; --cnt; }
        }
    }

    // ═══ state ═══
    Shot shotA_, shotB_;
    int frames_ = 0, outLen_ = 0;
    double rate_ = 48000.0;
    float f0A_ = 0.f, f0B_ = 0.f;
    bool analyzed_ = false, tuned_ = false;
};

} // namespace tw
