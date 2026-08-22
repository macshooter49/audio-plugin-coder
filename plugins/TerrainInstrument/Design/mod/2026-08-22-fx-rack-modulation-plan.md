# FX Rack Modulation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or
> superpowers:executing-plans to implement this plan task-by-task. Steps use `- [ ]` for tracking.

**Goal:** Every continuous knob on every FX rack device (16 kinds × 6 instances × 12 knobs) becomes a
modulation destination for the existing LFOs and envelopes, wearing the existing moving underline.

**Architecture:** Append 1,152 destinations to `ModDest` (`FxModBase = 694`). Resolve each to the
`std::atomic<float>*` the device's read site already holds, via a param-ID table GENERATED from the
UI's card definitions. Per block, walk the ≤128 assignments once into a sorted `(pointer → value)`
array; every read site's `X->load()` becomes `M(X)`, matching by pointer identity so a knob can never
mis-map. LFO adds, ENV owns — fb184's math, unchanged.

**Tech Stack:** JUCE 7 / C++17, one-file HTML+JS UI, `clang++` harnesses, puppeteer-core UI gates.

**Spec:** `Design/mod/2026-08-22-fx-rack-modulation-design.md`

## Global Constraints

- Build ONLY via `scratchpad/terrain-build.sh`, redirected to a log, in the background. It runs the
  EMBED GATE (all three binaries must carry the same `TERRAIN_BUILD`) and installs VST3 + AU.
- Harnesses compile `clang++ -std=c++17 -O2 -I Tests/shim -I Source`.
- 🚨 **A cert that sits beside a mirrored header includes the MIRROR.** After editing
  `Source/SynthModConfig.h`, mirror it anywhere `Design/**` holds a copy BEFORE running any cert.
- 🚨 **Mutation testing is mandatory (fb421).** Every gate added here must be shown RED under its own
  mutant before the task is done. A gate that has never failed has never been tested.
- 🚨 **Verify the PATH, not the engine (fb373).** A green C++ harness never proves the plugin reaches
  the code. The AU equivalence matrix in Task 5 is the real proof.
- `auval -v aumu Tern Wvcr` — **check the EXIT CODE**, the tail always looks like a pass.
- pluginval VST3 editor test is flaky ~1/3 with no crash report; rerun VST3 once before believing it.
- Bump `TERRAIN_BUILD` in `index.html` to `fb453` on the first build.
- NO agent fleets without asking Max first, with a token estimate.
- UI is shown to Max in Safari and approved before it is called done.

---

### Task 1: The destinations and their DestInfo rows

**Files:**
- Modify: `Source/SynthModConfig.h` (the `ModDest` tail, and `kDestInfo`)
- Create: `Tests/fxmod_cert.cpp`

**Interfaces:**
- Produces: `ModDest::FxModBase` (694), `ModDest::NumDests` (1846), `wc::fxModDest(kind,inst,knob)`,
  `wc::isFxModDest(int)`, `wc::FxModAddr{kind,inst,knob}`, `wc::fxModDecode(int)`,
  and the constants `wc::kFxModKinds`(16) `wc::kFxModInsts`(6) `wc::kFxModKnobs`(12).

- [ ] **Step 1: Write the failing cert**

Create `Tests/fxmod_cert.cpp`:

```cpp
// fxmod_cert — the FX rack's modulation destinations, gated before anything reads them.
#include <cstdio>
#include <set>
#include "SynthModConfig.h"
static int pass = 0, fail = 0;
static void gate (const char* what, bool ok, const std::string& d = {})
{ (ok ? pass : fail)++; std::printf ("  %-5s %s%s%s\n", ok ? "ok" : "FAIL", what,
    d.empty() ? "" : "   ", d.c_str()); }

int main()
{
    using namespace wc;
    std::printf ("\n[A. the destination block]\n");
    gate ("FxModBase is 694 — the NumDests the rack was appended after",
          (int) ModDest::FxModBase == 694,
          "FxModBase=" + std::to_string ((int) ModDest::FxModBase));
    gate ("NumDests is 1846 (694 + 16*6*12)", (int) ModDest::NumDests == 1846,
          "NumDests=" + std::to_string ((int) ModDest::NumDests));
    gate ("the first FX dest is FxModBase", fxModDest (0, 0, 0) == (int) ModDest::FxModBase);
    gate ("the last FX dest is NumDests-1", fxModDest (15, 5, 11) == (int) ModDest::NumDests - 1);

    std::printf ("\n[B. every one of the 1,152 round-trips and is unique]\n");
    std::set<int> seen; bool rt = true, uniq = true, inRange = true;
    for (int k = 0; k < kFxModKinds; ++k)
      for (int i = 0; i < kFxModInsts; ++i)
        for (int n = 0; n < kFxModKnobs; ++n)
        { const int d = fxModDest (k, i, n);
          if (! seen.insert (d).second) uniq = false;
          if (! isFxModDest (d)) inRange = false;
          const auto a = fxModDecode (d);
          if (a.kind != k || a.inst != i || a.knob != n) rt = false; }
    gate ("1,152 destinations, all unique", uniq && (int) seen.size() == 1152,
          std::to_string (seen.size()) + " distinct");
    gate ("every one decodes back to its own (kind, instance, knob)", rt);
    gate ("every one answers isFxModDest()", inRange);
    gate ("no legacy destination is inside the FX range", ! isFxModDest ((int) ModDest::DstMorph));

    std::printf ("\n[C. the DestInfo rows exist — the zero-fill trap]\n");
    // kDestInfo is sized by NumDests. Growing the enum WITHOUT growing the table zero-fills the new
    // rows (fullScale 0.0f) and every FX route silently does nothing. This is that gate.
    bool allLinear = true; float worst = 1.0f;
    for (int d = (int) ModDest::FxModBase; d < (int) ModDest::NumDests; ++d)
    { const auto& in = kDestInfo[d];
      if (in.domain != ModDomain::Linear01 || in.fullScale != 1.0f) allLinear = false;
      worst = std::min (worst, in.fullScale); }
    gate ("every FX dest is {Linear01, 1.0} — not a zero-filled hole", allLinear,
          "worst fullScale " + std::to_string (worst));
    gate ("the legacy rows are untouched (Cut1 is still Semitone 48)",
          kDestInfo[(int) ModDest::Cut1].domain == ModDomain::Semitone
          && kDestInfo[(int) ModDest::Cut1].fullScale == 48.0f);
    gate ("routeContribution on an FX dest: source +1 at depth 1 == +1.0",
          std::fabs (routeContribution (kDestInfo[(int) ModDest::FxModBase], 1.0f, 1.0f) - 1.0f) < 1e-6f);

    std::printf ("\n══ RESULT: %d pass, %d FAIL ══\n", pass, fail);
    return fail ? 1 : 0;
}
```

- [ ] **Step 2: Run it and watch it fail to compile**

```bash
cd plugins/TerrainInstrument
clang++ -std=c++17 -O2 -I Tests/shim -I Source Tests/fxmod_cert.cpp -o /tmp/fxmod_cert
```
Expected: FAIL — `no member named 'FxModBase' in 'wc::ModDest'`.

- [ ] **Step 3: Append the destinations**

In `Source/SynthModConfig.h`, replace the `ModDest` tail:

```cpp
    DstMorph = LfoPhaseBase + 10,
    // ── fb453 — THE FX RACK JOINS THE MATRIX. 12 knobs per device (4 front dials incl. Mix,
    //    then the 8 back knobs), kFxKinds(16) x kFxInstances(6) = 1,152. The block is a FIXED 12
    //    even where a device has fewer knobs — the Filter has 4 and no back panel (fb384) — so a
    //    hole costs one table row and buys arithmetic that never has to move again. APPEND-ONLY:
    //    saved projects carry dest INTS, so nothing above this line may ever shift.
    FxModBase = DstMorph + 1,
    NumDests  = FxModBase + 16 * 6 * 12
};

inline constexpr int kFxModKinds = 16, kFxModInsts = 6, kFxModKnobs = 12;
inline constexpr int fxModDest (int kind, int inst, int knob) noexcept
{ return (int) ModDest::FxModBase + (kind * kFxModInsts + inst) * kFxModKnobs + knob; }
inline constexpr bool isFxModDest (int d) noexcept
{ return d >= (int) ModDest::FxModBase && d < (int) ModDest::NumDests; }
struct FxModAddr { int kind, inst, knob; };
inline constexpr FxModAddr fxModDecode (int d) noexcept
{ const int o = d - (int) ModDest::FxModBase;
  return FxModAddr { o / (kFxModInsts * kFxModKnobs), (o / kFxModKnobs) % kFxModInsts, o % kFxModKnobs }; }
```

