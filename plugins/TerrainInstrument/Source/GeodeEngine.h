#pragma once
// ════════════════════════════════════════════════════════════════════════════
//  tw::GeodeEngine — Terrain GEODE resynthesis oscillator (Engine::SPEC)
//
//  SMS-flavored resynthesis: OFFLINE-analyze a sound into per-frame sinusoidal
//  PARTIALS + a noise-residual band envelope; per-voice RESYNTHESIZE it as an
//  oscillator bank + colored noise, played back polyphonically with a
//  POSITION / FOSSIL(freeze) / CREEP(scan) read-head and spectral sculpt.
//
//  Two source doors (the hybrid):
//   • sample   → GeodeAnalyzer::analyzeSample  (STFT peak-track → partials+noise)
//   • wavetable→ GeodeAnalyzer::buildFromWave   (frames ARE partials — near free)
//
//  Architecture (real-time safety):
//   • The heavy analysis is done ONCE per source on the MESSAGE thread and stored
//     in a shared read-only GeodeFrameStore (the processor double-buffers + atomic-
//     publishes it, MorphSlot-style). Every voice/unison instance just holds a
//     const pointer and resynthesizes — per-voice CPU is a bounded sine bank.
//   • renderBlockAdd() allocates nothing (working arrays sized in prepare()).
//   • Self-contained (copies the small FFT/STFT/f0/peak helpers, like BlendEngine),
//     so it drops into CMake with no new deps and has its own offline test.
//
//  CPU: a shared partial budget (mirrors the grain budget) thins the quietest
//  partials gracefully instead of dropping voices — Max's soft-budget rule.
//
//  Offline proof loop (Pattern A — NOT in CMakeLists):
//    c++ -std=c++17 -O2 -Wall -Wextra -ISource Source/GeodeEngine_test.cpp -o /tmp/gd && /tmp/gd
// ════════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <cmath>
#include <complex>
#include <vector>
#include <array>
#include <algorithm>

namespace tw {

namespace geode {
    constexpr int   kMaxPartials = 96;    // matches Wavetable kMaxPartials (slot cap)
    constexpr int   kNoiseBands  = 16;    // residual log-band envelope resolution
    constexpr int   kMaxFrames   = 256;   // frame-store cap (long samples subsampled)
    constexpr int   kWin         = 2048;  // analysis window
    constexpr int   kHop         = 512;   // 75% overlap
    constexpr int   kBins        = kWin / 2 + 1;
    constexpr float kPi          = 3.14159265358979323846f;
    constexpr float kRefHz       = 261.6256f; // C4 — unvoiced-sample transpose reference
}

// One analyzed spectral frame: K partials (ratio-to-fundamental + linear amp) and
// a residual noise band envelope. ratio lets sample & wavetable playback share one path.
struct GeodeFrame
{
    std::array<float, geode::kMaxPartials> ratio {}; // partial freq / fundamental
    std::array<float, geode::kMaxPartials> amp   {}; // linear amplitude
    std::array<float, geode::kNoiseBands>  noise {}; // residual band energies (linear)
    int nPartials = 0;
};

// Shared read-only analyzed store (processor-owned, per osc, double-buffered).
struct GeodeFrameStore
{
    std::vector<GeodeFrame> frames;
    float f0       = 0.f;             // detected fundamental (Hz); 0 = unvoiced / wavetable
    bool  fromWave = false;
    bool  valid    = false;
    int   numFrames() const noexcept { return (int) frames.size(); }
};

// Per-block sculpt/play parameters (gathered by the voice; == gates the push).
struct GeodeParams
{
    // ── page 1 (Play) ──
    float position = 0.f;    // 0..1 read position (scrub)
    float fossil   = 0.f;    // 0..1 freeze amount (holds the read-head)
    float creep    = 0.f;    // 0..1 auto-scan rate (store-lengths/sec ×2)
    float silt     = 0.15f;  // 0..1 partials(0) ↔ noise-residual(1) equal-power balance
    float formant  = 0.5f;   // 0..1 bipolar formant/envelope shift (0.5 = neutral)
    float cut      = 1.f;    // 0..1 spectral low-pass over partials (1 = fully open)
    // ── page 2 (Sculpt) ──
    float sieve    = 0.f;    // 0..1 spectral gate (drop partials below threshold)
    float distill  = 0.f;    // 0..1 keep only the loudest partials (trace)
    float haze     = 0.f;    // 0..1 spectral blur (amplitude neighbor-smear)
    float fracture = 0.5f;   // 0..1 bipolar harmonic↔inharmonic remap (0.5 = neutral)
    float tilt     = 0.5f;   // 0..1 bipolar spectral tilt (0.5 = flat)
    float quality  = 0.66f;  // 0..1 active partial cap (→ 16..kMaxPartials)
    // ── source / loop ──
    int   loopMode = 1;      // 0=one-shot 1=fwd loop 2=reverse 3=ping-pong
    bool  formantKeep = true;

