// ══════════════════════════════════════════════════════════════════════════════════════════════
//  synthstretch_cert.cpp — SYNTH SAMPLE-OSC STRETCH / WARP CLICK HARNESS, ON THE REAL PROCESSOR.
//
//  Max: "I would love the Sample oscillator's Tones-mode crackle fix enabled, and other stretch modes
//  that do relentless crackling when used and stretched." tp101's Source/ChopStretch_test.cpp cleaned
//  the CHOP voice; this is its twin for the SYNTH's Sample oscillator. Linked against the built
//  libTerrain_SharedCode.a (Tests/synthstretch_gate.sh), so every sample is the shipping processBlock:
//  osc A = Sample engine, the source stored into the processor's own per-osc SampleBuffer, notes sent
//  as MIDI, parameters set through the APVTS — the whole voice (SampleEngine read → WarpProcessor →
//  tilt → amp env → the default chain) is what is measured.
//
//  SCENARIOS: every stretch mode the Sample osc offers (SYN_OSC_x_SAMPLE_STRETCH_MODE = Tones / Beats /
//  Texture) × ratios 1.25 1.5 2 4 (the knob maps 0..1 → 1×..4×: ratios < 1 are NOT reachable from the
//  synth — the chop harness covers those) × pad / vox / drum loop / pure sine × notes 36 48 60 72 ×
//  unison 1 and 4, looping (Forward) — plus one-shot endings, and PARAMETER CHANGES DURING A STRETCH:
//  a stretch sweep 0.2 → 0.8 while the note holds, the warp engaging mid-note (stretch 0 → 0.33) and
//  disengaging (0.33 → 0), the stretch MODE switched mid-note — and FORMANT alone (stretch 0, formant +0.3: the warp engaged at ratio 1).
//  DRY (stretch 0, no warp) renders are the reference for every row.
//
//  THE MEASUREMENT is ChopStretch_test.cpp's, unchanged (fb283 perceptual law): every source is
//  band-limited so that AFTER the note's resample it has no energy above ~4.6 kHz; then
//   [HF]    click events in the > 7.5 kHz band, transient vs the local HF floor, above −75 dBFS
//   [D2]    |second difference| above 1.5× the Bernstein bound of the band-limited signal
//   [FLUX]  > 6 kHz spectral-flux spikes
//   [NOTCH] dropouts on sustained material (Texture's are by design — excluded)
//   LTAS    1/3-octave long-term spectrum + centroid: the sound's character before → after.
//  A "dirty scenario" = HF + D2 > 0. Every event's absolute level is recorded (worst dBFS per row).
//
//    synthstretch run out.tsv [idFilter] | compare before.tsv after.tsv [strict] | selftest | cpu
// ══════════════════════════════════════════════════════════════════════════════════════════════
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <vector>
#include <array>
#include <deque>
#include <list>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <string>
#include <sstream>
#include <fstream>
#include <random>
#include <optional>
#include <bit>
#include <span>
#include <chrono>
#include <complex>
#include <cmath>
#include <cstring>
#include <algorithm>
#define private public
#define protected public
#include "PluginProcessor.h"
#undef private
#undef protected

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

// ── SOURCE MATERIALS — band-limited so the NOTE's resample keeps them < 3.5 kHz ────────────────
struct Material { std::string name; std::vector<float> L, R; bool sustained; };
static uint32_t rngState = 0x1234567u;
static double urand() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return (rngState & 0xFFFFFF) / 16777216.0; }
static const char* matName[] = { "pad", "vox", "beat", "sine" };