(The `};` that used to close the enum moves up to just after `NumDests`.)

- [ ] **Step 4: Grow kDestInfo instead of letting it zero-fill**

Add `#include <array>` at the top of the file. Rename the existing literal and build the full table:

```cpp
// The hand-written rows, one per legacy destination. fb453: this array now stops at FxModBase and
// the FX rack's 1,152 rows are generated below — 1,152 identical hand-typed rows would be 1,152
// chances to typo one, and every FX knob is a normalised 0..1 parameter anyway.
static constexpr DestInfo kDestInfoBase[(int) ModDest::FxModBase] = {
    ... the existing rows, unchanged ...
};
inline constexpr std::array<DestInfo, (int) ModDest::NumDests> makeDestInfo() noexcept
{
    std::array<DestInfo, (int) ModDest::NumDests> a {};
    for (int i = 0; i < (int) ModDest::FxModBase; ++i) a[(size_t) i] = kDestInfoBase[i];
    for (int i = (int) ModDest::FxModBase; i < (int) ModDest::NumDests; ++i)
        a[(size_t) i] = DestInfo { ModDomain::Linear01, 1.0f };
    return a;
}
static constexpr auto kDestInfo = makeDestInfo();
```

- [ ] **Step 5: Run the cert — all green**

```bash
clang++ -std=c++17 -O2 -I Tests/shim -I Source Tests/fxmod_cert.cpp -o /tmp/fxmod_cert && /tmp/fxmod_cert
```
Expected: `RESULT: 11 pass, 0 FAIL`.

- [ ] **Step 6: MUTATE — prove the zero-fill gate has teeth**

Temporarily change the builder's FX row to `DestInfo {}` (the zero-fill it exists to catch), rebuild,
run. Expected: the `{Linear01, 1.0}` gate goes RED. Revert.

- [ ] **Step 7: Prove nothing else moved, then commit**

```bash
# the matrix's own harness must be untouched by an append-only enum change
clang++ -std=c++17 -O2 -I Tests/shim -I Source Source/ModCore_test.cpp -o /tmp/modcore && /tmp/modcore | tail -3
git add Source/SynthModConfig.h Tests/fxmod_cert.cpp
git commit -m "fb453 T1 — 1,152 FX rack modulation destinations (FxModBase=694) + fxmod_cert

kDestInfo is sized by NumDests and was a 694-row literal: growing the enum without growing the table
zero-fills the new rows (fullScale 0.0f) and every FX route would silently do nothing. The table is
now kDestInfoBase[694] plus a constexpr builder, and the cert gates that every FX dest reports
{Linear01, 1.0} — mutation-tested by zero-filling it on purpose."
```

---

### Task 2: The param-ID table, GENERATED from the UI's card definitions