    bool operator== (const GeodeParams& o) const noexcept
    {
        return position==o.position && fossil==o.fossil && creep==o.creep && silt==o.silt
            && formant==o.formant && cut==o.cut && sieve==o.sieve && distill==o.distill
            && haze==o.haze && fracture==o.fracture && tilt==o.tilt && quality==o.quality
            && loopMode==o.loopMode && formantKeep==o.formantKeep;
    }
    bool operator!= (const GeodeParams& o) const noexcept { return ! (*this == o); }
};

// ═══ small self-contained spectral helpers (adapted from BlendEngine, offline) ═══
namespace geodedsp {

inline void fft (std::complex<float>* x, int n, bool inverse) noexcept
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
        const double ang = (inverse ? 2.0 : -2.0) * geode::kPi / len;
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

inline const std::vector<float>& hann() noexcept
{
    static const std::vector<float> w = [] {
        std::vector<float> v ((size_t) geode::kWin);
        for (int i = 0; i < geode::kWin; ++i)
            v[(size_t) i] = 0.5f - 0.5f * std::cos (2.f * geode::kPi * (float) i / (float) (geode::kWin - 1));
        return v;
    }();
    return w;
}

// autocorrelation f0 (NSDF), post-attack window; 0 = unvoiced (bells/drums transpose rigidly).
inline float detectF0 (const float* m, int n, double rate) noexcept
{
    const int start = std::min (n / 4, (int) (0.010 * rate));
    const int win   = std::min (n - start, 8192);
    if (win < 2048) return 0.f;
    std::vector<float> s ((size_t) win);
    double e0 = 0.0;
    for (int i = 0; i < win; ++i) { s[(size_t) i] = m[start + i]; e0 += (double) s[(size_t) i] * s[(size_t) i]; }
    if (e0 < 1e-8) return 0.f;
    const int minLag = std::max (2, (int) (rate / 1200.0));
    const int maxLag = std::min (win / 2, (int) (rate / 35.0));
    if (maxLag <= minLag + 2) return 0.f;
    int bestLag = 0; double bestV = 0.0;
    std::vector<double> nsdf ((size_t) maxLag + 1, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double ac = 0.0, e1 = 0.0, e2 = 0.0;
        const int lim = win - lag;
        for (int i = 0; i < lim; ++i)
        { const double a = s[(size_t) i], b = s[(size_t) (i + lag)]; ac += a * b; e1 += a * a; e2 += b * b; }
        const double v = (e1 + e2 > 1e-12) ? 2.0 * ac / (e1 + e2) : 0.0;
        nsdf[(size_t) lag] = v;
        if (v > bestV) { bestV = v; bestLag = lag; }
    }
    if (bestV < 0.72 || bestLag <= minLag || bestLag >= maxLag) return 0.f;
    for (int lag = minLag; lag < bestLag; ++lag)
        if (nsdf[(size_t) lag] > bestV * 0.92 && lag > minLag && lag < maxLag
            && nsdf[(size_t) lag] >= nsdf[(size_t) (lag - 1)] && nsdf[(size_t) lag] >= nsdf[(size_t) (lag + 1)])
        { bestLag = lag; break; }
    double lagF = bestLag;
    const double ym1 = nsdf[(size_t) (bestLag - 1)], y0 = nsdf[(size_t) bestLag], y1 = nsdf[(size_t) (bestLag + 1)];
    const double den = ym1 - 2.0 * y0 + y1;
    if (std::fabs (den) > 1e-12) lagF += 0.5 * (ym1 - y1) / den;
    return (float) (rate / lagF);
}

} // namespace geodedsp

// ═══ GeodeAnalyzer — OFFLINE, message-thread only (allocates freely) ═══════════
class GeodeAnalyzer
{
public:
    // Analyze a mono view of a sample into partials + noise per frame.
    static void analyzeSample (const float* mono, int n, double rate, GeodeFrameStore& out)
    {
        out = GeodeFrameStore {};
        if (mono == nullptr || n < geode::kWin / 2 || rate <= 0.0) return;

        const float f0   = geodedsp::detectF0 (mono, n, rate);
        const float fund = (f0 > 0.f) ? f0 : geode::kRefHz;
        out.f0 = f0; out.fromWave = false;

        const int totalFrames = std::max (1, (n + geode::kHop - 1) / geode::kHop);
        const int stride      = std::max (1, (totalFrames + geode::kMaxFrames - 1) / geode::kMaxFrames);
        const auto& w         = geodedsp::hann();
        const double binHz    = rate / (double) geode::kWin;

        std::vector<std::complex<float>> buf ((size_t) geode::kWin);
        std::array<float, geode::kBins> mag {};

        for (int fi = 0; fi < totalFrames; fi += stride)
        {
            const int start = fi * geode::kHop;
            for (int i = 0; i < geode::kWin; ++i)
            {
                const int idx = start + i - geode::kWin / 2;
                const float sv = (idx >= 0 && idx < n) ? mono[idx] : 0.f;
                buf[(size_t) i] = { sv * w[(size_t) i], 0.f };
            }
            geodedsp::fft (buf.data(), geode::kWin, false);
            for (int b = 0; b < geode::kBins; ++b) mag[(size_t) b] = std::abs (buf[(size_t) b]);

            GeodeFrame fr;
            extractPartials (mag.data(), binHz, fund, fr);
            extractNoise (mag.data(), rate, fr);
            out.frames.push_back (fr);
        }
        normalize (out);
        out.valid = ! out.frames.empty();
    }

