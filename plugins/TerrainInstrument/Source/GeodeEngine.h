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
//    LOW/HIGH TWO coexisting spectral cuts: Low = HP, High = LP (~24 dB/oct each) [amp]  (rs2-cut: the back-row SPECTRAL_LO/HI knobs)
//    SHAPE    morph→sine/square/saw: tune overtones to harmonics [amp+tune] (Chebyshev W(n))
//    FORMANT  true-envelope shift — moves envelope, not pitch  [amp]  (Röbel; peak interpolant, thins AFTER the shift)
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
    constexpr int   kMinActive   = 4;     // floor of the QUALITY range — low QUALITY = skeletal lo-fi trash
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
    // rs2-cut — the single CUT knob (GEODE_CUT + GEODE_CUT_MODE, LP-or-HP) is RETIRED: those two params stay
    // registered (state / mod-dest numbering) but nothing reads them. The BACK ROW's Low/High are the cuts now.
    float lo      = 0.f;    // 0..1 HIGH-PASS corner, 0 = off (exact identity) (APVTS: SYN_OSC_x_SPECTRAL_LO)
    float hi      = 1.f;    // 0..1 LOW-PASS corner,  1 = off (exact identity) (APVTS: SYN_OSC_x_SPECTRAL_HI)
    float shape   = 0.f;    // 0..1 morph toward target waveform       (APVTS: GEODE_DISTILL)
    float drive   = 0.f;    // 0..1 spectral distortion (post-synth)   (APVTS: GEODE_HAZE)
    // ── page 2 (Sculpt & Fidelity) ──
    float quality = 0.80f;  // 0..1 spectral bitrate: partial count + amp quantize (APVTS: GEODE_QUALITY)
    float formant = 0.5f;   // 0..1 bipolar formant/envelope shift     (APVTS: GEODE_FORMANT)
    float tilt    = 0.5f;   // 0..1 bipolar spectral tilt (0.5 = flat) (APVTS: GEODE_TILT)
    float crush   = 0.f;    // 0..1 bitcrush + rate decimate (post-synth) (APVTS: GEODE_SILT)
    float smear   = 0.f;    // 0..1 MELT — temporal amp smear across frames (APVTS: GEODE_FRACTURE, repurposed)
    // ── choices / toggles ──
    int   shapeTarget = 2;  // 0..10 sine…metal (see shapeWeight)       (APVTS: GEODE_SHAPE_TARGET)
    int   driveMode   = 0;  // 0 Saturate 1 Bloom 2 Glint 3 Moire 4 Foldback 5 Ember (APVTS: GEODE_DRIVE_MODE)
    int   sieveMode   = 0;  // 0 Floor 1 Sparse 2 Cloak 3 Flicker 4 Rake 5 Parity   (APVTS: GEODE_SIEVE_MODE)
    // ── SAMPLER-PARITY region/loop/fades (rs7) — SHARED with the Sample engine's params (they are
    // idle while the osc runs Resynth, so Resynth reads the SAME SYN_OSC_*_SAMPLE_* ids: one region
    // UI, one preset story). START (GEODE_POSITION) becomes the read offset WITHIN the region.
    float regionStart = 0.f;   // 0..1                                (APVTS: SAMPLE_START)
    float regionEnd   = 1.f;   // 0..1                                (APVTS: SAMPLE_END)
    float loopStart   = 0.f;   // 0..1, clamped into the region       (APVTS: SAMPLE_LOOP_START)
    float loopEnd     = 1.f;   // 0..1, clamped into the region       (APVTS: SAMPLE_LOOP_END)
    float fadeIn      = 0.f;   // fraction of region                  (APVTS: SAMPLE_FADE_IN)
    float fadeOut     = 0.f;   // fraction of region                  (APVTS: SAMPLE_FADE_OUT)
    float fadeInCurve = 0.5f;  // 0..1, 0.5 = linear (exp-bias)       (APVTS: SAMPLE_FADEIN_CURVE)
    float fadeOutCurve= 0.5f;  //                                     (APVTS: SAMPLE_FADEOUT_CURVE)
    int   loopMode    = 1;  // 0 One-Shot 1 Fwd 2 Reverse 3 Ping-Pong 4 Tailed(≈Fwd) (APVTS: SAMPLE_LOOP_MODE)
    bool  formantKeep = true;                                        // (APVTS: GEODE_FKEEP)

    bool operator== (const GeodeParams& o) const noexcept
    {
        return start==o.start && stretch==o.stretch && scan==o.scan && sieve==o.sieve
            && lo==o.lo && hi==o.hi && shape==o.shape && drive==o.drive && quality==o.quality
            && formant==o.formant && tilt==o.tilt && crush==o.crush && smear==o.smear
            && shapeTarget==o.shapeTarget
            && driveMode==o.driveMode && sieveMode==o.sieveMode
            && regionStart==o.regionStart && regionEnd==o.regionEnd
            && loopStart==o.loopStart && loopEnd==o.loopEnd
            && fadeIn==o.fadeIn && fadeOut==o.fadeOut
            && fadeInCurve==o.fadeInCurve && fadeOutCurve==o.fadeOutCurve
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
        scatMul_.fill (1.f);
        childKey_.fill (-1);   // fb598
        wr_.ratio.fill (0.f); wr_.amp.fill (0.f);
        pos01_ = 0.f; prevPos_ = -1.f; driveSm_ = 0.f;   // fb204
        makeup_ = 1.0f;
        crushHoldL_ = crushHoldR_ = 0.f; crushCnt_ = 0;
        emberLpL_ = emberLpR_ = 0.f;   // fb598
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
        uniScatCents_ = (nn > 1) ? 2.2f : 0.f;   // per-sibling partial decorrelation (hm2)
    }

    void setFrameStore (const GeodeFrameStore* s) noexcept { store_ = s; }
    bool hasStore() const noexcept { return store_ != nullptr && store_->valid && store_->numFrames() > 0; }
    float readPos01() const noexcept { return pos01_; }   // current read-head [0,1] (tests / UI follower)
    float driveSm_ = 0.f;   // fb204 — block-ramped drive (postProcess)

    void setParams (const GeodeParams& p) noexcept
    {
        // user scrub → snap the read-head to the START offset (within the region); SCAN owns it otherwise
        if (prevPos_ >= 0.f && std::fabs (p.start - prevPos_) > 1e-4f)
        {
            const float rs = clamp01 (p.regionStart);
            const float re = std::max (rs + 0.01f, clamp01 (p.regionEnd));
            const float tgt204 = (p.loopMode == 2) ? re - clamp01 (p.start) * (re - rs)   // mirror noteOn's REVERSE map
                                                   : rs + clamp01 (p.start) * (re - rs);
            pos01_ += (tgt204 - pos01_) * 0.35f;   // fb204 — modulated START scrubs (fast pole), never teleports (block-rate click)
            if (std::fabs (tgt204 - pos01_) < 1e-4f) pos01_ = tgt204;
        }
        prevPos_ = p.start;
        p_ = p;
    }

    void noteOn (double playedHz, std::uint32_t seed) noexcept
    {
        playedHz_ = playedHz > 0.0 ? playedHz : 261.6256;
        rng_ = seed ? seed : 0x9E3779B9u;
        for (auto& ph : phase_) ph = 0.f;
        std::fill (ampZ_.begin(), ampZ_.end(), 0.f);   // start silent → first block ramps up (clean attack)
        // per-sibling per-partial static frequency scatter (hm2): decorrelates the unison stack
        // so shared-bank motion (smear/flicker/drift) is ENSEMBLE, not group vibrato
        for (int j = 0; j < geode::kMaxPartials; ++j)
            scatMul_[(size_t) j] = (uniScatCents_ > 0.f)
                ? std::pow (2.f, uniScatCents_ * ((float) ((rng_ ^ (std::uint32_t) ((std::uint32_t) j * 2654435761u)) >> 8)
                                                   * (1.f / 16777216.f) - 0.5f) * 2.f * (1.f / 1200.f))
                : 1.f;
        const float rs = clamp01 (p_.regionStart);
        const float re = std::max (rs + 0.01f, clamp01 (p_.regionEnd));
        if (p_.loopMode == 2) { pos01_ = re - clamp01 (p_.start) * (re - rs); dir_ = -1.f; }   // REVERSE plays end→start
        else                  { pos01_ = rs + clamp01 (p_.start) * (re - rs); dir_ =  1.f; }
        entered_ = false;
        prevPos_ = p_.start;
        crushHoldL_ = crushHoldR_ = 0.f; crushCnt_ = 0;
    }

    void setPitchRatio (double r) noexcept { pitchMul_ = r > 0.0 ? r : 1.0; }
    // live retune (Coarse/oct/semi/cents moved mid-note): phase accumulators make this
    // C0-continuous — same law as HarmonicEngine::setPlayedHz (hm4)
    void setPlayedHz (double hz) noexcept { if (hz > 8.0) playedHz_ = hz; }

    // ═══ per-block render, split for CONSTANT-COST UNISON (rs7): the unison ANCHOR runs
    // prepareBank() once (head advance + interp + smear + governor + sculpt + bitrate + children —
    // identical for every unison sibling, so computing it N× was pure waste); siblings adoptBank()
    // the result and only pay for their own detuned sine banks. renderBlockAdd() = the classic
    // single-engine path (prepare + render), used by tests/tools and any non-unison caller. ═══
    bool prepareBank (int n) noexcept
    {
        if (! hasStore() || n <= 0) return false;
        const int nf = store_->numFrames();

        advanceHead ((float) n / (float) rate_);

        interpFrame (clamp01 (pos01_), nf);
        int nP = wr_.nPartials;
        flickerTick_ += (std::uint32_t) n;               // FLICKER epoch clock in SAMPLES (time-based —
                                                         // a block counter re-rolled faster at small buffers)

        applySmear (clamp01 (pos01_), nf);   // MELT — temporal amp smear: frames bleed together (pad-ify)

        // ── CPU GOVERNOR: decide the active-partial count BEFORE sculpting (rs2). Thinning first means every
        // per-partial sculpt op only touches partials we will actually render (rs2-formant: FORMANT, now
        // O(n log n), thins AFTER its shift — see below). QUALITY sets the ceiling (its floor is
        // the lo-fi zone — see applyBitrate); CONSTANT-COST UNISON divides it; the shared processor
        // budget is the final hard ceiling (thins gracefully).
        // rs-shapefix: `quota` = the partial count this voice is ALLOWED (QUALITY → unison → shared
        // budget), NOT clamped to the source count — SHAPE may spawn up to it. `active` = the thin.
        int quota = geode::kMinActive + (int) (clamp01 (p_.quality)
                        * (float) (geode::kMaxActive - geode::kMinActive));
        if (unisonDiv_ > 1) quota = std::max (std::min (quota, geode::kUnisonFloor), quota / unisonDiv_);
        if (budgetUsed_ != nullptr && budgetCap_ > 0)
        {
            const int room = budgetCap_ - *budgetUsed_;
            if (room < quota) quota = std::max (0, room);
        }
        const int active = std::min (quota, nP);
        // rs2-formant: with FORMANT active the thin moves AFTER the envelope shift (inside applySculpt, as
        // SHAPE's post-thin): the shifted humps need LIVE carriers, and thinning on pre-shift loudness killed
        // exactly the partials the shift was about to lift (measured: quality 0.3 on a 3-hump vowel, the hump
        // due at harmonic 32 had no partial to land on). FORMANT=0.5 keeps the shipped pre-thin bit-identical.
#ifdef GEODE_FORMANT_PRETHIN
        const bool formantOn = false;   // cert seam: the SHIPPED thin-before-shift order → F8 must go red
#else
        const bool formantOn = std::fabs (p_.formant - 0.5f) > 5e-4f && p_.formantKeep && nP > 1;
#endif
        if (active < nP && ! formantOn) keepLoudest (nP, active);   // zero all but the loudest `active` (slots preserved)

        nP = applySculpt (nP, quota);   // SHAPE/FORMANT/TILT/LOW/HIGH/SIEVE — SHAPE may SPAWN (≤ quota), returns the new count

        applyBitrate (nP);  // QUALITY low = spectral bit-crush: quantize amps to few levels (lossy encode)

        // BLOOM/GLINT/MOIRE spectral drive — children partials appended within the shared budget
        int alive = 0;
        for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] > 0.f) ++alive;
        int childRoom = 16;
        if (budgetUsed_ != nullptr && budgetCap_ > 0)
            childRoom = std::max (0, std::min (16, budgetCap_ - *budgetUsed_ - std::max (active, alive)));   // fb597 — at SHAPE=0 alive ≤ active (SIEVE / bitrate / silent slots zero partials), and 922aec6 subtracted ACTIVE: subtracting `alive` gave DRIVE children MORE room under budget pressure = different audio with the knob at 0 (measured 16 → 20 partials). max() is bit-identical at 0; spawn can only push alive ABOVE active.
        nP = applyDriveChildren (nP, childRoom);

        regionGain_ = fadeGain (pos01_);             // sampler-parity FADE IN/OUT (positional gain)
        preparedActive_ = 0;
        for (int j = 0; j < nP; ++j)
            if (wr_.amp[(size_t) j] > 1e-6f && wr_.ratio[(size_t) j] > 0.f) ++preparedActive_;   // renderable only
        return true;
    }

    // Copy the anchor's prepared bank + head state — this sibling then renders its OWN detuned
    // sine bank from the shared spectrum (phases/seeds stay per-sibling = real unison width).
    void adoptBank (const GeodeEngine& src) noexcept
    {
        wr_ = src.wr_;
        pos01_ = src.pos01_; dir_ = src.dir_; entered_ = src.entered_;
        regionGain_ = src.regionGain_; preparedActive_ = src.preparedActive_;
    }

    // Anchor priority under budget pressure: the anchor reserves its partial count while the
    // (quieter, detuned) siblings render — a saturated budget thins SIBLINGS first, never the core.
    void reserveBudget() noexcept { if (budgetUsed_ != nullptr) { *budgetUsed_ += preparedActive_; reserved_ = preparedActive_; } }
    void releaseBudget() noexcept { if (budgetUsed_ != nullptr && reserved_ > 0) { *budgetUsed_ -= reserved_; reserved_ = 0; } }
    int  preparedActive() const noexcept { return preparedActive_; }
    // fb596 test seams (the HarmonicEngine::partialsForTesting precedent): the cert's G6 counts live
    // slots whose ratio jumps between blocks — the property the SHAPE home-slot map exists to hold.
    const float* wrRatioForTesting() const noexcept { return wr_.ratio.data(); }
    const float* ampZForTesting()    const noexcept { return ampZ_.data(); }

    void renderBankAdd (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0) return;
        // shared budget saturated → this bank contributes nothing NEW this block, but anything it
        // was sounding last block RAMPS out through the declick loop below (hard-zeroing ampZ_ was
        // an audible click — cleanup sweep). Once fully silent, saturated blocks take the fast skip.
        const bool sat = (budgetUsed_ != nullptr && budgetCap_ > 0 && *budgetUsed_ >= budgetCap_);
        if (sat)
        {
            bool silent = true;
            for (int j = 0; j < geode::kMaxPartials; ++j)
                if (ampZ_[(size_t) j] > 1e-7f) { silent = false; break; }
            if (silent) return;
        }
        const int nP = wr_.nPartials;
        // ── oscillator bank — per-partial amplitude RAMP (declick) ──────────────────────
        // Partial FREQUENCY = analysisRatio × playedHz (× pitchMul from key/oct/semi/cents).
        // No bedrock, no envelope repitch — the sample plays at the KEY pitch, always tuned.
        const float baseHz = (float) (playedHz_ * pitchMul_);
        const float gain   = makeup_ * regionGain_;
        int voiced = 0;
