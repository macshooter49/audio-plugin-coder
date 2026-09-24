// ══════════════════════════════════════════════════════════════════════════════════════════════
//  ChopStretch_test.cpp — CHOP ENGINE STRETCH / WARP CLICK HARNESS (offline, perceptual).
//
//  Max: "some of the chop engine's stretch modes have clicks and crackles — as many crossfades as
//  possible, like the Sample oscillator." This drives the SHIPPED tw::SamplerVoice (Source/
//  SamplerVoice.h) through every warp mode the chop offers — NONE (varispeed), BEATS (grain
//  looper), TONES (Signalsmith phase vocoder), TEXTURE (Signalsmith + scatter) — across stretch
//  ratios and every slice boundary the voice can hit: note-on mid-waveform, one-shot end, reverse,
//  a tiny slice, loop wraps (long + fast), and scan ping-pong (NONE path + the warp render cache).
//
//  SCENARIOS (1194): 4 modes × pad / vox / drum-loop × ratios 0.5 0.75 1 1.33 2 4 (NONE: 1) × slice
//  kinds × Attack/Chop-Fade {5/5 default, 0/0, 0/20 ms} × pitch {0, −7 st}, plus a VIBRATO set (60 c
//  @ 6 Hz — the warp engines take it as a per-block pitch step, the "ratio changes mid-note" case).
//
//  THE MEASUREMENT (the fb283 perceptual law: things the EAR hears, not sample-difference RMS).
//  Every source is BAND-LIMITED to < 3.5 kHz (255-tap Blackman FIR), so the source itself has no
//  energy above ~4.6 kHz. A click / crackle is broadband by nature, so:
//   [HF]    CLICK EVENTS — the output's > 7.5 kHz band (255-tap FIR highpass), 64-sample frames. An
//           event = a frame above −50 dB of the broadband RMS of its ±20 ms, ≥ 10 dB above the HF
//           floor of its own ±50 ms (transient, not a steady image floor), above −75 dBFS. Events
//           closer than 10 ms merge into one.
//   [D2]    HARD DISCONTINUITIES — |x[n] − 2x[n−1] + x[n−2]| above 1.5× the Bernstein bound of a
//           < 4.6 kHz signal at the local (±5 ms) peak. A band-limited read can't exceed it.
//   [FLUX]  SPECTRAL-FLUX SPIKES — positive flux of the > 6 kHz bins (STFT 512/128), frames above
//           8× the running median AND above 3 % of the frame's total magnitude.
//   [NOTCH] DROPOUTS — 1 ms RMS below 30 % of the local floor (median of 10 ms minima) on SUSTAINED
//           sources — the "gated tick" family (a fade-to-zero joint puts no energy above 4.6 kHz, so
//           the HF detector cannot see it; this does). TEXTURE's dropouts are by design (excluded).
//  `selftest` proves the detectors first: the clean sources read 0 everywhere, a planted −2 dB gain
//  step is caught by HF, a planted 4 ms dip by NOTCH.
//  CHARACTER — long-term average spectrum in 1/3-octave bands 63 Hz..4 kHz + spectral centroid +
//  RMS per scenario. `compare` reports the band-shape move before → after AND each build's FIDELITY:
//  the warped render's spectral shape vs the unstretched dry read of the same slice.
//  CS_DEBUG=1 prints every event's time; CS_DUMP=<file> writes the render (raw float32, mono L).
//  ⚠️ TONES / TEXTURE are not bit-reproducible run to run (Signalsmith seeds its phase randomiser from
//  std::random_device; Texture's scatter uses a time-seeded juce::Random) — expect ±1 event noise.
//
//  BUILD + RUN: Tests/chopstretch_gate.sh (builds this against the base commit AND the working tree,
//  runs both, prints the per-mode before → after table).
//    clang++ -std=c++17 -O2 -ObjC++ -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1
//      -DNDEBUG=1 -I <JUCE>/modules -I Tests/shim_ap -I Source -I <signalsmith-stretch>/include
//      -I <signalsmith-linear>/include Source/ChopStretch_test.cpp <JUCE>/modules/juce_core/juce_core.mm
//      <JUCE>/modules/juce_audio_basics/juce_audio_basics.mm -framework CoreFoundation -framework Accelerate
//      -framework IOKit -framework Cocoa -framework Security -o /tmp/chopstretch
//    /tmp/chopstretch selftest | run out.tsv [idFilter] | compare before.tsv after.tsv [strict]
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_basics/juce_audio_basics.h>
#include "SamplerVoice.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <complex>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <array>
namespace juce { extern const char* const juce_compilationDate = __DATE__; extern const char* const juce_compilationTime = __TIME__; }