    // Wavetable door — frames are ALREADY partials. Flat layout: ratio/amp[frame*maxP + p].
    static void buildFromWave (const float* ratioFlat, const float* ampFlat,
                               int nFrames, int maxP, GeodeFrameStore& out)
    {
        out = GeodeFrameStore {};
        if (ratioFlat == nullptr || ampFlat == nullptr || nFrames < 1 || maxP < 1) return;
        out.fromWave = true; out.f0 = 0.f;
        const int fN = std::min (nFrames, geode::kMaxFrames);
        for (int f = 0; f < fN; ++f)
        {
            GeodeFrame fr;
            int c = 0;
            for (int p = 0; p < maxP && c < geode::kMaxPartials; ++p)
            {
                const float rt = ratioFlat[(size_t) f * maxP + p];
                const float am = ampFlat[(size_t) f * maxP + p];
                if (rt > 0.f && am > 1e-7f)
                { fr.ratio[(size_t) c] = rt; fr.amp[(size_t) c] = am; ++c; }
            }
            fr.nPartials = c;
            out.frames.push_back (fr);
        }
        normalize (out);
        out.valid = ! out.frames.empty();
    }

private:
    static float frameMedian (const float* m, int n)
    {
        static thread_local std::vector<float> v;
        v.assign (m, m + n);
        std::nth_element (v.begin(), v.begin() + n / 2, v.end());
        return v[(size_t) (n / 2)];
    }

