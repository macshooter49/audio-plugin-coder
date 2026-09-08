# Terrain Granular Oscillator Engine — Implementation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans to implement task-by-task. Steps use `- [ ]` checkboxes. This project's proof loop is the **offline g++ harness** (Pattern A), not CTest.

**Goal:** Add a third live oscillator engine — **Granular** (`Engine::GRAN=2`) — that granulates the same loaded sample buffer the Sample engine uses, reusing the entire sample UI shell with only the bottom control row, DSP, and follower changed.

**Architecture:** New header-only `tw::GranularEngine` mirroring `tw::SampleEngine`'s contract (borrowed `const float* const*` buffer view, zero-alloc `tick()`, `prepare`/`setSample`/`noteOn`/`noteOff`, no `reset()`). Per-voice instances live in `SynthVoice` beside the sample engines; a new `SYN_OSC_*_GRAIN_*` param block feeds them through the existing 6-link WebView bind chain. The DSP is a fixed 64-grain pool with an async, jitter-scheduled spawner and a master read-head (`Scan`) decoupled from grain pitch.

**Tech Stack:** C++17, JUCE 8 (APVTS + WebView relays), header-only DSP, WebView2/WKWebView UI (`index.html`), CMake "Unix Makefiles" (arm64, macOS).

**Reference:** Scope/design doc `plugins/TerrainInstrument/.ideas/granular-engine-v1-design.md`. Serum-2 ground-truth research in `.ideas/` (calibration only — we copy neither names nor code).

## Global Constraints

*(Every task implicitly includes these.)*

- **All-original DSP + naming.** No boilerplate granular library, no copied labels. Reworked math grounded in research.
- **UI PARITY IS LAW (Max, hard rule).** The granular view is a *literal clone* of the sample view: identical font, colors, knob style (white round-cap SVG arc), sizes, spacing, and chrome. Change **labels and `data-syn` wiring only**. The **ONLY** new visual anywhere is the grain-dot scatter follower (thin-white/purple grains) replacing the single playhead line. No new panels, no new visual ideas. **The 6 knobs must fit the existing `.samp-knobs` footprint at the existing knob size — the space does not grow to fit knobs; knobs conform to the space.**
- **Mirror `tw::SampleEngine`:** header-only, `namespace tw`, zero-alloc `tick()`, borrowed buffer view, no `reset()` (lifecycle = `prepare()` + `noteOn`/`noteOff` + `clearSample`). Reuse `hermite()` interpolation verbatim.
- **6-link bind chain per param** (miss (b)/(c)/(d) → silent no-op, builds clean): (a) `ParameterIDs.hpp` const, (b) `WebSliderRelay` + `WebSliderParameterAttachment` members in `PluginEditor.h`, (c) `.withOptionsFrom(relay)` in the Options chain, (d) `mkAtt(...)` construction, (e) generic JS `[data-syn]` loop — **free, no per-param JS**, (f) `data-syn="…"` on the knob `<div>`.
- **AudioParameterChoice:** `getRawParameterValue()` returns **normalised [0,1], not the index** — convert with `* (numChoices-1)` + round.
- **Test harness (Pattern A):** standalone `int main()` file with a documented g++ one-liner in its header comment; **do NOT add `*_test.cpp` to `CMakeLists.txt target_sources`** (duplicate `main`). Command (from the plugin dir):
  `c++ -std=c++17 -Wall -Wextra -ISource Source/GranularEngine_test.cpp -o /tmp/ge && /tmp/ge`
- **Build both VST3 AND AU every time.** On any `index.html` change, bust the WebUI BinaryData cache first, then verify the new HTML embedded via `strings | grep -c`. Full sequence in §Build.
- **Commits happen at Max's "checkpoint," not per-task.** Each task's deliverable is a green harness or a verified build; batch the commit at checkpoint.

---

# PHASE 1 — DSP core (`GranularEngine.h`), proven offline

Deliverable: a header-only `tw::GranularEngine` that passes an offline harness proving no-clicks, correct freeze/reverse, RMS stability, and correct spawn behavior. No plugin wiring yet.

### Task 1: Engine skeleton + params struct + harness scaffold

**Files:**
- Create: `Source/GranularEngine.h`
- Create: `Source/GranularEngine_test.cpp`

**Interfaces:**
- Produces:
  - `struct tw::GrainViz { float pos01; float age01; float pan; };`
  - `struct tw::GranularEngineParams { float scan=0.15f, position=0.f, density=0.4f, size=0.25f, spray=0.10f, shape=0.5f, skew=0.f, pitch=0.f, pitchSpray=0.f, width=0.f, dir=1.f, life=0.15f, jump=0.5f; int key=0; };`
  - `class tw::GranularEngine` with:
    `void prepare(double outputSampleRate) noexcept;`
    `void setSample(const float* const* ch,int numCh,int numSamples,double nativeRate) noexcept;`
    `void clearSample() noexcept; bool hasSample() const noexcept;`
    `void setRegion(float start01,float end01) noexcept;`
    `void setParams(const GranularEngineParams& p) noexcept;`
    `void setPitchRatio(double r) noexcept;`
    `void noteOn(double pitchRatio,uint32_t seed) noexcept; void noteOff() noexcept;`
    `bool isActive() const noexcept;`
    `bool tick(float& outL,float& outR) noexcept;`
    `int cloudSnapshot(GrainViz* out,int maxOut) const noexcept;`
    `float scanPos01() const noexcept;`