static Material makeMaterial (int which, int note)
{
    rngState = 0x1234567u + (uint32_t) which * 977u;
    const int N = (int) (SR * 1.0);   // 1 s: a Forward loop wraps inside every hold
    std::vector<float> L ((size_t) N), R ((size_t) N);
    Material m; m.name = matName[which]; m.sustained = (which != 2);
    if (which == 0)          // PAD — 110 Hz saw-ish stack, slow AM, stereo detune
    {
        std::vector<double> ph (40); for (auto& p : ph) p = urand() * 2 * PI;
        for (int n = 0; n < N; ++n)
        {
            const double t = n / SR; double l = 0, r = 0;
            for (int k = 1; k <= 27; ++k) { const double a = 1.0 / k;
                l += a * std::sin (2 * PI * 110.0 * k * t + ph[(size_t) k]);
                r += a * std::sin (2 * PI * 110.4 * k * t + ph[(size_t) k] + 0.3); }
            const double am = 0.8 + 0.2 * std::sin (2 * PI * 1.0 * t);
            L[(size_t) n] = (float) (0.22 * am * l); R[(size_t) n] = (float) (0.22 * am * r);
        }
    }
    else if (which == 1)     // VOX — 196 Hz with 5.5 Hz vibrato, formant-weighted harmonics
    {
        double phase = 0;
        std::vector<double> vph (17); for (auto& p : vph) p = urand() * 2 * PI;
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
    else if (which == 2)     // BEAT — 120 BPM kick / snare / hat loop (2 beats)
    {
        const int step = (int) (SR * 0.125);
        for (int s = 0; s * step < N; ++s)
        {
            const int at = s * step;
            const bool kick = (s % 8 == 0) || (s % 8 == 5), snare = (s % 8 == 4);
            for (int i = 0; i < (int) (SR * 0.3) && at + i < N; ++i)
            {
                const double t = i / SR; double v = 0;
                const double atk = std::min (1.0, t / 0.001);
                if (kick)  v += 0.7 * atk * std::exp (-t / 0.12) * std::sin (2 * PI * (50 * t + 70 * 0.03 * (1 - std::exp (-t / 0.03))));
                if (snare) v += 0.35 * atk * std::exp (-t / 0.07) * (2 * urand() - 1) + 0.2 * atk * std::exp (-t / 0.05) * std::sin (2 * PI * 190 * t);
                if (i < (int) (SR * 0.05)) v += 0.08 * atk * std::exp (-t / 0.015) * (2 * urand() - 1);
                L[(size_t) (at + i)] += (float) v; R[(size_t) (at + i)] += (float) (0.95 * v);
            }
        }
    }
    else                     // SINE — pure 220 Hz (220 whole cycles per second: the loop seam is continuous)
    {
        for (int n = 0; n < N; ++n) { const float v = (float) (0.5 * std::sin (2 * PI * 220.0 * n / SR)); L[(size_t) n] = v; R[(size_t) n] = v; }
    }
    // Band-limit so that after the note's resample (×2^((note−60)/12)) nothing is above 3.5 kHz.
    const double up = std::max (1.0, std::pow (2.0, (note - 60) / 12.0));
    const auto lp = firLowpass (3500.0 / up, 255);
    if (which != 3) { L = convolveSame (L, lp); R = convolveSame (R, lp); }
    // The file's own edges: 5 ms equal-power in/out, so a DRY one-shot is clean at both ends.
    const int e = (int) (0.005 * SR);
    for (int i = 0; i < e; ++i) { const float g = (float) std::sin (0.5 * PI * i / e);
        L[(size_t) i] *= g; R[(size_t) i] *= g; L[(size_t) (N - 1 - i)] *= g; R[(size_t) (N - 1 - i)] *= g; }
    m.L = L; m.R = R;
    return m;
}

// ── SCENARIOS ────────────────────────────────────────────────────────────────────────────────
enum Kind { Loop, OneShot, Sweep, Engage, Disengage, Formant, ModeSwitch, NKinds };
static const char* kindName[] = { "loop", "oneshot", "sweep", "engage", "disengage", "formant", "modeswitch" };
static const char* modeName[] = { "DRY", "TONES", "BEATS", "TEXTURE" };   // DRY = stretch 0 (no warp)
struct Scenario { int mode, mat, note, uni, kind; float ratio; std::string id; };

static std::vector<Scenario> buildScenarios()
{
    std::vector<Scenario> s;
    auto add = [&] (int mode, int mat, int note, int uni, int kind, float ratio)
    {
        char buf[160];
        std::snprintf (buf, sizeof buf, "%s/%s/n%d/u%d/%s/r%.2f", modeName[mode], matName[mat], note, uni, kindName[kind], ratio);
        s.push_back ({ mode, mat, note, uni, kind, ratio, buf });
    };
    const int notes[] = { 36, 48, 60, 72 };
    const float ratios[] = { 1.25f, 1.5f, 2.0f, 4.0f };
    for (int mat = 0; mat < 4; ++mat)
        for (int note : notes)
            for (int uni : { 1, 4 })
            {
                add (0, mat, note, uni, Loop, 1.0f);
                for (int mode = 1; mode < 4; ++mode)
                    for (float r : ratios) add (mode, mat, note, uni, Loop, r);
            }
    for (int mat = 0; mat < 4; ++mat)
    {
        add (0, mat, 60, 1, OneShot, 1.0f);
        for (int mode = 1; mode < 4; ++mode)
        {
            for (float r : ratios) add (mode, mat, 60, 1, OneShot, r);
            add (mode, mat, 60, 1, Sweep, 2.0f);        // stretch 0.2 → 0.8 while held (ratio 1.6 → 3.4)
            add (mode, mat, 60, 1, Engage, 2.0f);       // stretch 0 → 0.333 mid-note (the warp switches IN)
            add (mode, mat, 60, 1, Disengage, 2.0f);    // stretch 0.333 → 0 mid-note (the warp switches OUT)
            add (mode, mat, 60, 1, Formant, 1.0f);      // stretch 0, FORMANT +0.3: the warp runs at ratio 1
            add (mode, mat, 60, 1, ModeSwitch, 2.0f);   // the stretch MODE changes mid-note (→ the next mode)
        }
    }
    return s;
}

// ── RENDER through the REAL processor ─────────────────────────────────────────────────────────
static void setP (TerrainAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("!! no param %s\n", id); std::exit (2); }
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}
static float knobFor (float ratio) { return juce::jlimit (0.0f, 1.0f, (ratio - 1.0f) / 3.0f); }