#ifndef GEODE_NO_DECLICK
        const float invN = 1.f / (float) n;
        for (int j = 0; j < geode::kMaxPartials; ++j)
        {
            const float prevGa = ampZ_[(size_t) j];
            const float rj  = wr_.ratio[(size_t) j];
            const float hz  = rj * baseHz * scatMul_[(size_t) j];
            const bool  aud = (rj > 0.f && hz > 0.f && hz < (float) rate_ * 0.48f);   // renderable this block
            const float tgtGa = (! sat && j < nP && aud) ? wr_.amp[(size_t) j] * gain : 0.f;
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
        if (sat) return;
        for (int j = 0; j < nP; ++j)
        {
            const float a = wr_.amp[(size_t) j];
            if (a <= 1e-6f || wr_.ratio[(size_t) j] <= 0.f) continue;
            const float hz = wr_.ratio[(size_t) j] * baseHz;
            if (hz <= 0.f || hz >= (float) rate_ * 0.48f) continue;
            const float inc = hz / (float) rate_;
            float ph = phase_[(size_t) j];
            const float ga = a * gain;
            for (int i = 0; i < n; ++i) { const float s = ga * sineAt (ph); L[i] += s; R[i] += s; ph += inc; if (ph >= 1.f) ph -= 1.f; }
            phase_[(size_t) j] = ph;
            ++voiced;
        }
#endif
        if (budgetUsed_ != nullptr) *budgetUsed_ += voiced;
    }

    void renderBlockAdd (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0) return;
        if (! prepareBank (n)) return;
        renderBankAdd (L, R, n);
    }

    // ── POST-SYNTH degrade — applied ONCE per osc (after the unison sum) on this osc's block.
    // DRIVE = soft-clip distortion (a periodic input keeps its period → adds harmonics, no detune).
    // CRUSH = bit-depth quantize + sample-rate decimate (the lossy "old-data" degrade).
    void postProcess (float* L, float* R, int n) noexcept
    {
        if (L == nullptr || R == nullptr || n <= 0) return;
        const float driveT = clamp01 (p_.drive);
        // fb204 — DRIVE ramps across the block (start/step lands exactly on target): the
        // block-pushed mod stepped the shaper gain (confirmed zipper on Saturate/Fold/Ember).
        const float drvStep204 = (driveT - driveSm_) / (float) n;
        const float drive = driveT > driveSm_ ? driveT : driveSm_;   // gate on the louder end
        const float crush = clamp01 (p_.crush);

        if (drive > 1e-3f)
        {
            switch (p_.driveMode)
            {
                case 1: case 2: case 3:   // BLOOM / GLINT / MOIRE — spectral-domain (children partials
                    break;                // grown in renderBlockAdd); no audio-domain shaping here.
                case 4:                   // FOLDBACK — sine-fold: peaks REFLECT, spectrum keeps
                {                         // reorganizing as you push (metallic / West-Coast shimmer)
                    for (int i = 0; i < n; ++i)
                    {
                        driveSm_ += drvStep204;   // fb204 — ramped drive, coefs per sample (cheap)
                        const float ang  = 0.5f * geode::kPi * (1.f + driveSm_ * 6.f);
                        const float wmix = driveSm_ * 2.5f > 1.f ? 1.f : driveSm_ * 2.5f;
                        const float wl = std::sin (L[i] * ang), wr2 = std::sin (R[i] * ang);
                        L[i] += (wl - L[i]) * wmix;
                        R[i] += (wr2 - R[i]) * wmix;
                    }
                    break;
                }
                case 5:                   // EMBER — asymmetric tube/tape bias: even harmonics, warm
                {
                    for (int i = 0; i < n; ++i)
                    {
                        driveSm_ += drvStep204;   // fb204 — ramped
                        const float g  = 1.f + driveSm_ * 7.f;
                        const float b  = driveSm_ * 0.4f;
                        const float mk = 1.f / (1.f + driveSm_ * 0.8f);
                        const float dc = softClip (b);
#ifdef GEODE_EMBER_WARMTH   /* fb598: OPT-IN. Max: "Saturate sounds great, Foldback sounds great, and the one under Foldback" — Ember stays EXACTLY as shipped. It measures 1.8-2.4 dB from Saturate on real samples (same softClip, two gains); the warmth one-pole that would separate them is here under a seam for the day he wants it. */
                        // fb598 — the WARMTH that makes Ember its own mode: a one-pole low-pass AFTER the shaper
                        // whose corner falls 12 kHz → 2.5 kHz with the knob. It darkens the distortion PRODUCTS
                        // (which the spectral CUT, applied before synthesis, never touches). Without it Ember
                        // measured 1.8-2.4 dB from Saturate on analysed samples = the same shaper at a lower gain.
                        const float k = (12000.f - driveSm_ * 9500.f) * (float) (2.0 * geode::kPi / rate_);
                        emberLpL_ += ((softClip (L[i] * g + b) - dc) * mk - emberLpL_) * (k > 1.f ? 1.f : k);
                        emberLpR_ += ((softClip (R[i] * g + b) - dc) * mk - emberLpR_) * (k > 1.f ? 1.f : k);
                        L[i] = emberLpL_; R[i] = emberLpR_;
#else
                        L[i] = (softClip (L[i] * g + b) - dc) * mk;
                        R[i] = (softClip (R[i] * g + b) - dc) * mk;
#endif
                    }
                    break;
                }
                default:                  // SATURATE — the amplified rs4 soft-clip: slams the shaper
                {                         // (g→37×) with gentle makeup so 100% is ear-blasting.
                    for (int i = 0; i < n; ++i)
                    {
                        driveSm_ += drvStep204;   // fb204 — ramped
                        const float g  = 1.f + driveSm_ * 36.f;
                        const float mk = 1.f / (1.f + driveSm_ * 0.8f);
                        L[i] = softClip (L[i] * g) * mk;
                        R[i] = softClip (R[i] * g) * mk;
                    }
                    break;
                }
            }
        }

        driveSm_ = driveT;   // fb204 — settle exactly (spectral modes 1-3 stay block-rate by design)

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

        float scanRate = clamp01 (p_.scan) * 2.0f;
        if (store_->naturalSec > 1e-4f) scanRate /= store_->naturalSec;
        if (scanRate < 1e-4f)
        {   // held — park at the START offset within the region (sampler-parity)
            const float rs = clamp01 (p_.regionStart);
            const float re = std::max (rs + 0.01f, clamp01 (p_.regionEnd));
            pos01_ = rs + clamp01 (p_.start) * (re - rs);
        }
        else
        {
            advanceHead (advanceDt);   // same region/loop-bracket logic as the audio path
        }
        outPos01 = clamp01 (pos01_);
        return computeFrameEnvelope (pos01_, outBins, nBins, false);
    }

    // ═══ DISPLAY-ONLY: rasterize ONE frame's spectrum into log-freq magnitude bins. ═══
    // Powers the live display tick above AND the editor's spectral-image bake: neutral=true gives
    // the RAW analyzed frame (the "ghost" = the original data), neutral=false runs the FULL current
    // sculpt chain (smear → quality → sculpt → bitrate → drive children) = the "survivors" the knobs
    // leave alive. Touches wr_ only — never audio-voice state, never the read-head.
    bool computeFrameEnvelope (float pos01, float* outBins, int nBins, bool neutral) noexcept
    {
        if (! hasStore() || outBins == nullptr || nBins <= 0) return false;
        for (int b = 0; b < nBins; ++b) outBins[b] = 0.f;
        const int nf = store_->numFrames();
        interpFrame (clamp01 (pos01), nf);
        int nP = wr_.nPartials;
        if (! neutral)
        {
            applySmear (clamp01 (pos01), nf);
            const int quota = geode::kMinActive + (int) (clamp01 (p_.quality)
                            * (float) (geode::kMaxActive - geode::kMinActive));
            const int active = std::min (quota, nP);
            if (active < nP) keepLoudest (nP, active);      // (no shared budget / unison for display)
            nP = applySculpt (nP, quota);                   // rs-shapefix: spawned harmonics show in the viz for free
            applyBitrate (nP);
            nP = applyDriveChildren (nP, 16);
        }
        rasterizeBins (nP, outBins, nBins);
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

    // ── advance the read-head — PLAY-THROUGH within the SAMPLER REGION (rs7). SCAN is play speed
    // (0.5 = natural, referenced to the sample's real length); STRETCH slows/freezes the advance.
    // The head lives in [regionStart, regionEnd]; loop modes wrap it inside the loop brackets:
    // One-Shot clamps at region end · Forward/Tailed wrap loopEnd→loopStart · Reverse runs backwards
    // (dir −1 from noteOn) wrapping loopStart→loopEnd · Ping-Pong reflects between the brackets.
    void advanceHead (float dt) noexcept
    {
        const float rs = clamp01 (p_.regionStart);
        const float re = std::max (rs + 0.01f, clamp01 (p_.regionEnd));
        float ls = clamp01 (p_.loopStart), le = clamp01 (p_.loopEnd);
        ls = ls < rs ? rs : (ls > re ? re : ls);
        le = le < ls + 0.005f ? std::min (re, ls + 0.005f) : (le > re ? re : le);
        const float freeze = clamp01 (p_.stretch);
        float scanRate = clamp01 (p_.scan) * 2.0f;
        if (store_->naturalSec > 1e-4f) scanRate /= store_->naturalSec;
        if (scanRate > 1e-4f && freeze < 0.999f && dt > 0.f)
        {
            if (pos01_ >= ls && pos01_ <= le) entered_ = true;   // pre-advance entry test (fast heads)
            pos01_ += scanRate * (1.f - freeze) * dt * dir_;
            if (pos01_ >= ls && pos01_ <= le) entered_ = true;
            const float len = le - ls;
            // Loop wraps/reflections only engage once the head has ENTERED the brackets — a START
            // placed outside them free-runs in until then (sampler behavior; no teleports — sweep).
            switch (p_.loopMode)
            {
                case 0:   // one-shot — clamp inside the region
                    break;
                case 2:   // reverse loop — wrap loopStart → loopEnd
                    if (entered_ && len > 1e-5f)
                    { while (pos01_ < ls) pos01_ += len; while (pos01_ > le) pos01_ -= len; }
                    break;
                case 3:   // ping-pong — reflect between the brackets
                    if (entered_)
                    {
                        int guard = 4;
                        while (guard-- > 0 && (pos01_ > le || pos01_ < ls))
                        {
                            if (pos01_ > le) { pos01_ = 2.f * le - pos01_; dir_ = -dir_; }
                            else             { pos01_ = 2.f * ls - pos01_; dir_ = -dir_; }
                        }
                    }
                    break;
                default:  // 1 forward / 4 tailed — wrap loopEnd → loopStart
                    if (entered_ && len > 1e-5f && pos01_ > le) pos01_ = ls + std::fmod (pos01_ - le, len);
                    break;
            }
        }
        pos01_ = pos01_ < rs ? rs : (pos01_ > re ? re : pos01_);   // hard region clamp
    }

    // sampler-parity FADE IN/OUT as a positional gain over the region (per-block scalar — the
    // per-partial declick ramps smooth it). Curves use the house exponential-bias rule.
    static float fadeCurve (float t, float c) noexcept
    {
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        const float P = (c - 0.5f) * 8.f;
        if (std::fabs (P) < 1e-3f) return t;
        return (std::exp (P * t) - 1.f) / (std::exp (P) - 1.f);
    }
    float fadeGain (float pos) const noexcept
    {
        const float rs = clamp01 (p_.regionStart);
        const float re = std::max (rs + 0.01f, clamp01 (p_.regionEnd));
        const float len = re - rs;
        float g = 1.f;
        const float fin = clamp01 (p_.fadeIn), fout = clamp01 (p_.fadeOut);
        if (fin  > 1e-4f) { const float t = (pos - rs) / (fin  * len); if (t < 1.f) g *= fadeCurve (t, p_.fadeInCurve); }
        if (fout > 1e-4f) { const float t = (re - pos) / (fout * len); if (t < 1.f) g *= fadeCurve (t, p_.fadeOutCurve); }
        return g;
    }

    // interpolate the two frames bracketing pos01 into the working partial bank (shared by the
    // audio render, the display tick, and the spectral-image bake).
    void interpFrame (float pos01, int nf) noexcept
    {
        const float fp = pos01 * (float) (nf - 1);
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
    }

    // ── MELT — temporal smear: blur each partial's AMPLITUDE across neighbouring analysis frames
    // (the tracker keeps slot j the same partial across frames, so per-slot averaging is coherent).
    // Attacks dissolve into a sustained wash — "turn any sample into a pad". Amp-only → pitch-safe.
    void applySmear (float pos01, int nf) noexcept
    {
        const float sm = clamp01 (p_.smear);
        if (sm <= 1e-3f || nf < 2) return;
        const float spread = sm * sm * 0.35f;                       // up to ±35% of the sample
        static constexpr float off[4] = { -1.f, -0.45f, 0.45f, 1.f };
        constexpr float wTap = 0.16f, wCenter = 1.f - 4.f * wTap;   // gaussian-ish 5-tap kernel
        const int nP = wr_.nPartials;
        float acc[geode::kMaxPartials];                       // stack — never allocates on the audio thread
        std::fill (acc, acc + nP, 0.f);
        for (int t = 0; t < 4; ++t)
        {
            float tp = pos01 + off[t] * spread;
            tp = tp < 0.f ? 0.f : (tp > 1.f ? 1.f : tp);
            const float fp = tp * (float) (nf - 1);
            const int   fa = (int) fp, fb = std::min (nf - 1, fa + 1);
            const float fr = fp - (float) fa;
            const GeodeFrame& A = store_->frames[(size_t) fa];
            const GeodeFrame& B = store_->frames[(size_t) fb];
            for (int j = 0; j < nP; ++j)
                acc[j] += wTap * (A.amp[(size_t) j] + (B.amp[(size_t) j] - A.amp[(size_t) j]) * fr);
        }
        for (int j = 0; j < nP; ++j)
            if (wr_.ratio[(size_t) j] > 0.f)                  // dead slots stay dead — a smeared amp on a
                wr_.amp[(size_t) j] = wCenter * wr_.amp[(size_t) j] + acc[j];   // ratio-0 slot is unrenderable
                                                              // but eats the QUALITY quota (cleanup sweep)
    }

    // ── QUALITY = spectral BITRATE. Above ~0.7 it's transparent; below, partial amplitudes are
    // quantized to ever-fewer levels (2..48) — the spectral bit-crush half of the lossy encode
    // (the other half is the shrinking partial count). Quiet partials round to ZERO = data gone.
    void applyBitrate (int nP) noexcept
    {
        const float q = clamp01 (p_.quality);
        if (q >= 0.7f || nP < 1) return;
        float mx = 1e-9f;
        for (int j = 0; j < nP; ++j) mx = std::max (mx, wr_.amp[(size_t) j]);
        const float t = q / 0.7f;
        const float L = 2.f + t * t * 46.f;                          // 2..48 amplitude levels
        for (int j = 0; j < nP; ++j)
            if (wr_.amp[(size_t) j] > 0.f)
                wr_.amp[(size_t) j] = std::round (wr_.amp[(size_t) j] / mx * L) / L * mx;
    }

    // ── DRIVE spectral modes (BLOOM/GLINT/MOIRE) — "waveshaping in the additive domain": grow NEW
    // child partials from the sculpted parents instead of clipping the audio. Band-limited by
    // construction (children are explicit sines, gated by the same Nyquist check in the bank loop).
    // Children never sit below 0.75× the fundamental → the played pitch always dominates.
    // Returns the new partial count (children appended in slots nP..). Foldback/Ember/Saturate are
    // audio-domain and live in postProcess.
    int applyDriveChildren (int nP, int room) noexcept
    {
        const float drive = clamp01 (p_.drive);
        const int   mode  = p_.driveMode;
        if (drive <= 1e-3f || mode < 1 || mode > 3 || room <= 0 || nP < 1)
        {
            for (int j = 0; j < geode::kMaxPartials; ++j) childKey_[(size_t) j] = -1;   // no children this block → nothing is owned
            return nP;
        }
        float ref = 1e-9f;
        for (int j = 0; j < nP; ++j) ref = std::max (ref, wr_.amp[(size_t) j]);
        // fb598 — CHILDREN TAKE FREE SLOTS, NOT "THE SLOTS AFTER nP". The tracker leaves a real sample's
        // bank 96 slots deep (kMaxPartials is also the analysis depth), and the SHAPE spawn's home slots run
        // to 95 too — so `total = nP` started AT the cap and add() rejected every child: Bloom/Glint/Moire
        // were silent on every dense sample and on every SHAPE > 0 (measured 0 children, dF 0.0 dB).
        // A free slot = one the source/spawn is not sounding this block (amp 0, or above nP) whose voice is
        // SILENT (ampZ_ ≈ 0 → the child ramps in from nothing, no frequency hop) — or a slot this same
        // child (parent slot × kind) owned last block, so a sustained child keeps its phase and slot.
        int total = nP, hi = nP;                                                    // total: the cert's append seam only
        const int keyBase = mode * geode::kMaxPartials * 4;                         // a mode switch never inherits the other mode's slot
        bool owned[geode::kMaxPartials];
        for (int j = 0; j < geode::kMaxPartials; ++j) owned[j] = false;
        for (int j = nP; j < geode::kMaxPartials; ++j) wr_.amp[(size_t) j] = 0.f;   // last block's children above the source
        auto isFree = [&] (int j) noexcept
        {
            if (owned[j]) return false;
            if (j < nP && wr_.amp[(size_t) j] > 0.f) return false;               // the source / spawn sounds here
            return true;
        };
        auto add = [&] (int key, float r, float a) noexcept
        {
            if (room <= 0 || r < 0.75f || a <= 1e-6f) return;
            int s = -1;
#ifdef GEODE_DRIVE_MUT_APPEND
            if (total < geode::kMaxPartials) s = total; else return;                // cert seam: the shipped "slot after nP" placement → starved on a full bank
            wr_.ratio[(size_t) s] = r; wr_.amp[(size_t) s] = a; owned[s] = true; childKey_[(size_t) s] = key; ++total; --room; if (s + 1 > hi) hi = s + 1; return;
#endif
            for (int j = 0; j < geode::kMaxPartials; ++j)                          // (1) my own slot from last block
#ifndef GEODE_DRIVE_MUT_NOKEY
                if (childKey_[(size_t) j] == key && isFree (j)) { s = j; break; }
#else
                (void) key;   // cert seam: no sticky slot → a child re-lands wherever the top-down scan says (hops)
#endif
            if (s < 0)
                for (int j = geode::kMaxPartials - 1; j >= 0; --j)                 // (2) a SILENT free slot, top down
                    if (isFree (j) && ampZ_[(size_t) j] <= 1e-7f && childKey_[(size_t) j] < 0) { s = j; break; }
            if (s < 0)
                for (int j = geode::kMaxPartials - 1; j >= 0; --j)                 // (3) any free slot (a dying voice — rare)
                    if (isFree (j)) { s = j; break; }
            if (s < 0) return;
            wr_.ratio[(size_t) s] = r; wr_.amp[(size_t) s] = a; owned[s] = true; childKey_[(size_t) s] = key; ++total; --room;
            if (s + 1 > hi) hi = s + 1;
        };
        // the loudest-N parents — a threshold from an nth_element over the audible amps (slot order is
        // NOT loudness order: the shipped "first 8 over 0.06·ref" took whatever sat low in the bank)
        auto loudestThr = [&] (int keep, float minR) noexcept
        {
            float tmp[geode::kMaxPartials]; int n = 0;
            for (int j = 0; j < nP; ++j)
                if (wr_.amp[(size_t) j] > 1e-6f && wr_.ratio[(size_t) j] >= minR) tmp[n++] = wr_.amp[(size_t) j];
            if (n <= keep) return 0.f;
            std::nth_element (tmp, tmp + (n - keep), tmp + n);
            return tmp[n - keep];
        };
        if (mode == 1)          // BLOOM — Chebyshev harmonic growth on the loudest parents (2nd + 3rd harmonic of each)
        {
            const float thr = loudestThr (8, 0.f);
            int used = 0;
            for (int j = 0; j < nP && used < 8; ++j)
            {
                const float a = wr_.amp[(size_t) j], r = wr_.ratio[(size_t) j];
                if (a <= 1e-6f || a < thr || r <= 0.f) continue;
                add (keyBase + j * 4 + 0, r * 2.f, a * drive * 0.90f);
                add (keyBase + j * 4 + 1, r * 3.f, a * drive * 0.50f);
                ++used;
            }
        }
        else if (mode == 2)     // GLINT — HF exciter: the loudest parents each throw a 4th and 6th harmonic (two octaves
        {                       // up and a twelfth over that — new sizzle where the sample had little) and everything above the
                                // spectral centroid gets a presence lift. (was: octave copies of partials ≥ ratio 3 — on a real
                                // sample whose f0 the analyser read an octave or two low that is EVERY partial, so the "lift" was
                                // a volume knob and the octave copies sat above Nyquist = silent)
            float num = 0.f, den = 0.f;
            for (int j = 0; j < nP; ++j) { const float a = wr_.amp[(size_t) j]; if (a > 1e-6f && wr_.ratio[(size_t) j] > 0.f) { num += a * wr_.ratio[(size_t) j]; den += a; } }
            const float rc = den > 1e-9f ? num / den : 3.f;
            const float thr = loudestThr (8, 0.f);
            int used = 0;
            for (int j = 0; j < nP; ++j)
            {
                const float a = wr_.amp[(size_t) j], r = wr_.ratio[(size_t) j];
                if (a <= 1e-6f || r <= 0.f) continue;
                if (r >= rc) wr_.amp[(size_t) j] *= (1.f + drive * 0.6f);        // presence lift on the uppers
                if (used < 8 && a >= thr) { add (keyBase + j * 4 + 0, r * 4.f, a * drive * 0.80f); add (keyBase + j * 4 + 1, r * 6.f, a * drive * 0.40f); ++used; }
            }
        }
        else                    // MOIRE — a detuned GHOST LATTICE: each of the loudest overtones gets a copy above and
        {                       // below it (±(2..8)% = 35..135 cents at full knob) → beating, clangorous interference.
                                // The fundamental never gets a ghost (r ≥ 1.5) → the played pitch stays unambiguous.
                                // (was: f_i±f_j intermod of parents over 0.3·ref — on harmonic material those land ON
                                // existing harmonics, and a 1/n² source has one such parent = no pairs = nothing)
            const float thr = loudestThr (8, 1.5f);
            const float eps = 0.02f + 0.06f * drive;
            int used = 0;
            for (int j = 0; j < nP && used < 8; ++j)
            {
                const float a = wr_.amp[(size_t) j], r = wr_.ratio[(size_t) j];
                if (a <= 1e-6f || r < 1.5f || a < thr) continue;
                add (keyBase + j * 4 + 0, r * (1.f + eps), a * drive * 0.65f);
                add (keyBase + j * 4 + 1, r * (1.f - eps), a * drive * 0.65f);
                ++used;
            }
        }
        for (int j = 0; j < geode::kMaxPartials; ++j)
            if (! owned[j]) childKey_[(size_t) j] = -1;                            // a slot nobody claimed this block is nobody's
        wr_.nPartials = hi;
        return hi;
    }

    // rasterize the working bank → log-freq magnitude bins (peak-hold, normalized, 3-tap smoothed)
    void rasterizeBins (int nP, float* outBins, int nBins) noexcept
    {
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
            case 4:  return std::fabs (std::sin (fn * geode::kPi * 0.25f)) / fn;    // Pulse  — |sin(nπd)|/n, d=0.25 (rs-shapefix: was 0.28 = 3.7 dB from Saw; 25 % pulse = 11.9 dB from Saw, every 4th harmonic notched)
            case 5:  return (n & 1) ? std::pow (fn, -1.5f) : 0.f;                   // Hollow — odd 1/n^1.5 (clarinet)
            case 6:  switch (n) { case 1: return 1.f;  case 2: return 0.8f; case 3: return 0.6f;   // Organ drawbar
                                  case 4: return 0.5f; case 5: return 0.4f; case 6: return 0.3f;
                                  case 8: return 0.25f; default: return 0.f; }
            case 7:  return (n == 1) ? 0.5f : ((n & 1) ? 0.f : (2.f / geode::kPi) / (fn * fn - 1.f)); // Half-wave
            case 8:  { const float a = fn - 3.f, b = fn - 9.f;                      // Vowel "ah" (formant bumps @ n≈3,9)
                       return std::exp (-a * a / 4.5f) + 0.7f * std::exp (-b * b / 8.f); }
            case 9:  return std::pow (fn, -0.6f);                                   // Bright — 1/n^0.6 (supersaw-ish)
            case 10: return std::pow (fn, 0.3f) * std::exp (-fn / 12.f) * (n == 1 ? 0.4f : 1.f);   // Metal — clang/tine (its peak trim lives AFTER the level match below — a recipe scale would be undone by equal-RMS)
            default: return 1.f / fn;
        }
    }

    // sculpt the working partial bank in place. AMPLITUDE-domain, except SHAPE may glide OVERTONE
    // ratios onto exact harmonics (it tunes them — the fundamental at ratio 1 never moves, so the
    // played pitch is fixed). Order: SHAPE → FORMANT → TILT → LOW(HP) → HIGH(LP) → SIEVE. All identity at neutral.
    int applySculpt (int nP, int quota) noexcept
    {
#ifdef GEODE_MUT_SHAPE0
        if (nP > 0) wr_.amp[0] *= 1.0001f;   // cert seam: touches the bank OUTSIDE the SHAPE gate → G7 must go red
#endif
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
            float ref = 1e-9f, srcE = 0.f;   // loudest / energy of the SOURCE bank (post-thin) — energy feeds the level match below
            for (int j = 0; j < nP; ++j)
            {
                const float a = wr_.amp[(size_t) j];
                if (a > 0.f) { ref = std::max (ref, a); srcE += a * a; }
            }

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

            if (quota > 0)   // fb597 — a saturated shared budget (quota 0) must not spawn-then-thin-to-nothing (fb598: the brace pairs OUTSIDE the -D seams so every mutant still compiles)
            {
#ifndef GEODE_NO_SPAWN
            // ── rs-shapefix (1): SPAWN the target's MISSING harmonics — "the sample BECOMES the wave".
            // winner[n] == -1 is the "no source partial rounds to n" map. Each such harmonic is born
            // at ratio n EXACTLY with amp = ref·W(n)·shape (the blend above with a zero source term),
            // in a slot ABOVE the source bank. HOME slot = kMaxPartials-kMaxH-1+n, so harmonic n keeps
            // the SAME slot (same phase_/ampZ_ → phase-continuous) block after block while the source
            // set churns; a home slot the source occupies falls back to the lowest free slot. Births
            // ramp from ampZ_=0 through the declick loop → click-free by construction. Cost is bounded
            // by `quota` (QUALITY → unison → shared budget) in the post-thin below.
            for (int j = nP; j < geode::kMaxPartials; ++j) wr_.amp[(size_t) j] = 0.f;   // stale children/spawns of earlier blocks
            int hi = nP;
            const float nyqR   = 0.48f * (float) rate_ / (float) (playedHz_ * pitchMul_);   // the bank loop's own Nyquist gate
            const float floorA = ref * 1e-3f;                                             // -60 dB re loudest: not worth a sine
            int pend[kMaxH]; int nPend = 0;
            for (int n = 1; n <= kMaxH; ++n)
            {
                if (winner[n] >= 0) continue;
                if ((float) n >= nyqR) break;
                const float a = ref * shapeWeight (p_.shapeTarget, n) * shape;
                if (a < floorA) continue;
 #ifdef GEODE_SPAWN_APPEND_ONLY
                const int s = -1;
 #else
                const int s = geode::kMaxPartials - kMaxH - 1 + n;
 #endif
                if (s >= nP) { wr_.ratio[(size_t) s] = (float) n; wr_.amp[(size_t) s] = a; if (s + 1 > hi) hi = s + 1; }
                else if (nPend < kMaxH) pend[nPend++] = n;
            }
            for (int k = 0, cur = nP; k < nPend; ++k)
            {
                while (cur < geode::kMaxPartials && wr_.amp[(size_t) cur] > 0.f) ++cur;
                if (cur >= geode::kMaxPartials) break;
                const int n = pend[k];
                wr_.ratio[(size_t) cur] = (float) n;
                wr_.amp[(size_t) cur]   = ref * shapeWeight (p_.shapeTarget, n) * shape;
                if (cur + 1 > hi) hi = cur + 1;
            }
            nP = hi; wr_.nPartials = hi;
#endif

#ifndef GEODE_NO_LEVELMATCH
            // ── rs-shapefix (3): LEVEL — SHAPE changes timbre, not loudness. Match the shaped bank's
            // RMS to the source bank's, blended by `shape` so SHAPE=0 stays bit-identical. Removes the
            // 17 dB spread between targets (Metal +9.6 dB, Half -7.4 dB measured on the default store).
            // Peaks: a zero-phase Metal has crest 4.7 vs the saw's 2.0 → its peak sits +7.5 dB over the
            // source's (1.51 on the default store); every other kept target ≤ +4.6 dB.
            // (Measured alternative, rejected: g = min(sqrt(srcE/shE), srcSum/shSum) is peak-safe (≤ 1.08 on
            // the default store) but lands a Saw 5-8 dB BELOW the sample it came from on sparse sources.)
            {
                float shE = 0.f;
                for (int j = 0; j < nP; ++j) { const float a = wr_.amp[(size_t) j]; if (a > 0.f) shE += a * a; }
                if (shE > 1e-12f && srcE > 0.f)
                {
                    float g = std::sqrt (srcE / shE);                         // equal RMS: measured 0.1 dB spread across targets on every source
                    if (p_.shapeTarget == 10) g *= 1.f - 0.3f * shape;        // fb596/597 — METAL's zero-phase crest is 4.7 vs a saw's 2.0: at equal RMS its PEAK sat +7.5 dB over the source's (1.50 abs). ×0.7 at full knob holds it near 1.05 (Vowel 1.08) for 3 dB of Metal and nothing else — and the trim FADES IN with the knob so Metal does not step −3 dB the instant SHAPE leaves 0. (A recipe scale would be cancelled by the match.)
                    // fb597 — NO second `shape` blend on g: shE is already the CURRENT-knob bank energy, so sqrt(srcE/shE) is the exact equal-RMS gain at every knob position (the blend left a mid-knob RMS bulge: Half −1.3 / Metal +2.0 dB at 0.5). The enclosing `if (shape > 1e-3f)` keeps SHAPE=0 bit-identical.
                    for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] > 0.f) wr_.amp[(size_t) j] *= g;
                }
            }