- [ ] **Step 1: Write the failing test** — `Source/GranularEngine_test.cpp`

```cpp
//  Offline harness — build+run from the plugin dir:
//  c++ -std=c++17 -Wall -Wextra -ISource Source/GranularEngine_test.cpp -o /tmp/ge && /tmp/ge
#include "GranularEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>
using tw::GranularEngine;
using tw::GranularEngineParams;

static int g_checks = 0, g_fail = 0;
static void check (bool ok, const char* what)
{ ++g_checks; if (! ok) { ++g_fail; std::printf ("  FAIL: %s\n", what); } }

// A 1-second stereo ramp buffer at 48k for tests.
static std::vector<float> g_l, g_r;
static void makeBuffer (int n) {
    g_l.resize (n); g_r.resize (n);
    for (int i = 0; i < n; ++i) { g_l[i] = std::sin (6.2831853f * 220.f * i / 48000.f); g_r[i] = g_l[i]; }
}

int main() {
    makeBuffer (48000);
    const float* chans[2] = { g_l.data(), g_r.data() };

    GranularEngine ge;
    ge.prepare (48000.0);
    check (! ge.hasSample(), "no sample before setSample");
    ge.setSample (chans, 2, 48000, 48000.0);
    check (ge.hasSample(), "hasSample after setSample");
    check (! ge.isActive(), "inactive before noteOn");

    std::printf ("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `c++ -std=c++17 -Wall -Wextra -ISource Source/GranularEngine_test.cpp -o /tmp/ge && /tmp/ge`
Expected: FAIL to compile — `GranularEngine.h` not found / class undefined.

- [ ] **Step 3: Write the minimal skeleton** — `Source/GranularEngine.h`

```cpp
#pragma once
#include <cstdint>
#include <cmath>
#include <array>

namespace tw {

struct GrainViz { float pos01 = 0.f; float age01 = 0.f; float pan = 0.f; };

struct GranularEngineParams {
    float scan = 0.15f, position = 0.f, density = 0.4f, size = 0.25f, spray = 0.10f,
          shape = 0.5f, skew = 0.f, pitch = 0.f, pitchSpray = 0.f, width = 0.f,
          dir = 1.f, life = 0.15f, jump = 0.5f;
    int   key = 0;   // 0=Off 1=Oct 2=5th 3=Chord 4=Maj 5=Min 6=Penta
};

class GranularEngine {
public:
    GranularEngine() = default;

    void prepare (double outputSampleRate) noexcept { outRate_ = outputSampleRate; buildWindows(); }
    void setSample (const float* const* ch, int numCh, int numSamples, double nativeRate) noexcept {
        ch0_ = numCh > 0 ? ch[0] : nullptr;
        ch1_ = numCh > 1 ? ch[1] : ch0_;
        numSamples_ = numSamples; nativeRate_ = nativeRate;
    }
    void clearSample() noexcept { ch0_ = ch1_ = nullptr; numSamples_ = 0; }
    bool hasSample() const noexcept { return ch0_ != nullptr && numSamples_ > 1; }

    void setRegion (float s, float e) noexcept { regStart01_ = s; regEnd01_ = e; }
    void setParams (const GranularEngineParams& p) noexcept { p_ = p; }
    void setPitchRatio (double r) noexcept { basePitch_ = r; }

    void noteOn (double pitchRatio, uint32_t seed) noexcept {
        basePitch_ = pitchRatio; rng_ = seed ? seed : 0x9E3779B9u;
        for (auto& g : pool_) g.active = false;
        scanPos_ = regStart01_ + p_.position * (regEnd01_ - regStart01_);
        countdown_ = 0.0; active_ = true; releasing_ = false;
    }
    void noteOff() noexcept { releasing_ = true; }
    bool isActive() const noexcept { return active_; }
    float scanPos01() const noexcept { return (float) scanPos_; }

    bool tick (float& outL, float& outR) noexcept { outL = outR = 0.f; return active_; }
    int cloudSnapshot (GrainViz*, int) const noexcept { return 0; }

private:
    struct Grain { double readPos=0, readInc=0; int age=0, len=0; float gain=0, panL=0, panR=0;
                   const float* win=nullptr; bool active=false; };

    void buildWindows() noexcept {}      // Task 2