struct Proc
{
    std::unique_ptr<TerrainAudioProcessor> p;
    Proc()
    {
        p = std::make_unique<TerrainAudioProcessor>();
        p->setPlayConfigDetails (0, 2, SR, BLK);
        p->prepareToPlay (SR, BLK);
    }
};

static void configure (TerrainAudioProcessor& p, const Material& m, int mode, int uni, bool loop, float stretchKnob)
{
    auto buf = std::make_shared<juce::AudioBuffer<float>> (2, (int) m.L.size());
    std::memcpy (buf->getWritePointer (0), m.L.data(), m.L.size() * sizeof (float));
    std::memcpy (buf->getWritePointer (1), m.R.data(), m.R.size() * sizeof (float));
    auto& sb = p.getOscSampleBuffer (0);
    sb.setSampleRate (SR);
    sb.store (buf);
    setP (p, ParameterIDs::SYN_OSC_A_ENGINE, 1.f);   // SAMP
    setP (p, ParameterIDs::SYN_OSC_B_ENABLE, 0.f);
    setP (p, ParameterIDs::SYN_OSC_C_ENABLE, 0.f);
    setP (p, ParameterIDs::SYN_OSC_D_ENABLE, 0.f);
    setP (p, ParameterIDs::SYN_NOISE_LEVEL, 0.f);
    setP (p, ParameterIDs::SYN_ENV_AMP_A, 5.f);
    setP (p, ParameterIDs::SYN_ENV_AMP_S, 1.f);
    setP (p, ParameterIDs::SYN_ENV_AMP_R, 60.f);
    setP (p, ParameterIDs::SYN_OSC_A_UNISON, (float) uni);
    setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_LOOP_MODE, loop ? 1.f : 0.f);
    setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH_MODE, (float) juce::jmax (0, mode == 1 ? 0 : mode == 2 ? 1 : 2));
    setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH, stretchKnob);
}

