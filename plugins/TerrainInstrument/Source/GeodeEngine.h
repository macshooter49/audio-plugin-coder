#pragma once
// ════════════════════════════════════════════════════════════════════════════
//  tw::GeodeEngine — Terrain RESYNTH oscillator (Engine::SPEC)
//
//  (Internal name stays "Geode" for preset/param-ID stability; the UI calls it
//   "Resynth".) A SAMPLE resynthesizer: OFFLINE-analyze a dropped sound into
//   per-frame sinusoidal PARTIALS; per-voice RESYNTHESIZE it as an oscillator
//   bank, played back polyphonically with a START / STRETCH / SCAN read-head and
//   a spectral-sculpt + degrade chain — pitched to the key, NEVER detuned.
//
//  Controls (amplitude-domain unless noted — the played FUNDAMENTAL is never moved, so nothing
//  detunes; SHAPE may tune OVERTONES onto exact harmonics; DSP grounded in 2026-07-07 research):
//    SCAN     play/scan rate (0=hold .5=natural 1=2×)          [read-head]
//    STRETCH  time-stretch / freeze (slows the read-head)      [read-head]
//    START    read-head start position                         [read-head]
//    SIEVE    spectral gate (the lossy "data-removal" hero)    [amp]
//    CUT      spectral filter, LP or HP (~24 dB/oct)           [amp]
//    SHAPE    morph→sine/square/saw: tune overtones to harmonics [amp+tune] (Chebyshev W(n))
//    FORMANT  true-envelope shift — moves envelope, not pitch  [amp]  (Röbel; kernel-smoothed)
//    TILT     spectral tilt bright/dark                        [amp]
//    QUALITY  active partial budget (16..96)                   [amp]
//    DRIVE    spectral distortion (soft-clip, adds harmonics)  [post-synth, period-preserving]
//    CRUSH    bit-quantize + rate-decimate lo-fi degrade       [post-synth]
//
//  Real-time safety: heavy analysis runs ONCE per source on the message thread
//  into a shared read-only GeodeFrameStore (processor double-buffers + atomic-
//  publishes it). Every voice/unison instance holds a const pointer and only
//  resynthesizes. renderBlockAdd()/postProcess() allocate nothing.
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
    constexpr int   kMaxPartials = 96;    // matches Wavetable kMaxPartials (slot cap / analysis depth)
    // ── real-time CPU governor (added rs2 — the STRETCH/unison CPU-cliff fix) ──
    // Additive resynthesis costs ~1 sine-osc PER PARTIAL PER SAMPLE. Left uncapped it pegs a
    // core (measured: 3072 partials ≈ 40% of one core for the oscillator ALONE). These bound it.
    constexpr int   kMinActive   = 16;    // floor of the QUALITY active-partial range
    constexpr int   kMaxActive   = 64;    // per-voice active-partial CEILING (was kMaxPartials=96)
    constexpr int   kUnisonFloor = 10;    // a single unison bank never thins below this
    constexpr int   kNoiseBands  = 16;    // residual log-band envelope resolution (analysis only)
    constexpr int   kMaxFrames   = 256;   // frame-store cap (long samples subsampled)
    constexpr int   kWin         = 2048;  // analysis window
    constexpr int   kHop         = 512;   // 75% overlap
    constexpr int   kBins        = kWin / 2 + 1;
    constexpr float kPi          = 3.14159265358979323846f;
    constexpr float kRefHz       = 261.6256f; // C4 — unvoiced-sample transpose reference
    // Display (data-removal viz) log-frequency window, in partial-ratio units.
    constexpr float kDispRMin    = 0.5f;      // half the fundamental (sub) …
    constexpr float kDispRMax    = 64.f;      // …to +6 octaves of partials
}

// One analyzed spectral frame: K partials (ratio-to-fundamental + linear amp) and
// a residual noise band envelope (kept for analysis stability; not rendered).
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
    float f0         = 0.f;          // detected fundamental (Hz); 0 = unvoiced
    float naturalSec = 0.f;          // original sample DURATION (s) → play-through at natural speed
    bool  fromWave   = false;
    bool  valid      = false;
    int   numFrames() const noexcept { return (int) frames.size(); }
};