    static constexpr int kPool = 64;
    std::array<Grain, kPool> pool_ {};
    const float* ch0_ = nullptr; const float* ch1_ = nullptr;
    int numSamples_ = 0; double nativeRate_ = 48000.0, outRate_ = 48000.0;
    double basePitch_ = 1.0, scanPos_ = 0.0, countdown_ = 0.0;
    float regStart01_ = 0.f, regEnd01_ = 1.f;
    uint32_t rng_ = 0x9E3779B9u;
    bool active_ = false, releasing_ = false;
    GranularEngineParams p_ {};

    inline float nextRand01() noexcept { rng_ ^= rng_<<13; rng_ ^= rng_>>17; rng_ ^= rng_<<5;
        return (float)((rng_ & 0x00FFFFFFu) / (double) 0x01000000); }
};

} // namespace tw
```

- [ ] **Step 4: Run to verify it passes**

Run: `c++ -std=c++17 -Wall -Wextra -ISource Source/GranularEngine_test.cpp -o /tmp/ge && /tmp/ge`
Expected: `3 checks, 0 failed`.

---

### Task 2: Window LUTs (Tukey↔Gaussian morph + skew)

**Files:** Modify `Source/GranularEngine.h` (`buildWindows`, add LUT storage + `windowLookup`), `Source/GranularEngine_test.cpp`.

**Interfaces:**
- Produces: `float windowAt(float shape01,float skew,float phase01) const noexcept;` — amplitude in [0,1], `phase01` across the grain (0=start,1=end).

- [ ] **Step 1: Write the failing test** (append to `main`, before the summary print)

```cpp
{
    GranularEngine w; w.prepare (48000.0);
    // Endpoints ~0, center ~1 for both extremes; monotonic rise to center.
    check (w.windowAt (0.f, 0.f, 0.0f) < 0.02f, "tukey start ~0");
    check (w.windowAt (0.f, 0.f, 0.5f) > 0.98f, "tukey center ~1");
    check (w.windowAt (1.f, 0.f, 0.0f) < 0.02f, "gauss start ~0");
    check (w.windowAt (1.f, 0.f, 0.5f) > 0.90f, "gauss center high");
    // Skew moves the peak: +skew peaks later, -skew earlier.
    float pkPos = 0.f, pkVal = -1.f;
    for (int i = 0; i <= 100; ++i) { float ph=i/100.f; float v=w.windowAt(0.5f, 0.6f, ph); if(v>pkVal){pkVal=v;pkPos=ph;} }
    check (pkPos > 0.5f, "positive skew peaks after center");
}
```

- [ ] **Step 2: Run to verify it fails** — `windowAt` undefined → compile error.

- [ ] **Step 3: Implement** — replace `buildWindows()`/add `windowAt`:

```cpp
static constexpr int kWin = 2048;
std::array<float, kWin> tukey_ {}, gauss_ {};

void buildWindows() noexcept {
    const float alpha = 0.5f; // Tukey taper fraction
    for (int i = 0; i < kWin; ++i) {
        float x = (float) i / (kWin - 1);
        // Tukey (tapered cosine): flat middle, cosine tapers over alpha/2 each edge.
        float t;
        if (x < alpha*0.5f)        t = 0.5f*(1.f+std::cos(3.14159265f*(2.f*x/alpha-1.f)));
        else if (x > 1.f-alpha*0.5f) t = 0.5f*(1.f+std::cos(3.14159265f*(2.f*x/alpha-2.f/alpha+1.f)));
        else                        t = 1.f;
        tukey_[i] = t;
        // Gaussian bell centered at 0.5.
        float d = (x-0.5f)/0.20f;
        gauss_[i] = std::exp (-0.5f*d*d);
    }
}

float windowAt (float shape01, float skew, float phase01) const noexcept {
    // Skew warps phase before lookup: +skew pushes the peak later.
    float ph = phase01;
    if (skew != 0.f) { float k = 1.f + (skew<0? skew*0.6f : skew*1.5f); ph = std::pow (phase01, k>0.05f?k:0.05f); }
    ph = ph < 0.f ? 0.f : (ph > 1.f ? 1.f : ph);
    int idx = (int) (ph * (kWin - 1));
    float a = tukey_[idx], b = gauss_[idx];
    return a + (b - a) * shape01;
}
```

- [ ] **Step 4: Run to verify it passes** — expect all window checks green.

---

### Task 3: Async spawn scheduler (density → rate + regularity jitter)

**Files:** Modify `Source/GranularEngine.h` (add `spawnGrain`, scheduler in a new `advanceScheduler(int n)` helper or inside `tick`), `Source/GranularEngine_test.cpp`.

**Interfaces:**
- Produces: internal `float densityHz() const` mapping `p_.density` 0..1 → 1..200 g/s (log); `float grainLenSamples() const` mapping `p_.size` 0..1 → 2..500 ms (log). Grains spawn into `pool_`.

- [ ] **Step 1: Write the failing test**

```cpp
{
    GranularEngine s; s.prepare (48000.0);
    static std::vector<float> l(48000), r(48000);
    for (int i=0;i<48000;++i){ l[i]=std::sin(6.2831853f*220.f*i/48000.f); r[i]=l[i]; }
    const float* ch[2]={l.data(),r.data()};
    s.setSample (ch,2,48000,48000.0);
    GranularEngineParams p; p.density=0.5f; p.size=0.25f; p.scan=0.15f; p.spray=0.f; s.setParams (p);
    s.noteOn (1.0, 0xBEEF);
    // Count spawns over 1 second by sampling active-count deltas is hard; instead expose a spawn counter for testing.
    float ol, orr; long spawns=0; int prevActive=0;
    for (int i=0;i<48000;++i){ s.tick(ol,orr); int a=s.activeGrainsForTesting(); if(a>prevActive) spawns+=(a-prevActive); prevActive=a; }
    // density 0.5 (log 1..200) ≈ ~14 g/s; accept a wide band (scheduler jitter + retirement).
    check (spawns > 5 && spawns < 60, "spawn count in plausible band for density=0.5");
}
```

- [ ] **Step 2: Run to verify it fails** — `activeGrainsForTesting` undefined.

- [ ] **Step 3: Implement** the scheduler + testing accessor + range maps:

```cpp
int activeGrainsForTesting() const noexcept { int n=0; for (auto& g:pool_) n+=g.active?1:0; return n; }