static std::vector<float> render (const Scenario& sc, const Material& m, int& onsetAt)
{
    Proc P; auto& p = *P.p;
    const bool loop = sc.kind != OneShot;
    const float knob = sc.mode == 0 ? 0.f : knobFor (sc.ratio);
    float k0 = knob;
    if (sc.kind == Sweep) k0 = 0.2f;
    if (sc.kind == Engage || sc.kind == Formant) k0 = 0.f;
    configure (p, m, sc.mode, sc.uni, loop, k0);
    if (sc.kind == Formant) setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT, 0.3f);
    for (int t = 0; t < 3; ++t) p.timerCallback();

    const double holdSec = (sc.kind == OneShot) ? (1.0 * sc.ratio + 0.3) : 4.0;
    const int pre = 4 * BLK;                         // settle the parameter smoothers first
    const int total = pre + (int) ((holdSec + 0.5) * SR);
    std::vector<float> out; out.reserve ((size_t) total + BLK);
    juce::AudioBuffer<float> b (2, BLK);
    onsetAt = pre;
    const int offAt = pre + (int) (holdSec * SR);
    for (int pos = 0; pos < total; pos += BLK)
    {
        const double tIn = (pos - pre) / SR;
        if (sc.kind == Sweep && pos >= pre)
            setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH, (float) juce::jlimit (0.2, 0.8, 0.2 + 0.6 * (tIn - 0.8) / 2.0));
        if (sc.kind == Engage && pos >= pre + (int) (1.5 * SR) && pos - BLK < pre + (int) (1.5 * SR))
            setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH, 1.f / 3.f);
        if (sc.kind == Disengage && pos >= pre + (int) (1.5 * SR) && pos - BLK < pre + (int) (1.5 * SR))
            setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH, 0.f);
        if (sc.kind == ModeSwitch && pos >= pre + (int) (1.5 * SR) && pos - BLK < pre + (int) (1.5 * SR))
            setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_STRETCH_MODE, (float) (sc.mode % 3));   // TONES→BEATS→TEXTURE→TONES
        b.clear(); juce::MidiBuffer mb;
        if (pos == pre) mb.addEvent (juce::MidiMessage::noteOn (1, sc.note, (juce::uint8) 100), 0);
        if (pos <= offAt && offAt < pos + BLK) mb.addEvent (juce::MidiMessage::noteOff (1, sc.note), offAt - pos);
        p.processBlock (b, mb);
        const float* l = b.getReadPointer (0);
        for (int i = 0; i < BLK; ++i) out.push_back (l[i]);   // L only (the pad's detune would comb-null a mid sum)
    }
    p.releaseResources();
    return out;
}

// ── DETECTORS (ChopStretch_test.cpp's, unchanged; + the absolute level of every event) ─────────
struct Metrics { int hf = 0; double worstDb = -200, worstAbsDb = -200; int d2 = 0; double d2WorstDb = -200; int flux = 0; int notch = 0; double rmsDb = -200, centroid = 0; std::vector<double> bands; };
static const std::vector<double>& hpf() { static auto h = firHighpass (7500.0, 255); return h; }