#endif

#ifndef GEODE_NO_POSTTHIN
            // ── rs-shapefix (2): the governor thinned on SOURCE loudness before it knew the target;
            // re-thin on SHAPED loudness so the quota holds the target's loudest harmonics (an odd-only
            // target no longer wastes half of it on evens the sculpt just killed) — and spawns above
            // the quota are cut here, so SHAPE can never cost more than QUALITY allows.
            {
                int alive = 0;
                for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] > 0.f) ++alive;
                if (alive > quota) keepLoudest (nP, quota);
            }
#endif
            }   // if (quota > 0)
        }

        // ── FORMANT — pitch-preserving envelope shift (true-envelope method) ──
        // Move the spectral ENVELOPE, never the partial frequencies (research: P(k)=A(k·f)/A(k)).
        // rs2-formant: the envelope is the line THROUGH THE PARTIAL PEAKS — linear in (ln r, ln a) between
        // neighbouring live partials (Röbel's true envelope on a peak-picked bank IS the peak interpolant),
        // with a −12 dB/oct resonance skirt beyond the first and the last partial. The shipped estimator was
        // a NORMALISED Gaussian mean ±0.7 oct wide (smoothEnv, bw 0.5): it reduced every envelope to its
        // trend, so E(r/s)/E(r) collapsed to the trend ratio — a constant s^k on power-law sources (C: −11 dB
        // at x=0 with dF 0.9 = a VOLUME knob) and a +10 dB tilt on a 3-hump vowel whose humps never moved.
        // Gain = E(r/s)/E(r) clamped ±30 dB (the old ±12 dB capped a 24 dB hump), then an equal-RMS level
        // match (as SHAPE's) so the knob changes timbre, never loudness. FKEEP off = no formant processing.
        // Cost: one insertion sort of ≤ kMaxActive partials + 2 binary searches per partial — cheaper than
        // the shipped O(active²) exp() kernel.
        const float fShift = (p_.formant - 0.5f) * 2.f;   // -1..+1
        if (std::fabs (fShift) > 1e-3f && p_.formantKeep && nP > 1)
        {
#ifdef GEODE_FORMANT_OLD_ENV
            float snapR[geode::kMaxPartials], snapA[geode::kMaxPartials];   // cert seam: the SHIPPED estimator
            std::copy (wr_.ratio.begin(), wr_.ratio.begin() + nP, snapR);
            std::copy (wr_.amp.begin(),   wr_.amp.begin()   + nP, snapA);
            const float shiftMul = std::pow (2.f, fShift);
            for (int j = 0; j < nP; ++j)
            {
                if (wr_.amp[(size_t) j] <= 0.f) continue;
                const float r = wr_.ratio[(size_t) j];
                if (r <= 0.f) continue;
                const float eHere  = smoothEnv (r,            snapR, snapA, nP);
                const float eThere = smoothEnv (r / shiftMul, snapR, snapA, nP);
                if (eHere > 1e-6f)
                {
                    float gain = eThere / eHere;
                    if (gain < 0.25f) gain = 0.25f; else if (gain > 4.f) gain = 4.f;
                    wr_.amp[(size_t) j] *= gain;
                }
            }
#else
            float ex[geode::kMaxPartials], ey[geode::kMaxPartials];   // (ln r, ln a) of the live bank, sorted by r — stack, no RT alloc
            int ne = 0;
            for (int j = 0; j < nP; ++j)
            {
                const float a = wr_.amp[(size_t) j], r = wr_.ratio[(size_t) j];
                if (a <= 0.f || r <= 0.f) continue;
                const float x = std::log (r), y = std::log (a);
                int k = ne++;                                               // insertion sort: the bank is mostly ascending already
                while (k > 0 && ex[k - 1] > x) { ex[k] = ex[k - 1]; ey[k] = ey[k - 1]; --k; }
                ex[k] = x; ey[k] = y;
            }
            if (ne >= 2)
            {
                const float shiftLn = fShift * 0.6931472f;   // ln(2^fShift): ±1 octave envelope shift
 #ifdef GEODE_FORMANT_CLAMP12
                constexpr float kClampLn = 1.3862944f;       // cert seam: the shipped ±12 dB clamp
 #else
                constexpr float kClampLn = 3.4538776f;       // ±30 dB
 #endif
                float e0 = 0.f, e1 = 0.f;
                for (int j = 0; j < nP; ++j)
                {
                    const float a = wr_.amp[(size_t) j], r = wr_.ratio[(size_t) j];
                    if (a <= 0.f || r <= 0.f) continue;
                    const float x = std::log (r);
                    float g = trueEnv (x - shiftLn, ex, ey, ne) - trueEnv (x, ex, ey, ne);   // ln gain = ln E(r/s) − ln E(r)
                    if (g < -kClampLn) g = -kClampLn; else if (g > kClampLn) g = kClampLn;
                    const float an = a * std::exp (g);
                    e0 += a * a; e1 += an * an;
                    wr_.amp[(size_t) j] = an;
 #ifdef GEODE_MUT_FORMANT_PITCH
                    wr_.ratio[(size_t) j] = r * std::exp (shiftLn);   // cert seam: shift the FREQUENCIES → F3 must go red
 #endif
                }
 #ifndef GEODE_NO_FORMANT_LEVEL
                if (e1 > 1e-12f && e0 > 0.f)
                {
                    const float lm = std::sqrt (e0 / e1);                 // equal RMS: timbre, not loudness
                    for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] > 0.f) wr_.amp[(size_t) j] *= lm;
                }
 #endif
            }