float densityHz() const noexcept { return 1.f * std::pow (200.f, p_.density); }          // 1..200 g/s log
float grainLenSec() const noexcept { return 0.002f * std::pow (250.f, p_.size); }         // 2..500 ms log

void spawnGrain() noexcept {
    int slot = -1; for (int i=0;i<kPool;++i) if(!pool_[i].active){slot=i;break;}
    if (slot < 0) return;                                   // Max-Grains reached → skip-and-wait
    Grain& g = pool_[slot];
    double birth01 = scanPos_ + p_.spray * (nextRand01()*2.f-1.f) * 0.25f;
    birth01 = birth01 < regStart01_ ? regStart01_ : (birth01 > regEnd01_ ? regEnd01_ : birth01);
    g.readPos = birth01 * (numSamples_ - 1);
    double pr = basePitch_ * std::pow (2.0, (p_.pitch)/12.0);   // Task 6 adds pitch spray + key
    g.readInc = pr * (p_.dir >= 0 ? 1.0 : -1.0);
    g.len = (int) (grainLenSec() * outRate_); if (g.len < 4) g.len = 4;
    g.age = 0; g.gain = 1.f; g.panL = g.panR = 0.70710678f; g.active = true;
}
```

Add to `tick()` the scheduler step (replace the stub body):

```cpp
bool tick (float& outL, float& outR) noexcept {
    outL = outR = 0.f;
    if (! active_ || ! hasSample()) return active_;
    // schedule
    countdown_ -= 1.0;
    while (countdown_ <= 0.0) {
        if (! releasing_) spawnGrain();
        double interval = outRate_ / densityHz();
        double jit = 0.5 * interval * (nextRand01()*2.f-1.f);   // regularity jitter (async cloud)
        countdown_ += interval + jit; if (countdown_ < 1.0) countdown_ = 1.0;
    }
    // advance scan head (Task 5 refines freeze/reverse/wrap)
    scanPos_ += (double) p_.scan * 2.0 / outRate_;
    if (scanPos_ > regEnd01_) scanPos_ = regStart01_; if (scanPos_ < regStart01_) scanPos_ = regEnd01_;
    // mix (Task 4)
    return active_;
}
```

- [ ] **Step 4: Run to verify it passes** — spawn-band check green.

---

### Task 4: Grain read + windowed mix + retire (anti-click + RMS-stable)

**Files:** Modify `Source/GranularEngine.h` (grain mix loop in `tick`, add `hermite`/`readHermite`), `Source/GranularEngine_test.cpp`.

**Interfaces:**
- Consumes: `windowAt` (Task 2), `spawnGrain`/pool (Task 3).
- Produces: `tick` now sums active grains and retires them; reuse of `hermite()`.

- [ ] **Step 1: Write the failing test** (no-click + finite + RMS band)

```cpp
{
    GranularEngine m; m.prepare (48000.0);
    static std::vector<float> l(48000), r(48000);
    for (int i=0;i<48000;++i){ l[i]=std::sin(6.2831853f*220.f*i/48000.f); r[i]=l[i]; }
    const float* ch[2]={l.data(),r.data()};
    m.setSample (ch,2,48000,48000.0);
    GranularEngineParams p; p.density=0.6f; p.size=0.25f; p.shape=0.5f; m.setParams (p);
    m.noteOn (1.0, 0x1234);
    float prev=0.f, maxJump=0.f; double sumSq=0; int N=48000;
    for (int i=0;i<N;++i){ float ol,orr; m.tick(ol,orr); check(std::isfinite(ol),"finite"); float d=std::fabs(ol-prev); if(d>maxJump)maxJump=d; prev=ol; sumSq+=ol*ol; }
    check (maxJump < 0.25f, "no click: bounded inter-sample delta");   // windowed grains → smooth
    double rms = std::sqrt (sumSq/N);
    check (rms > 0.02 && rms < 1.0, "RMS in audible-stable band");
}
```

- [ ] **Step 2: Run to verify it fails** — `tick` still outputs silence → RMS check fails.

- [ ] **Step 3: Implement** the mix loop (insert into `tick` before `return active_;`) + hermite:

```cpp
// ── grain mix ──
float accL=0.f, accR=0.f; bool any=false;
for (auto& g : pool_) {
    if (! g.active) continue;
    any = true;
    float ph = (float) g.age / (float) g.len;
    float w  = windowAt (p_.shape, p_.skew, ph);
    float sl, sr; readHermite (g.readPos, sl, sr);
    accL += w * g.gain * g.panL * sl;
    accR += w * g.gain * g.panR * sr;
    g.readPos += g.readInc; g.age++;
    // wrap read within region so long grains don't run off the buffer:
    double lo = regStart01_*(numSamples_-1), hi = regEnd01_*(numSamples_-1);
    if (g.readPos > hi) g.readPos = lo + (g.readPos-hi);
    if (g.readPos < lo) g.readPos = hi - (lo-g.readPos);
    if (g.age >= g.len) g.active = false;                 // retire at window→0, no click
}
// gentle RMS tame vs grain count (like the sample unison path)
outL = accL; outR = accR;
if (releasing_ && ! any) active_ = false;                 // fully silent after release → free
```

Add the interpolators (reuse `SampleEngine`'s `hermite` verbatim):

```cpp
inline float samp0 (int i) const noexcept { i = i<0?0:(i>=numSamples_?numSamples_-1:i); return ch0_[i]; }
inline float samp1 (int i) const noexcept { i = i<0?0:(i>=numSamples_?numSamples_-1:i); return ch1_[i]; }
static inline float hermite (float xm1,float x0,float x1,float x2,float t) noexcept {
    const float c=(x1-xm1)*0.5f, v=x0-x1, w=c+v, a=w+v+(x2-x0)*0.5f, bn=w+a;
    return ((((a*t)-bn)*t+c)*t+x0);
}
void readHermite (double p,float& l,float& r) const noexcept {
    int i1=(int)std::floor(p); float f=(float)(p-i1);
    l=hermite(samp0(i1-1),samp0(i1),samp0(i1+1),samp0(i1+2),f);
    r=hermite(samp1(i1-1),samp1(i1),samp1(i1+1),samp1(i1+2),f);
}
```

- [ ] **Step 4: Run to verify it passes** — no-click + RMS + finite green.

---

### Task 5: Scan decoupling — freeze / reverse / position

**Files:** Modify `Source/GranularEngine.h` (scan-advance already stubbed in Task 3 `tick`; refine + honor `position` on freeze), `Source/GranularEngine_test.cpp`.

- [ ] **Step 1: Write the failing test**

```cpp
{
    GranularEngine f; f.prepare (48000.0);
    static std::vector<float> l(48000,0.5f), r(48000,0.5f); const float* ch[2]={l.data(),r.data()};
    f.setSample (ch,2,48000,48000.0);
    GranularEngineParams p; p.scan=0.f; p.position=0.3f; f.setParams (p); f.noteOn (1.0, 7);
    float before=f.scanPos01(); for(int i=0;i<4800;++i){float a,b;f.tick(a,b);} float after=f.scanPos01();
    check (std::fabs(after-before) < 1e-4f, "scan=0 freezes the read-head");
    check (std::fabs(before-0.3f) < 1e-3f, "frozen head sits at position=0.3");
    // reverse: negative scan moves head down
    GranularEngineParams p2=p; p2.scan=-0.5f; p2.position=0.8f; f.setParams(p2); f.noteOn(1.0,7);
    float b0=f.scanPos01(); for(int i=0;i<2400;++i){float a,b;f.tick(a,b);} check (f.scanPos01() < b0, "scan<0 moves head backward");
}
```

- [ ] **Step 2: Run to verify it fails** — frozen head currently starts at `regStart + position*range` (OK) but Task 3's wrap may nudge it; confirm failure/pass and adjust.

- [ ] **Step 3: Implement** — ensure `noteOn` sets `scanPos_ = regStart01_ + p_.position*(regEnd01_-regStart01_)` (already), and in `tick` guard freeze exactly:

```cpp
if (p_.scan != 0.f) {
    scanPos_ += (double) p_.scan * 2.0 / outRate_ * (regEnd01_-regStart01_>0?(regEnd01_-regStart01_):1.f);
    if (scanPos_ > regEnd01_) scanPos_ = regStart01_ + (scanPos_-regEnd01_);
    if (scanPos_ < regStart01_) scanPos_ = regEnd01_ - (regStart01_-scanPos_);
}
// scan==0 → frozen; grains keep spawning from scanPos_ (the living freeze)
```

- [ ] **Step 4: Run to verify it passes** — freeze/reverse/position green.

---

### Task 6: Per-grain pitch, pitch-spray, direction bias

**Files:** Modify `Source/GranularEngine.h` (`spawnGrain` pitch/dir), `Source/GranularEngine_test.cpp`.

- [ ] **Step 1: Write the failing test**

```cpp
{
    GranularEngine g; g.prepare (48000.0);
    static std::vector<float> l(48000), r(48000);
    for(int i=0;i<48000;++i){l[i]=std::sin(6.2831853f*220.f*i/48000.f);r[i]=l[i];}
    const float* ch[2]={l.data(),r.data()}; g.setSample(ch,2,48000,48000.0);
    GranularEngineParams p; p.pitch=12.f; p.density=0.6f; g.setParams(p); g.noteOn(1.0,42);
    // +12 semis → grains read ~2× speed → higher output centroid; smoke-check it stays finite + audible.
    double sumSq=0; for(int i=0;i<24000;++i){float a,b;g.tick(a,b);sumSq+=a*a;} check(std::sqrt(sumSq/24000)>0.01,"pitched cloud audible");
    // direction -1 → all grains reverse: readInc negative on spawn
    GranularEngineParams p2; p2.dir=-1.f; p2.density=0.6f; g.setParams(p2); g.noteOn(1.0,42);
    for(int i=0;i<2000;++i){float a,b;g.tick(a,b);} check(g.anyGrainReversedForTesting(),"dir=-1 spawns reversed grains");
}
```

- [ ] **Step 2: Run to verify it fails** — `anyGrainReversedForTesting` undefined.

- [ ] **Step 3: Implement** pitch-spray + direction in `spawnGrain` and the accessor:

```cpp
bool anyGrainReversedForTesting() const noexcept { for(auto& g:pool_) if(g.active&&g.readInc<0)return true; return false; }
```
In `spawnGrain`, replace the pitch/dir lines:
```cpp
double semis = p_.pitch + p_.pitchSpray * (nextRand01()*2.f-1.f) * 12.0;   // ±12 st spray
double pr = basePitch_ * std::pow (2.0, semis/12.0);
float dsel = p_.dir + (nextRand01()*2.f-1.f)*0.0f;          // dir bias; 0 = random handled below
bool rev = (p_.dir <= -1.f) ? true : (p_.dir >= 1.f ? false : (nextRand01() > (p_.dir*0.5f+0.5f)));
g.readInc = pr * (rev ? -1.0 : 1.0);
if (rev) g.readPos = /*start at grain end so it reads backward in-region*/ g.readPos;
```

- [ ] **Step 4: Run to verify it passes** — pitch + direction green.

---

### Task 7: Follower snapshot + lifecycle finalize

**Files:** Modify `Source/GranularEngine.h` (`cloudSnapshot`, birth ring, `noteOff` fade), `Source/GranularEngine_test.cpp`.

**Interfaces:**
- Produces: `cloudSnapshot(GrainViz* out,int maxOut)` fills up to `maxOut` recent births (pos01, age01, pan); returns count. `scanPos01()` = marker.

- [ ] **Step 1: Write the failing test**

```cpp
{
    GranularEngine v; v.prepare (48000.0);
    static std::vector<float> l(48000,0.4f), r(48000,0.4f); const float* ch[2]={l.data(),r.data()};
    v.setSample(ch,2,48000,48000.0);
    GranularEngineParams p; p.density=0.7f; v.setParams(p); v.noteOn(1.0,9);
    for(int i=0;i<4800;++i){float a,b;v.tick(a,b);}
    tw::GrainViz buf[16]; int n=v.cloudSnapshot(buf,16);
    check (n > 0, "cloud snapshot returns active grains");
    check (buf[0].pos01>=0.f && buf[0].pos01<=1.f, "grain pos01 in range");
    v.noteOff(); bool wentInactive=false; for(int i=0;i<96000;++i){float a,b; if(!v.tick(a,b)){wentInactive=true;break;}}
    check (wentInactive, "voice frees after release when all grains retire");
}
```

- [ ] **Step 2: Run to verify it fails** — `cloudSnapshot` returns 0.

- [ ] **Step 3: Implement** snapshot from the live pool:

```cpp
int cloudSnapshot (GrainViz* out, int maxOut) const noexcept {
    int n=0; for (auto& g : pool_) { if(!g.active) continue; if(n>=maxOut) break;
        out[n].pos01 = (float)(g.readPos/(numSamples_>1?numSamples_-1:1));
        out[n].age01 = (float) g.age/(float)(g.len>0?g.len:1);
        out[n].pan   = g.panR - g.panL; ++n; }
    return n;
}
```

- [ ] **Step 4: Run to verify it passes** — snapshot + lifecycle green. **Phase 1 deliverable met: harness fully green.**

---

# PHASE 2 — Integration to audible (params + voice wiring + bind)

Deliverable: selecting **Granular** in the existing osc dropdown renders granular audio at defaults (slow drift), no crash; the 6 primary params exist in APVTS and are host-automatable. (Custom UI is Phase 3 — at this stage the knobs aren't on-screen yet, but the engine sounds.)

### Task 8: Per-voice GranularEngine instances + setters in `SynthVoice`

**Files:** Modify `Source/SynthVoice.h` (include `GranularEngine.h`; add arrays + setters near `:428-446` and `:3041`).

- [ ] Add `#include "GranularEngine.h"`.
- [ ] Beside `sampleEngA_..D_`, add `std::array<tw::GranularEngine, kMaxUnison> granEngA_, granEngB_, granEngC_, granEngD_;`
- [ ] Add per-osc `tw::GranularEngineParams granParamsA_..D_;` + setters `void setGranParamsA(const tw::GranularEngineParams& p) noexcept { granParamsA_=p; }` (B/C/D likewise).
- [ ] In `prepare`, call `prepare(outputRate)` on all granular engines wherever the sample engines are prepared.
- [ ] Reuse the *same* `sampleSource_[]` buffers for granular (no new buffer plumbing) — granular reads the loaded sample.
- [ ] **Verify:** full plugin build compiles (see §Build, C++-only path). Deliverable: compiles clean, no behavior change yet.