static Metrics analyse (const std::vector<float>& x, bool sustained, int onsetAt)
{
    Metrics M; const int N = (int) x.size();
    std::vector<double> pre (x.size() + 1, 0.0);
    for (int n = 0; n < N; ++n) pre[(size_t) n + 1] = pre[(size_t) n] + (double) x[(size_t) n] * x[(size_t) n];
    auto rmsWin = [&] (int a, int b) { a = std::max (0, a); b = std::min (N, b); return b > a ? std::sqrt ((pre[(size_t) b] - pre[(size_t) a]) / (b - a)) : 0.0; };

    const auto y = convolveSame (x, hpf());
    { int lastEv = -100000;
      std::vector<double> hrs;
      for (int f = 0; f + 64 <= N; f += 32)
      { double e = 0; for (int i = 0; i < 64; ++i) e += (double) y[(size_t) (f + i)] * y[(size_t) (f + i)]; hrs.push_back (std::sqrt (e / 64)); }
      for (int fi = 0; fi < (int) hrs.size(); ++fi)
      {
          const int f = fi * 32;
          const double hr = hrs[(size_t) fi], ref = rmsWin (f + 32 - 960, f + 32 + 960);
          if (hr < 1.8e-4 || ref < 1e-7) continue;
          const double db = 20 * std::log10 (hr / ref);
          if (db <= -50.0) continue;
          std::vector<double> w; for (int j = std::max (0, fi - 75); j < std::min ((int) hrs.size(), fi + 75); ++j) w.push_back (hrs[(size_t) j]);
          std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end());
          if (hr < 3.16 * w[w.size() / 2]) continue;
          if (f - lastEv > 480) { ++M.hf; if (std::getenv ("SS_DEBUG")) std::printf ("    HF event @ %8.1f ms  %6.1f dB  (HF %6.1f dBFS)\n", (f - onsetAt) / SR * 1000.0, db, 20 * std::log10 (hr)); }
          lastEv = f; M.worstDb = std::max (M.worstDb, db); M.worstAbsDb = std::max (M.worstAbsDb, 20 * std::log10 (hr));
      } }

    { const double fmax = 4600.0;
      const double w = 2 * PI * fmax / SR, bound = 1.5 * w * w;
      std::vector<float> blkMax ((size_t) (N / 64 + 1), 0.f);
      for (int n = 0; n < N; ++n) blkMax[(size_t) (n / 64)] = std::max (blkMax[(size_t) (n / 64)], std::abs (x[(size_t) n]));
      int lastEv = -100000;
      for (int n = 2; n < N; ++n)
      {
          const double d2 = std::abs ((double) x[(size_t) n] - 2.0 * x[(size_t) n - 1] + x[(size_t) n - 2]);
          if (d2 < 1e-4) continue;
          float pk = 0; for (int b = std::max (0, n / 64 - 4); b <= std::min ((int) blkMax.size() - 1, n / 64 + 4); ++b) pk = std::max (pk, blkMax[(size_t) b]);
          if (d2 > bound * pk + 1e-4)
          { if (n - lastEv > 480) { ++M.d2; if (std::getenv ("SS_DEBUG")) std::printf ("    D2 event @ %8.1f ms  step %.4f (bound %.4f)\n", (n - onsetAt) / SR * 1000.0, d2, bound * pk); }
            lastEv = n; M.d2WorstDb = std::max (M.d2WorstDb, 20 * std::log10 (d2 - bound * pk)); }
      } }

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

    if (sustained)
    { int last = N - 1; while (last > 0 && std::abs (x[(size_t) last]) < 1e-4f) --last;
      int first = onsetAt; while (first < last && std::abs (x[(size_t) first]) < 1e-3f) ++first;
      const int a0 = first + (int) (0.03 * SR), a1 = last - (int) (0.12 * SR);
      std::vector<double> env; for (int n = a0; n + 48 <= a1; n += 24) env.push_back (rmsWin (n, n + 48));
      std::vector<double> blkMin; for (size_t j = 0; j < env.size(); j += 20) { double mn = 1e9; for (size_t k = j; k < std::min (env.size(), j + 20); ++k) mn = std::min (mn, env[k]); blkMin.push_back (mn); }
      int lastEv = -1000;
      for (int i = 0; i < (int) env.size(); ++i)
      {
          const int bi = i / 20; std::vector<double> w;
          for (int j = std::max (0, bi - 4); j <= std::min ((int) blkMin.size() - 1, bi + 4); ++j) if (j != bi) w.push_back (blkMin[(size_t) j]);
          if (w.empty()) continue;
          std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end()); const double floorLv = w[w.size() / 2];
          if (floorLv > 3e-4 && env[(size_t) i] < 0.3 * floorLv)
          { if (i - lastEv > 12) { ++M.notch; if (std::getenv ("SS_DEBUG")) std::printf ("    NOTCH @ %8.1f ms  %.2f of floor\n", (a0 + i * 24 - onsetAt) / SR * 1000.0, env[(size_t) i] / floorLv); } lastEv = i; }
      } }

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
    std::map<std::pair<int, int>, Material> mats;
    auto scs = buildScenarios();
    std::ofstream o (outPath);
    int done = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (auto& sc : scs)
    {
        if (filter && sc.id.find (filter) == std::string::npos) continue;
        auto key = std::make_pair (sc.mat, sc.note);
        if (! mats.count (key)) mats[key] = makeMaterial (sc.mat, sc.note);
        int onsetAt = 0;
        const auto y = render (sc, mats[key], onsetAt);
        if (const char* dump = std::getenv ("SS_DUMP"))
        { if (FILE* fp = std::fopen (dump, "wb")) { std::fwrite (y.data(), sizeof (float), y.size(), fp); std::fclose (fp); } }
        const auto M = analyse (y, mats[key].sustained, onsetAt);
        o << sc.id << '\t' << modeName[sc.mode] << '\t' << kindName[sc.kind] << '\t' << M.hf << '\t' << M.worstDb << '\t' << M.d2 << '\t'
          << M.flux << '\t' << M.notch << '\t' << M.rmsDb << '\t' << M.centroid << '\t' << M.worstAbsDb << '\t' << M.d2WorstDb;
        for (double b : M.bands) o << '\t' << b;
        o << '\n'; o.flush();
        if (filter) std::printf ("%-40s HF %3d (worst %6.1f dB, %6.1f dBFS)  D2 %3d  FLUX %3d  NOTCH %3d  rms %6.1f  cent %6.0f\n",
                                 sc.id.c_str(), M.hf, M.worstDb, M.worstAbsDb, M.d2, M.flux, M.notch, M.rmsDb, M.centroid);
        ++done;
    }
    std::printf ("rendered %d scenarios -> %s  (%.0f s)\n", done, outPath,
                 std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    return 0;
}