    static void extractPartials (const float* mag, double binHz, float fund, GeodeFrame& fr)
    {
        struct Pk { float ratio, amp; };
        static thread_local std::vector<Pk> pks;
        pks.clear();
        const float med = frameMedian (mag, geode::kBins);
        const float scale = 2.f / (float) geode::kWin;
        for (int b = 2; b < geode::kBins - 2; ++b)
        {
            const float v = mag[b];
            if (! (v > 3.f * med + 1e-12f && v >= mag[b - 1] && v >= mag[b + 1]
                   && v > mag[b - 2] && v > mag[b + 2])) continue;
            const int lo = std::max (0, b - 3), hi = std::min (geode::kBins - 1, b + 3);
            double m = 0.0, c = 0.0;
            for (int k = lo; k <= hi; ++k) { m += mag[k]; c += (double) mag[k] * k; }
            if (m <= 1e-12) continue;
            const double centroid = c / m;
            const float  hz = (float) (centroid * binHz);
            if (hz < 15.f) continue;
            pks.push_back ({ hz / fund, (float) m * scale });
        }
        // keep the STRONGEST kMaxPartials, then sort by ratio (stable slot order for interp)
        if ((int) pks.size() > geode::kMaxPartials)
        {
            std::nth_element (pks.begin(), pks.begin() + geode::kMaxPartials, pks.end(),
                              [] (const Pk& a, const Pk& b) { return a.amp > b.amp; });
            pks.resize (geode::kMaxPartials);
        }
        std::sort (pks.begin(), pks.end(), [] (const Pk& a, const Pk& b) { return a.ratio < b.ratio; });
        fr.nPartials = (int) pks.size();
        for (int i = 0; i < fr.nPartials; ++i) { fr.ratio[(size_t) i] = pks[(size_t) i].ratio; fr.amp[(size_t) i] = pks[(size_t) i].amp; }
    }

    static void extractNoise (const float* mag, double rate, GeodeFrame& fr)
    {
        const float med = frameMedian (mag, geode::kBins);
        const double binHz = rate / (double) geode::kWin;
        const double loF = 20.0, hiF = rate * 0.5;
        const double logLo = std::log (loF), logHi = std::log (std::max (loF + 1.0, hiF));
        for (int b = 1; b < geode::kBins; ++b)
        {
            // residual = bins that are NOT strong local peaks (the stochastic part)
            const bool strong = mag[b] > 4.f * med;
            if (strong) continue;
            const double hz = b * binHz;
            if (hz < loF) continue;
            int band = (int) ((std::log (hz) - logLo) / (logHi - logLo) * geode::kNoiseBands);
            band = std::min (geode::kNoiseBands - 1, std::max (0, band));
            fr.noise[(size_t) band] += mag[b] * mag[b];
        }
        for (int k = 0; k < geode::kNoiseBands; ++k) fr.noise[(size_t) k] = std::sqrt (fr.noise[(size_t) k]);
    }

    // Scale amps + noise to a musical target so SILT crossfades between comparable levels.
    static void normalize (GeodeFrameStore& s)
    {
        float pMax = 1e-9f, nMax = 1e-9f;
        for (auto& f : s.frames)
        {
            for (int i = 0; i < f.nPartials; ++i) pMax = std::max (pMax, f.amp[(size_t) i]);
            for (int k = 0; k < geode::kNoiseBands; ++k) nMax = std::max (nMax, f.noise[(size_t) k]);
        }
        const float pG = 0.35f / pMax, nG = 0.5f / nMax;
        for (auto& f : s.frames)
        {
            for (int i = 0; i < f.nPartials; ++i) f.amp[(size_t) i] *= pG;
            for (int k = 0; k < geode::kNoiseBands; ++k) f.noise[(size_t) k] *= nG;
        }
    }
};

// ═══ GeodeEngine — per-voice resynthesizer (real-time; allocates only in prepare) ═══
class GeodeEngine
{
public:
    GeodeEngine() = default;

    void prepare (double sampleRate) noexcept
    {
        rate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        (void) sineLUT();
        phase_.assign (geode::kMaxPartials, 0.f);
        wr_.ratio.fill (0.f); wr_.amp.fill (0.f);
        nzLpZ_ = nzLpZr_ = 0.f;
        pos01_ = 0.f; prevPos_ = -1.f;
        makeup_ = 1.0f;
    }

    // Shared partial budget (processor-owned int, audio-thread only — no atomics).
    void setPartialBudget (int* used, int cap) noexcept { budgetUsed_ = used; budgetCap_ = cap; }

    void setFrameStore (const GeodeFrameStore* s) noexcept { store_ = s; }
    bool hasStore() const noexcept { return store_ != nullptr && store_->valid && store_->numFrames() > 0; }

    void setParams (const GeodeParams& p) noexcept
    {
        // user scrub → snap the read-head to POSITION; otherwise CREEP owns it
        if (prevPos_ >= 0.f && std::fabs (p.position - prevPos_) > 1e-4f) pos01_ = clamp01 (p.position);
        prevPos_ = p.position;
        p_ = p;
    }