### Task 9: Render path + engine-switch wiring

**Files:** Modify `Source/SynthVoice.h` (`renderGranularBlocks` mirroring `renderSampleBlocks` ~`:3220-3251`; replace the `case Engine::GRAN` silent stubs at `:1504,1739,1970,+D` to pull from the granular block).

- [ ] Add `renderGranularBlocks(int numSamples)`: for each osc whose engine == GRAN, for each active unison voice, `setParams` + `setRegion` + `tick` into the per-osc audio accumulation buffer, RMS-normalized like `renderSampleBlocks`.
- [ ] At each `case Engine::GRAN:` swap point (currently zeroing `sAu`), route the granular block output into `sAu` exactly as the sample case routes `renderSampleBlocks`.
- [ ] **Verify:** build both formats; load a sample on osc A; switch engine to Granular; confirm audio renders (granular texture), no crash. Deliverable: **audible granular at default params.**

### Task 10: Register `SYN_OSC_*_GRAIN_*` params (primary 6 × 4 osc)

**Files:** Modify `Source/ParameterIDs.hpp` (new block after the sample IDs ~`:478`), `Source/PluginProcessor.cpp` (`createParameterLayout`, after the sample block ~`:2075`).

- [ ] Add IDs (per osc A–D): `SYN_OSC_A_GRAIN_SCAN`, `_DENSITY`, `_SIZE`, `_SPRAY`, `_SHAPE`, `_KEY` (primary 6). Example const:
  `constexpr char SYN_OSC_A_GRAIN_SCAN[] = "SYN_OSC_A_GRAIN_SCAN";`