static constexpr double SR  = 48000.0;
static constexpr int    BLK = 512;
static constexpr double PI  = 3.14159265358979323846;

// ── FIR design (windowed sinc, Blackman) ─────────────────────────────────────────────────────
static std::vector<double> firLowpass (double fc, int taps)
{
    std::vector<double> h ((size_t) taps); const int M = taps - 1; double s = 0;
    for (int n = 0; n < taps; ++n)
    {
        const double k = n - M / 2.0;
        const double sinc = (k == 0.0) ? 2.0 * fc / SR : std::sin (2 * PI * fc / SR * k) / (PI * k);
        const double w = 0.42 - 0.5 * std::cos (2 * PI * n / M) + 0.08 * std::cos (4 * PI * n / M);
        h[(size_t) n] = sinc * w; s += h[(size_t) n];
    }
    for (auto& v : h) v /= s;
    return h;
}
static std::vector<double> firHighpass (double fc, int taps)
{
    auto h = firLowpass (fc, taps);
    for (auto& v : h) v = -v;
    h[(size_t) (taps / 2)] += 1.0;
    return h;
}
static std::vector<float> convolveSame (const std::vector<float>& x, const std::vector<double>& h)
{
    const int N = (int) x.size(), T = (int) h.size(), D = T / 2;
    std::vector<float> y ((size_t) N, 0.f);
    for (int n = 0; n < N; ++n)
    {
        double acc = 0;
        const int kLo = std::max (0, n + D - (N - 1)), kHi = std::min (T - 1, n + D);
        for (int k = kLo; k <= kHi; ++k) acc += h[(size_t) k] * x[(size_t) (n + D - k)];
        y[(size_t) n] = (float) acc;
    }
    return y;
}

// ── FFT (radix-2) ─────────────────────────────────────────────────────────────────────────────
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * PI / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j)
        { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

// ── SOURCE MATERIALS (all band-limited < 3.5 kHz) ─────────────────────────────────────────────
struct Material { std::string name; std::vector<float> L, R; bool sustained; };
static uint32_t rngState = 0x1234567u;
static double urand() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (rngState & 0xFFFFFF) / 16777216.0; }

static Material makeMaterial (int which)
{
    const int N = (int) (SR * 4.0);
    std::vector<float> L ((size_t) N), R ((size_t) N);
    Material m;
    if (which == 0)          // PAD — 110 Hz saw-ish stack to 3 kHz, slow AM, stereo detune
    {
        m.name = "pad"; m.sustained = true;
        std::vector<double> ph (40); for (auto& p : ph) p = urand() * 2 * PI;
        for (int n = 0; n < N; ++n)
        {
            const double t = n / SR; double l = 0, r = 0;
            for (int k = 1; k <= 27; ++k) { const double a = 1.0 / k;
                l += a * std::sin (2 * PI * 110.0 * k * t + ph[(size_t) k]);
                r += a * std::sin (2 * PI * 110.4 * k * t + ph[(size_t) k] + 0.3); }
            const double am = 0.8 + 0.2 * std::sin (2 * PI * 0.3 * t);
            L[(size_t) n] = (float) (0.22 * am * l); R[(size_t) n] = (float) (0.22 * am * r);
        }
    }
    else if (which == 1)     // VOX — 196 Hz with 5.5 Hz vibrato, formant-weighted harmonics
    {
        m.name = "vox"; m.sustained = true;
        double phase = 0;
        std::vector<double> vph (17); for (auto& p : vph) p = urand() * 2 * PI;   // random partial phases: low crest, so a 4 ms RMS is steady
        for (int n = 0; n < N; ++n)
        {
            const double t = n / SR;
            const double f0 = 196.0 * std::pow (2.0, 0.35 / 12.0 * std::sin (2 * PI * 5.5 * t));
            phase += 2 * PI * f0 / SR; double v = 0;
            for (int k = 1; k <= 16; ++k) { const double f = f0 * k;
                const double g = std::exp (-std::pow ((f - 700) / 300, 2)) + 0.6 * std::exp (-std::pow ((f - 1200) / 350, 2))
                               + 0.3 * std::exp (-std::pow ((f - 2500) / 400, 2)) + 0.05;
                v += g * std::sin (k * phase + vph[(size_t) k]); }
            L[(size_t) n] = (float) (0.3 * v); R[(size_t) n] = (float) (0.29 * v);
        }
    }
    else                     // BEAT — 120 BPM kick / snare / hat pattern (band-limited by the FIR below)
    {
        m.name = "beat"; m.sustained = false;
        const int step = (int) (SR * 0.125);   // 16ths at 120 BPM
        for (int s = 0; s * step < N; ++s)
        {
            const int at = s * step;
            const bool kick = (s % 8 == 0) || (s % 8 == 5), snare = (s % 8 == 4), hat = true;
            for (int i = 0; i < (int) (SR * 0.3) && at + i < N; ++i)
            {
                const double t = i / SR; double v = 0;
                const double atk = std::min (1.0, t / 0.001);
                if (kick)  v += 0.7 * atk * std::exp (-t / 0.12) * std::sin (2 * PI * (50 * t + 70 * 0.03 * (1 - std::exp (-t / 0.03))));
                if (snare) v += 0.35 * atk * std::exp (-t / 0.07) * (2 * urand() - 1) + 0.2 * atk * std::exp (-t / 0.05) * std::sin (2 * PI * 190 * t);
                if (hat && i < (int) (SR * 0.05)) v += 0.08 * atk * std::exp (-t / 0.015) * (2 * urand() - 1);
                L[(size_t) (at + i)] += (float) v; R[(size_t) (at + i)] += (float) (0.95 * v);
            }
        }
    }
    const auto lp = firLowpass (3500.0, 255);
    m.L = convolveSame (L, lp); m.R = convolveSame (R, lp);
    return m;
}