    void noteOn (double playedHz, std::uint32_t seed) noexcept
    {
        playedHz_ = playedHz > 0.0 ? playedHz : 261.6256;
        rng_ = seed ? seed : 0x9E3779B9u;
        for (auto& ph : phase_) ph = 0.f;
        nzLpZ_ = nzLpZr_ = 0.f;
        pos01_ = clamp01 (p_.position);
        prevPos_ = p_.position;
        dir_ = 1.f;
    }

    void setPitchRatio (double r) noexcept { pitchMul_ = r > 0.0 ? r : 1.0; }

    void renderBlockAdd (float* L, float* R, int n) noexcept
    {
        if (! hasStore() || L == nullptr || R == nullptr || n <= 0) return;
        const int nf = store_->numFrames();

        // ── advance the read-head at CREEP rate (block-rate; frozen by FOSSIL) ──
        const float freeze = clamp01 (p_.fossil);
        const float creepRate = clamp01 (p_.creep) * 2.0f;           // store-lengths / sec
        if (creepRate > 1e-4f && freeze < 0.999f)
        {
            const float dt = (float) n / (float) rate_;
            float adv = creepRate * (1.f - freeze) * dt * dir_;
            pos01_ += adv;
            if (p_.loopMode == 3) // ping-pong
            { while (pos01_ > 1.f || pos01_ < 0.f) { if (pos01_ > 1.f) { pos01_ = 2.f - pos01_; dir_ = -dir_; } else { pos01_ = -pos01_; dir_ = -dir_; } } }
            else if (p_.loopMode == 0) // one-shot: clamp
            { pos01_ = clamp01 (pos01_); }
            else // fwd/reverse loop: wrap
            { pos01_ -= std::floor (pos01_); }
        }

        // ── interpolate the two bracketing frames into the working partial bank ──
        const float fp = clamp01 (pos01_) * (float) (nf - 1);
        const int   fa = (int) fp;
        const int   fb = std::min (nf - 1, fa + 1);
        const float fr = fp - (float) fa;
        const GeodeFrame& A = store_->frames[(size_t) fa];
        const GeodeFrame& B = store_->frames[(size_t) fb];
        const int nP = std::max (A.nPartials, B.nPartials);
        for (int j = 0; j < nP; ++j)
        {
            const float rA = A.ratio[(size_t) j], rB = B.ratio[(size_t) j];
            const float aA = A.amp[(size_t) j],   aB = B.amp[(size_t) j];
            wr_.ratio[(size_t) j] = (rA > 0.f && rB > 0.f) ? (rA + (rB - rA) * fr) : (rA > 0.f ? rA : rB);
            wr_.amp[(size_t) j]   = aA + (aB - aA) * fr;
        }
        wr_.nPartials = nP;

        applySculpt (nP);   // FRACTURE / FORMANT / TILT / CUT / SIEVE / DISTILL / HAZE

        // ── QUALITY + shared partial budget: cap active partials, thin quietest first ──
        int active = 16 + (int) (clamp01 (p_.quality) * (float) (geode::kMaxPartials - 16));
        active = std::min (active, nP);
        if (budgetUsed_ != nullptr && budgetCap_ > 0)
        {
            const int room = budgetCap_ - *budgetUsed_;
            if (room < active) active = std::max (0, room);
        }
        if (active < nP) keepLoudest (nP, active);   // zero all but the loudest `active`

        // ── equal-power SILT crossfade (partials ↔ noise residual) ──
        const float silt = clamp01 (p_.silt);
        const float gPart = std::cos (silt * 0.5f * geode::kPi);
        const float gNoise = std::sin (silt * 0.5f * geode::kPi);

        // ── oscillator bank ──
        const float baseHz = (float) (playedHz_ * pitchMul_);
        int voiced = 0;
        for (int j = 0; j < nP; ++j)
        {
            const float a = wr_.amp[(size_t) j];
            if (a <= 1e-6f || wr_.ratio[(size_t) j] <= 0.f) continue;
            const float hz = wr_.ratio[(size_t) j] * baseHz;
            if (hz <= 0.f || hz >= (float) rate_ * 0.48f) continue;   // anti-alias: skip > Nyquist
            const float inc = hz / (float) rate_;
            float ph = phase_[(size_t) j];
            const float ga = a * gPart * makeup_;
            for (int i = 0; i < n; ++i)
            {
                const float s = ga * sineAt (ph);       // partials are centered (mono → both channels)
                L[i] += s; R[i] += s;
                ph += inc; if (ph >= 1.f) ph -= 1.f;
            }
            phase_[(size_t) j] = ph;
            ++voiced;
        }
        if (budgetUsed_ != nullptr) *budgetUsed_ += voiced;

        // ── colored noise residual (decorrelated L/R for width) ──
        if (gNoise > 1e-4f) addNoise (L, R, n, A, B, fr, gNoise);
    }

private:
    static inline float clamp01 (float x) noexcept { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    static const std::vector<float>& sineLUT() noexcept
    {
        static const std::vector<float> t = [] {
            std::vector<float> v (kLUT + 1);
            for (int i = 0; i <= kLUT; ++i) v[(size_t) i] = std::sin (2.f * geode::kPi * (float) i / (float) kLUT);
            return v;
        }();
        return t;
    }
    static inline float sineAt (float phase01) noexcept
    {
        const float x = phase01 * (float) kLUT;
        const int   i = (int) x;
        const float f = x - (float) i;
        const auto& t = sineLUT();
        return t[(size_t) i] + (t[(size_t) (i + 1)] - t[(size_t) i]) * f;
    }

    // sculpt the working partial bank in place (all identity at neutral)
    void applySculpt (int nP) noexcept
    {
        // FRACTURE — bipolar harmonic↔inharmonic spacing (stretch the ratios about 1.0)
        const float frac = (p_.fracture - 0.5f) * 2.f;   // -1..+1
        if (std::fabs (frac) > 1e-3f)
        {
            const float e = std::pow (2.f, frac);        // 0.5 .. 2
            for (int j = 0; j < nP; ++j)
                if (wr_.ratio[(size_t) j] > 0.f)
                    wr_.ratio[(size_t) j] = std::pow (wr_.ratio[(size_t) j], e);
        }

        // FORMANT — shift the amplitude envelope along ratio (resample amp-vs-ratio)
        const float fShift = (p_.formant - 0.5f) * 2.f;  // -1..+1
        if (std::fabs (fShift) > 1e-3f && nP > 1)
        {
            const float mul = std::pow (2.f, fShift);    // envelope stretch factor
            for (int j = 0; j < nP; ++j)
            {
                const float srcRatio = wr_.ratio[(size_t) j] / mul;
                wr_.amp[(size_t) j] *= p_.formantKeep ? envAt (srcRatio, nP) / (envAt (wr_.ratio[(size_t) j], nP) + 1e-9f)
                                                      : 1.f;
            }
        }

        // TILT — bipolar spectral tilt about ratio 1.0 (bright/dark)
        const float tilt = (p_.tilt - 0.5f) * 2.f;       // -1..+1
        if (std::fabs (tilt) > 1e-3f)
            for (int j = 0; j < nP; ++j)
            {
                const float r = std::max (0.05f, wr_.ratio[(size_t) j]);
                wr_.amp[(size_t) j] *= std::pow (r, tilt * 1.5f);
            }

        // CUT — spectral low-pass (soft) over ratio
        if (p_.cut < 0.999f)
        {
            const float cutRatio = 0.25f + clamp01 (p_.cut) * 32.f;   // 0.25×..32× fundamental
            for (int j = 0; j < nP; ++j)
                if (wr_.ratio[(size_t) j] > cutRatio)
                {
                    const float over = wr_.ratio[(size_t) j] / cutRatio;
                    wr_.amp[(size_t) j] /= (over * over);   // 12 dB/oct-ish rolloff
                }
        }

        // HAZE — amplitude neighbor blur (spectral de-focus)
        const float haze = clamp01 (p_.haze);
        if (haze > 1e-3f && nP > 2)
        {
            float prev = wr_.amp[0];
            for (int j = 1; j < nP - 1; ++j)
            {
                const float blur = 0.25f * prev + 0.5f * wr_.amp[(size_t) j] + 0.25f * wr_.amp[(size_t) (j + 1)];
                prev = wr_.amp[(size_t) j];
                wr_.amp[(size_t) j] = wr_.amp[(size_t) j] + (blur - wr_.amp[(size_t) j]) * haze;
            }
        }

        // SIEVE — spectral gate: drop partials below a rising threshold
        const float sieve = clamp01 (p_.sieve);
        if (sieve > 1e-3f)
        {
            float mx = 1e-9f;
            for (int j = 0; j < nP; ++j) mx = std::max (mx, wr_.amp[(size_t) j]);
            const float thr = mx * sieve * 0.9f;
            for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] < thr) wr_.amp[(size_t) j] = 0.f;
        }