struct Row { std::string id, mode, kind; int hf, d2, flux, notch; double worst, rms, cent, worstAbs, d2Worst; std::vector<double> bands; };
static std::map<std::string, Row> readRows (const char* p)
{
    std::map<std::string, Row> m; std::ifstream in (p); std::string line;
    while (std::getline (in, line))
    {
        std::stringstream ss (line); Row r;
        ss >> r.id >> r.mode >> r.kind >> r.hf >> r.worst >> r.d2 >> r.flux >> r.notch >> r.rms >> r.cent >> r.worstAbs >> r.d2Worst;
        double b; while (ss >> b) r.bands.push_back (b); m[r.id] = r;
    }
    return m;
}

static int compare (const char* a, const char* b, bool strict)
{
    auto A = readRows (a), B = readRows (b);
    struct Agg { int n = 0, hfA = 0, hfB = 0, d2A = 0, d2B = 0, flA = 0, flB = 0, noA = 0, noB = 0, sA = 0, sB = 0, nb = 0;
                 double wA = -200, wB = -200, absA = -200, absB = -200, maxBand = 0, sumBand = 0, maxCent = 0, maxRms = 0; };
    std::map<std::string, Agg> byMode, byKind, byRatio;
    std::vector<std::pair<double, std::string>> worstChar;
    for (auto& [id, ra] : A)
    {
        auto it = B.find (id); if (it == B.end()) continue; const Row& rb = it->second;
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
        const std::string ratio = id.substr (id.rfind ('/') + 1);
        for (auto* g : { &byMode[ra.mode], &byKind[ra.mode + "/" + ra.kind], &byRatio[ra.mode + "/" + ratio] })
        {
            g->n++; g->hfA += ra.hf; g->hfB += rb.hf; g->d2A += ra.d2; g->d2B += rb.d2; g->flA += ra.flux; g->flB += rb.flux;
            const bool tex = (ra.mode == "TEXTURE");
            g->noA += tex ? 0 : ra.notch; g->noB += tex ? 0 : rb.notch;
            g->sA += (ra.hf + ra.d2 > 0); g->sB += (rb.hf + rb.d2 > 0);
            g->wA = std::max (g->wA, ra.worst); g->wB = std::max (g->wB, rb.worst);
            g->absA = std::max ({ g->absA, ra.worstAbs, ra.d2Worst }); g->absB = std::max ({ g->absB, rb.worstAbs, rb.d2Worst });
            if (bothSounding) { g->maxBand = std::max (g->maxBand, mb); g->sumBand += mb; g->nb++; g->maxCent = std::max (g->maxCent, dc);
                               g->maxRms = std::max (g->maxRms, std::abs (rb.rms - ra.rms)); }
        }
    }
    auto pr = [] (const std::string& k, const Agg& g)
    { std::printf ("%-22s %4d | HF %5d -> %-5d | worst %6.1f -> %6.1f dB rel, %6.1f -> %6.1f dBFS | D2 %4d -> %-4d | FLUX %4d -> %-4d | NOTCH %4d -> %-4d | dirty %3d -> %-3d | LTAS max %5.2f mean %4.2f dB | cent %4.1f%% | rms %4.2f dB\n",
                   k.c_str(), g.n, g.hfA, g.hfB, g.wA, g.wB, g.absA, g.absB, g.d2A, g.d2B, g.flA, g.flB, g.noA, g.noB, g.sA, g.sB, g.maxBand, g.nb ? g.sumBand / g.nb : 0, g.maxCent, g.maxRms); };
    std::printf ("\n=== PER MODE (before -> after) ===\n");      for (auto& [k, g] : byMode) pr (k, g);
    std::printf ("\n=== PER MODE / KIND ===\n");                   for (auto& [k, g] : byKind) pr (k, g);
    std::printf ("\n=== PER MODE / RATIO ===\n");                  for (auto& [k, g] : byRatio) pr (k, g);
    // FIDELITY: each warped render's spectral shape vs the DRY render of the same material/note/unison, same build.
    auto shapeDiff = [] (const Row& x, const Row& ref)
    {
        const double tx = *std::max_element (x.bands.begin(), x.bands.end()), tr = *std::max_element (ref.bands.begin(), ref.bands.end());
        double s = 0; int n = 0;
        for (size_t i = 0; i < std::min (x.bands.size(), ref.bands.size()); ++i)
        { if (ref.bands[i] < tr - 20) continue; s += std::abs ((x.bands[i] - tx) - (ref.bands[i] - tr)); ++n; }
        return n ? s / n : 0.0;
    };
    auto refId = [] (const std::string& id)
    {   // MODE/mat/nN/uU/kind/rR  ->  DRY/mat/nN/uU/<loop|oneshot>/r1.00
        auto p1 = id.find ('/'), p4 = id.find ('/', id.find ('/', id.find ('/', p1 + 1) + 1) + 1), p5 = id.find ('/', p4 + 1);
        const std::string kind = id.substr (p4 + 1, p5 - p4 - 1);
        return "DRY" + id.substr (p1, p4 - p1) + "/" + (kind == "oneshot" ? "oneshot" : "loop") + "/r1.00";
    };
    std::map<std::string, std::array<double, 3>> fid;
    for (auto& [id, ra] : A)
    {
        if (ra.mode == "DRY") continue;
        auto rbI = B.find (id), refA = A.find (refId (id)), refB = B.find (refId (id));
        if (rbI == B.end() || refA == A.end() || refB == B.end()) continue;
        if (ra.rms < -80 || rbI->second.rms < -80) continue;
        auto& f = fid[ra.mode + "/" + ra.kind]; f[0] += shapeDiff (ra, refA->second); f[1] += shapeDiff (rbI->second, refB->second); f[2] += 1;
    }
    std::printf ("\n=== FIDELITY: mean |spectral-shape dB| vs the DRY render (lower = closer to the source) ===\n");
    for (auto& [k, f] : fid) std::printf ("  %-22s %3.0f scen   %5.2f dB -> %5.2f dB\n", k.c_str(), f[2], f[0] / f[2], f[1] / f[2]);
    std::sort (worstChar.rbegin(), worstChar.rend());
    std::printf ("\n=== largest LTAS moves (before -> after, same scenario) ===\n");
    for (int i = 0; i < 10 && i < (int) worstChar.size(); ++i) std::printf ("  %5.2f dB  %s\n", worstChar[(size_t) i].first, worstChar[(size_t) i].second.c_str());
    std::printf ("\n=== dirty scenarios remaining (after) ===\n");
    int remaining = 0, audible = 0;
    for (auto& [id, rb] : B) if (rb.hf + rb.d2 > 0)
    {
        remaining += rb.hf + rb.d2;
        const double lv = std::max (rb.worstAbs, rb.d2Worst);
        audible += lv >= -70.0;
        std::printf ("  %-40s HF %d (%.1f dBFS)  D2 %d (%.1f dBFS)\n", id.c_str(), rb.hf, rb.worstAbs, rb.d2, rb.d2Worst);
    }
    std::printf ("remaining events %d, rows with a leftover at or above -70 dBFS: %d\n", remaining, audible);
    if (strict && audible > 0) { std::printf ("\nSTRICT: %d rows carry an event at or above -70 dBFS\n", audible); return 1; }
    return 0;
}

