# NOISE ROUTING PLAN — Noise → per-osc Filter routing + Noise as a Blend source

Research-only plan (no source edited). Line numbers are as of this read (build fb54); they will
drift a little, but the anchor strings quoted are unique enough to relocate.

## TL;DR of what's already true (verified against code)

- **Noise generation** lives in `SynthVoice::noiseTick()` (~L1043) + the per-sample injection block
  `if (noiseOn_)` at **SynthVoice.h:3391–3418**. It currently adds noise ONLY into `scratchL/R`
  (= the Filter-1 bus, since `sL/sR = scratch_.getWritePointer()` — confirmed at L3473–3474). It
  is **hardwired to F1** (memory's "N still mock").
- **Per-osc filter routing** is real for A/B/C/D/Sub. Masks `fltSrc1_[5]`, `fltSrc2_[5]`
  (SynthVoice.h:4158–4159) → per-block coefficients `busCo1_/busCo2_/busCoD_` (built at
  **SynthVoice.h:1716–1728**) → distributed into the 3 buses at **SynthVoice.h:3388–3422**.
  Masks are set by `setFilterSources(s1,s2)` (**SynthVoice.h:419–420**), fed from params
  `SYN_FILTER{1,2}_SRC_{A,B,C,D,SUB}` read at **PluginProcessor.cpp:3601–3610** and pushed at
  **PluginProcessor.cpp:4135**. There is NO bitmask — it's 10 individual `AudioParameterBool`s
  registered at **PluginProcessor.cpp:1352–1366**.
- **Blend source index space is ALREADY 7-wide and ALREADY includes Noise.** The `SRC` choice is
  registered as `{ "Osc A","Osc B","Osc C","Osc D","Sub","Noise","Self" }` at
  **PluginProcessor.cpp:2568** → indices **0..3=A/B/C/D, 4=Sub, 5=Noise, 6=Self**. The per-sample
  blend loop at **SynthVoice.h:2213–2215** already routes `b.src<4 → modPrev_[b.src]`,
  `b.src==6 → modPrev_[c]` (Self), and `else → 0.f` with the comment **"Sub(4)/Noise(5): P1 no-op"**.
  So **no new param and no enum append is needed for blend** — the only gap is the DSP tap for src 5.

Net: **Filter side needs 2 new params + small DSP.** **Blend side needs ZERO new params — pure DSP + 2 UI lines.**

---

## PART 1 — NOISE → FILTER ROUTING

### 1.1 New params (ParameterIDs.hpp)
After `SYN_FILTER2_SRC_SUB` (**ParameterIDs.hpp:221**) add:

```cpp
    constexpr char SYN_FILTER1_SRC_NOISE[] = "SYN_FILTER1_SRC_NOISE";
    constexpr char SYN_FILTER2_SRC_NOISE[] = "SYN_FILTER2_SRC_NOISE";
```
(The comment on L210–211 already says "Noise (N) reserved" — this fulfills it.)

### 1.2 Register them (PluginProcessor.cpp:1352–1366)
Add two rows to the `fltSrc[]` table (default `false`, matching the osc masks — modular "wire it up"):

```cpp
        { ParameterIDs::SYN_FILTER1_SRC_NOISE, "Synth Filter 1 Source Noise" },
        { ParameterIDs::SYN_FILTER2_SRC_NOISE, "Synth Filter 2 Source Noise" },
```
They flow through the existing `for (auto& s : fltSrc)` loop → `AudioParameterBool(..., false)`.
**Enum-append note:** these are standalone bools, not a choice list, so there is no count array to
grow — the "dead annulus" append trap (kDestInfo-style) does **not** apply here.

### 1.3 Read them (PluginProcessor.cpp:3601–3610)
The osc masks are `bool f1src[5]`/`f2src[5]`. Add two scalars right after:

```cpp
        const bool  n1src = *rawParam (ParameterIDs::SYN_FILTER1_SRC_NOISE) > 0.5f;
        const bool  n2src = *rawParam (ParameterIDs::SYN_FILTER2_SRC_NOISE) > 0.5f;
```

### 1.4 New setter (SynthVoice.h, next to setFilterSources @ L419–420)
```cpp
        void setNoiseFilterSources (bool n1, bool n2) noexcept
        { noiseFltSrc1_ = n1; noiseFltSrc2_ = n2; }
```

### 1.5 Push it (PluginProcessor.cpp:4135, right after setFilterSources)
```cpp
                sv->setNoiseFilterSources (n1src, n2src);
```

### 1.6 New members (SynthVoice.h — beside the noise members ~L1155–1173, or beside busCo @ L4158–4163)
```cpp
        bool  noiseFltSrc1_ = false, noiseFltSrc2_ = false;   // N pill → F1 / F2 (default: neither = dry)
        float noiseBusCo1_ = 0.0f, noiseBusCo2_ = 0.0f, noiseBusCoD_ = 1.0f;   // per-block, mirrors busCo*_
        float noisePrev_    = 0.0f;    // BLEND: prev-sample noise mono tap (see Part 2)
        bool  noiseModForce_ = false;  // BLEND: a slot uses Noise(5) as src → generate even if noiseOn_ is false
```

### 1.7 Compute the noise coefficients per block (SynthVoice.h:1716–1728)
The osc loop already computes `par`, `busCo*_[k]`, and `anySrc1_/anySrc2_`. Immediately AFTER the
`for (int k=0;k<5;++k)` loop (still inside the same `{}` block, after L1727), add:

```cpp
                // NOISE routing — identical model to the oscs (independent + dry-bypass).
                {
                    const bool nm1 = noiseFltSrc1_, nm2 = noiseFltSrc2_;
                    noiseBusCo1_ = nm1 ? 1.0f : 0.0f;
                    noiseBusCo2_ = (par ? nm2 : (nm2 && ! nm1)) ? 1.0f : 0.0f;
                    noiseBusCoD_ = (! nm1 && ! nm2) ? 1.0f : 0.0f;
                    anySrc1_ = anySrc1_ || (noiseBusCo1_ != 0.0f);   // so F1 still engages if ONLY noise feeds it
                    anySrc2_ = anySrc2_ || (noiseBusCo2_ != 0.0f);
                }
```
`anySrc1_/anySrc2_` gate the `a1/a2` filter-active flags at **L3595–3596**; without the OR-in, a
filter fed by noise alone (no oscs routed) would be bypassed and the noise would pass unfiltered.

### 1.8 Distribute the noise into all 3 buses (SynthVoice.h:3391–3418 → MOVE + rewrite)
**Why move:** the current block adds into `scratchL/R` with `+=` (OK, scratch was assigned at
L3388), but `busB2*`/`busDry*` are ASSIGNED with `=` at **L3419–3422**, AFTER this block — so
injecting into them here would be clobbered. **Relocate the whole `if (noiseOn_)` block to just
AFTER L3422** (after all three buses are written), and distribute by the coefficients:

```cpp
                // NOISE ENGINE — generate once/sample, distribute into the routed filter buses,
                // and (Part 2) publish a 1-sample-delayed mono tap for blend modulators.
                if (noiseOn_ || noiseModForce_)
                {
                    float _nL, _nR;
                    if (noiseSampLen_ > 1 && noiseSampL_ != nullptr)
                    {   // ... UNCHANGED looping-sample (P5) branch (old L3398–3403) ...
                    }
                    else
                    {   // ... UNCHANGED algorithmic SCAN branch (old L3409–3413) ...
                    }
                    // BLEND tap (Part 2): pre-gain mono, 1-sample delay, clamped like modPrev_.
                    noisePrev_ = juce::jlimit (-4.0f, 4.0f, 0.5f * (_nL + _nR));
                    if (noiseOn_)
                    {
                        const float _ng = noiseLevel_ * velEnv;
                        const float nLg = _nL * _ng * noisePanL_, nRg = _nR * _ng * noisePanR_;
                        scratchL[i] += noiseBusCo1_ * nLg;  scratchR[i] += noiseBusCo1_ * nRg;
                        busB2L[i]   += noiseBusCo2_ * nLg;  busB2R[i]   += noiseBusCo2_ * nRg;
                        busDryL[i]  += noiseBusCoD_ * nLg;  busDryR[i]  += noiseBusCoD_ * nRg;
                    }
                }
```
`velEnv` (L3353) and `i` are in scope. Both filter paths (oversample L3656 + normal L3670) already
read all three buses and re-add dry at the end, so filtered/parallel/dry all work with no further
change. The steal-fade loop (L3431–3439) already fades all three buses, so noise fades on steal too.

### 1.9 UI — make the N pill real (index.html)
The `flt-src` handler (`initFilterBackMock`, **index.html:14293–14336**) already toggles the N pill
(`data-fsrc="5"`, L6485) into `fst[f].src[5]` but **guards the param write with `if (i <= 4)`**
(L14307), so N is UI-only. Two edits:

1. **L14299–14302** — extend `FSRC_IDS` to include index 5:
```js
            1: ['SYN_FILTER1_SRC_A','SYN_FILTER1_SRC_B','SYN_FILTER1_SRC_C','SYN_FILTER1_SRC_D','SYN_FILTER1_SRC_SUB','SYN_FILTER1_SRC_NOISE'],
            2: ['SYN_FILTER2_SRC_A','SYN_FILTER2_SRC_B','SYN_FILTER2_SRC_C','SYN_FILTER2_SRC_D','SYN_FILTER2_SRC_SUB','SYN_FILTER2_SRC_NOISE'],
```
2. **L14305–14308** — drop the `i <= 4` gate so index 5 writes too:
```js
          const setSrc = (f, i, on) => {
            fst[f].src[i] = on ? 1 : 0;
            try { if (window.__setSynParam) window.__setSynParam(FSRC_IDS[f][i], on ? 1 : 0); } catch (e) {}
          };
```
`syncSrc()` (L14324–14333) iterates `FSRC_IDS[f].forEach` so it auto-restores index 5 on reopen
once the array has 6 entries. `fst[].src` is already length-6 (`[0,0,0,0,0,0]`, L14295–14296) and
`renderFb` already paints all pills via `forEach((p,i)=>…src[i])` (L14311). No CSS/markup change.
Uses `__setSynParam`/`getSynParam` (relay-ceiling bypass, per the >~700-relay rule) — same as the
existing A/B/C/D/Sub pills.

### 1.10 CPU
Effectively free. Per-block: ~6 extra float ops in the routing computation. Per-sample: when
`noiseOn_`, 6 MACs vs the old 2 (4 are multiply-by-{0,1} constants, branchless); when noise is off
and no blend forces it, the whole block is skipped exactly as today. No new allocations, no atomics.

### 1.11 Default-behaviour note (call it out for Max/Opus)
Defaulting `noiseFltSrc1_/2_ = false` sends noise to the **dry** bus. At the shipped default
(filters = None/OFF) this is **sonically identical** to today's hardwired-to-F1 (an inactive filter
passes its bus through unchanged). The only difference: with a filter turned ON, noise is no longer
auto-filtered — the user opts in via the N pill (which is the whole point). If you'd rather preserve
"noise always through F1" exactly, default `noiseBusCo1_ = 1, noiseBusCoD_ = 0` and
`noiseFltSrc1_ = true` instead — but that diverges from the osc convention (all masks default false).
Recommendation: **default false** (matches oscs; N pill starts unlit).