// ── SCENARIOS ────────────────────────────────────────────────────────────────────────────────
enum Kind { OneShot, Loop, Reverse, Short, LoopShort, Scan, NKinds };
static const char* kindName[] = { "oneshot", "loop", "reverse", "short", "loopshort", "scan" };
static const char* modeName[] = { "NONE", "BEATS", "TONES", "TEXTURE" };
struct Env { float atk, fade; const char* name; };
static const Env envs[] = { { 5.f, 5.f, "a5f5" }, { 0.f, 0.f, "a0f0" }, { 0.f, 20.f, "a0f20" } };

struct Scenario { int mode, mat; float ratio; int kind, env; float pitch; std::string id; bool vib = false; };

static std::vector<Scenario> buildScenarios()
{
    std::vector<Scenario> s;
    const float ratios[] = { 0.5f, 0.75f, 1.0f, 1.33f, 2.0f, 4.0f };
    for (int mode = 0; mode < 4; ++mode)
        for (int mat = 0; mat < 3; ++mat)
            for (float r : ratios)
            {
                if (mode == 0 && r != 1.0f) continue;          // NONE ignores the ratio
                for (int kind = 0; kind < NKinds; ++kind)
                    for (int e = 0; e < 3; ++e)
                        for (float p : { 0.0f, -7.0f })
                        {
                            if (p != 0.0f && ! (r == 1.0f || r == 2.0f)) continue;
                            if (p != 0.0f && e != 0) continue;
                            Scenario sc { mode, mat, r, kind, e, p, "" };
                            char buf[160];
                            std::snprintf (buf, sizeof buf, "%s/%s/r%.2f/%s/%s/p%+.0f", modeName[mode], mat == 0 ? "pad" : mat == 1 ? "vox" : "beat",
                                           r, kindName[kind], envs[e].name, p);
                            sc.id = buf; s.push_back (sc);
                            // VIBRATO (tp55): the warp engines take it as a per-BLOCK pitch step — the
                            // "ratio changes mid-note" case. 60 cents @ 6 Hz on one-shot + loop.
                            if (e == 0 && p == 0.0f && (kind == OneShot || kind == Loop) && (r == 1.0f || r == 2.0f))
                            { Scenario v = sc; v.vib = true; v.id += "/vib"; s.push_back (v); }
                        }
            }
    return s;
}