static int selftest()
{
    int bad = 0;
    for (int w = 0; w < 4; ++w)
    {
        const auto m = makeMaterial (w, 60);
        std::vector<float> x;
        for (int r = 0; r < 3; ++r) x.insert (x.end(), m.L.begin(), m.L.end());   // 3 s (the file edges are faded)
        auto M = analyse (x, m.sustained, 0);
        std::printf ("  clean %-5s  HF %d  D2 %d  FLUX %d  NOTCH %d\n", m.name.c_str(), M.hf, M.d2, M.flux, M.notch);
        bad += (M.hf + M.d2) != 0;
        // −2 dB gain STEP, placed on the loudest sample of 1.00..1.35 s (a step at a zero crossing is only a slope kink)
        size_t at = (size_t) (1.0 * SR);
        for (size_t i = at; i < (size_t) (1.35 * SR); ++i) if (std::abs (x[i]) > std::abs (x[at])) at = i;
        auto s = x; for (size_t i = at; i < s.size(); ++i) s[i] *= 0.8f;
        auto M2 = analyse (s, m.sustained, 0);
        std::printf ("  step  %-5s  HF %d  D2 %d  FLUX %d\n", m.name.c_str(), M2.hf, M2.d2, M2.flux);
        bad += (M2.hf == 0);
    }
    std::printf ("selftest %s\n", bad ? "FAILED" : "ok");
    return bad ? 1 : 0;
}