---

## PART 2 — NOISE → BLEND MODES (FM / PD / AM / RM using the noise signal)

### 2.1 The source index space (already correct)
`setBlendSlot(osc,slot,mode,src,depth)` (**SynthVoice.h:685–692**) stores `b.src` verbatim. The SRC
param choice (**PluginProcessor.cpp:2568**) is 7-wide with **5 = Noise**, pushed per block via
`blendCfg` at **PluginProcessor.cpp:3997–4004 / 4193–4195**. **No param/enum change needed** — the
choice list, the `NSRC=7` JS, and the C++ push all already carry Noise. This is the append-safe path
the memory warns about: it was reserved from the start, so the count is already right.

### 2.2 Block-rate: force noise to generate when it's a modulator (SynthVoice.h:2156–2166)
The blend pre-pass already sets `modSrcForce_[o]` so a Level-0 / gated osc still renders for its tap.
Noise needs the same so it generates even when `noiseOn_` is off but a slot selects it. In that loop
add a reset before and a set inside:

```cpp
                for (int o = 0; o < 4; ++o) { modSrcForce_[o] = false; blkCarrierArmed_[o] = false; }
                noiseModForce_ = false;                              // ← add
                ...
                        if      (b.src < 4)  modSrcForce_[b.src] = true;   // Osc A..D
                        else if (b.src == 5) noiseModForce_ = true;        // ← add: Noise as source
                        else if (b.src == 6) modSrcForce_[c]     = true;   // Self
```
`noiseModForce_` is consumed by the injection block (Part 1.8: `if (noiseOn_ || noiseModForce_)`),
which also writes `noisePrev_`. Note the noise engine is per-voice and always available; it is NOT
gated by `oscDead_`, so no `blkGateLevel`/`uLoop` plumbing is needed for it.