- [ ] Register each (mirror `PluginProcessor.cpp:1829-1832`). Scan bipolar, slow-drift default:
```cpp
layout.add (std::make_unique<juce::AudioParameterFloat> (
    juce::ParameterID { ParameterIDs::SYN_OSC_A_GRAIN_SCAN, 1 },
    "Synth OSC A Grain Scan",
    juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.15f));
```
  Density/Size/Spray/Shape unipolar `(0,1,0.001f)` with defaults `0.4/0.25/0.10/0.5`. `_KEY` = `AudioParameterChoice({"Off","Oct","5th","Chord","Maj","Min","Penta"}, 0)`.
- [ ] **Verify:** build; params appear in the host's parameter list. Deliverable: params exist.

### Task 11: Processor gather + push each block

**Files:** Modify `Source/PluginProcessor.cpp` (gather beside `SampleEngineParams spA..spD` ~`:3051-3095`; push beside `setSampleParamsA..D` ~`:3243-3246`).

- [ ] Build a `tw::GranularEngineParams` per osc from `*apvts.getRawParameterValue(...)`. Choice→index: `int key = (int) std::round (*getRaw(SYN_OSC_A_GRAIN_KEY) * 6.f);`
- [ ] `voice.setGranParamsA(gpA); …D`.
- [ ] **Verify:** build both formats; automate `SYN_OSC_A_GRAIN_DENSITY` in the host → audibly changes the cloud. Deliverable: **params drive the engine.**