// Per-block sculpt/play parameters (gathered by the voice; == gates the push).
// NOTE: field names are the Resynth vocabulary. The APVTS param IDs they map from
// keep their historical GEODE_* strings (see PluginProcessor gather) for preset safety.
struct GeodeParams
{
    // ── page 1 (Play & Character) ──
    float start   = 0.f;    // 0..1 read-head START position          (APVTS: GEODE_POSITION)
    float stretch = 0.f;    // 0..1 time-stretch / freeze amount       (APVTS: GEODE_FOSSIL)
    float scan    = 0.5f;   // 0..1 play/scan rate: 0=hold .5=natural 1=2× (APVTS: GEODE_CREEP)
    float sieve   = 0.f;    // 0..1 spectral gate (lossy hero)         (APVTS: GEODE_SIEVE)
    float cut     = 1.f;    // 0..1 spectral filter (1 = open)         (APVTS: GEODE_CUT)
    float shape   = 0.f;    // 0..1 morph toward target waveform       (APVTS: GEODE_DISTILL)
    float drive   = 0.f;    // 0..1 spectral distortion (post-synth)   (APVTS: GEODE_HAZE)
    // ── page 2 (Sculpt & Fidelity) ──
    float quality = 0.80f;  // 0..1 active partial cap (→ 16..kMaxPartials) (APVTS: GEODE_QUALITY)
    float formant = 0.5f;   // 0..1 bipolar formant/envelope shift     (APVTS: GEODE_FORMANT)
    float tilt    = 0.5f;   // 0..1 bipolar spectral tilt (0.5 = flat) (APVTS: GEODE_TILT)
    float crush   = 0.f;    // 0..1 bitcrush + rate decimate (post-synth) (APVTS: GEODE_SILT)
    // ── choices / toggles ──
    int   shapeTarget = 2;  // 0=sine 1=square 2=saw                    (APVTS: GEODE_SHAPE_TARGET)
    int   cutMode     = 0;  // 0=LP 1=HP                                (APVTS: GEODE_CUT_MODE)
    int   loopMode    = 1;  // 0=one-shot 1=fwd 2=reverse 3=ping-pong   (APVTS: GEODE_LOOP)
    bool  formantKeep = true;                                        // (APVTS: GEODE_FKEEP)

    bool operator== (const GeodeParams& o) const noexcept
    {
        return start==o.start && stretch==o.stretch && scan==o.scan && sieve==o.sieve
            && cut==o.cut && shape==o.shape && drive==o.drive && quality==o.quality
            && formant==o.formant && tilt==o.tilt && crush==o.crush
            && shapeTarget==o.shapeTarget && cutMode==o.cutMode
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
    // Analyze a mono view of a sample into partials per frame (SAMPLE-ONLY resynth).
    static void analyzeSample (const float* mono, int n, double rate, GeodeFrameStore& out)
    {
        out = GeodeFrameStore {};
        if (mono == nullptr || n < geode::kWin / 2 || rate <= 0.0) return;

        const float f0   = geodedsp::detectF0 (mono, n, rate);
        const float fund = (f0 > 0.f) ? f0 : geode::kRefHz;
        out.f0 = f0; out.fromWave = false;
        out.naturalSec = (float) ((double) n / rate);   // play-through reference: the sample's real length

        const int totalFrames = std::max (1, (n + geode::kHop - 1) / geode::kHop);
        const int stride      = std::max (1, (totalFrames + geode::kMaxFrames - 1) / geode::kMaxFrames);
        const auto& w         = geodedsp::hann();
        const double binHz    = rate / (double) geode::kWin;

        std::vector<std::complex<float>> buf ((size_t) geode::kWin);
        std::array<float, geode::kBins> mag {};
        Tracker T; T.reset();                 // persists across frames → stable partial slots
        std::vector<Peak> peaks; peaks.reserve ((size_t) geode::kMaxPartials);

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
            detectPeaks (mag.data(), binHz, fund, peaks);   // raw peaks this frame
            trackFrame  (T, peaks, fr);                     // → stable slots (birth/death tracking)
            extractNoise (mag.data(), rate, fr);
            out.frames.push_back (fr);
        }
        normalize (out);
        out.valid = ! out.frames.empty();
    }

    // Wavetable door (retained for API compatibility; Resynth is sample-only and does not call this).
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

    struct Peak { float ratio, amp; };