**Files:**
- Create: `Tools/gen_fx_mod_ids.py`
- Create: `Source/fx_mod_ids.inc` (generated, committed)
- Modify: `Tests/fxmod_cert.cpp` (gate the table's shape)

**Interfaces:**
- Produces: `kFxModIds[16][12]` — `const char*` parameter-ID leaves, `nullptr` for a hole. Consumed by
  Task 3 (`cacheFxModRefs`) and by the UI gate in Task 6.

**Why generated:** the dial and the destination must be authored in exactly ONE place. The UI's card
definitions already name the parameter behind every dial (`data-p`). Typing that mapping again in C++
is 184 chances to modulate the wrong knob with every gate still green. Same pattern as
`Design/fx4/eq/shipped_labels.inc`.

- [ ] **Step 1: Write the generator**

`Tools/gen_fx_mod_ids.py` parses `Source/ui/public/index.html`, walks the card definitions in KIND
ORDER (`0=reverb 1=delay 2=saturate 3=granular 4=tape 5=flt 6=cho 7=fla 8=pha 9=eqz 10=wid 11=cmp
12=ott 13=bod 14=utl 15=spl` — the order documented at `PluginProcessor.h:1660`), and for each takes
the 4 front `knobs[].p` then the 8 `back.knobs[][2]`, padding with `nullptr`. It emits:

```cpp
// GENERATED by Tools/gen_fx_mod_ids.py — DO NOT EDIT. Regenerate and commit.
// The parameter behind each rack dial, in the UI's own order: [kind][knob], knob 0..3 = the front
// dials (Mix is 3), 4..11 = the back knobs. nullptr = a knob that device does not have.
static const char* const kFxModIds[16][12] = {
  /* 0 reverb */ { "SYN_RVB_SIZE", "SYN_RVB_DECAY", ... },
  ...
  /* 5 flt    */ { "SYN_FLT_CUT", "SYN_FLT_RES", "SYN_FLT_DRIVE", "SYN_FLT_MIX",
                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
  ...
};
```

The ids it emits are INSTANCE 1. Instances 2..6 insert the number before the leaf's underscore
(`SYN_UTL_GAIN` → `SYN_UTL2_GAIN`), matching the declaration loop
(`p = "SYN_UTL" + sfx + "_"`, `PluginProcessor.cpp:4303`). The generator emits the split point so
C++ does not have to guess:

```cpp
static const char* const kFxModTag[16] = { "SYN_RVB", "SYN_DLY", ... };   // the instance-suffix point
static const char* const kFxModLeaf[16][12] = { { "SIZE", "DECAY", ... }, ... };
```

- [ ] **Step 2: Generate, and eyeball the Filter and Tape rows**

```bash
python3 Tools/gen_fx_mod_ids.py > Source/fx_mod_ids.inc
grep -c nullptr Source/fx_mod_ids.inc     # expect 8 — the Filter's back panel (fb384), and nothing else
```
If any other kind produces a `nullptr`, STOP: either the generator mis-parsed that card or that device
really has fewer knobs, and the spec's audit (15 kinds × 12 + 1 × 4 = 184) is wrong. Resolve before
continuing — a silent hole is a knob that will never modulate.

- [ ] **Step 3: Gate the table's shape in fxmod_cert**

Append to `Tests/fxmod_cert.cpp` (it can `#include "fx_mod_ids.inc"` directly):

```cpp
    std::printf ("\n[D. the generated parameter map]\n");
    int live = 0; bool wellFormed = true;
    std::set<std::string> ids;
    for (int k = 0; k < kFxModKinds; ++k)
      for (int n = 0; n < kFxModKnobs; ++n)
        if (kFxModLeaf[k][n] != nullptr)
        { ++live;
          const std::string id = std::string (kFxModTag[k]) + "_" + kFxModLeaf[k][n];
          if (! ids.insert (id).second) wellFormed = false;   // the same param on two dials
          if (id.rfind ("SYN_", 0) != 0) wellFormed = false; }
    gate ("184 live (kind, knob) cells — 15 kinds x 12 + the Filter's 4", live == 184,
          std::to_string (live) + " live");
    gate ("no parameter is claimed by two dials", wellFormed);
    gate ("the Filter's back panel is 8 holes, and it is the ONLY device with holes",
          kFxModLeaf[5][4] == nullptr && kFxModLeaf[0][11] != nullptr && kFxModLeaf[15][11] != nullptr);
```

- [ ] **Step 4: Run — expect 14 pass, 0 FAIL. Commit.**

```bash
git add Tools/gen_fx_mod_ids.py Source/fx_mod_ids.inc Tests/fxmod_cert.cpp
git commit -m "fb453 T2 — the dial->parameter map, GENERATED from the UI's card defs

184 live cells. Typing this map in C++ would be 184 chances to modulate the wrong knob with every
gate still green, so it is generated from the one place that already knows (the card definitions'
data-p) and committed beside them, like Design/fx4/eq/shipped_labels.inc."
```

---

### Task 3: Resolve the pointers and compute the per-block modulation

**Files:**
- Modify: `Source/PluginProcessor.h` (the ref table + `M()`), `Source/PluginProcessor.cpp`
  (`cacheFxModRefs()`, the per-block build)

**Interfaces:**
- Consumes: `wc::fxModDecode`, `kFxModTag`/`kFxModLeaf` (Task 2).
- Produces: `float M (std::atomic<float>* p) const noexcept` — the read-site call Task 4 substitutes in.

- [ ] **Step 1: Cache one pointer per (kind, instance, knob)**

In `PluginProcessor.h`:

```cpp
    #include "fx_mod_ids.inc"
    std::atomic<float>* fxModRef_[16][ParameterIDs::kFxInstances][12] {};
    // the per-block sparse map: only knobs that actually carry a route, sorted by pointer
    std::atomic<float>* fxModPtr_ [wc::MAX_ASSIGNMENTS] {};
    float               fxModVal_ [wc::MAX_ASSIGNMENTS] {};   // the additive (LFO) sum, then the final value
    float               fxModOwnW_[wc::MAX_ASSIGNMENTS] {};   // fb184 ownership weight  = sum |depth| over ENV routes
    float               fxModOwnV_[wc::MAX_ASSIGNMENTS] {};   // fb184 ownership value   = sum |depth| * env
    int                 fxModCount_ = 0;
```

In `PluginProcessor.cpp`, called once from `prepareToPlay` beside the other `cache*Refs()`:

```cpp
void TerrainInstrumentAudioProcessor::cacheFxModRefs()
{
    for (int k = 0; k < 16; ++k)
      for (int i = 0; i < ParameterIDs::kFxInstances; ++i)
        for (int n = 0; n < 12; ++n)
        { fxModRef_[k][i][n] = nullptr;
          if (kFxModLeaf[k][n] == nullptr) continue;
          const juce::String id = juce::String (kFxModTag[k])
                                + (i == 0 ? juce::String() : juce::String (i + 1))
                                + "_" + kFxModLeaf[k][n];
          fxModRef_[k][i][n] = apvts.getRawParameterValue (id); }
}
```

- [ ] **Step 2: Build the sparse map each block, with fb184's math**

Place it beside the existing `flowKnob` build (`PluginProcessor.cpp:~8855`), AFTER `synModCfg` is
assembled so it sees resolved depths:

```cpp
    // ── fb453 — THE RACK'S MODULATION. LFO ADDS, ENV OWNS: the same law flowKnob() applies to the
    //    FLOW knobs (fb184), so a modulated rack knob behaves exactly like a modulated FLOW knob.
    //    Walked ONCE over the <=128 assignments — the 1,152 destinations are never iterated.
    fxModCount_ = 0;
    for (int a = 0; a < synModCfg.numAssignments; ++a)
    {
        const auto& as = synModCfg.assignments[a];
        if (! as.enabled || ! wc::isFxModDest ((int) as.dest)) continue;
        const auto ad = wc::fxModDecode ((int) as.dest);
        auto* ref = fxModRef_[ad.kind][ad.inst][ad.knob];
        if (ref == nullptr) continue;                     // a hole: nothing to modulate
        int slot = -1;
        for (int s = 0; s < fxModCount_; ++s) if (fxModPtr_[s] == ref) { slot = s; break; }
        if (slot < 0) { if (fxModCount_ >= wc::MAX_ASSIGNMENTS) continue;
                        slot = fxModCount_++; fxModPtr_[slot] = ref;
                        fxModVal_[slot] = ref->load(); fxModOwnW_[slot] = 0.0f; fxModOwnV_[slot] = 0.0f; }
        const auto& info = wc::kDestInfo[(int) as.dest];
        if (wc::isEnvModSource ((int) as.source))
        { const float dw = std::abs (as.depth);           // ENV OWNS
          fxModOwnW_[slot] += dw;
          fxModOwnV_[slot] += dw * (monoEnvLevelOf ((int) as.source) + 1.0f) * 0.5f; }
        else
        { const int si = (int) as.source - (int) wc::ModSource::L1;
          if (si < 0 || si >= wc::NUM_LFOS) continue;     // LFO ADDS
          fxModVal_[slot] += wc::routeContribution (info, flowLfo_[si].peek(), as.depth); }
    }
    for (int s = 0; s < fxModCount_; ++s)                 // the ownership crossfade, then clamp once
    { const float w = juce::jmin (1.0f, fxModOwnW_[s]);
      fxModVal_[s] = juce::jlimit (0.0f, 1.0f, fxModVal_[s] * (1.0f - w) + fxModOwnV_[s]); }
```

Note the `* 0.5f` on the env term: `monoEnvLevelOf` returns −1..+1 for the matrix's bipolar sources
and the FX knobs are 0..1, so `(env + 1) * 0.5` maps a full envelope across the knob's own travel.
**Gate this in Task 5** — an envelope at rest must leave the knob at 0, not at half.

- [ ] **Step 3: The read-site helper**

```cpp
    // fb453 — the modulated value for a rack parameter, matched BY POINTER so a read site can never
    // mis-map (there is no knob index here to get wrong). Zero routes = one branch.
    inline float M (std::atomic<float>* p) const noexcept
    {
       #ifdef FXMOD_MUT_NO_APPLY
        return p->load();          // MUTATION: the rack ignores the matrix entirely
       #endif
        for (int s = 0; s < fxModCount_; ++s) if (fxModPtr_[s] == p) return fxModVal_[s];
        return p->load();
    }
```

- [ ] **Step 4: Commit (no behaviour change yet — nothing calls `M`)**

```bash
git add Source/PluginProcessor.h Source/PluginProcessor.cpp
git commit -m "fb453 T3 — resolve rack params to pointers and compute the per-block modulation"
```

---

### Task 4: Substitute the read sites

**Files:**
- Modify: `Source/PluginProcessor.cpp` — the 16 per-instance device blocks (`:5386`–`:5620` and the
  Reverb/Delay/Distortion/Granular/Tape/Filter blocks above them)

- [ ] **Step 1: Substitute mechanically, one device at a time**

For each of the 184 live cells, the load of that knob's parameter becomes `M(...)`:

```cpp
    q.size = M (V.size);        // was V.size->load()
    q.hp   = M (V.b[0]);        // was V.b[0]->load()
```

Do NOT wrap `type`, `chr`, dropdown, pill, `active`, `rank`, `power` or `src[]` reads — switches and
Types are out of scope (Max's call), and wrapping them would silently make a boolean modulatable.

- [ ] **Step 2: Prove every live cell got substituted**

```bash
python3 Tools/check_fx_mod_sites.py     # written in this step: parses fx_mod_ids.inc, then greps
                                        # PluginProcessor.cpp for an M(...) per live cell
```
Expected: `184/184 substituted, 0 missing`. Missing entries are knobs that will silently never
modulate — the exact failure this check exists for. Commit the checker with the change.

- [ ] **Step 3: Build and install**

```bash
nohup scratchpad/terrain-build.sh > scratchpad/build1.log 2>&1 &
grep -E "error:|embed gate|ALL DONE" scratchpad/build1.log
```

- [ ] **Step 4: Commit**

```bash
git commit -am "fb453 T4 — the 184 rack read sites take their value through M()"
```

---

### Task 5: The equivalence matrix on the REAL AU

**Files:**
- Modify: `Tests/au_fx_path.cpp`

**The gate that matters.** For every live (kind, knob): render the device with the parameter moved to
`base + x`, then render it again with the parameter at `base` and a modulation route supplying `x`
through the same `setSynthMod` bridge the UI uses. **The two renders must match.** A mis-mapped knob,
a hole, a wrong DestInfo scale or an env/LFO sign error all fail here, on the real plugin.

- [ ] **Step 1: Add the matrix**

For each kind 0..15, for each knob 0..11 with a live id:
1. instantiate the AU, add that device to the chain, set the knob to 0.35;
2. render a fixed noise burst → `A`;
3. reset, set the knob to 0.35, install `[{s:<LFO1>, d:fxModDest(kind,inst,knob), v:1.0}]` with LFO 1
   parked at a DC value of +0.25 (rate 0, so `peek()` is constant);
4. render the same burst → `B`;
5. set the knob to 0.60 with no route, render → `C`;
6. gate `rms(B − C) < −80 dB` (the route equals the knob) and `rms(B − A) > −60 dB` (it did something).

- [ ] **Step 2: Add the envelope-at-rest gate**

With an ENV route installed at full depth and NO note playing, the knob must sit at its base value —
this is what catches the `(env+1)*0.5` mapping being wrong (Task 3, Step 2).

- [ ] **Step 3: Run — 184 cells green**

```bash
clang++ -std=c++17 -O2 -I Tests/shim -I Source Tests/au_fx_path.cpp -o /tmp/au_fx_path \
  -framework AudioToolbox -framework CoreFoundation -framework CoreAudio && /tmp/au_fx_path
```

- [ ] **Step 4: MUTATE — the gate must have teeth**

Rebuild the plugin with `-DFXMOD_MUT_NO_APPLY`, install, rerun: every one of the 184 cells must go
RED while the pre-existing 28 gates stay green. Then rebuild clean and reinstall.

- [ ] **Step 5: Commit**

---

### Task 6: The UI — the underline lands on rack knobs

**Files:**
- Modify: `Source/ui/public/index.html` (the card renderer, the underline's label lookup, route
  pruning on delete), `Tests/fx4_ui.js`

- [ ] **Step 1: Hang the destination on the knob WRAPPER**

Front (`index.html:~10195`) — the wrapper contains both the dial and its `.fxr-lab`:

```js
'<div class="fxr-knob" data-mod-dest="'+fxModDest(d.core,d.inst,i)+'">'
```
Back (`bkKnob`, `:~9821`): the same on `.fxr-bk-knob`, with knob index `4 + l`.

`fxModDest(core, inst, knobIdx)` is `694 + (FX_KIND[core]*6 + inst)*12 + knobIdx`, with
`FX_KIND = {reverb:0,delay:1,saturate:2,granular:3,tape:4,flt:5,cho:6,fla:7,pha:8,eqz:9,wid:10,
cmp:11,ott:12,bod:13,utl:14,spl:15}`. Emit NO attribute where the device has no such knob (the
Filter's back), so a hole can never be dropped on.

- [ ] **Step 2: The one-line label fix**

`index.html:~28662`:
```js
var lb=(el.querySelector&&el.querySelector('.knob-label,.fxr-lab'))||el;   /* fb453 — rack labels */
```
Without this the underline measures the wrapper box instead of the word, and covers too much — fb188's
requirement is that it covers the WHOLE word, exactly.

- [ ] **Step 3: Prune routes when a device is deleted**

Where the rack removes a card, drop assignments whose dest falls in that device's 12-wide block, so a
later card in the same slot never inherits a ghost route.

- [ ] **Step 4: Extend `Tests/fx4_ui.js`**

```js
// fb453 — the rack's dials are modulation targets, and the map is the C++'s
const FXIDS = fs.readFileSync(<Source/fx_mod_ids.inc>,'utf8');
```
Gates:
1. every rendered rack knob wrapper carries a `data-mod-dest`; all unique across a full rack;
2. each dial's `data-p` equals the `.inc`'s id for that (kind, knob) — **the cross-language check that
   keeps the dial and the destination authored in one place**;
3. a synthetic LFO drop on a rack knob writes a route and raises an `.sm-ul` on that knob;
4. the underline's width equals the LABEL'S ink, not the wrapper's box (fb188);
5. the underline stays on its word after the rack is SCROLLED (it is `position:fixed`, measured per
   frame — a case the synth panel never exercised);
6. deleting the device removes its routes.

- [ ] **Step 5: Screenshot for Max, in Safari**

Install a few routes across a full rack, screenshot, and READ the PNG before showing it: 12 underlines
on one card is the density question, and it is Max's call whether it reads busy.

- [ ] **Step 6: Full verification, then commit**

```bash
# certs (mirror any Design/ copy of SynthModConfig.h FIRST — a cert beside a mirror includes it)
fxmod_cert · eq_cert 159 · utl_cert 67 · spl_cert 150 · bod_cert 68
au_fx_path (28 + 184) · fx4_ui.js (108 + 6)
pluginval VST3 + AU · auval -v aumu Tern Wvcr  → CHECK THE EXIT CODE
```

---

## Self-review notes

- **Spec coverage:** §2 dests → T1 · §3 apply + one-authorship → T2/T3/T4 · §4 underline → T6 S2 ·
  §5 UI → T6 · §6 route pruning → T6 S3 · §7 verification → T1 S6, T4 S2, T5, T6 S4.
- **The riskiest step is Task 3 Step 2's env mapping** (`(env+1)*0.5`), which is why it gets its own
  gate in Task 5 Step 2 rather than being assumed correct.
- **Open until measured:** whether `M()`'s linear scan needs to become a binary search. With zero
  routes it is one branch; the worst case is 128 routes × 184 reads per block. Task 5 should time a
  full-rack, fully-routed block and, if it exceeds ~1% of a core, sort `fxModPtr_` and
  `std::lower_bound` it.