// ── RENDER through the SHIPPED SamplerVoice ────────────────────────────────────────────────────
static std::vector<float> render (const Scenario& sc, const Material& m, int& onsetAt)
{
    tw::SampleBuffer sb;
    auto buf = std::make_shared<juce::AudioBuffer<float>> (2, (int) m.L.size());
    std::memcpy (buf->getWritePointer (0), m.L.data(), m.L.size() * sizeof (float));
    std::memcpy (buf->getWritePointer (1), m.R.data(), m.R.size() * sizeof (float));
    sb.store (buf); sb.setSampleRate (SR);

    std::atomic<int>   root { 60 }, loopMode { 0 };
    std::atomic<float> atk { envs[sc.env].atk }, rel { 30.f }, fade { envs[sc.env].fade };
    std::atomic<float> vibD { sc.vib ? 60.f : 0.f }, vibR { 6.f }, tsMul { 1.f };

    const bool looping = (sc.kind == Loop || sc.kind == LoopShort);
    loopMode = looping ? 1 : 0;

    // Slice: starts mid-waveform on purpose (0.4137 s is on no zero crossing).
    const int sStart = (int) (0.4137 * SR);
    const int sLen   = (sc.kind == Short) ? (int) (0.080 * SR) : (sc.kind == LoopShort) ? (int) (0.150 * SR) : (int) (1.2 * SR);

    tw::WarpRenderCache cache;
    cache.setSource (buf->getReadPointer (0), buf->getReadPointer (1), buf->getNumSamples());
    cache.setSampleRate (SR);
    cache.setSliceBounds (0, sStart, sStart + sLen);

    tw::SamplerVoice v (sb, root, atk, rel, loopMode, nullptr, &cache, &fade, &vibD, &vibR, &tsMul);
    v.setCurrentPlaybackSampleRate (SR);
    tw::SamplerSound sound;

    tw::VoiceConfig cfg;
    cfg.startSample = sStart; cfg.endSample = sStart + sLen;
    cfg.reverse = (sc.kind == Reverse);
    cfg.pitchSemitones = sc.pitch;
    cfg.sliceIndex = 0; cfg.sourceVersionId = 0;
    cfg.warpMode = (tw::WarpMode) sc.mode;
    cfg.stretchRatio = sc.ratio;
    cfg.scanEnabled = (sc.kind == Scan); cfg.scanRate = 1.0f; cfg.scanWindow = 0.6f;

    if (sc.kind == Scan && sc.mode != 0)
    {
        // Warm the cache first (normal use: prewarm fires when Scan is switched on in the UI).
        cache.prewarm ({ 0, 0, sc.ratio, (tw::WarpMode) sc.mode });
        for (int i = 0; i < 2000 && ! cache.isReady ({ 0, 0, sc.ratio, (tw::WarpMode) sc.mode }); ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    const double playDur = sLen / SR * (sc.mode == 0 ? std::pow (2.0, -sc.pitch / 12.0) : sc.ratio);
    const double holdSec = (looping || sc.kind == Scan) ? 3.0 : playDur + 0.6;
    const int pre = (int) (0.05 * SR);
    const int total = pre + (int) ((holdSec + 0.4) * SR);
    std::vector<float> out;
    out.reserve ((size_t) total + BLK);
    juce::AudioBuffer<float> b (2, BLK);
    bool started = false, stopped = false;
    onsetAt = pre;
    for (int pos = 0; pos < total; pos += BLK)
    {
        if (! started && pos >= pre) { v.prepareForNoteOn (cfg); v.startNote (60, 1.0f, &sound, 8192); started = true; onsetAt = pos; }
        if (started && ! stopped && pos >= onsetAt + (int) (holdSec * SR)) { v.stopNote (1.0f, true); stopped = true; }
        b.clear();
        if (started) v.renderNextBlock (b, 0, BLK);
        const float* l = b.getReadPointer (0); const float* r = b.getReadPointer (1);
        (void) r; for (int i = 0; i < BLK; ++i) out.push_back (l[i]);   // L only: the pad's stereo detune would comb-null a mid sum
    }
    return out;
}

// ── DETECTORS ─────────────────────────────────────────────────────────────────────────────────
struct Metrics { int hf = 0; double worstDb = -200; int d2 = 0; int flux = 0; int notch = 0; double rmsDb = -200, centroid = 0; std::vector<double> bands; };

static const std::vector<double>& hpf() { static auto h = firHighpass (7500.0, 255); return h; }

static Metrics analyse (const std::vector<float>& x, bool sustained, float pitch, int onsetAt)
{
    Metrics M; const int N = (int) x.size();
    std::vector<double> pre (x.size() + 1, 0.0);
    for (int n = 0; n < N; ++n) pre[(size_t) n + 1] = pre[(size_t) n] + (double) x[(size_t) n] * x[(size_t) n];
    auto rmsWin = [&] (int a, int b) { a = std::max (0, a); b = std::min (N, b); return b > a ? std::sqrt ((pre[(size_t) b] - pre[(size_t) a]) / (b - a)) : 0.0; };

    // [HF] click events
    const auto y = convolveSame (x, hpf());
    { int lastEv = -100000;
      std::vector<double> hrs;
      for (int f = 0; f + 64 <= N; f += 32)
      { double e = 0; for (int i = 0; i < 64; ++i) e += (double) y[(size_t) (f + i)] * y[(size_t) (f + i)]; hrs.push_back (std::sqrt (e / 64)); }
      for (int fi = 0; fi < (int) hrs.size(); ++fi)
      {
          const int f = fi * 32;
          const double hr = hrs[(size_t) fi], ref = rmsWin (f + 32 - 960, f + 32 + 960);
          if (hr < 1.8e-4 || ref < 1e-7) continue;                         // −75 dBFS floor: quieter HF is inaudible in context
          const double db = 20 * std::log10 (hr / ref);
          if (db <= -50.0) continue;
          // TRANSIENT: a click stands ≥ 10 dB above the HF floor of its own ±50 ms (a steady
          // interpolation-image floor — e.g. linear-interp varispeed at −7 st — is not a click).
          std::vector<double> w; for (int j = std::max (0, fi - 75); j < std::min ((int) hrs.size(), fi + 75); ++j) w.push_back (hrs[(size_t) j]);
          std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end());
          if (hr < 3.16 * w[w.size() / 2]) continue;
          if (f - lastEv > 480) { ++M.hf; if (std::getenv ("CS_DEBUG")) std::printf ("    HF event @ %8.1f ms  %6.1f dB  (HF %6.1f dBFS)\n", (f - onsetAt) / SR * 1000.0, db, 20 * std::log10 (hr)); }
          lastEv = f; M.worstDb = std::max (M.worstDb, db);
      } }

    // [D2] hard discontinuities vs the Bernstein bound of the band-limited read
    { const double fmax = 4600.0 * std::pow (2.0, std::max (0.0f, pitch) / 12.0);
      const double w = 2 * PI * fmax / SR, bound = 1.5 * w * w;
      std::vector<float> blkMax ((size_t) (N / 64 + 1), 0.f);
      for (int n = 0; n < N; ++n) blkMax[(size_t) (n / 64)] = std::max (blkMax[(size_t) (n / 64)], std::abs (x[(size_t) n]));
      int lastEv = -100000;
      for (int n = 2; n < N; ++n)
      {
          const double d2 = std::abs ((double) x[(size_t) n] - 2.0 * x[(size_t) n - 1] + x[(size_t) n - 2]);
          if (d2 < 1e-4) continue;
          float pk = 0; for (int b = std::max (0, n / 64 - 4); b <= std::min ((int) blkMax.size() - 1, n / 64 + 4); ++b) pk = std::max (pk, blkMax[(size_t) b]);
          if (d2 > bound * pk + 1e-4) { if (n - lastEv > 480) ++M.d2; lastEv = n; }
      } }

    // [FLUX] HF spectral-flux spikes
    { const int F = 512, H = 128; std::vector<double> win ((size_t) F); for (int i = 0; i < F; ++i) win[(size_t) i] = 0.5 - 0.5 * std::cos (2 * PI * i / F);
      const int kLo = (int) (6000.0 / SR * F);
      std::vector<double> prevMag ((size_t) F / 2, 0.0), fluxes, totals;
      for (int f = 0; f + F <= N; f += H)
      {
          std::vector<std::complex<double>> a ((size_t) F); for (int i = 0; i < F; ++i) a[(size_t) i] = x[(size_t) (f + i)] * win[(size_t) i];
          fft (a); double fl = 0, tot = 0;
          for (int k = 1; k < F / 2; ++k) { const double mg = std::abs (a[(size_t) k]); tot += mg;
              if (k >= kLo) fl += std::max (0.0, mg - prevMag[(size_t) k]); prevMag[(size_t) k] = mg; }
          fluxes.push_back (fl); totals.push_back (tot);
      }
      int lastEv = -1000;
      for (int i = 0; i < (int) fluxes.size(); ++i)
      {
          std::vector<double> w; for (int j = std::max (0, i - 60); j < std::min ((int) fluxes.size(), i + 60); ++j) w.push_back (fluxes[(size_t) j]);
          std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end()); const double med = w[w.size() / 2];
          if (fluxes[(size_t) i] > 8.0 * med + 1e-9 && totals[(size_t) i] > 1e-3 && fluxes[(size_t) i] > 3e-2 * totals[(size_t) i])
          { if (i - lastEv > 3) ++M.flux; lastEv = i; }
      } }

    // [NOTCH] dropouts on sustained material, within the voice's body (skip onset + release).
    // 1 ms RMS vs the local FLOOR: the median of the per-10 ms MINIMA of that 1 ms RMS within
    // ±40 ms. A periodic waveform's quietest millisecond per cycle is stable (and a phase vocoder's
    // phasiness moves it only slowly), so this ignores the waveform's own within-period swing; a
    // gated joint / dropout goes far below it. Event = 1 ms RMS < 30 % of that floor.
    if (sustained)
    { int last = N - 1; while (last > 0 && std::abs (x[(size_t) last]) < 1e-4f) --last;
      int first = onsetAt; while (first < last && std::abs (x[(size_t) first]) < 1e-3f) ++first;   // a stretcher's latency is not a dropout
      const int a0 = first + (int) (0.03 * SR), a1 = last - (int) (0.12 * SR);
      std::vector<double> env; for (int n = a0; n + 48 <= a1; n += 24) env.push_back (rmsWin (n, n + 48));   // 1 ms RMS, 0.5 ms hop
      std::vector<double> blkMin; for (size_t j = 0; j < env.size(); j += 20) { double mn = 1e9; for (size_t k = j; k < std::min (env.size(), j + 20); ++k) mn = std::min (mn, env[k]); blkMin.push_back (mn); }
      int lastEv = -1000;
      for (int i = 0; i < (int) env.size(); ++i)
      {
          const int bi = i / 20; std::vector<double> w;
          for (int j = std::max (0, bi - 4); j <= std::min ((int) blkMin.size() - 1, bi + 4); ++j) if (j != bi) w.push_back (blkMin[(size_t) j]);
          if (w.empty()) continue;
          std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end()); const double floorLv = w[w.size() / 2];
          if (floorLv > 3e-4 && env[(size_t) i] < 0.3 * floorLv)
          { if (i - lastEv > 12) { ++M.notch; if (std::getenv ("CS_DEBUG")) std::printf ("    NOTCH @ %8.1f ms  %.2f of floor\n", (a0 + i * 24 - onsetAt) / SR * 1000.0, env[(size_t) i] / floorLv); } lastEv = i; }
      } }

    // CHARACTER — LTAS 1/3-oct 63 Hz..4 kHz, centroid, RMS
    { const int F = 4096, H = 1024; std::vector<double> ps ((size_t) F / 2, 0.0); int cnt = 0;
      for (int f = 0; f + F <= N; f += H)
      { std::vector<std::complex<double>> a ((size_t) F); for (int i = 0; i < F; ++i) a[(size_t) i] = x[(size_t) (f + i)] * (0.5 - 0.5 * std::cos (2 * PI * i / F));
        fft (a); for (int k = 0; k < F / 2; ++k) ps[(size_t) k] += std::norm (a[(size_t) k]); ++cnt; }
      double num = 0, den = 0;
      for (int k = 1; k < F / 2; ++k) { const double fz = k * SR / F; if (fz > 8000) break; num += fz * ps[(size_t) k]; den += ps[(size_t) k]; }
      M.centroid = den > 0 ? num / den : 0;
      for (int b = 0; b < 19; ++b)
      { const double fc = 63.0 * std::pow (2.0, b / 3.0), lo = fc * std::pow (2.0, -1.0 / 6), hi = fc * std::pow (2.0, 1.0 / 6);
        double e = 1e-20; for (int k = 1; k < F / 2; ++k) { const double fz = k * SR / F; if (fz >= lo && fz < hi) e += ps[(size_t) k]; }
        M.bands.push_back (10 * std::log10 (e / std::max (1, cnt))); }
      M.rmsDb = 20 * std::log10 (std::max (1e-12, rmsWin (0, N))); }
    return M;
}