### 2.3 Per-sample: wire src 5 to the noise tap (SynthVoice.h:2213–2215)
Replace the no-op:

```cpp
                            if      (b.src < 4)  mod = modPrev_[b.src];   // Osc A..D (any-to-any)
                            else if (b.src == 5) mod = noisePrev_;        // ← Noise (1-sample delay, like modPrev_)
                            else if (b.src == 6) mod = modPrev_[c];       // Self (feedback)
                            else                 mod = 0.f;               // Sub(4): still no-op
```
`noisePrev_` is the previous sample's pre-gain mono noise (written in Part 1.8), exactly matching the
1-sample-delay contract that `modPrev_` uses (`modPrev_` is written at L3425–3427, read at the top of
the next sample). FM/PD/AM/RM math at L2216–2219 is untouched — noise just becomes another `mod`.

**Ordering is correct:** the blend loop (L2203–2226) runs at the TOP of the per-sample body and reads
`noisePrev_`; the injection block (moved to after L3422) runs at the BOTTOM and writes `noisePrev_`
for the next sample. Same phase relationship as `modPrev_`. First-sample value is 0 (member init).

### 2.4 Amplitude / safety
`noisePrev_` is clamped to ±4 (same ceiling as `modPrev_`). Raw white/pink sit in ~[-1,1]; brown
(`*3.5`), space (`*2.6`), vinyl rumble stay within the clamp. The blend depth curve + the AM/RM
`±6` `blendAmp` clamp (L2224) and FM phase clamp (L2222) already bound the downstream result. No new
guard needed.