#endif
            // the governor's thin, deferred from prepareBank: keep the loudest `quota` of the SHIFTED bank
            {
                int alive = 0;
                for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] > 0.f) ++alive;
                if (alive > quota) keepLoudest (nP, quota);
            }
        }
#ifdef GEODE_MUT_FORMANT0
        if (nP > 0) wr_.amp[0] *= 1.0001f;   // cert seam: touches the bank OUTSIDE the FORMANT gate → F5 must go red
#endif

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

        // ── LOW / HIGH — TWO coexisting spectral cuts, ~24 dB/oct each (rs2-cut). Max: "this IS spectral so they
        // need a low and a high pass filter ... these two need to coexist and sculpt the sound, and we can't do
        // that if we're right-clicking and moving back and forth between the two." The back-row LOW knob
        // (SYN_OSC_x_SPECTRAL_LO, 0 = off) is the HIGH-PASS; the HIGH knob (SYN_OSC_x_SPECTRAL_HI, 1 = off) is
        // the LOW-PASS. SAME DSP as the retired single CUT knob: the corner sweeps EXPONENTIALLY across the knob
        // (like a real filter freq) and a partial past the corner is divided by (distance ratio)^4. High at x is
        // bit-identical to the old Cut LP at x; Low at x to the old Cut HP at 1-x. Each end is an EXACT identity
        // (no partial touched), so Low=0 / High=1 is a no-op and the two stages compose into a BAND.
        if (p_.lo > 0.001f) // LOW = high-pass: remove the bottom
        {
#ifdef GEODE_MUT_LO_CURVE
            const float cutR = 0.25f * std::pow (2.f, 11.f * clamp01 (p_.lo));   // cert seam: the WAVETABLE's curve, not the Cut's → the "same DSP" bar must go red
#else
            const float cutR = 0.5f * std::pow (90.f, clamp01 (p_.lo));          // lo 0→0.5× (off) · 0.5→4.7× · 1→45×  (== the old Cut HP at knob 1-lo)
#endif
            for (int j = 0; j < nP; ++j)
                if (wr_.amp[(size_t) j] > 0.f && wr_.ratio[(size_t) j] < cutR)
                {
                    const float under = cutR / std::max (0.05f, wr_.ratio[(size_t) j]);
                    wr_.amp[(size_t) j] /= (under * under * under * under);   // ~24 dB/oct
                }
        }