### Task 12: Bind chain for the 6 primary params × 4 osc

**Files:** Modify `Source/PluginEditor.h` (relay + attachment members), `Source/PluginEditor.cpp` (`.withOptionsFrom` chain ~`:291`; `mkAtt` block ~`:2200-2212`).

- [ ] For each of the 24 params add: (b) `juce::WebSliderRelay synOscAGrainScanRelay { ParameterIDs::SYN_OSC_A_GRAIN_SCAN };` + `std::unique_ptr<juce::WebSliderParameterAttachment> synOscAGrainScanAttachment;`
- [ ] (c) `.withOptionsFrom(synOscAGrainScanRelay)` in the Options chain.
- [ ] (d) `mkAtt(synOscAGrainScanAttachment, ParameterIDs::SYN_OSC_A_GRAIN_SCAN, synOscAGrainScanRelay);`
- [ ] (e) JS is generic — nothing to add.
- [ ] **Verify:** deferred until Phase 3 places the `data-syn` knobs; for now confirm build is clean and no relay is missing (a missing relay throws no error — audit the list of 24 against ParameterIDs). Deliverable: bind scaffolding complete, ready for the UI.

---

# PHASE 3-6 — Roadmap (detailed plan authored after Phase 2 lands & is heard)

*These phases mirror existing patterns and will be shaped by how the DSP actually sounds, so they get their own step-level plan once Phase 2 is verified audible. Task-level scope + anchors:*