### 2.5 UI — un-gray the Noise menu item (index.html)
The blend source picker (`initBlendPills`, **index.html:12900–12951**) already lists Noise but
**disabled**: `items.push({ label: 'Noise', isDisabled: true, badge: 'soon' });` (**L12946**). Two edits:

1. **L12946** — make it a real pick (src 5), mirroring the Osc A..D items (L12943–12945):
```js
            items.push({ label: 'Noise', isChecked: (b.src === 5), keepOpen: true,
              onPick: function () { wSrc(o, s, 5); paintLabel(pill, o, s); menu(pill, o, s, x, y); } });
```
`wSrc` writes `5/(NSRC-1) = 5/6` → choice index 5 (AudioParameterChoice normalization). Correct.
2. **L12914** — teach `srcLtr` to render N so the pill shows e.g. `FM(N)`:
```js
        function srcLtr (v) { return (v < 4) ? 'ABCD'.charAt(v) : (v === 5 ? 'N' : v === 6 ? 'S' : '?'); }
```
Noise is a valid source for **all four** families (FM/PD/AM/RM), so it belongs in the always-shown
Source list (unlike Self, which stays PD-only at L12947). Note the mode-switch guard at **L12938**
(`if (cs === ci || (cs > 3 && mi !== 2)) wSrc(...(ci+1)%4)`) currently BOOTS any `src>3` off non-PD
modes — that would kick Noise(5) back to an osc when switching to FM/AM/RM. **Relax it** so Noise
survives: change to `if (cs === ci || (cs === 6 && mi !== 2))` (i.e. only "Self" is PD-only; Sub/Noise
allowed everywhere). Verify in the real rendered UI.

### 2.6 Optional — Noise as a blend DEST
Out of scope / not recommended. A blend DEST is a carrier oscillator whose read-phase is warped
(`blendOff[c]`, `fmPhase_[c]`, c∈0..3). Noise has no phase/carrier to FM or PD, and AM/RM-ing noise
by an osc is just amplitude-gating the noise — already achievable more musically via the amp env +
level. If ever wanted, it would need a 5th carrier lane and a rework of the `blendOff/blendAmp[4]`
arrays and the per-osc gate mix — a much larger change than the source path. Leave noise as
**source-only**.

---

## FILE-BY-FILE CHANGE LIST (quick index)

| File | Line(s) | Change |
|---|---|---|
| ParameterIDs.hpp | after 221 | + `SYN_FILTER1_SRC_NOISE`, `SYN_FILTER2_SRC_NOISE` |
| PluginProcessor.cpp | 1352–1366 | + 2 rows in `fltSrc[]` (default false) |
| PluginProcessor.cpp | 3601–3610 | + read `n1src`, `n2src` |
| PluginProcessor.cpp | 4135 | + `sv->setNoiseFilterSources(n1src, n2src);` |
| SynthVoice.h | 419–420 | + `setNoiseFilterSources(bool,bool)` |
| SynthVoice.h | ~1155 / ~4158 | + members `noiseFltSrc1_/2_`, `noiseBusCo1_/2_/D_`, `noisePrev_`, `noiseModForce_` |
| SynthVoice.h | 1716–1728 | + noise routing coeffs + `anySrc*` OR-in |
| SynthVoice.h | 2156–2166 | + reset+set `noiseModForce_` (src==5) |
| SynthVoice.h | 2213–2215 | + `b.src==5 → mod = noisePrev_` |
| SynthVoice.h | 3391–3422 | MOVE noise block after L3422; distribute into 3 buses; write `noisePrev_`; gate `noiseOn_ \|\| noiseModForce_` |
| index.html | 14299–14308 | filter N pill → real param (add NOISE ids, drop `i<=4`) |
| index.html | 12914, 12938, 12946 | blend Noise item enabled; `srcLtr`→'N'; relax PD-only guard |

**Blend needs NO new params** (Noise=5 already registered everywhere). **Filter needs exactly 2 new
bool params.** Both changes are CPU-free bus/tap adds. No BinaryData/enum-count/kDestInfo growth traps.
Remember: index.html change ⇒ bust the WebUI BinaryData cache before rebuild; build BOTH VST3 + AU.