#ifdef GEODE_MUT_EXCLUSIVE
        else                // cert seam: LP only when HP is idle = the old one-mode knob → the BAND bar must go red
#endif
        if (p_.hi < 0.999f) // HIGH = low-pass: remove the top
        {
            const float cutR = 0.6f * std::pow (96.f, clamp01 (p_.hi));    // hi 0→0.6× · 0.5→5.9× · 1→58× — tops just above real content, no dead zone (hm2)
            for (int j = 0; j < nP; ++j)
                if (wr_.amp[(size_t) j] > 0.f && wr_.ratio[(size_t) j] > cutR)
                {
                    const float over = wr_.ratio[(size_t) j] / cutR;
                    wr_.amp[(size_t) j] /= (over * over * over * over);   // ~24 dB/oct
                }
        }

        // ── SIEVE — the lossy data-removal hero, now with 6 flavors (right-click the knob).
        // All amp-only (kill/attenuate partials) → pitch-safe. Identity at sieve=0 for every mode.
        const float sieve = clamp01 (p_.sieve);
        if (sieve > 1e-3f && nP > 0)
        {
            float mx = 1e-9f;
            for (int j = 0; j < nP; ++j) mx = std::max (mx, wr_.amp[(size_t) j]);
            switch (p_.sieveMode)
            {
                case 1: // SPARSE — adaptive keep-loudest-N: the surviving set changes frame-to-frame
                {       //          (glassy, underwater, low-bitrate shimmer)
                    int na = 0;
                    for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] > 1e-6f) ++na;
                    const int keep = std::max (1, (int) ((1.f - sieve) * (float) na + 0.5f));
                    if (keep < na) keepLoudest (nP, keep);
                    break;
                }
                case 2: // CLOAK — psychoacoustic masking: delete what louder neighbours hide first
                {       //          (the literal MP3-at-low-bitrate / lossy-codec sound)
                    float lr[geode::kMaxPartials];        // stack (fully overwritten below) — no RT alloc
                    for (int j = 0; j < nP; ++j)
                        lr[(size_t) j] = std::log (std::max (0.05f, wr_.ratio[(size_t) j]));
                    const float t = sieve * 0.9f;                 // knob raises the masking threshold
                    constexpr float spread = 0.9f;                // triangular skirt ~1.3 oct wide
                    for (int i = 0; i < nP; ++i)
                    {
                        const float ai = wr_.amp[(size_t) i];
                        if (ai <= 1e-6f) continue;
                        float m = 0.f;
                        for (int j = 0; j < nP; ++j)
                        {
                            const float aj = wr_.amp[(size_t) j];
                            if (aj <= ai || j == i) continue;      // only LOUDER partials mask
                            const float d = std::fabs (lr[(size_t) i] - lr[(size_t) j]);
                            if (d < spread) m += aj * (1.f - d / spread);
                        }
                        if (ai < t * m) wr_.amp[(size_t) i] = 0.f;
                    }
                    break;
                }
                case 3: // FLICKER — probabilistic dropout re-rolled ~12×/s: packet-loss sparkle
                {       //          (loud partials survive longer; quiet ones flicker in and out)
                    const std::uint32_t epoch = flickerTick_ / (std::uint32_t) (rate_ > 0.0 ? rate_ * 0.085 : 4096.0);   // ~85ms per re-roll at any buffer size
                    for (int j = 0; j < nP; ++j)
                    {
                        const float a = wr_.amp[(size_t) j];
                        if (a <= 1e-6f) continue;
                        std::uint32_t h = ((std::uint32_t) j * 2654435761u) ^ (epoch * 40503u) ^ rng_;
                        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
                        const float u = (float) (h & 0xFFFFFF) / 16777215.f;
                        const float killP = sieve * (1.f - 0.8f * a / mx);
                        if (u < killP) wr_.amp[(size_t) j] = 0.f;
                    }
                    break;
                }
                case 4: // RAKE — every-Nth harmonic comb: hollow → metallic gaps as N rises (2..8)
                {
                    const int N = 2 + (int) (sieve * 6.99f);
                    for (int j = 0; j < nP; ++j)
                    {
                        if (wr_.amp[(size_t) j] <= 1e-6f) continue;
                        const int nH = std::max (1, (int) (wr_.ratio[(size_t) j] + 0.5f));
                        if ((nH - 1) % N != 0) wr_.amp[(size_t) j] = 0.f;   // fundamental always kept
                    }
                    break;
                }
                case 5: // PARITY — fade the EVEN harmonics: hollow square/clarinet woodiness
                {
                    for (int j = 0; j < nP; ++j)
                    {
                        if (wr_.amp[(size_t) j] <= 1e-6f) continue;
                        const int nH = std::max (1, (int) (wr_.ratio[(size_t) j] + 0.5f));
                        if ((nH & 1) == 0) wr_.amp[(size_t) j] *= (1.f - sieve);
                    }
                    break;
                }
                default: // FLOOR — the shipped rising threshold gate (sqrt curve = bites early)
                {
                    const float thr = mx * std::sqrt (sieve) * 0.97f;
                    for (int j = 0; j < nP; ++j) if (wr_.amp[(size_t) j] < thr) wr_.amp[(size_t) j] = 0.f;
                    break;
                }
            }
        }
        return nP;
    }

    // smooth spectral envelope sampled at `ratio` — Gaussian kernel over the (snapshot) partial bank.
    // A band-limited interpolation through the peaks: it passes through the partials without tracking
    // any single one (research: true-envelope property). Reads a SNAPSHOT so the caller can mutate amps.
    // rs2-formant: true envelope of the live bank at log-ratio x — linear in (ln r, ln a) between the two
    // neighbouring partials; beyond the first / last partial a −12 dB/oct resonance skirt (slope −2 per ln unit),
    // so shifting DOWN pulls the bank's top edge in (darker, "bigger") and shifting UP thins the fundamental
    // below the first hump (smaller) — both what a scaled vocal tract does, both audible on a hump-less source.
    static float trueEnv (float x, const float* ex, const float* ey, int ne) noexcept
    {
        constexpr float kSkirt = 2.f;   // −12 dB/oct: ×¼ per octave = −2·ln2 per ln2 of frequency
        if (x <= ex[0])      return ey[0]      - kSkirt * (ex[0] - x);
        if (x >= ex[ne - 1]) return ey[ne - 1] - kSkirt * (x - ex[ne - 1]);
        int lo = 0, hi = ne - 1;              // binary search: ex[lo] ≤ x < ex[hi]
        while (hi - lo > 1) { const int mid = (lo + hi) >> 1; if (ex[mid] <= x) lo = mid; else hi = mid; }
        const float dx = ex[hi] - ex[lo];
        const float t  = dx > 1e-9f ? (x - ex[lo]) / dx : 0.f;
        return ey[lo] + (ey[hi] - ey[lo]) * t;
    }

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
        float tmp[geode::kMaxPartials];                       // stack — never allocates on the audio thread
        std::copy (wr_.amp.begin(), wr_.amp.begin() + nP, tmp);
        std::nth_element (tmp, tmp + (nP - keep), tmp + nP);
        const float thr = tmp[nP - keep];
        // thr==0 ⇒ fewer than `keep` partials are alive (the tracker leaves zero-amp gap slots below
        // nPartials) — everything audible already fits, and counting gap slots against the quota was
        // muting real partials in high slots (cleanup sweep). Nothing to thin.
        if (thr <= 0.f) return;
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
    bool   entered_ = false;          // ping-pong/reverse: head has reached the loop brackets
    float  regionGain_ = 1.f;         // sampler-parity fade in/out gain at the current head pos
    int    preparedActive_ = 0;       // partials alive after prepareBank (unison budget reserve)
    int    reserved_ = 0;             // budget currently reserved by this (anchor) engine
    float  makeup_ = 1.f;
    float  crushHoldL_ = 0.f, crushHoldR_ = 0.f;   // CRUSH sample-and-hold state
    float  emberLpL_ = 0.f, emberLpR_ = 0.f;       // fb598 — EMBER post-shaper warmth one-pole state
    int    crushCnt_ = 0;
    std::uint32_t rng_ = 0x9E3779B9u;
    std::uint32_t flickerTick_ = 0;   // FLICKER sieve epoch clock (incremented per rendered block)
    int*   budgetUsed_ = nullptr;
    int    budgetCap_  = 0;
    int    unisonDiv_  = 1;   // constant-cost unison divisor = ceil(sqrt(unison count))
    float  uniScatCents_ = 0.f;                                    // hm2 — per-sibling decorrelation depth
    std::array<float, geode::kMaxPartials> scatMul_ { };           // hm2 — per-sibling static freq scatter
    std::array<int,   geode::kMaxPartials> childKey_ { };          // fb598 — DRIVE child (parent slot × 4 + kind) owning each slot, -1 = none
};

} // namespace tw