// ── MAIN ─────────────────────────────────────────────────────────────────────────────────────
static int runAll (const char* outPath, const char* filter)
{
    Material mats[3] = { makeMaterial (0), makeMaterial (1), makeMaterial (2) };
    auto scs = buildScenarios();
    std::ofstream o (outPath);
    int done = 0;
    for (auto& sc : scs)
    {
        if (filter && sc.id.find (filter) == std::string::npos) continue;
        int onsetAt = 0;
        const auto y = render (sc, mats[sc.mat], onsetAt);
        if (const char* dump = std::getenv ("CS_DUMP"))   // raw float32 mono render, for eyeballing a scenario
        { if (FILE* fp = std::fopen (dump, "wb")) { std::fwrite (y.data(), sizeof (float), y.size(), fp); std::fclose (fp); } }
        const auto M = analyse (y, mats[sc.mat].sustained, sc.pitch, onsetAt);
        o << sc.id << '\t' << modeName[sc.mode] << '\t' << kindName[sc.kind] << '\t' << M.hf << '\t' << M.worstDb << '\t' << M.d2 << '\t'
          << M.flux << '\t' << M.notch << '\t' << M.rmsDb << '\t' << M.centroid;
        for (double b : M.bands) o << '\t' << b;
        o << '\n';
        if (filter) std::printf ("%-44s HF %3d (worst %6.1f dB)  D2 %3d  FLUX %3d  NOTCH %3d  rms %6.1f  cent %6.0f\n",
                                 sc.id.c_str(), M.hf, M.worstDb, M.d2, M.flux, M.notch, M.rmsDb, M.centroid);
        ++done;
    }
    std::printf ("rendered %d scenarios -> %s\n", done, outPath);
    return 0;
}