    // detect this frame's spectral peaks → list of {ratio, amp} (loudest kMaxPartials kept).
    static void detectPeaks (const float* mag, double binHz, float fund, std::vector<Peak>& out)
    {
        out.clear();
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
            out.push_back ({ hz / fund, (float) m * scale });
        }
        if ((int) out.size() > geode::kMaxPartials)
        {
            std::nth_element (out.begin(), out.begin() + geode::kMaxPartials, out.end(),
                              [] (const Peak& a, const Peak& b) { return a.amp > b.amp; });
            out.resize (geode::kMaxPartials);
        }
    }

    // ─── cross-frame partial TRACKER (McAulay–Quatieri birth/death) ────────────────
    struct Tracker
    {
        std::array<float, geode::kMaxPartials> ratio {};
        std::array<float, geode::kMaxPartials> amp   {};
        std::array<int,   geode::kMaxPartials> miss  {};
        void reset() noexcept { ratio.fill (0.f); amp.fill (0.f); miss.fill (0); }
    };

    static void trackFrame (Tracker& T, std::vector<Peak>& peaks, GeodeFrame& fr) noexcept
    {
        constexpr float kTol        = 0.05f;   // match window (±~0.85 semitone)
        constexpr int   kCoast      = 4;       // frames a missed track survives
        constexpr float kCoastDecay = 0.5f;    // amp decay per missed frame while coasting

        std::array<bool, geode::kMaxPartials> used {};
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.amp > b.amp; });
        for (const auto& p : peaks)
        {
            int best = -1; float bestErr = kTol;
            for (int s = 0; s < geode::kMaxPartials; ++s)
            {
                if (T.ratio[(size_t) s] <= 0.f || used[(size_t) s]) continue;
                const float err = std::fabs (p.ratio - T.ratio[(size_t) s]) / T.ratio[(size_t) s];
                if (err < bestErr) { bestErr = err; best = s; }
            }
            if (best < 0)   // BIRTH — take a FREE slot only (never steal a live/coasting slot)
            {
                for (int s = 0; s < geode::kMaxPartials; ++s)
                    if (T.ratio[(size_t) s] <= 0.f && ! used[(size_t) s]) { best = s; break; }
            }
            if (best < 0) continue;   // no free slot → drop this (quieter) peak
            fr.ratio[(size_t) best] = p.ratio; fr.amp[(size_t) best] = p.amp; used[(size_t) best] = true;
            T.ratio[(size_t) best] = p.ratio; T.amp[(size_t) best] = p.amp; T.miss[(size_t) best] = 0;
        }
        for (int s = 0; s < geode::kMaxPartials; ++s)
        {
            if (T.ratio[(size_t) s] > 0.f && ! used[(size_t) s])
            {
                if (++T.miss[(size_t) s] > kCoast) { T.ratio[(size_t) s] = 0.f; T.amp[(size_t) s] = 0.f; }
                else { T.amp[(size_t) s] *= kCoastDecay; fr.ratio[(size_t) s] = T.ratio[(size_t) s]; fr.amp[(size_t) s] = T.amp[(size_t) s]; }
            }
        }
        int nP = 0; for (int s = 0; s < geode::kMaxPartials; ++s) if (fr.amp[(size_t) s] > 1e-6f) nP = s + 1;
        fr.nPartials = nP;
    }

    static void extractNoise (const float* mag, double rate, GeodeFrame& fr)
    {
        const float med = frameMedian (mag, geode::kBins);
        const double binHz = rate / (double) geode::kWin;
        const double loF = 20.0, hiF = rate * 0.5;
        const double logLo = std::log (loF), logHi = std::log (std::max (loF + 1.0, hiF));
        for (int b = 1; b < geode::kBins; ++b)
        {
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
        ampZ_.assign  (geode::kMaxPartials, 0.f);   // declick: per-partial previous-block gain
        wr_.ratio.fill (0.f); wr_.amp.fill (0.f);
        pos01_ = 0.f; prevPos_ = -1.f;
        makeup_ = 1.0f;
        crushHoldL_ = crushHoldR_ = 0.f; crushCnt_ = 0;
    }

    // Shared partial budget (processor-owned int, audio-thread only — no atomics).
    void setPartialBudget (int* used, int cap) noexcept { budgetUsed_ = used; budgetCap_ = cap; }

    // CONSTANT-COST UNISON: divide this bank's active partials by ceil(sqrt(N)) so N detuned
    // unison banks cost ~one full bank instead of N (detuned partials fuse in the ear — full
    // spectral resolution on every voice is wasted CPU). N=1→÷1, 4→÷2, 9→÷3, 16→÷4.
    void setUnisonScale (int n) noexcept
    {
        int d = 1; const int nn = n > 1 ? n : 1;
        while (d * d < nn) ++d;             // ceil(sqrt(n))
        unisonDiv_ = d < 1 ? 1 : d;
    }

    void setFrameStore (const GeodeFrameStore* s) noexcept { store_ = s; }
    bool hasStore() const noexcept { return store_ != nullptr && store_->valid && store_->numFrames() > 0; }
    float readPos01() const noexcept { return pos01_; }   // current read-head [0,1] (tests / UI follower)

    void setParams (const GeodeParams& p) noexcept
    {
        // user scrub → snap the read-head to START; otherwise SCAN owns it
        if (prevPos_ >= 0.f && std::fabs (p.start - prevPos_) > 1e-4f) pos01_ = clamp01 (p.start);
        prevPos_ = p.start;
        p_ = p;
    }

    void noteOn (double playedHz, std::uint32_t seed) noexcept
    {
        playedHz_ = playedHz > 0.0 ? playedHz : 261.6256;
        rng_ = seed ? seed : 0x9E3779B9u;
        for (auto& ph : phase_) ph = 0.f;
        std::fill (ampZ_.begin(), ampZ_.end(), 0.f);   // start silent → first block ramps up (clean attack)
        pos01_ = clamp01 (p_.start);
        prevPos_ = p_.start;
        dir_ = 1.f;
        crushHoldL_ = crushHoldR_ = 0.f; crushCnt_ = 0;
    }

    void setPitchRatio (double r) noexcept { pitchMul_ = r > 0.0 ? r : 1.0; }

    void renderBlockAdd (float* L, float* R, int n) noexcept
    {
        if (! hasStore() || L == nullptr || R == nullptr || n <= 0) return;
        const int nf = store_->numFrames();

        // ── advance the read-head — PLAY-THROUGH (block-rate; STRETCH freezes/slows it) ──
        // For a sample (naturalSec>0), SCAN is play-through speed referenced to the sample's real
        // length: 0.5 → 1× natural, 0 → hold, 1 → 2×. STRETCH scales the advance toward 0 (slow /
        // time-stretch / freeze) — max STRETCH parks the read-head on a frame (infinite pad).
        const float freeze = clamp01 (p_.stretch);
        float scanRate = clamp01 (p_.scan) * 2.0f;
        if (store_->naturalSec > 1e-4f) scanRate /= store_->naturalSec;
        if (scanRate > 1e-4f && freeze < 0.999f)
        {
            const float dt = (float) n / (float) rate_;
            float adv = scanRate * (1.f - freeze) * dt * dir_;
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

        // ── CPU GOVERNOR: decide the active-partial count BEFORE sculpting (rs2). The sculpt pass
        // is where FORMANT's O(n²) envelope lives — thinning first means it (and every per-partial
        // op) only touches partials we will actually render. QUALITY sets the ceiling; CONSTANT-COST
        // UNISON divides it; the shared processor budget is the final hard ceiling (thins gracefully).
        int active = geode::kMinActive + (int) (clamp01 (p_.quality)
                        * (float) (geode::kMaxActive - geode::kMinActive));
        if (unisonDiv_ > 1) active = std::max (geode::kUnisonFloor, active / unisonDiv_);
        active = std::min (active, nP);
        if (budgetUsed_ != nullptr && budgetCap_ > 0)
        {
            const int room = budgetCap_ - *budgetUsed_;
            if (room < active) active = std::max (0, room);
        }
        if (active < nP) keepLoudest (nP, active);   // zero all but the loudest `active` (slots preserved)

        applySculpt (nP);   // SHAPE/FORMANT/TILT/CUT/SIEVE (amp-only, never ratios) — skips zeroed slots

        // ── oscillator bank — per-partial amplitude RAMP (declick) ──────────────────────
        // Partial FREQUENCY = analysisRatio × playedHz (× pitchMul from key/oct/semi/cents).
        // No bedrock, no envelope repitch — the sample plays at the KEY pitch, always tuned.
        const float baseHz = (float) (playedHz_ * pitchMul_);
        int voiced = 0;
#ifndef GEODE_NO_DECLICK
        const float invN = 1.f / (float) n;
        for (int j = 0; j < geode::kMaxPartials; ++j)
        {
            const float prevGa = ampZ_[(size_t) j];
            const float rj  = wr_.ratio[(size_t) j];
            const float hz  = rj * baseHz;
            const bool  aud = (rj > 0.f && hz > 0.f && hz < (float) rate_ * 0.48f);   // renderable this block
            const float tgtGa = (j < nP && aud) ? wr_.amp[(size_t) j] * makeup_ : 0.f;
            if (prevGa <= 1e-7f && tgtGa <= 1e-7f) { ampZ_[(size_t) j] = 0.f; continue; }   // silent slot — skip
            const float inc = aud ? hz / (float) rate_ : 0.f;   // a dying partial keeps its freq while it fades
            float ph = phase_[(size_t) j];
            const float dGa = (tgtGa - prevGa) * invN;
            float ga = prevGa;
            for (int i = 0; i < n; ++i)
            {
                const float s = ga * sineAt (ph);       // partials are centered (mono → both channels)
                L[i] += s; R[i] += s;
                ph += inc; if (ph >= 1.f) ph -= 1.f;
                ga += dGa;
            }
            phase_[(size_t) j] = ph;
            ampZ_[(size_t) j] = tgtGa;
            if (tgtGa > 1e-6f) ++voiced;
        }
#else
        for (int j = 0; j < nP; ++j)
        {
            const float a = wr_.amp[(size_t) j];
            if (a <= 1e-6f || wr_.ratio[(size_t) j] <= 0.f) continue;
            const float hz = wr_.ratio[(size_t) j] * baseHz;
            if (hz <= 0.f || hz >= (float) rate_ * 0.48f) continue;
            const float inc = hz / (float) rate_;
            float ph = phase_[(size_t) j];
            const float ga = a * makeup_;
            for (int i = 0; i < n; ++i) { const float s = ga * sineAt (ph); L[i] += s; R[i] += s; ph += inc; if (ph >= 1.f) ph -= 1.f; }
            phase_[(size_t) j] = ph;
            ++voiced;
        }
#endif
        if (budgetUsed_ != nullptr) *budgetUsed_ += voiced;
    }

    // ── POST-SYNTH degrade — applied ONCE per osc (after the unison sum) on this osc's block.
    // DRIVE = soft-clip distortion (a periodic input keeps its period → adds harmonics, no detune).
    // CRUSH = bit-depth quantize + sample-rate decimate (the lossy "old-data" degrade).
    void postProcess (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0) return;
        const float drive = clamp01 (p_.drive);
        const float crush = clamp01 (p_.crush);

        if (drive > 1e-3f)
        {
            // AMPLIFIED: slams the shaper (g up to ~37×) with only gentle makeup, so DRIVE gets LOUDER
            // and harmonically denser as you push it — 100% is meant to be ear-blasting, 50% already hot.
            const float g  = 1.f + drive * 36.f;         // pre-gain into the shaper
            const float mk = 1.f / (1.f + drive * 0.8f); // gentle makeup (0.56 at full) → level jumps UP
            for (int i = 0; i < n; ++i)
            {
                L[i] = softClip (L[i] * g) * mk;
                R[i] = softClip (R[i] * g) * mk;
            }
        }

        if (crush > 1e-3f)
        {
            const float bits   = 16.f - crush * 13.f;                 // 16 → ~3 bits
            const float levels = std::pow (2.f, bits);
            const float inv    = 1.f / levels;
            const int   hold   = 1 + (int) (crush * crush * 40.f);     // sample-and-hold decimation
            for (int i = 0; i < n; ++i)
            {
                if (crushCnt_ <= 0) { crushHoldL_ = L[i]; crushHoldR_ = R[i]; crushCnt_ = hold; }
                --crushCnt_;
                L[i] = std::round (crushHoldL_ * levels) * inv;
                R[i] = std::round (crushHoldR_ * levels) * inv;
            }
        }
    }

    // ═══ DISPLAY-ONLY: the sculpted spectrum as a log-frequency magnitude envelope ═══
    // Runs the SAME interpolate → sculpt → QUALITY path as renderBlockAdd, synthesizes NO audio
    // and touches NO audio-voice state — the editor "display engine" calls this each UI tick so the
    // data-removal viz reacts to POSITION + every sculpt knob even when no note sounds.
    bool computeDisplayEnvelope (float* outBins, int nBins, float advanceDt, float& outPos01) noexcept
    {
        for (int b = 0; b < nBins; ++b) outBins[b] = 0.f;
        outPos01 = clamp01 (pos01_);
        if (! hasStore() || outBins == nullptr || nBins <= 0) return false;
        const int nf = store_->numFrames();

        const float freeze = clamp01 (p_.stretch);
        float scanRate = clamp01 (p_.scan) * 2.0f;
        if (store_->naturalSec > 1e-4f) scanRate /= store_->naturalSec;
        if (scanRate < 1e-4f)
        {
            pos01_ = clamp01 (p_.start);
        }
        else if (freeze < 0.999f && advanceDt > 0.f)
        {
            pos01_ += scanRate * (1.f - freeze) * advanceDt * dir_;
            if (p_.loopMode == 3)
            { while (pos01_ > 1.f || pos01_ < 0.f) { if (pos01_ > 1.f) { pos01_ = 2.f - pos01_; dir_ = -dir_; } else { pos01_ = -pos01_; dir_ = -dir_; } } }
            else if (p_.loopMode == 0) pos01_ = clamp01 (pos01_);
            else pos01_ -= std::floor (pos01_);
        }
        outPos01 = clamp01 (pos01_);

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

        // mirror renderBlockAdd's CPU governor: thin BEFORE sculpt (display uses no shared budget /
        // no unison, so just the QUALITY ceiling) — keeps the message-thread viz cost bounded too.
        int active = geode::kMinActive + (int) (clamp01 (p_.quality)
                        * (float) (geode::kMaxActive - geode::kMinActive));
        active = std::min (active, nP);
        if (active < nP) keepLoudest (nP, active);          // (no shared budget for display)

        applySculpt (nP);                                   // SHAPE/FORMANT/TILT/CUT/SIEVE

        // rasterise partials → log-freq bins (peak-hold)
        const float lnLo = std::log (geode::kDispRMin), lnHi = std::log (geode::kDispRMax);
        for (int j = 0; j < nP; ++j)
        {
            const float a = wr_.amp[(size_t) j];
            const float r = wr_.ratio[(size_t) j];
            if (a <= 1e-6f || r <= 0.f) continue;
            const float rr = r < geode::kDispRMin ? geode::kDispRMin : (r > geode::kDispRMax ? geode::kDispRMax : r);
            int b = (int) ((std::log (rr) - lnLo) / (lnHi - lnLo) * (float) (nBins - 1) + 0.5f);
            if (b < 0) b = 0; else if (b >= nBins) b = nBins - 1;
            if (a > outBins[b]) outBins[b] = a;
        }

        const float ref = 0.36f;
        for (int b = 0; b < nBins; ++b) { const float v = outBins[b] / ref; outBins[b] = v > 1.5f ? 1.5f : v; }
        if (nBins > 2)
        {
            float prev = outBins[0];
            for (int b = 1; b < nBins - 1; ++b)
            { const float cur = outBins[b]; outBins[b] = 0.25f * prev + 0.5f * cur + 0.25f * outBins[b + 1]; prev = cur; }
        }
        return true;
    }

private:
    static inline float clamp01 (float x) noexcept { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

    // cheap, bounded ~tanh soft-clip (period-preserving distortion; no per-sample tanh cost)
    static inline float softClip (float x) noexcept
    {
        if (x < -3.f) return -1.f;
        if (x >  3.f) return  1.f;
        return x * (27.f + x * x) / (27.f + 9.f * x * x);
    }

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

    // SHAPE target harmonic weight W(n) = the amplitude of harmonic n for each target waveform.
    // Research-derived Fourier recipes (all bounded ~[0,1.1]); n = harmonic index ≥ 1. Adding a target
    // = one case here + one entry in the GEODE_SHAPE_TARGET StringArray + one menu label.
    static inline float shapeWeight (int target, int n) noexcept
    {
        const float fn = (float) n;
        switch (target)
        {
            case 0:  return (n == 1) ? 1.f : 0.f;                                   // Sine  — fundamental only
            case 1:  return (n & 1) ? 1.f / fn : 0.f;                               // Square — odd 1/n
            case 2:  return 1.f / fn;                                               // Saw    — all 1/n
            case 3:  return (n & 1) ? 1.f / (fn * fn) : 0.f;                        // Triangle — odd 1/n²
            case 4:  return std::fabs (std::sin (fn * geode::kPi * 0.28f)) / fn;    // Pulse  — |sin(nπd)|/n, d≈0.28
            case 5:  return (n & 1) ? std::pow (fn, -1.5f) : 0.f;                   // Hollow — odd 1/n^1.5 (clarinet)
            case 6:  switch (n) { case 1: return 1.f;  case 2: return 0.8f; case 3: return 0.6f;   // Organ drawbar
                                  case 4: return 0.5f; case 5: return 0.4f; case 6: return 0.3f;
                                  case 8: return 0.25f; default: return 0.f; }
            case 7:  return (n == 1) ? 0.5f : ((n & 1) ? 0.f : (2.f / geode::kPi) / (fn * fn - 1.f)); // Half-wave
            case 8:  { const float a = fn - 3.f, b = fn - 9.f;                      // Vowel "ah" (formant bumps @ n≈3,9)
                       return std::exp (-a * a / 4.5f) + 0.7f * std::exp (-b * b / 8.f); }
            case 9:  return std::pow (fn, -0.6f);                                   // Bright — 1/n^0.6 (supersaw-ish)
            case 10: return std::pow (fn, 0.3f) * std::exp (-fn / 12.f) * (n == 1 ? 0.4f : 1.f);   // Metal — clang/tine
            default: return 1.f / fn;
        }
    }

    // sculpt the working partial bank in place. AMPLITUDE-domain, except SHAPE may glide OVERTONE
    // ratios onto exact harmonics (it tunes them — the fundamental at ratio 1 never moves, so the
    // played pitch is fixed). Order: SHAPE → FORMANT → TILT → CUT → SIEVE. All identity at neutral.
    void applySculpt (int nP) noexcept
    {
        // ── SHAPE — morph the sample toward a synth waveform (sine / square / saw). ──────────────
        // A real sample's partials sit at ARBITRARY ratios: inharmonic overtones + a sub-fundamental
        // peak. A synth wave is a pure HARMONIC series, so SHAPE:
        //   (a) GLIDES each partial onto its nearest integer harmonic — the fundamental (ratio≈1)
        //       snaps to exactly 1, so the played PITCH never moves. This TUNES the overtones onto the
        //       harmonic grid; it does not detune the note (the old code left them inharmonic → "spray").
        //   (b) FADES OUT sub-fundamental content (ratio < 0.75) — a synth wave has none. (The old code
        //       rounded a 0.5× sub to harmonic 1 and BOOSTED it → the octave-down "rumble".)
        //   (c) blends each amplitude toward the Chebyshev target weight W(n): saw = 1/n (all),
        //       square = 1/n (odd only), sine = fundamental only.
        // At SHAPE=1 the bank is a clean saw/square/sine at the note pitch.
        const float shape = std::pow (clamp01 (p_.shape), 0.72f);   // amplified: reaches fuller morph earlier
        if (shape > 1e-3f && nP > 0)
        {
            float ref = 1e-9f;
            for (int j = 0; j < nP; ++j) ref = std::max (ref, wr_.amp[(size_t) j]);

            // Multiple messy partials can round to the SAME harmonic; if they all took the target
            // weight they'd SUM and overpower the fundamental (a saw whose 2nd harmonic is louder
            // than its root). So elect ONE partial per harmonic (the loudest) to carry the target;
            // the rest fade out. Result = exactly one partial per harmonic at W(n) → a clean wave.
            constexpr int kMaxH = 64;
            int   winner [kMaxH + 1];
            float winAmp [kMaxH + 1];
            for (int h = 0; h <= kMaxH; ++h) { winner[h] = -1; winAmp[h] = -1.f; }
            for (int j = 0; j < nP; ++j)
            {
                const float a = wr_.amp[(size_t) j];
                if (a <= 0.f) continue;
                const float r = wr_.ratio[(size_t) j];
                if (r < 0.75f) continue;                                 // sub handled in the pass below
                int nH = (int) (r + 0.5f); if (nH > kMaxH) nH = kMaxH;
                if (a > winAmp[nH]) { winAmp[nH] = a; winner[nH] = j; }
            }
            for (int j = 0; j < nP; ++j)
            {
                if (wr_.amp[(size_t) j] <= 0.f) continue;         // skip thinned slots (don't revive → CPU)
                const float r = wr_.ratio[(size_t) j];
                if (r <= 0.f) continue;
                if (r < 0.75f) { wr_.amp[(size_t) j] *= (1.f - shape); continue; }   // sub-fundamental → fade (kills rumble)
                const int nH  = (int) (r + 0.5f);                 // nearest integer harmonic (r≥0.75 ⇒ nH≥1)
                const int nHc = nH > kMaxH ? kMaxH : nH;
                wr_.ratio[(size_t) j] = r + ((float) nH - r) * shape;   // glide onto the harmonic (fund stays 1 → pitch fixed)
                const float w = (winner[nHc] == j) ? shapeWeight (p_.shapeTarget, nH) : 0.f;  // non-elected → fade
                const float target = ref * w;
                wr_.amp[(size_t) j] = wr_.amp[(size_t) j] * (1.f - shape) + target * shape;
            }
        }

        // ── FORMANT — pitch-preserving envelope shift (true-envelope method) ──
        // Move the spectral ENVELOPE, never the partial frequencies (research: P(k)=A(k·f)/A(k)).
        // Envelope is a Gaussian log-frequency KERNEL SMOOTHER (band-limited interpolation through the
        // peaks) → it can't track individual partials; gain clamp keeps it from nulling the fundamental
        // or getting harsh. FKEEP off = no formant processing.
        const float fShift = (p_.formant - 0.5f) * 2.f;   // -1..+1
        if (std::fabs (fShift) > 1e-3f && p_.formantKeep && nP > 1)
        {
            static thread_local std::vector<float> snapR, snapA;
            snapR.assign (wr_.ratio.begin(), wr_.ratio.begin() + nP);
            snapA.assign (wr_.amp.begin(),   wr_.amp.begin()   + nP);
            const float shiftMul = std::pow (2.f, fShift);   // ±1 octave envelope stretch
            for (int j = 0; j < nP; ++j)
            {
                if (wr_.amp[(size_t) j] <= 0.f) continue;    // skip thinned slots → FORMANT is O(active²), not O(96²)
                const float r = wr_.ratio[(size_t) j];
                if (r <= 0.f) continue;
                const float eHere  = smoothEnv (r,            snapR.data(), snapA.data(), nP);
                const float eThere = smoothEnv (r / shiftMul, snapR.data(), snapA.data(), nP);
                if (eHere > 1e-6f)
                {
                    float gain = eThere / eHere;
                    if (gain < 0.25f) gain = 0.25f; else if (gain > 4.f) gain = 4.f;
                    wr_.amp[(size_t) j] *= gain;
                }
            }
        }

        // ── TILT — bipolar spectral tilt about ratio 1.0 (bright/dark). Amplified: ±3.2 exponent so
        // it goes from fully dark to screaming bright well before the extremes. ──
        const float tilt = (p_.tilt - 0.5f) * 2.f;   // -1..+1
        if (std::fabs (tilt) > 1e-3f)
            for (int j = 0; j < nP; ++j)
            {
                if (wr_.amp[(size_t) j] <= 0.f) continue;
                const float r = std::max (0.05f, wr_.ratio[(size_t) j]);
                wr_.amp[(size_t) j] *= std::pow (r, tilt * 3.2f);
            }

        // ── CUT — spectral filter, LP (remove highs) or HP (remove lows), ~24 dB/oct ──
        // 1.0 = fully open; turning DOWN filters harder. The cutoff sweeps EXPONENTIALLY across the whole
        // knob (like a real filter freq) so it's audible everywhere — not crammed into the bottom 15%.
        if (p_.cut < 0.999f)
        {
            const float knob = clamp01 (p_.cut);
            if (p_.cutMode == 0) // LP
            {
                const float cutR = 0.6f * std::pow (140.f, knob);   // knob 0→0.6× · 0.5→7× · 1→84× (open)
                for (int j = 0; j < nP; ++j)
                    if (wr_.amp[(size_t) j] > 0.f && wr_.ratio[(size_t) j] > cutR)
                    {
                        const float over = wr_.ratio[(size_t) j] / cutR;
                        wr_.amp[(size_t) j] /= (over * over * over * over);   // ~24 dB/oct
                    }
            }
            else // HP
            {
                const float cutR = 0.5f * std::pow (90.f, 1.f - knob);   // knob 1→0.5× (open) · 0.5→4.7× · 0→45×
                for (int j = 0; j < nP; ++j)
                    if (wr_.amp[(size_t) j] > 0.f && wr_.ratio[(size_t) j] < cutR)
                    {
                        const float under = cutR / std::max (0.05f, wr_.ratio[(size_t) j]);
                        wr_.amp[(size_t) j] /= (under * under * under * under);
                    }
            }
        }

        // ── SIEVE — spectral gate: drop partials below a rising threshold (the lossy hero). Amplified:
        // sqrt curve so it bites HARD early (knob 0.5 already strips to ~70% of peak = very lossy;
        // knob 1 leaves only the loudest partial or two = near-sine "data gone"). ──
        const float sieve = clamp01 (p_.sieve);
        if (sieve > 1e-3f)
        {
            float mx = 1e-9f;
            for (int j = 0; j < nP; ++j) mx = std::max (mx, wr_.amp[(size_t) j]);
            const float thr = mx * std::sqrt (sieve) * 0.97f;
            for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] < thr) wr_.amp[(size_t) j] = 0.f;
        }
    }

    // smooth spectral envelope sampled at `ratio` — Gaussian kernel over the (snapshot) partial bank.
    // A band-limited interpolation through the peaks: it passes through the partials without tracking
    // any single one (research: true-envelope property). Reads a SNAPSHOT so the caller can mutate amps.
    static float smoothEnv (float ratio, const float* rr, const float* aa, int nP) noexcept
    {
        if (ratio < 1e-4f) ratio = 1e-4f;
        const float lr = std::log (ratio);
        const float bw = 0.5f;   // log-freq kernel half-width (~±0.7 oct)
        float num = 0.f, den = 0.f;
        for (int j = 0; j < nP; ++j)
        {
            const float rj = rr[j];
            if (rj <= 0.f || aa[j] <= 0.f) continue;   // skip thinned slots → kernel loop is O(active)
            const float d = (std::log (rj) - lr) / bw;
            const float wgt = std::exp (-d * d);
            num += wgt * aa[j];
            den += wgt;
        }
        return den > 1e-9f ? num / den : 0.f;
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

    // ── state ──
    static constexpr int kLUT = 4096;
    const GeodeFrameStore* store_ = nullptr;
    GeodeParams p_;
    GeodeFrame  wr_;                          // working (sculpted) partial bank
    std::vector<float> phase_;                // per-partial phase [0,1)
    std::vector<float> ampZ_;                 // per-partial previous-block effective gain (declick ramp)
    double rate_ = 48000.0, playedHz_ = 261.6256, pitchMul_ = 1.0;
    float  pos01_ = 0.f, prevPos_ = -1.f, dir_ = 1.f;
    float  makeup_ = 1.f;
    float  crushHoldL_ = 0.f, crushHoldR_ = 0.f;   // CRUSH sample-and-hold state
    int    crushCnt_ = 0;
    std::uint32_t rng_ = 0x9E3779B9u;
    int*   budgetUsed_ = nullptr;
    int    budgetCap_  = 0;
    int    unisonDiv_  = 1;   // constant-cost unison divisor = ceil(sqrt(unison count))
};

} // namespace tw