        // DISTILL — keep only the loudest fraction of partials
        const float distill = clamp01 (p_.distill);
        if (distill > 1e-3f)
        {
            const int keep = std::max (1, (int) ((1.f - distill) * (float) nP));
            keepLoudest (nP, keep);
        }
    }

    // amplitude at an arbitrary ratio (nearest-partial envelope sample, for FORMANT)
    float envAt (float ratio, int nP) const noexcept
    {
        if (nP <= 0) return 0.f;
        float best = 1e9f, amp = 0.f;
        for (int j = 0; j < nP; ++j)
        {
            const float d = std::fabs (wr_.ratio[(size_t) j] - ratio);
            if (d < best) { best = d; amp = wr_.amp[(size_t) j]; }
        }
        return amp;
    }

    // zero all but the `keep` loudest partials (in place)
    void keepLoudest (int nP, int keep) noexcept
    {
        if (keep >= nP) return;
        if (keep <= 0) { for (int j = 0; j < nP; ++j) wr_.amp[(size_t) j] = 0.f; return; }
        static thread_local std::vector<float> tmp;
        tmp.assign (wr_.amp.begin(), wr_.amp.begin() + nP);
        std::nth_element (tmp.begin(), tmp.begin() + (nP - keep), tmp.end());
        const float thr = tmp[(size_t) (nP - keep)];
        int kept = 0;
        for (int j = 0; j < nP; ++j)
        {
            if (wr_.amp[(size_t) j] >= thr && kept < keep) ++kept;
            else wr_.amp[(size_t) j] = 0.f;
        }
    }