struct Row { std::string id, mode, kind; int hf, d2, flux, notch; double worst, rms, cent; std::vector<double> bands; };
static std::map<std::string, Row> readRows (const char* p)
{
    std::map<std::string, Row> m; std::ifstream in (p); std::string line;
    while (std::getline (in, line))
    {
        std::stringstream ss (line); Row r; ss >> r.id >> r.mode >> r.kind >> r.hf >> r.worst >> r.d2 >> r.flux >> r.notch >> r.rms >> r.cent;
        double b; while (ss >> b) r.bands.push_back (b); m[r.id] = r;
    }
    return m;
}

static int compare (const char* a, const char* b, bool strict)
{
    auto A = readRows (a), B = readRows (b);
    struct Agg { int nb = 0, n = 0, hfA = 0, hfB = 0, d2A = 0, d2B = 0, flA = 0, flB = 0, noA = 0, noB = 0, sA = 0, sB = 0; double wA = -200, wB = -200, maxBand = 0, sumBand = 0, maxCent = 0, maxRms = 0; };
    std::map<std::string, Agg> byMode, byKind;
    std::vector<std::pair<double, std::string>> worstChar;
    for (auto& [id, ra] : A)
    {
        auto it = B.find (id); if (it == B.end()) continue; const Row& rb = it->second;
        // Character: bands within 20 dB of the row's loudest band (the material's actual content —
        // an empty band's numeric floor is not "character"). Silent-in-either rows are reported apart.
        double mb = 0;
        const double topA = *std::max_element (ra.bands.begin(), ra.bands.end()), topB = *std::max_element (rb.bands.begin(), rb.bands.end());
        const bool bothSounding = ra.rms > -80 && rb.rms > -80;
        if (bothSounding)
            for (size_t i = 0; i < std::min (ra.bands.size(), rb.bands.size()); ++i)
            { if (ra.bands[i] < topA - 20 && rb.bands[i] < topB - 20) continue; mb = std::max (mb, std::abs ((ra.bands[i] - topA) - (rb.bands[i] - topB))); }
        else if (ra.rms > -80 || rb.rms > -80)
            std::printf ("  (silent in one build: %s  rms %.1f -> %.1f dB)\n", id.c_str(), ra.rms, rb.rms);
        const double dc = ra.cent > 0 ? std::abs (rb.cent - ra.cent) / ra.cent * 100.0 : 0.0;
        worstChar.push_back ({ mb, id });
        for (auto* g : { &byMode[ra.mode], &byKind[ra.mode + "/" + ra.kind] })
        {
            g->n++; g->hfA += ra.hf; g->hfB += rb.hf; g->d2A += ra.d2; g->d2B += rb.d2; g->flA += ra.flux; g->flB += rb.flux;
            const bool tex = (ra.mode == "TEXTURE");
            g->noA += tex ? 0 : ra.notch; g->noB += tex ? 0 : rb.notch;
            g->sA += (ra.hf + ra.d2 > 0); g->sB += (rb.hf + rb.d2 > 0);
            g->wA = std::max (g->wA, ra.worst); g->wB = std::max (g->wB, rb.worst);
            if (bothSounding) { g->maxBand = std::max (g->maxBand, mb); g->sumBand += mb; g->nb++; g->maxCent = std::max (g->maxCent, dc);
                               g->maxRms = std::max (g->maxRms, std::abs (rb.rms - ra.rms)); }
        }
    }
    auto pr = [] (const std::string& k, const Agg& g)
    { std::printf ("%-20s %4d | HF clicks %5d -> %-5d | worst %6.1f -> %6.1f dB | D2 %4d -> %-4d | FLUX %4d -> %-4d | NOTCH %4d -> %-4d | dirty scen %3d -> %-3d | LTAS max|d| %5.2f dB mean %4.2f | cent %4.1f%% | rms %4.2f dB\n",
                   k.c_str(), g.n, g.hfA, g.hfB, g.wA, g.wB, g.d2A, g.d2B, g.flA, g.flB, g.noA, g.noB, g.sA, g.sB, g.maxBand, g.nb ? g.sumBand / g.nb : 0, g.maxCent, g.maxRms); };
    std::printf ("\n=== PER MODE (before -> after) ===\n"); for (auto& [k, g] : byMode) pr (k, g);
    std::printf ("\n=== PER MODE / SLICE KIND ===\n");  for (auto& [k, g] : byKind) pr (k, g);
    // FIDELITY TO THE SOURCE: each warped render's spectral SHAPE vs the unstretched dry read of the same
    // slice (NONE/<mat>/r1.00/<kind>/<env>/<pitch>, same build). A stretcher should move time, not tone.
    auto shapeDiff = [] (const Row& x, const Row& ref)
    {
        const double tx = *std::max_element (x.bands.begin(), x.bands.end()), tr = *std::max_element (ref.bands.begin(), ref.bands.end());
        double s = 0; int n = 0;
        for (size_t i = 0; i < std::min (x.bands.size(), ref.bands.size()); ++i)
        { if (ref.bands[i] < tr - 20) continue; s += std::abs ((x.bands[i] - tx) - (ref.bands[i] - tr)); ++n; }
        return n ? s / n : 0.0;
    };
    auto refId = [] (const std::string& id)
    { auto p1 = id.find ('/'), p2 = id.find ('/', p1 + 1), p3 = id.find ('/', p2 + 1);
      return "NONE" + id.substr (p1, p2 - p1) + "/r1.00" + id.substr (p3); };
    std::map<std::string, std::array<double, 3>> fid;   // mode -> {sumA, sumB, n}
    for (auto& [id, ra] : A)
    {
        if (ra.mode == "NONE") continue;
        auto rbI = B.find (id), refA = A.find (refId (id)), refB = B.find (refId (id));
        if (rbI == B.end() || refA == A.end() || refB == B.end()) continue;
        if (ra.rms < -80 || rbI->second.rms < -80) continue;
        auto& f = fid[ra.mode + "/" + ra.kind]; f[0] += shapeDiff (ra, refA->second); f[1] += shapeDiff (rbI->second, refB->second); f[2] += 1;
    }
    std::printf ("\n=== FIDELITY: mean |spectral-shape dB| vs the unstretched dry slice (lower = closer to the source) ===\n");
    for (auto& [k, f] : fid) std::printf ("  %-20s %3.0f scen   %5.2f dB -> %5.2f dB\n", k.c_str(), f[2], f[0] / f[2], f[1] / f[2]);

    std::sort (worstChar.rbegin(), worstChar.rend());
    std::printf ("\n=== largest LTAS moves ===\n"); for (int i = 0; i < 8 && i < (int) worstChar.size(); ++i) std::printf ("  %5.2f dB  %s\n", worstChar[(size_t) i].first, worstChar[(size_t) i].second.c_str());
    int remaining = 0; for (auto& [id, rb] : B) remaining += rb.hf + rb.d2;
    if (strict && remaining > 0) { std::printf ("\nSTRICT: %d click events remain\n", remaining); return 1; }
    return 0;
}