// CPU per warped voice: an 8-note chord of the Sample osc, ratio 2, held; processBlock time per block,
// warped minus the same chord DRY, divided by the notes = one warped voice's share of one core.
static int cpu()
{
    const auto m = makeMaterial (0, 60);
    auto run = [&] (int mode, float knob, float formant = 0.f) -> double
    {
        Proc P; auto& p = *P.p;
        configure (p, m, mode, 1, true, knob);
        if (formant != 0.f) setP (p, ParameterIDs::SYN_OSC_A_SAMPLE_FORMANT, formant);
        for (int t = 0; t < 3; ++t) p.timerCallback();
        juce::AudioBuffer<float> b (2, BLK);
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep)
        {
            double acc = 0; int n = 0;
            for (int blk = 0; blk < 700; ++blk)
            {
                b.clear(); juce::MidiBuffer mb;
                if (blk == 0) for (int k = 0; k < 8; ++k) mb.addEvent (juce::MidiMessage::noteOn (1, 48 + 3 * k, (juce::uint8) 100), 0);
                if (blk == 690) for (int k = 0; k < 8; ++k) mb.addEvent (juce::MidiMessage::noteOff (1, 48 + 3 * k), 0);
                const auto t0 = std::chrono::steady_clock::now();
                p.processBlock (b, mb);
                const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                if (blk >= 40 && blk < 680) { acc += dt; ++n; }
            }
            for (int blk = 0; blk < 200; ++blk) { b.clear(); juce::MidiBuffer mb; p.processBlock (b, mb); }
            best = std::min (best, acc / n);
        }
        return best;
    };
    const double blkSec = BLK / SR;
    const double dry = run (0, 0.f);
    std::printf ("CPU  8 voices DRY           %.4f ms/block (%.2f %% of a core)\n", dry * 1e3, dry / blkSec * 100);
    {
        const double w = run (1, 0.f, 0.3f);
        std::printf ("CPU  8 voices TONES    r1.00 + formant  %.4f ms/block (%.2f %% of a core)  ->  %.3f %% of a core PER WARPED VOICE\n",
                     w * 1e3, w / blkSec * 100, (w - dry) / 8.0 / blkSec * 100);
    }
    for (int mode = 1; mode < 4; ++mode)
    {
        const double w = run (mode, knobFor (2.0f));
        std::printf ("CPU  8 voices %-8s r2.00  %.4f ms/block (%.2f %% of a core)  ->  %.3f %% of a core PER WARPED VOICE\n",
                     modeName[mode], w * 1e3, w / blkSec * 100, (w - dry) / 8.0 / blkSec * 100);
    }
    return 0;
}

int main (int argc, char** argv)
{
    setenv ("TERRAIN_DETERMINISTIC", "1", 1);
    juce::ScopedJuceInitialiser_GUI init;
    juce::ScopedNoDenormals noDenormals;
    if (argc >= 2 && std::strcmp (argv[1], "selftest") == 0) return selftest();
    if (argc >= 2 && std::strcmp (argv[1], "cpu") == 0) return cpu();
    if (argc >= 3 && std::strcmp (argv[1], "run") == 0) return runAll (argv[2], argc >= 4 ? argv[3] : nullptr);
    if (argc >= 4 && std::strcmp (argv[1], "compare") == 0) return compare (argv[2], argv[3], argc >= 5);
    std::printf ("usage: %s selftest | cpu | run out.tsv [idFilter] | compare before.tsv after.tsv [strict]\n", argv[0]);
    return 2;
}