- **Phase 3 — UI controls (UI PARITY IS LAW).**
  - **First task = audit `.samp-knobs`:** exact current knob count, pixel width of the row, knob size — confirm **6 knobs fit at the existing size**; if the sample row holds fewer, the granular row matches that knob size and proves 6 fit the same total width (screenshot-verify headless before/after). The space does not grow.
  - Clone `.sample-view`→`.granular-view` (CSS-gated like `:3822-3823`), `engine-granular` toggle beside `engine-sample` (`index.html:9769`), `.gran-knobs` row of 6 `<div class="knob" data-syn="SYN_OSC_A_GRAIN_…">` (identical markup to `:5135`, labels only differ), `wireGranular` cloning `wireSample` (`:10007-10192`), region handles + drop/copy/paste reused verbatim.
  - Reassignable slots: right-click a slot → function menu (reassign `data-syn` target) + ⚙ deep-options; slot assignment persisted per-osc, pushed on `signalPageReady` (no polling).
  - Replace the loop-mode selector with a Freeze/Scan affordance.
- **Phase 4 — Viz + follower (the ONE new visual).** `granCloudSnapshot` tap in the processor gather (`:3494-3516`) → 60 Hz push (`:3144-3163`) → `updateGranularCloud(osc, grains)` (`index.html:10199-10215`): thin-white/purple grain dots at `left:pos%`, `opacity=1-age01`, scan marker. try/catch rAF + `<8px` bail (purple-filter lesson). Transient-snap precompute-on-load.
- **Phase 5 — Flagship.** `Life` = bounded OU (reuse WAVER σ/τ + Murmur3) on density/size/spray/pitchSpray, scaled by `p_.life`. `Key` = in-key snap at grain birth (Off/Oct/5th/Chord-Follow/Maj/Min/Penta), Chord-Follow reads the held-MIDI `Held[64]`. Add the expanded functions (Position/Pitch/PitchSpray/Width/Skew/Direction/Jump) to the reassignable menu + their param/bind.
- **Phase 6 — Polish.** Grain-cap/CPU tuning across unison; aliasing check at tiny sizes; hero presets (frozen living pad, drum-loop drift-scan, in-key shimmer). Checkpoint + memory save.

---

# Build

**C++-only change** (Phase 1 harness, or Phases 2/8-12 with no `index.html` edit):
```bash
cd /Users/macshooter/Developer/VST-Plugins/audio-plugin-coder/.worktrees/terrain-instrument
cmake --build build --target TerrainInstrument_VST3 --target TerrainInstrument_AU
```
**Any `index.html` change** (Phase 3+): bust the WebUI cache first, reconfigure, build both, verify embed:
```bash
rm -rf build/plugins/TerrainInstrument/juce_binarydata_TerrainInstrument_WebUI \
       build/plugins/TerrainInstrument/CMakeFiles/TerrainInstrument_WebUI.dir \
       build/plugins/TerrainInstrument/libTerrainInstrument_WebUI.a
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64"
cmake --build build --target TerrainInstrument_VST3 --target TerrainInstrument_AU
strings "$HOME/Library/Audio/Plug-Ins/VST3/TerrainInstrument.vst3/Contents/MacOS/TerrainInstrument" | grep -c gran-knobs   # → non-zero
```
Install paths: VST3 → `~/Library/Audio/Plug-Ins/VST3/`, AU → `~/Library/Audio/Plug-Ins/Components/`.

---

# Self-review notes
- **Spec coverage:** every §1 locked decision maps to a task — flagship→Phase 5, feel(slow-drift)→Task 10 default 0.15, default-6→Tasks 10/Phase 3, follower→Phase 4, transient-snap→Phase 4, unison-low→Phase 6 cap, single-buffer→Task 8 reuse, keep-list→Phase 3 verbatim reuse, A/B-deferred→out of scope.
- **UI parity** is a Global Constraint + the first Phase-3 task (knob-fit audit).
- **Types:** `GranularEngineParams` fields are the single source of truth (Task 1); processor gather (Task 11) and voice setters (Task 8) consume exactly those names.
- **Known refinements deferred to their task:** reverse-grain read-start offset (Task 6 note), OU/Key math (Phase 5) — not placeholders, scoped where the test drives them.