    // colored-noise residual from the interpolated band envelope
    void addNoise (float* L, float* R, int n, const GeodeFrame& A, const GeodeFrame& B, float fr, float gNoise) noexcept
    {
        // total residual level + spectral centroid → drive a one-pole tilt (bright↔dark)
        float total = 0.f, cen = 0.f;
        for (int k = 0; k < geode::kNoiseBands; ++k)
        {
            const float e = A.noise[(size_t) k] + (B.noise[(size_t) k] - A.noise[(size_t) k]) * fr;
            total += e; cen += e * (float) k;
        }
        if (total < 1e-6f) return;
        cen /= total;                                   // 0..kNoiseBands-1
        const float bright = cen / (float) (geode::kNoiseBands - 1);   // 0 dark .. 1 bright
        const float lpCoef = 0.02f + bright * 0.85f;    // one-pole coefficient
        const float lvl = total * gNoise * makeup_ * 0.6f;
        for (int i = 0; i < n; ++i)
        {
            const float wl = whiteNoise();
            const float wrr = whiteNoise();             // decorrelated R
            nzLpZ_  += lpCoef * (wl  - nzLpZ_);
            nzLpZr_ += lpCoef * (wrr - nzLpZr_);
            // blend the LP (dark) with the raw (bright) by `bright`
            L[i] += lvl * (nzLpZ_  + bright * (wl  - nzLpZ_));
            R[i] += lvl * (nzLpZr_ + bright * (wrr - nzLpZr_));
        }
    }

    inline float whiteNoise() noexcept
    {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (float) ((int32_t) rng_) * (1.f / 2147483648.f);
    }

    // ── state ──
    static constexpr int kLUT = 4096;
    const GeodeFrameStore* store_ = nullptr;
    GeodeParams p_;
    GeodeFrame  wr_;                          // working (sculpted) partial bank
    std::vector<float> phase_;                // per-partial phase [0,1)
    double rate_ = 48000.0, playedHz_ = 261.6256, pitchMul_ = 1.0;
    float  pos01_ = 0.f, prevPos_ = -1.f, dir_ = 1.f;
    float  nzLpZ_ = 0.f, nzLpZr_ = 0.f, makeup_ = 1.f;
    std::uint32_t rng_ = 0x9E3779B9u;
    int*   budgetUsed_ = nullptr;
    int    budgetCap_  = 0;
};

} // namespace tw