// Detector calibration: the raw band-limited sources must read CLEAN (no false positives), and a
// planted click / step / gated dip must be caught (no false negatives). Run first, always.
static int selftest()
{
    int bad = 0;
    for (int w = 0; w < 3; ++w)
    {
        const auto m = makeMaterial (w);
        std::vector<float> x (m.L.begin(), m.L.begin() + (long) (3.0 * SR));
        for (int i = 0; i < 480; ++i) { const float r = (float) std::sin (0.5 * PI * i / 480.0); x[(size_t) i] *= r; x[x.size() - 1 - (size_t) i] *= r; }   // the excerpt's own edges
        auto M = analyse (x, m.sustained, 0.f, 0);
        std::printf ("  clean %-5s  HF %d  D2 %d  FLUX %d  NOTCH %d\n", m.name.c_str(), M.hf, M.d2, M.flux, M.notch);
        bad += (M.hf + M.d2 + M.flux + M.notch) != 0;
        auto s = x; for (size_t i = (size_t) (1.3 * SR); i < s.size(); ++i) s[i] *= 0.8f;    // −2 dB gain STEP at 1.3 s
        auto M2 = analyse (s, m.sustained, 0.f, 0);
        auto g = x; for (int i = 0; i < 192; ++i) g[(size_t) (1.5 * SR) + (size_t) i] *= (float) (1.0 - std::sin (PI * i / 191.0));   // 4 ms smooth dip to zero
        auto M3 = analyse (g, m.sustained, 0.f, 0);
        std::printf ("  step  %-5s  HF %d  D2 %d  FLUX %d | 4ms dip NOTCH %d  HF %d\n", m.name.c_str(), M2.hf, M2.d2, M2.flux, M3.notch, M3.hf);
        bad += (M2.hf == 0);
        if (m.sustained) bad += (M3.notch == 0);
    }
    std::printf ("selftest %s\n", bad ? "FAILED" : "ok");
    return bad ? 1 : 0;
}

int main (int argc, char** argv)
{
    juce::ScopedNoDenormals noDenormals;   // same as the plugin's processBlock
    if (argc >= 2 && std::strcmp (argv[1], "selftest") == 0) return selftest();
    if (argc >= 3 && std::strcmp (argv[1], "run") == 0) return runAll (argv[2], argc >= 4 ? argv[3] : nullptr);
    if (argc >= 4 && std::strcmp (argv[1], "compare") == 0) return compare (argv[2], argv[3], argc >= 5);
    std::printf ("usage: %s run out.tsv [idFilter] | compare before.tsv after.tsv [strict]\n", argv[0]);
    return 2;
}
