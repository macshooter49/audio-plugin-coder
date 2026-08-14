# Terrain Instrument — FX Chain Bible
### Multi-instance architecture + the Utility (gain-staging) device
*The foundation document for the MULTI-DEVICE CHAIN epic — "the biggest task yet."*
*Research locked 2026-08-14. Written against tree fb345 (70de2d9). Every file:line below was read, not assumed.*
*Adversarially audited 2026-08-14 (same day): every in-tree citation re-opened, every arithmetic
claim re-derived, the Serum 2 PDF re-extracted and the JUCE/Kilohearts sources re-fetched. Corrected
numbers are marked "a prior draft said …" at the point of correction. Claims that could not be
re-verified are marked ⚠️ **UNVERIFIED** inline — do not quote those as fact.*

This bible has two halves plus a device:

* **HALF A (§3)** — how ONE chain of N devices (duplicates allowed, reorderable, dynamic — "the +
  button spawns devices from a list") is built on top of today's 3-singleton chain without
  re-detonating the fb305 landmine or fighting the host.
* **HALF B (§4)** — inter-device LEVELING: Keon's warning ("effects that ignore each other = mud")
  turned into laws, gates and countermeasures.
* **The Utility device (§5–§8)** — the gain-staging FX device that makes HALF B a thing the user can
  *hold*: Terrain's answer to Serum 2's `Utility`, on the locked fb275 chassis.

A builder must be able to implement from this file alone. Where a sibling bible already owns a law
(DISTORTION §4.4 latency, SPLITTER §6 lanes, COMPRESSOR §2.1 thresholds), this file cites it rather
than re-deriving it — those files are part of the build contract.

---

## 0. Scope decision

**One new device (`Utility`), one architecture refactor (the slot chain), zero new DSP families.**

Why this shape:

1. Serum 2's rack — the competitive bar — ships **16 modules** (13 FX + 3 splitters, `Utility`
   among them; p. 3 verbatim: *"Choose from 13 effects and 3 splitter modules"*),
   **"Add multiple instances of a single effect"** verbatim (p. 11), drag-reorder, per-module
   bypass, across **two FX busses** (p. 11: *"Dual FX Busses — Two separate FX busses"*) plus the
   mixer's separate **Main/Direct Outputs** channel (p. 16 — that is an output channel, NOT a third
   FX rack). *(Text of the official* What's New in Serum 2 *PDF re-extracted and checked 2026-08-14;
   quotes above are verbatim from the PDF text layer. Claims below sourced only to its SCREENSHOTS —
   per-module Mix knob, per-module output meter, printed fader dB scales — are flagged inline as
   figure-read.)*
   Terrain has 3 flagship devices + 5 more bibled (Chorus, Compressor, EQ, Flanger, Hyper, Phaser,
   Splitter). Multi-instance is the chassis they all land on — it must be designed BEFORE device 4
   ships, or every device ships twice.
2. The gain-staging device is the cheapest device in the whole program (§11: ~0.05 % core) and the
   one that makes an 8-device chain *survivable* (§4). It is also the first device whose primary
   job is to *measure* — which seeds the rack-rail metering every other device inherits (§8.4).
3. No new DSP family: every Utility type is one-poles, shelves, and an M/S matrix — all recycled
   (§15).

**Not in scope:** a second FX bus (Serum's Bus 1/Bus 2) — one serial chain + splitter lanes covers
the musical territory at a fraction of the landmine surface; parked in §14. Nested racks — Patcher
endgame (`terrain-instrument-node-architecture-patcher`).

---

## 1. The chain as it exists (fb341/fb345 — all verified in-tree)

Read this section with the code open. Every line number checked 2026-08-14.

### 1.1 The serial chain

* Three insert lambdas run per-sample inside the master loop of `processBlock`:
  `applyRvb` (`PluginProcessor.cpp:7137`), `applyDst` (`:7309`), `applyDly` (`:7345`),
  dispatched by `switch (fxPerm_)` over the **6-way permutation** (`:7383–7392`).
* `fxPerm_` is read once per block at `:5860` from `SYN_FX_ORDER` — an `AudioParameterChoice(6)`
  built at `PluginProcessor.cpp:3488`. ⚠️ The comment at `ParameterIDs.hpp:435` still says
  *"bool: false = Reverb→Delay"* — stale since fb341; fix in passing.
* Each device is an **env-gated insert**: reverb/delay use `leftChannel[i] += wetG·rl − duck·sgL`
  (the add itself at `:7189/:7191`; the equal-power sin/cos mix TARGETS are computed once per block
  at `:7112–7113`); the distortion uses the fb318 **replace grammar**
  `leftChannel[i] += e·(wl − sgL)` (`:7337`) — engine owns Mix, dry latency-aligned inside.
  All three fade power on/off through `hallSm_` (~15 ms one-pole) — the no-clicks law in code.

### 1.2 The fb305 landmine — exact addresses

MAIN-SEND devices eat `sg = leftChannel[i] − rtd` where `rtd` is the **exclusion sum** — every
per-osc-routed oscillator's dry, exactly as it sits in the mix:

```cpp
const float rtdL = ((rvbSendL ? rvbSendL[i] : 0.0f) + (dlySendL ? dlySendL[i] : 0.0f)
                  + (dstSendL ? dstSendL[i] : 0.0f)) * outputGain * kVoiceToFxPad;
```

This expression is **duplicated at three sites** (L+R pairs): reverb `:7159/7161`, distortion
`:7326/7328`, delay `:7358/7360`, each carrying the comment *"the fb305 law: EVERY send bus joins
EVERY main-send exclusion."* Send buses live at `PluginProcessor.h:1534/1559/1572`.
**The landmine: every new send bus must join every sum, and every new main-send device adds a new
sum.** Today that is 3×3; at 8 devices it is 8×8 = 64 hand-edited terms. §3.6 kills this class.

⚠️ **Address correction (checked 2026-08-14).** `MEMORY.md` still carries the shorthand *"re-breaks
fb305 at :6979/:7111"*. Those are **stale pre-fb341 `PluginProcessor.cpp` lines, and they are NOT
`index.html` lines** — `index.html:6979` is the fb120 robin SVG and `index.html:7111` is the ribbon
CSS block. The live addresses are the three C++ sites above (`:7159/7161`, `:7326/7328`,
`:7358/7360`). Anyone touching the exclusion sums must use those, not the memory shorthand.

### 1.3 Gain constants (the −26 dBFS reality's plumbing)

* `kVoiceToFxPad = 0.5f` (−6.02 dB) — the pre-FX pad on every send and main-send read
  (`PluginProcessor.cpp:6300`).
* `kInstrumentMakeup = 2.0f` (+6.02 dB) — the fb299 measured-Serum-match output makeup
  (`PluginProcessor.cpp:46`; single note −20 → −14 dBFS = Serum's −14.01).
* The fb264 master stage after makeup is the only clipper in the path (`PluginProcessor.cpp:44–47`):
  peak-limiter threshold **= soft-clip knee = 0.90 (−0.92 dBFS)**, hard ceiling **0.96605
  (−0.30 dBFS)**. Quote the **−0.92 dB knee** when computing headroom — program meets the limiter
  first; −0.3 dB is the DAC-protection catch behind it.
* Net: **FX-bus program sits ≈ −26 dBFS** (measured, DISTORTION bible §2.1). Every threshold,
  drive, target and ceiling in this file is stated **relative to −26 dBFS program** (LAW 1).

### 1.4 The UI rack

* `DEVS` array (`index.html:7479`) — one JS object per device: `core:` key
  (`'reverb'|'delay'|'saturate'`), `pwrP/tp/rp/pp` param IDs, 4 front `knobs`, `back:{d1,d2,knobs[8]}`.
* **The `+ Add effect` button already exists**: rendered at `:7705`
  (`<div class="fxr-add" data-act="add">`), styled `:7256–7259`, drag-insert respects it `:8311`.
  Today it spawns nothing — it is the door §3 walks through.
* Drag order → `SYN_FX_ORDER` at `:8315–8323`: builds the core-key sequence, matches it against the
  6-row `PERMS` table (`:8320`), writes `pi/5` normalized (`:8323`). Restore: `fxrRestoreOrder`
  (`:7972–7987`, its `PERMS` twin at `:7976`).
* Per-device restore reads a **24-slot vector** (see `fxrRestoreDistortion`, `:7947–7971`):
  `[type, power, front×4, d1, d2, route×6, pills×2, back×8]` — counted against the code: 1+1+4+1+1+6+2+8
  = 24 ✔. This vector IS the de-facto device state ABI — §3.3 reuses it verbatim. (For the distortion,
  `d1 = SYN_DST_CHARACTER`, `d2 = SYN_DST_QUALITY`.)

### 1.5 The precedents the architecture stands on

* **Sleep:** `DistortionEngine.h` — `asleep_` gate (`:513`), 2048-sample double-sided silence
  detector (`:566`, in/out both < 1e-12), closed-form wake re-seed (`:572–584`). The proven
  per-device CPU kill-switch. Reverb/delay lack it — §3.8 spreads it.
* **Multi-instance DSP:** `IndyFxChain.h:1–40` — *"One private FX-chain instance … own copies of
  all 7 global FX modules … shares APVTS parameter VALUES … but has INDEPENDENT STATE."* Terrain
  has already built a second instance of a whole chain once. What it has never done is give an
  instance **independent parameters** — that is the actual new ground (§3.2).
* **State:** `getStateInformation` (`PluginProcessor.cpp:8810`, `apvts.copyState()` + versioned
  extras; the `state.setProperty ("version", 2, …)` marker is at `:8863`), `setStateInformation`
  (`:9008`, `replaceState` at `:9145`, V1→V2 migration precedent).
* **Latency:** DISTORTION bible §4.4 — fixed-8/report-0, dry-side delay composition
  `delay8(left − duck·sg) = delay8(left) − duck·delay8(sg)`; `ConvolutionReverb.h:166`
  (`getLatency() { return B; }`, `B = 512` at `:25`) still runs 512 late while reporting 0.
  The multi-instance ledger (§3.7) generalizes this.
* Param registry (re-counted 2026-08-14): **972** `constexpr char` IDs in `ParameterIDs.hpp`, of
  which **964 are actually registered as APVTS parameters** — the 8 `FLOW_GLI_*_FLT` IDs are never
  passed to `createParameterLayout`. FX today = **77** IDs: 25 `SYN_RVB_*` + 28 `SYN_DLY_*` +
  24 `SYN_DST_*`. Use **964**, not 972, as the "existing parameter count" baseline in §3.2.

---

## 2. History and circuits — the gain-staging lineage

The Utility device has no glamorous circuit; its lineage is the **console channel strip** and the
discipline built around it.

* **The console trim.** Every analog desk (Neve 80-series, SSL 4000) put a mic/line TRIM at the top
  of the strip and calibrated the whole desk around a nominal operating level (+4 dBu ≈ **−18 dBFS**
  in the digital transcription). The entire art of "gain staging" is keeping every stage near
  nominal so no stage's noise floor or headroom is wasted. Terrain's nominal is **−26 dBFS** (§1.3)
  — 8 dB shyer than the classic desk, which is exactly why copied literature ranges land wrong
  (LAW 1, measured in the DISTORTION bible §2.1).
* **The VCA fader / rider.** Broadcast AGC and later Waves Vocal Rider automated the trim: a slow
  servo drives gain toward a target level. This is the `Match` type's ancestry (§5.4) — a gain
  *rider*, not a compressor: seconds-scale, dB-domain, no waveshaping.
* **M/S width.** Mid/Side matrixing (Blumlein's stereo patent — **filed 1931, accepted 1933**,
  GB 394,325; the often-quoted "1934" is the US filing date, so do not print 1934 as *the* date)
  → every modern "utility": width is a single multiplier on S. Ableton Live's Utility (Gain,
  Width 0–400 %, Bass Mono with adjustable crossover, per-channel phase invert) made "the boring
  device" the most-used device in the DAW.
  ⚠️ **UNVERIFIED (2026-08-14):** the exact Live ranges — the Live 12 manual's Utility section could
  not be reached (the audio-effect-reference page truncates before it) and no secondary source gave
  numbers. A prior draft printed "Gain ±35 dB"; Live's Gain fader in fact bottoms at **−∞**, so
  "±35 dB" is wrong as written. Treat *Width 0–400 %* and *Bass Mono crossover* as the load-bearing
  facts and re-check the Gain top (commonly cited as +35 dB) against the device before quoting it.
  Serum 2's `Utility` module transcribes the same idea into the synth rack.
  ⚠️ **UNVERIFIED:** the param set **Polarity Inv L/R · LPF · HPF · Mono Bass + Freq · Width · Pan ·
  Mix** is read off the *screenshot* on What's New p. 12 — the PDF's text layer for that page says
  only *"Utility — New utility effect"*. Do not quote the list as documented; it is our best read of
  the figure, and it is our competitive floor either way.
* **The tilt EQ.** Quad 34 (1982) "tilt" control — one knob pivots the whole spectrum around a
  center frequency. The cheapest possible "make it darker/brighter without thinking" — a
  gain-staging tool in the frequency domain (`Tilt` type, §5.3).
* **The safety clipper.** Every broadcast chain ends in a brick — ours is fb264's −0.3 dBFS
  soft-knee master limiter. `Fence` (§5.6) puts a *local*, earlier brick where the user can hear
  what it eats.

Why it matters commercially: Serum 2 shipped Utility as a first-class module and the community
immediately built preset workflows on it ("The Power of the Utility Insert", YouTube). A $99
Serum-killer without one loses every A/B where the user says "I just want it louder/wider/mono."

---

## 3. HALF A — the multi-instance architecture

### 3.1 What the greats do (verified)

| Host | Dynamic list | Duplicates | Reorder | Leveling hooks |
|---|---|---|---|---|
| **Serum 2** | `+ FX` button, rack list per bus — **two FX busses** (p. 11 verbatim: *"Dual FX Busses — Two separate FX busses"*), plus a separate Main/Direct output channel in the mixer (p. 16) | **"Add multiple instances of a single effect"** (What's New p. 11, verbatim) | drag-and-drop rows (p. 11: *"Rearrange Modules — Drag and drop"*) | per-module bypass (p. 11 text). ⚠️ **UNVERIFIED (figure-read):** per-row Mix knob + output meter, and the mixer fader scales *osc 0…−48, FX bus +12…−36, Main +3…−48 dB* — **no dB number appears anywhere in the PDF's text layer** (re-extracted 2026-08-14). Do not cite these as documented; re-read them off the running plugin before they gate a design decision |
| **Kilohearts Snap Heap** | click empty lane slot to add snapin | unlimited per lane | drag within/between **7 lanes** (✔ verified 2026-08-14, kilohearts.com/docs/snap_heap: *"Snap Heap features seven lanes"*), serial L→R, adjacent lanes pairable parallel | per-lane footer **Gain / Pan / Mix** (✔ verified verbatim); docs verbatim: *"It is advisable that you turn off all lanes that you are not using, to spare some CPU cycles"* |
| **Kilohearts Multipass** | same snapin grammar | unlimited | per-band chains | **up to 5 bands**, per-band chains (the splitter-lane cousin) |
| **Ableton racks** | unlimited devices/chains | unlimited | drag | Utility everywhere; chain Volume per rack chain (Live 12 manual) |
| **Serum 1 (the cautionary tale)** | fixed 10-row rack | **no** — users asked for it by name (r/serum, "Serum FX Tab: is there a way to add additional instances…") | reorder only | — |

Two structural lessons: (a) *every* modern rack treats "device row + per-row mix/meter + drag" as
the atomic unit — exactly Terrain's existing DEVS grammar; (b) Serum 2's *"LFO 7 to LFO 10 appear after
you assign LFO 6"* (What's New p. 14 — ✔ verified verbatim in the PDF text layer 2026-08-14) —
**grow-on-demand UI over a pre-allocated pool** is how a fixed-parameter plugin fakes a dynamic
list. That is the entire trick of §3.2.

### 3.2 🔑 THE HOST CONSTRAINT — parameters cannot be born at runtime

JUCE forum, verbatim (✔ **verified 2026-08-14** — t0m, 8 Jan 2020, thread *"Remove and create audio
parameters after processor creation"*, forum.juce.com/t/…/36941; the URL a prior draft cited,
`/t/adding-modify-audio-parameters-at-runtime/`, **404s and must not be quoted**):
*"JUCE doesn't support a dynamic number of plug-in parameters."* … *"You can change the names of
existing parameters and groups (and hide them in most hosts by giving them an empty name) but
changing the number of them won't work."* VST3/AU hosts cache the parameter list at instantiation;
add/remove breaks automation, preset diffing and some hosts outright. **Therefore: the + button can
never create parameters. It can only claim a pre-allocated SLOT.**

**THE SLOT POOL.** 🚨 **`K = 24` — MAX'S RULING, 2026-08-14 (supersedes the K=5 proposal below and
the CPU-derived justification in §11).** Verbatim: *"You can stack an unlimited number of effects
and use multiple instances of the same effect in Serum 2… we should have an unlimited amount of
effects basically. If people want to choose to do that then their CPU is on them."*

**The design consequences of that ruling, in order:**
1. **We never cap for CPU — we SHOW CPU.** The slot count exists only because hosts cache the
   parameter list, never as a taste or performance judgement.
2. **K = 24** (3 flagships + 24 slots = 27 devices). 25 params/slot × 24 = **600 new params** on
   top of the 964 registered ⇒ **1 564 total** — large but unremarkable (Serum-class plugins ship
   more), and the count is what the host caches once at load, not a per-block cost.
3. 🔑 **AN EMPTY OR BYPASSED SLOT MUST COST EXACTLY ZERO** — not "nearly zero". This is the price
   of a generous pool and it is non-negotiable: no engine allocation until `DEVICE ≠ Empty`, no
   per-block work for a powered-off slot, and the fb342 control-head sleep + fb345 silence-sleep
   applied per slot. Verify with a 24-empty-slot CPU null against the 0-slot build.
4. **Max's usage observation, which the CPU maths confirms:** the devices people actually stack —
   chorus, compressors, EQ, filter, utility — are the *cheap* ones (one-poles, allpass chains, a
   couple of gain computers). Reverb is the expensive device and nobody stacks five of them.
   So the cost curve bends favourably exactly where users pile up. **Design corollary: the cheap
   devices must be AGGRESSIVELY cheap**, because that is where the stacking happens.

*(Original proposal, retained for its reasoning:)* Ship `K = 5` generic slots (3 flagships + 5
slots = 8 devices max, §11 justifies the cap) with an instance-indexed namespace, mirroring the
24-vector ABI (§1.4) plus exactly ONE new field (`DEVICE`):

```
SYN_FXS{k}_DEVICE    choice(16)  FIXED cardinality, sized for the FINAL roster on day one:
                                 0 = Empty · 1 = Reverb · 2 = Delay · 3 = Distortion · 4 = Utility
                                 · 5 = Chorus · 6 = Compressor · 7 = EQ · 8 = Flanger · 9 = Hyper
                                 · 10 = Phaser · 11 = Splitter · 12..15 = "Reserved 1..4"
                                 (registered, greyed in the UI until claimed). See LAW C below.
SYN_FXS{k}_POWER     bool                  default OFF
SYN_FXS{k}_TYPE      choice(32)            FIXED. 32 ≥ the largest Type list we can foresee
                                          (DST is 23 today); entries past a device's list are
                                          greyed and the value CLAMPS per device template — the
                                          fb342 short-list choice trap law
SYN_FXS{k}_K1..K4    float 0..1            front knobs (K4 = Mix, 100 % = fully wet)
SYN_FXS{k}_D1        choice(16)            FIXED. back dropdown 1 (Character-class; 8 today)
SYN_FXS{k}_D2        choice(24)            FIXED. back dropdown 2 (largest existing d2 = delay's
                                          20-row sync list; 24 leaves 4 reserved rows)
SYN_FXS{k}_P1..P8    float 0..1            back-8, relabelled per device+type (SPL/DST precedent)
SYN_FXS{k}_PILL1/2   bool                  front pills
SYN_FXS{k}_SRC_A..SRC_NOISE  bool ×6       route pills → the slot's send bus (A/B/C/D/Sub/Noise,
                                          matching `SYN_DST_SRC_*`, `PluginProcessor.cpp:3539`)
```

**Count it honestly:** 1 DEVICE + 1 POWER + 1 TYPE + 4 K + 1 D1 + 1 D2 + 8 P + 2 PILL + 6 SRC =
**25 params per slot**. **At the ruled K = 24: 25 × 24 = 600 new params on top of the 964 already
registered (§1.5) ⇒ 1 564 total.** (At the old K=5 it was 125 ⇒ 1 089. A prior draft said
"26 × 5 = 130 on top of 972" — both numbers were wrong; the arithmetic and the baseline are
corrected here.) If 1 564 ever proves unwieldy in a specific host, the honest lever is trimming the
6 SRC route pills from slot devices (25 → 19/slot ⇒ 1 420), **not** shrinking K. Host automation lanes read
"FX Slot 2 P5" — generic, but Serum 2's own rack automation reads the same way; per-slot display
names can be refreshed via `AudioProcessorListener::audioProcessorChanged` when DEVICE changes
(names may update; the *count* never does).

🔑 **LAW C — CHOICE CARDINALITY IS FIXED AT BIRTH (fb342), and that binds every list above.**
A choice param's option count is part of the parameter's identity: hosts normalize automation
against it, and Terrain's own read/write path (`round(v·(N−1))`) silently retargets every existing
automation lane and preset if `N` changes. So **`DEVICE`, `TYPE`, `D1` and `D2` may never grow
after ship.** Any "append-only, we'll add devices later" plan is the same defect as adding
parameters at runtime — it is why the counts above are over-sized with explicitly *Reserved*
entries rather than sized to today's roster. The same law is why `SYN_FX_ORDER`'s `choice(6)`
cannot be widened to cover more devices, which is exactly what forces §3.4.

**The flagships stay singletons.** `SYN_RVB_*`, `SYN_DLY_*`, `SYN_DST_*` remain exactly as shipped
— they are certified, preset-referenced, automation-referenced, and UI-bound. A *duplicate* reverb
= a slot with `DEVICE = Reverb`. This is the zero-risk migration: old sessions restore untouched,
`fb345` certification stays valid, and slot code is purely additive.

### 3.3 Engine instances — allocate on claim, never on the audio thread

A slot must be able to host any device, but 5 slots × (9 reverb engines + delay + distortion +
utility) pre-allocated is megabytes of dead state. The law:

1. Each slot owns `std::unique_ptr<SlotEngine>` where `SlotEngine` is a small variant wrapping ONE
   device's engine bank (for Reverb: the 9-engine set + swap machinery, exactly the members the
   singleton uses; for Delay: one `DelayEngine`; for Distortion: one `DistortionEngine`; for
   Utility: §6's ~200 bytes).
2. `DEVICE` changes are handled on the **message thread** (parameterChanged → triggerAsyncUpdate):
   build the new engine, `prepare()` it, then hand it over through a lock-free pointer swap while
   the slot's env fades through zero — the fb345 **deferred char fade + re-seat** grammar, promoted
   from characters to whole devices. The audio thread never allocates (hard RT law).
3. An `Empty`/powered-off slot is **zero cost**: its lambda body is `if (!engine_ || (!power_ &&
   env_ < 1e-4f)) return;` — the same guard the three flagships already run.

Worst-case resident memory at 8 devices ≈ today's 3 + 5 × largest engine. Largest = a Reverb slot
(the 9-bank ≈ convolution's IR + delay lines, low single-digit MB). Acceptable; convolution-in-slot
memory is flagged in §14.

### 3.4 Ordering — the permutation dies, the rank list replaces it

`choice(6)` cannot scale: 4 devices = 24 perms, 8 = 40 320. And **order is topology, not a knob** —
automating it would click by construction (§12.9), the same class of host-machinery trap as
type-dependent latency (DST §4.4). So:

* **Truth:** a `fxChainOrder` **ValueTree property** (string of tokens, e.g. `"rvb dly dst s0 s2"`),
  saved/restored inside the existing `getStateInformation` extras block (`PluginProcessor.cpp:8863`
  version grammar). Not an APVTS param → not automatable → no host PDC/click trap.
* **Legacy:** `SYN_FX_ORDER` stays registered (removing a param is as illegal as adding one). On
  restore: if `fxChainOrder` is absent, derive it from the choice(6) — the 6-row `PERMS` table
  already in both C++ (`:7383`) and JS (`index.html:7976`) is the migration table. Going forward the property
  wins; the param is written once at migration and then frozen (documented stale, like the
  `ParameterIDs.hpp:435` comment it replaces).
* **Dispatch:** the per-sample `switch (fxPerm_)` becomes a per-block-resolved ordered array of
  function pointers / lambdas: `for (auto* f : chainOrder_) (*f)(i);` — identical cost class
  (indirect call vs switch), infinitely ordered. Resolved from the property once per block, exactly
  where `:5860` reads today.
* **Reorder is click-free by crossfade:** on an order change, run ONE block computing both the old
  and new chain outputs and raised-cosine crossfade over that block (~5–15 ms at 48 kHz/512) — the
  "crossfade the two OUTPUTS, not the table" law (DST §6.5) applied to topology. Cost: 2× chain for
  one block, only on a drag-drop. (Devices' own env fades do NOT cover this: a reorder instantly
  changes what a 100 %-wet distortion *eats* — that discontinuity is the click.)

### 3.5 The UI grammar — the + button comes alive

* `DEVS` becomes fully data-driven: the three flagship objects stay, and slot objects are minted
  from a `DEVICE→template` table (each device bible's chassis map is the template: names, opts,
  param-ID stems `SYN_FXS{k}_`). The 24-vector restore loop (`index.html:7947`) is already generic — point it
  at the slot stems.
* `+ Add effect` (`:7705`) opens the device list (Reverb · Delay · Distortion · Utility · … greyed
  when all 5 slots claimed). Choosing writes `SYN_FXS{k}_DEVICE` on the first Empty slot, then the
  slot card renders from its template with Power OFF — **spawning is silent** (Power-OFF default
  law, fb303).
* A slot card gets a small `×` (Remove): confirms, fades power, writes `DEVICE = Empty`, releases
  the engine on the message thread. Remove ≠ delete params — the slot returns to the pool.
* Drag order writes the token list (norm: UI posts the whole order string through the existing
  native-fn channel; C++ stores the property + rebuilds `chainOrder_`). The `SYN_FX_ORDER` write at
  `:8323` is retired behind the migration shim.
* ⚠️ The **WebView bind chain law** (`feedback-terrain-instrument-verify-webview-param-bind-chain`):
  every new `SYN_FXS*` id must join the relay tables or it builds clean and no-ops. Budget a
  harness pass that sets every slot param from JS and reads it back from C++ (the fb319 restore
  vector doubles as the checker).

### 3.6 🔑 THE EXCLUSION-SUM REFACTOR — kill the landmine class, not the next instance of it

Every slot with route pills owns a send bus → the fb305 law demands it join **every** main-send
exclusion. At 8 devices, hand-copying is 64 terms of latent bugs. The fix is one mechanical
refactor, byte-identical for the flagships:

```cpp
// once per sample, before the chain runs — THE single exclusion site:
float rtdL = 0.f, rtdR = 0.f;
for (auto* b : activeSendBuses_)          // flagship 3 + claimed slots, built per block
    { rtdL += b->l[i]; rtdR += b->r[i]; }
rtdL *= outputGain * kVoiceToFxPad;  rtdR *= outputGain * kVoiceToFxPad;
```

Each device's main-send branch then reads the shared `rtdL/rtdR` instead of its private copy.
**New law: a send bus registers itself in `activeSendBuses_` at birth — joining every exclusion sum
automatically, forever.** The three duplicated sums (`:7159/7326/7358` + R twins) collapse to one;
the DST-bible §4.5 "two exact lines" ritual is retired. Verify by the fb305 harness: osc routed to
slot-delay must vanish from every other device's main send, residual ≤ −90 dB.

### 3.7 The latency ledger — and 🔑 LAW A: ZERO LOOKAHEAD, ZERO REPORTED LATENCY, RACK-WIDE

**The rack reports `setLatencySamples(0)` forever, and no device in it may look ahead.** This is not
a preference; it falls straight out of §1.2. The main-send exclusion subtracts the routed dry
**sample-aligned** (`sg = left[i] − rtd[i]`, same index `i`). The moment any device delays the wet
path by *L* samples and asks the host to compensate, the host slides the *whole* plugin output by
*L* while the dry the exclusion subtracted is still the un-slid one — the cancelled dry stops
cancelling and leaks back **phase-smeared** at every device downstream. Consequences, binding on
every device bible:

* **No lookahead limiting.** A ceiling device (§5.6 `Fence`) is a *waveshaper*, never a
  peak-anticipating limiter. Any control that implies anticipation (a "Lead-In"/"Lookahead" knob)
  is banned by construction — it was in a prior draft of §7 and has been cut.
* **No linear-phase / FIR options** anywhere — the tilt, the crossovers, the oversamplers are all
  minimum-phase IIR.
* **Fixed, declared, type-INDEPENDENT latency only.** Latency may not vary with Type, Character or
  Quality tier (the DST §4.4 trap): a device that is 0 at Standard and 4 at High makes the ledger a
  function of a knob.
* If a design genuinely needs latency, the device is **cut or redesigned** — it does not get to
  report it.

Per-device fixed latency constants (DST §4.4 law: fixed per device class, host told 0):
`Reverb 0`, `Delay 0`, `Distortion 8`, `Utility 0` (in **every** Type and **every** Quality tier —
§6.6), future devices declare theirs in their bibles.
⚠️ **Known LAW-A violation, inherited:** the Convolution reverb type runs its wet **512 samples
late** (`ConvolutionReverb.h:25 B = 512`, `:166 getLatency()`) while the rack reports 0 and the
exclusion subtracts sample-aligned. Its dry therefore already leaks smeared today. Duplicating
Convolution into slots multiplies that defect — see §14 Q2; the honest options are (a) fix it to
zero-latency partitioning, or (b) grey Convolution in slots until it is fixed. With N devices the
composition law generalizes:

* Each main-send device delays its *retained-dry* subtraction by the **sum of latencies of devices
  upstream of it in the current order** (one shared integer-delay read per device — the
  `delayN(left − duck·sg)` identity, DST §4.4).
* The shared exclusion sum (§3.6) is computed **per upstream-depth** only when a latent device is
  actually awake; with only zero-latency devices awake, no delay line is in circuit (byte-identical
  default law).
* Reorders re-derive the ledger with the same one-block crossfade masking the transition (§3.4).

### 3.8 CPU scaling — sleep is the contract

* **Per-slot sleep** is mandatory for every engine in a slot: port the `DistortionEngine` detector
  (2048-sample double-silence at 1e-12, closed-form wake — `DistortionEngine.h:513/566/572`) into
  DelayEngine and the reverb bank as part of this epic (they idle-burn today; the fb342 awake-head
  precedent shows −35 % is real).
* **The control head sleeps too** (fb344 law): a sleeping slot skips its per-block param mirror to
  the engine; it re-seats params on wake (the fb345 re-seat law prevents the stale-shape trap).
* Budget gate (§11): 8 devices awake simultaneously must clear the Serum bar on the M1 reference;
  the cap `K = 5` is chosen so the worst legal patch stays inside it.
* UI: sleeping slots dim their rack meter to the idle state — visible truth (LAW 9).

### 3.9 Mod matrix & splitter hooks

* FX params are **not** mod destinations today — the env-routing dest list is a hardcoded
  choice enum (`ParameterIDs.hpp:497–510`, 0..7, synth-side only). When FX destinations land,
  address them as `(slot, paramIndex)` pairs so a "Delay Feedback" route follows the *slot*, not
  the device class — otherwise re-assigning a slot silently retargets the mod. Until the master
  matrix ships, slot params modulate via the existing per-param glide only. (Parked in §14.)
* **Splitter lanes claim slots.** SPLITTER bible §6.3's lane-ownership law ("a lane claims one of
  the three existing devices") generalizes verbatim to "a lane claims one *chain entry* — flagship
  or slot." Lanes therefore multiply instances *through the same pool*: no second allocation
  grammar, and the ownership conflict rules (pills XOR lanes, one owner per device) apply per slot.
  Sizing note: a 3-lane split with 3 owned slots leaves 5 chain entries un-owned — the K = 5 pool
  covers every demo in the Serum 2 splitter material. (What's New p. 13 documents three splitter
  modules by name — **Splitter L/H**, **Splitter L/M/H**, **Splitter M/S** — ✔ verified in the PDF
  text; ⚠️ the "lanes host 1–2 modules each" reading is figure-read, not document text.)

---

## 4. HALF B — inter-device leveling (the anti-mud doctrine)

### 4.1 🔑 THE UNITY-THROUGH LAW

**Every device, powered ON at its default settings, passes program within ±1.0 dB RMS (dry-path
devices: ±0.5 dB) of its powered-OFF level, measured on the certified chord.** (Nominal is the
single note at ≈ −26 dBFS; the certified 3-note chord sits ≈ −20 dBFS = **+6 dBp** in the
COMPRESSOR-bible dBp convention where 0 dBp ≡ −26 dBFS. Both numbers matter below.)

Precedents already on the books: COMPRESSOR bible §4.1 (default bit-transparent), SPLITTER §6.6
(unity ±0.05 dB gate). Serum 2 enforces the same discipline culturally: every module row carries an
output meter so a lying default is *visible* (What's New p. 11). Ableton's device folklore is
identical — Utility exists because devices that add level un-asked make A/B judgment impossible
(the loudness bias: listeners reliably prefer the louder option — SoundGym, Soneam guides).

Consequences:

* Wet-adding devices (reverb, delay) meet the gate through their equal-power Mix defaults — the
  default Mix values (35 %/34 %, `index.html:7479`) were tuned for this; the harness pins them.
* Devices with drive meet it through calibrated makeup (`Auto` pill grammar, DST §4.2 — default
  OFF but the *default drive* itself must be level-honest).
* **The gate is a certification item**: add `unity_gate` to every device cert harness — pink noise
  + the chord at −26 dBFS, 10 s, ON−OFF ΔRMS within gate. Run it per Type, not just per device
  (types re-voice gain paths; the fb345 preset-level-spread findings — Gargle +28 dB hot — are
  exactly what this gate catches earlier).

### 4.2 Headroom policy on a −26 dBFS bus

* The float engine cannot clip internally — headroom between devices is **not** a sample-format
  problem; it is a *perceptual and limiter* problem. The hard boundary is the fb264 master stage
  after `kInstrumentMakeup` (+6.02 dB): peak-limiter threshold = soft-clip knee at **0.90
  (−0.92 dBFS)**, hard ceiling at **0.96605 (−0.30 dBFS)**. A single note at −26 dBFS lands at
  −19.98 dBFS post-makeup ⇒ **≈ 19.1 dB to the limiter**, 19.7 dB to the ceiling.
* **The stacking budget (do the arithmetic once, here).** Each device may boost; boosts SUM.
  *"Clipping a sum of tones = broadband IMD"* (fb264 law) — that grind is mud mechanism #0.
  - fb264 constants, read from source: `kMasterCeiling = 0.96605f` = **−0.30 dBFS**,
    `kLimiterThresh = kSoftClipKnee = 0.90f` = **−0.92 dBFS** (`PluginProcessor.cpp:44–47`). The
    limiter, not the clipper, is what program actually meets — so the **operative ceiling is
    −0.92 dBFS**, and "−0.3" is the DAC-protection catch behind it.
  - The certified chord at −20 dBFS post-`kInstrumentMakeup` (+6.02 dB) sits at **−13.98 dBFS**,
    i.e. **13.1 dB below the limiter threshold**.
  - **Policy:** a single device's factory preset lands ≤ **+7 dB** over its input on the chord
    (leaving ~6 dB of chain headroom); a whole *chain* preset lands its output ≥ **6 dB below the
    limiter threshold** on the chord — i.e. **≤ −20 dBFS at the master input**, measured, and
    certified as a chain (§10.5). A single note (−26 dBFS nominal) therefore has ≈ 19.1 dB of
    post-makeup room to the limiter — that is the number a solo-device preset may spend.
    *(A prior draft's "≤ +13 dB over nominal" silently spent the chord's own +6 dB twice.)*
* ⚠️ **UNVERIFIED:** the corroborating Serum 2 fader ranges (FX bus +12 dB max, Main +3 dB) are
  figure-read from What's New p. 16 — **no dB value exists in that PDF's text layer** (re-extracted
  2026-08-14). The house budget above is derived from OUR measured constants and does not depend
  on them; keep them as colour, not as a source.
* Between-device nominal stays −26 dBFS: a device expecting hotter input states its ranges relative
  to it. COMPRESSOR bible §2.1, quoted correctly: the detector is lifted **+26.02 dB** so
  `0 dBp ≡ −26 dBFS`, and Push maps `T_dBp = +9 − 48·push^0.9` ⇒ threshold travels **+9 dBp →
  −39 dBp** (= **−17 dBFS → −65 dBFS**). *(A prior draft cited "−46…−6 dBFS ≡ −20…+20 rel"; no such
  range exists in that bible.)*

### 4.3 Ordering wisdom (defaults, and the creative inversions)

The classic serial doctrine — **dynamics → gain/dirt → EQ/tone → modulation → time** (iZotope
"Signal Chain: Order of Operations"; the pedalboard guides agree) — and *why* each link holds:

* Compress before dirt: stable drive into the nonlinearity → consistent harmonic bite.
* EQ before time: *"keep unwanted resonances from swelling"* (iZotope) — cut mud before it echoes.
* Time last: reverbs/delays are statistically dense; distorting or compressing them turns space
  into hash (…which is precisely the shoegaze/dub inversion below).

Terrain's grammar: with Utility in the pool, the recommended default order token list is
`utl? · dst · dly · rvb` (dirt → echo → space) = today's **perm 3**, `"Distortion > Delay > Reverb"`
(`PluginProcessor.cpp:3488` StringArray index 3 / `switch` case 3 at `:7389`; a prior draft said
perm 2, which is `Distortion > Reverb > Delay`). Chain presets
(§10.5) teach the inversions instead of forbidding them:

* **Reverb → Distortion** — the shoegaze wall: the tail becomes sustain; needs Fence after or it
  eats the limiter.
* **Delay → Distortion** — dub: repeats degrade per pass equivalent; with feedback the loop-gain
  law (fb306) still bounds it because the dist is OUTSIDE the delay's internal loop — document that
  stacking chain-level feedback around devices is impossible by construction (no chain-level
  feedback path exists; NOTHING FREE-RUNS survives N devices trivially).
* **Utility(Mono) → Reverb** — mono-in, wide-out: the classic space trick.

### 4.4 🔑 What actually makes mud — mechanisms and countermeasures

| # | Mechanism | Physics | Countermeasure (ours, concrete) |
|---|---|---|---|
| 0 | Loudness creep → limiter IMD | stacked boosts push the chord into the −0.3 dB knee; intermod products are broadband | unity gates (§4.1) + stacking budget (§4.2) + rack-rail meters (§8.4) + `Fence` |
| 1 | Low-mid masking buildup | reverb + delay + chorus all *add* 200–500 Hz energy; masking hides transients first | **Correction (source-checked):** the wet low-cut defaults are NOT already in place. `SYN_DLY_LOWCUT` defaults **0.22** (`PluginProcessor.cpp:3472`) but `SYN_RVB_LOWCUT` defaults **0.00** = 20 Hz = *no cut* (`:3434`, UI `index.html:7479` shows `['Low Cut',0,…]`), and the **Vintage** reverb type re-purposes that slot as `setDrive` entirely (`:6997`) so it has no low cut at all. Concrete law: (a) every wet device ships a low-cut param on every Type — Vintage must be re-slotted or given one; (b) the mapping is `20·50^x` Hz, so the chain-preset default is **x = 0.22 ⇒ ≈ 47 Hz** for one wet device and **x = 0.40 ⇒ ≈ 96 Hz** for the second and later wet devices in the same chain; (c) chain-cert gate: summed 100–400 Hz third-octave energy of the default chain ≤ **+3 dB** over the loudest single device in it |
| 2 | Cumulative resonance | multiple resonant stages within ~⅓ oct multiply: two +6 dB peaks aligned = +12 dB ring | Concrete: (a) **≥ ⅓-octave minimum separation** between the resonant/shelf centres of any two factory defaults in a chain preset — enforced by listing the centres in the preset file and diffing them at cert; (b) chain-cert gate on the chord: **no third-octave band of the chain output exceeds the same band of the loudest single device by more than +4 dB**; (c) `Tilt`'s pivot default (632 Hz) is deliberately off the delay/reverb low-cut region so the two never stack |
| 3 | Phase smear | cascaded allpasses (splitter crossovers, phaser stages, oversampler IIRs) rotate phase; crest factor drops, transients "blur" | prefer zero/fixed-latency paths (§3.7); SPL §6.5 phase-matched Mix law; measure crest Δ in chain cert (gate: ≤ 3 dB crest loss through the default chain) |
| 4 | Tail overlap wash | delay feedback tail + reverb decay both exceed note gaps → sustained bed | duck pills (reverb has it, `:7185`), env-gated feedback (NOTHING FREE-RUNS), chain presets pair long-tail devices with ducking on |
| 5 | Width collapse | stacked wideners drive correlation negative; mono playback cancels | correlation meter in Utility (§8), `Mono Below` default in wide chain presets, the SPL `Mono` audition pill precedent |

**Keon's warning, formalized:** mud = any mechanism above compounding *silently*. The doctrine is
(a) unity honesty, (b) bounded boosts, (c) visible meters between stages, (d) one device whose whole
job is repair — the Utility.

### 4.5 Auto-gain and loudness-compensated Mix

* Research: auto makeup *"automatically adjusts the output level of a plugin to match the input
  level"* (Waves); loudness-matched bypass is the only honest A/B (SoundGym, Soneam,
  whylogicprorules — Logic's auto-gain demo shows exactly the bias). But blanket auto-gain changes
  the sound of every knob (drive knobs stop getting louder — half their drama, fb-dramaticism).
* **House position (unchanged, now chain-wide):** per-device `Auto` compensation exists as a pill,
  **default OFF** (DST §4.2 precedent — measured LUFS-match, ±1 dB); Mix knobs stay equal-power
  crossfades; the *explicit* leveler is the Utility `Match` type. Loudness-compensated *bypass
  audition* is a UI affordance, not a DSP default.

### 4.6 Where Utility slots into the story

One sentence: **Utility is the chain's gain-staging made audible, visible and grabbable** — trim
between stages (mechanism 0), tilt/low-cut (1–2), width/mono discipline (5), Match against the −26
nominal (0), Fence before the master (0) — with the meter (d2) that teaches the user what the chain
is doing. Factory chain presets put one Utility where each doctrine point lives (§10.5).

---

## 5. The Utility device — Types (the `Type` dropdown, 6 ship)

Every type is night-and-day with a measurable discriminator (LAW 5), and every type keeps *all 8*
back knobs alive via per-type relabelling (the SPL/DST generic-P precedent — no dead knobs).

### 5.1 `Trim` — the console channel *(reference type)*
The Neve/SSL strip head: gain, pan, per-channel polarity, gentle bound cuts.
**DSP:** `out = g · pan(in)` with g gliding in dB; polarity flips as ±1 on L/R; 1-pole Low Cut /
High Cut bound filters (reuse the SVF grammar).
**Discriminator:** THD+N delta < −120 dB at any Gain; broadband |gain − knob| ≤ 0.05 dB. It is the
only type that is *provably transparent* apart from level.

### 5.2 `Stereo` — the width tool
Ableton/Serum Utility's heart, plus rotation.
**DSP:** M/S matrix `M=(L+R)/2, S=(L−R)/2`; `S *= width` (0–400 %); `Rotate` mixes M↔S by angle
(−45°…+45°); `Mono Below` = LR4 split with the low band summed to mono — the crossover is
`juce::dsp::LinkwitzRileyFilter` (SPL §3.1; ⚠️ **note: it is a JUCE module class, NOT currently
instantiated anywhere in Terrain's `Source/` — `grep -r LinkwitzRiley Source/` returns nothing.
This device would be its FIRST use, so budget the wiring**); Haas `Skew` micro-delays one channel
0–12 ms (integer+frac delay line, `MoogDelay.h` tap grammar).
**Discriminator:** side/mid energy ratio sweeps −∞…+12 dB; correlation meter travels +1 → −1 —
nothing else in the rack moves correlation at constant spectrum.

### 5.3 `Tilt` — the one-knob spectrum lever
Quad 34 lineage.
**DSP:** complementary low-shelf/high-shelf pair, ±9 dB each, pivot knob 100 Hz–6.3 kHz (log,
default 632 Hz).
🔑 **Recycle correction (read, not assumed):** the RBJ shelves are **NOT** in `ParametricEQ.h` — that
class is HP-cascade → 7 peaking bells → LP-cascade and has **no shelf at all** (`ParametricEQ.h`
header comment, `:8–21`). The shelf lives in **`TerrainFilters.h:1314`,
`BellEQ::setShelf (fc, gainDb, bool high, fs)` (RBJ, TDF2)** — and Terrain **already ships this exact
tilt**: `TerrainFilters` `Type::TILT` at `:1827–1833` runs `eqA.setShelf(cut, −g, low)` +
`eqB.setShelf(cut, +g, high)` with `g = (res01 − 0.5)·18` ⇒ **±9 dB**, the identical law. Lift that
case verbatim; do not re-derive it.
⚠️ `BellEQ::setShelf` **hardcodes shelf slope S = 1** (`al = 0.5·sin(w)·√2`), so `Focus` needs the
general term `α = sin(w)/2·√((A + 1/A)(1/S − 1) + 2)` — which already exists in
**`MoogDelay.h:108–140` (`TiltShelf`)**. Take the α line from there.
At max tilt: +9/−9 —
"just past useful" (a full-bright pad is genuinely destroyed, LAW 5's max-position mandate).
**Discriminator:** spectral slope: ±9 dB tilt = ±6 dB/decade measured pink-noise slope change;
level at pivot unchanged ±0.3 dB (that's what makes it *staging*, not EQ).

### 5.4 `Match` — the rider (the −26 law embodied)
Broadcast AGC / Vocal Rider lineage: a slow servo toward Target.
**DSP (dB-domain, block-rate):**
```
env   = rms detector per d2 Meter mode (VU 300 ms · RMS 400 ms · Loud (K-weighted 1-pole pair) · Peak 50 ms)
err   = targetDb − dB(env · g)                       // target stated REL −26 dBFS program
g_dB += clamp(err, ±slew·dt)                          // slew = 6 dB / Speed seconds
g_dB  = clamp(g_dB, ±24 dB)
FREEZE: if dB(env) < −70 rel FS → g_dB holds          // 🔑 NOTHING FREE-RUNS: silence never
                                                      // winds the rider up; gain parks, note dies
```
**Discriminator:** a +10 dB input step returns to Target ±1 dB within 2×Speed; at Speed max (8 s)
it is inaudible motion, at Speed min (0.05 s) it is frank pumping — the knob's 0→100 IS the drama.

### 5.5 `Pump` — Env-1-ridden gain *(the house special)*
"Everything follows Amp env" made a gain tool: gain rides Env 1 (or its inverse) — sidechain-pump
without a compressor, phase-perfect with the note by construction.
**DSP:** `g_dB = −Depth · shape(env1)` (Depth 0–24 dB; `shape` = exp-bias curve knob, the house
curve math law); Invert pill flips to swell. Uses the per-voice-summed Env 1 follower the blooms
already publish — zero new detectors.
**Discriminator:** RMS modulation depth at note rate = Depth ±1 dB, dead-locked to Env 1 timing
(cross-corr lag 0) — no free-running LFO duck can do that.

### 5.6 `Fence` — the local ceiling
The safety clipper, placed where the user chooses instead of only at the master.
**DSP:** soft-knee tanh-blend clip at `Ceiling` (−12…+18 dB rel −26 nominal; default +12), knee
width 3 dB, with 2× oversampling **only** in this type and only above Standard quality (it is the
sole nonlinearity in the device). `Grab` sets how hard the knee leans (soft-sat → hard wall).
**Discriminator:** output peak never exceeds Ceiling +0.2 dB; THD rises only when peaks cross it
(silent under-ceiling transparency ≤ −100 dB THD).

*(Cut candidates considered and folded in: `Flip` (into Trim's polarity + Stereo's rotate), `DC`
(a DC blocker rides free in every type — §6.5).)*

---

## 6. DSP core — math, param laws, stability

### 6.1 Gain, pan, polarity
* Gain knob: **linear-in-dB**, ±36 dB (Serum-Utility-class; Ableton's ±35), center detent 0.
  `g = 10^(dB/20)`, glided in the *linear* domain by the 15 ms one-pole (`hallSm_` class) — dB-side
  stepping through a linear glide is zipper-free at block rates.
* Pan: constant-power `L' = L·cos(θ), R' = R·sin(θ)`, θ = π/4·(1+pan). −3 dB center law keeps
  unity-through at default (pan 0 ⇒ cos=sin=√½ on both ⇒ compensate ×√2: net exactly 1.0).
* Polarity: ±1 multipliers, **crossfaded over 10 ms** (an instant flip is a click by definition).

### 6.2 Width / rotate / Haas
As §5.2. Stability: width ≤ 4 bounds S by +12 dB — the unity gate is on M; anti-correlated content
at width 4 can double peak level → the card's correlation bar warns (LAW 9, visible truth), Fence
catches. Rotation matrix stays orthogonal (energy-preserving by construction).

### 6.3 Tilt shelves
RBJ high/low shelf pair, mirrored gains, shared pivot (`TerrainFilters.h:1314` +
`Type::TILT` at `:1827`, see §5.3); recompute coefficients at block rate with
the weighted-cache-key law (fb344) so sweeping Pivot doesn't churn; coefficient glide via the
filter system's existing re-raster grammar (fb343 card-like-water).

### 6.4 Match & Pump ballistics
Both live entirely in the dB/control domain at block rate (≤ 1 kHz update), then glide the linear
gain per sample — no per-sample log/exp (CPU law). Detector state is the only memory; both freeze
under the −70 dB gate (denormal-safe by the freeze itself + FTZ).

### 6.5 Hygiene (all types)
* **DC blocker** (5 Hz one-pole HP, the corrected in-tree design per DST §4.1) always in circuit —
  free insurance for the whole chain (rotation/asymmetric upstream devices can leave offsets).
* Denormals: FTZ/DAZ already process-wide; detectors add +1e-20 bias.
* NaN fence: a single `std::isfinite` check per block on the slot output propagates a chain-safe
  mute + re-seat instead of poisoning downstream devices (new — cheap, and an 8-device chain makes
  one poisoned device 8× worse).

### 6.6 Oversampling verdict
**None**, except Fence's optional 2× (its clip is the only nonlinearity; everything else is LTI or
control-rate gain — oversampling anything else is pure waste, LAW 8).

🔑 **Latency contribution must be EXACTLY 0, in every Type and every Quality tier** — LAW A (§3.7),
and a prior draft contradicted itself here by budgeting "4 samples internal … only if Quality ≥
High", which would make Utility's ledger entry a function of a dropdown. The binding rule:

* Fence's 2× uses an **allpass-polyphase IIR halfband** — minimum-phase, **no compensation delay
  line, no integer latency**. Its sub-sample group delay is phase, not delay; nothing downstream
  and no exclusion sum is re-aligned for it.
* Standard runs 1× tanh; the knee softness hides the negligible alias at gain-staging levels.
  Switching tier therefore changes *alias floor only*, never timing.
* **Verification gate:** null-test Fence at Quality Standard vs High on a sub-ceiling sine — the
  peak of the difference must show **no sample-shift** (cross-correlation lag = 0). If an
  implementation cannot hit that, **the 2× is cut**, not compensated (LAW A).

### 6.7 Param law table (range · taper · glide)

| Param | Range | Taper | Glide |
|---|---|---|---|
| Gain | ±36 dB | linear-in-dB, detent 0 | 15 ms one-pole (linear domain) |
| Pan | −100…+100 | linear | 10 ms |
| Width | 0–400 % | linear to 100 at knob 50, then log to 400 | 15 ms |
| Mono Below | Off, 50–500 Hz | log | coefficient glide |
| Rotate | ±45° | linear, detent 0 | 10 ms |
| Skew (Haas) | 0–12 ms | t² (fine near 0) | crossfaded delay taps (comb-safe) |
| Tilt | ±9 dB | linear, detent 0 | coefficient glide |
| Pivot | 100 Hz–6.3 kHz | log | coefficient glide |
| Target | ±12 dB rel −26 | linear-in-dB | rider handles it |
| Speed | 0.05–8 s | log | n/a (is a time-constant) |
| Depth (Pump) | 0–24 dB | t^1.3 | env-locked |
| Ceiling | −12…+18 dB rel −26 | linear-in-dB | 15 ms |
| Grab | 0–100 | linear | 15 ms |
| Low/High Cut | 20 Hz–20 kHz | log | coefficient glide |

Time-sync note: Utility has **no tempo param** — the 4-bars→1/256 law (LAW 3) is honored vacuously;
Speed is a physical time-constant, not a musical division (riders in the wild are seconds-domain).

---

## 7. Chassis map — the locked fb275 grammar

**Front card:** hero knobs `Level` (=Gain) · `Width` · `Shape` (the type's signature knob —
per-type alias table below) · `Mix` + the live visualizer (§8) + 2 pills.
**Pills:** `Auto` (loudness-matched output trim, default OFF — DST §4.2 grammar) · type's 2nd:
Trim `Sum` (mono audition) / Stereo `Swap` / Tilt `Flat` / Match `Hold` / Pump `Invert` / Fence `Hard`.
**Back:** d1 `Type` (choice 6, §5) · d2 `Meter` (choice 5: VU · RMS · Loud · Peak · Corr — feeds
the card meter AND Match's detector) · the 4×2:

| | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| **Trim** | Gain | Pan | Low Cut | High Cut |
| | Width | Phase L | Phase R | Drift* |
| **Stereo** | Width | Rotate | Mono Below | Skew |
| | Pan | Side Tone | Gain | Center Keep |
| **Tilt** | Tilt | Pivot | Focus | Gain |
| | Low Cut | High Cut | Pan | Width |
| **Match** | Target | Speed | Range | Gain |
| | Attack Bias | Freeze Below | Pan | Width |
| **Pump** | Depth | Curve | Floor | Gain |
| | Attack Trim | Release Trim | Pan | Width |
| **Fence** | Ceiling | Grab | Knee | Gain |
| | Low Cut | High Cut | Pan | Width |

*Drift = ±0.5 dB slow stereo level wander, env-gated (character, honest, dies with the note).

Two corrections applied to this table (both were defects in the draft it replaces):

1. **Fence's third knob was `Lead-In` — a lookahead control, and lookahead is banned rack-wide
   (LAW A, §3.7).** It is now `Knee` (knee width 0–6 dB around Ceiling, default 3 dB). Nothing in
   this device may anticipate the signal.
2. **Trim's second row began `Balance`** — which is a near-twin of the `Pan` sitting one cell
   earlier in the same type (the no-doubles rule), **and** it left the front hero `Width` with
   nothing to alias in Trim. Replacing it with `Width` fixes both.

**The front-hero alias table** (front knobs are aliases of back params — the DEVS `p:` grammar,
like Delay's Time; a hero with no backing param in some type is a dead knob, LAW 5):

| Type | `Level` → | `Width` → | `Shape` → | `Mix` |
|---|---|---|---|---|
| Trim | Gain | Width | Drift (default 0) | own param |
| Stereo | Gain | Width | Rotate | own param |
| Tilt | Gain | Width | Tilt | own param |
| Match | Gain | Width | Target | own param |
| Pump | Gain | Width | Depth | own param |
| Fence | Gain | Width | Ceiling | own param |

All names pragmatic (say what it DOES), Title-case, acronym-free.
**Chassis arithmetic, stated explicitly so nobody re-litigates it:** the fb275 back panel is
**2 dropdowns + 8 knobs = 10 back params**, and `Mix` is the one front knob that is *not* an alias
⇒ **11 distinct automatable knob/dropdown params per device**, exactly as the spec says. `Power`
and the 2 pills are booleans on the chassis frame, and the 6 route pills belong to the send bus —
they are counted in the §3.2 slot total (25), not in the "11".

Param IDs: singleton `SYN_UTL_*` if Utility also ships as a 4th flagship, else purely slot-hosted
(`SYN_FXS{k}_*`) — **recommendation: slot-only.** Utility is the first *born-multi-instance* device
(you want 2–3 of them by doctrine §4.6), and making it slot-only forces the §3 machinery to be real
on day one instead of retrofitted.

---

## 8. Visualizers

### 8.1 How the greats draw gain staging (mechanisms, precisely)
* **Serum 2 rack rail:** every FX row ends in a vertical stereo output meter beside its Mix knob;
  the mixer page draws faders against printed dB scales with slim peak bars.
  ⚠️ **UNVERIFIED — figure-read only.** What's New pp. 11–12/16 carry no meter or dB text in the
  PDF text layer (re-extracted 2026-08-14); the specific scales (+12…−36 bus, +3…−48 main) could
  not be confirmed. The *mechanism* we are copying — per-module post meters, always-on, tiny — is
  what matters here and is visible in the figures.
* **Serum 2 Compressor row:** numeric THRESH readout + inline band meters in the row — the *device
  teaches its own level math*. ⚠️ **UNVERIFIED figure-read**: the "−18.1 dB" readout and the
  X-LOW/BELOW/X-HIGH labels are our reading of the p. 11 screenshot, not document text.
* **Ableton Utility:** no visualization at all — knobs only. The anti-example: users pair it with
  external meters; we get to fuse the two.
* **Console VU:** 300 ms ballistic needle — the ur-meter; slow enough to read loudness, fast
  enough to dance. **PPM/peak-hold** adds the transient truth VU hides.
* **Goniometer + correlation bar** (SPAN/MSED culture): Lissajous petal cloud — L=R collapses to a
  vertical line, anti-phase to horizontal; correlation bar −1…+1 below.
* **Waves Vocal Rider:** the *moving fader* — the automation the servo writes, shown as motion.
  The single best "what is this thing doing" visual in the leveling genre.

### 8.2 Ours — card concepts (canvas, CPU-cheap, dramatic, param-reflecting)
**A. `The Rider Rail` (recommended core).** A horizontal rail: input level flows in from the left
as a dim wave-band, passes a **ghost fader** that IS the current gain (Trim: parked at the knob;
Match/Pump: visibly riding — the Vocal Rider move), exits right as the bright output band into a
peak-tick against a printed rel-−26 scale + Fence ceiling line when active. Idle = dim ember;
playing = bright bands + the fader breathing. Every sound-changing param moves it: Gain slides the
fader, Target draws a target notch, Ceiling draws the fence line, Depth pulses it with Env 1.
Cost: 2 gradient bars + 1 rect + ticks per rAF from 3 atomics (`utlInPk/utlOutPk/utlGainDb` — the
bloom-publish grammar, `:7395`).
**B. `Orbit` (Stereo type swap-in).** 64-point Lissajous petal (downsampled xy pairs published per
block), width halo scaling with Width, correlation bar beneath sliding +1→−1 into a red zone.
Rotation visibly tilts the cloud. Idle: a single dim dot.
**C. `Tilt Lens` (Tilt swap-in).** The live analyzer's existing spectrum (filter-viz recycle) with
the tilt line overlaid as a lever pivoting at Pivot; audio-reactive bars behind, lever + shaded
gain area in front.
Grammar: one core visual per type family (the DST transfer-curve precedent §5.8), A is the default
and the Match/Pump/Fence/Trim home; B/C swap by Type with the fade-swap law.

### 8.3 The dramaticism gate
Idle→playing delta must be obvious at arm's length (fb311): idle ≤ 15 % luminance, signal ≥ 80 %,
peak ticks flash on Fence grabs. If Max "can barely see" the rider move at Speed 8 s, the ghost
fader gets a motion-trail (5-frame afterimage) — motion, not brightness, carries slow riders.

### 8.4 The rack rail (HALF A's chain-wide viz)
Every chain entry (flagship or slot) publishes `outPk` the way blooms do today; the rack draws a
3 px stereo meter on each card edge (Serum 2 row-meter precedent). Sleeping = dim, awake = live.
This is ~free (atomics exist for 3 devices already: `hallBloomViz_` etc.) and is the §4.4
"visible meters between stages" countermeasure. Ship it with the slot chassis, not later.

---

## 9. Interplay — behavior in the chain

* **Unity-through:** Utility at defaults is bit-transparent except the DC blocker (< −0.01 dB at
  20 Hz) — passes the §4.1 gate with margin; the strictest device in the rack, as it must be.
* **Downstream spectrum/dynamics:** Trim/Match/Pump are LTI-per-instant gain — they change *level
  into* downstream nonlinears (a rider before the distortion = slow auto-drive: intentional,
  audible, worth a preset). Tilt pre-distortion re-voices harmonics (its real power). Fence before
  reverb removes the spiky transients that ring plates. Stereo *after* reverb widens the tail
  without touching the dry.
* **Classic positions (the doctrine, §4.6):** head-of-chain Trim (stage the synth into the FX at
  nominal), mid-chain Tilt/Match (repair between dirt and time), tail Fence (protect the master).
  Chain presets encode all three (§10.5).
* **What breaks when stacked:** two Matches fight (both servo the same level — slower one loses;
  cap: chain cert warns when two Match instances are both awake, §12.13); stacked Stereos
  compound width → correlation red zone (the meter is the guard); stacked Fences are harmless
  (lower ceiling wins).
* **Feedback:** none anywhere in the device — trivially satisfies the loop-gain law; max stable
  loop gain n/a (and §4.3 notes the chain itself has no feedback topology by construction).

---

## 10. Presets — 12 device sketches + 4 chain templates

Device presets (Type · intent · key values):
1. **Stage Set** — Trim · park program at nominal · Gain 0, Low Cut 30 Hz, High Cut 20 k.
2. **Nudge Up** — Trim · the honest +6 · Gain +6, all else flat.
3. **Phone Check** — Trim · sum + band-limit audition · `Sum` pill on, Low Cut 300, High Cut 3.4 k.
4. **Wide Open** — Stereo · chorus-tail bloom · Width 180 %, Mono Below 120 Hz, Skew 0.
5. **Big Mono** — Stereo · club-safe low end · Width 120 %, Mono Below 240 Hz, Rotate 0.
6. **Haas Ghost** — Stereo · instant double · Skew 9 ms, Width 100 %, Mono Below 150 Hz.
7. **Darker** — Tilt · one-knob warmth · Tilt −4.5 dB, Pivot 632 Hz.
8. **Air Lift** — Tilt · presence tilt · Tilt +3 dB, Pivot 1.6 k, Focus 65.
9. **Hold Steady** — Match · invisible rider · Target 0, Speed 4 s, Range ±9.
10. **Iron Grip** — Match · audible AGC pump · Target +3, Speed 0.12 s, Range ±24 (the 100 %-is-
    destructive end, proudly).
11. **Duck Under** — Pump · note-locked pump · Depth 9 dB, Curve 60, Invert off.
12. **Last Fence** — Fence · master saver · Ceiling +12 rel, Grab 35, `Hard` off.

Chain templates (§4 doctrine as factory racks; each certified as a chain per §4.2):
13. **Staged Classic** — Trim → Distortion → Delay → Reverb → Fence.
14. **Shoegaze Wall** — Reverb → Distortion → Fence (inversion #1, §4.3).
15. **Dub Bounce** — Delay → Distortion → Utility(Big Mono) → Reverb.
16. **Two Delays** *(the multi-instance hero demo)* — Delay(1/8 dotted) → Utility(Darker) →
    Delay(1/4, `Ping`) → Reverb — impossible before slots; ship it first.

---

## 11. CPU budget

* **Utility instance:** all types ≤ ~40 flops/sample stereo + block-rate control ≈ **< 0.05 %** of
  one M1 core at 48 k — the cheapest device in the rack. Fence at High (2×) ≈ 0.15 %. No tier
  needed below Fence-High: Quality dropdown reads `Off/Standard/High/Ultra` but only Fence listens
  (documented on the card tooltip; not a dead dropdown — it is chassis-shared).
* **Chain scaling:** cost = Σ awake devices only (sleep law §3.8). Budget gate for the epic: the
  8-device worst legal patch (3 flagships + 5 slots all awake, heaviest types) ≤ **2× today's
  3-device worst case** on the reference machine — enforced by extending `dst_cpu` into a
  `chain_cpu` harness. If it fails, K shrinks before quality does (Serum is the bar, LAW 8).
* Meters/viz: atomics + rAF only; no per-frame shadowBlur/filters (fb344 law).

---

## 12. Pitfalls — collected, each with its kill

1. **Adding params at runtime** — hosts cache the list; **pre-allocated slot pool only** (§3.2).
2. **The exclusion-sum landmine at N×N** — single shared `rtd` sum + bus self-registration (§3.6).
3. **Reorder clicks** — order is topology: one-block dual-run crossfade (§3.4); never automate order.
4. **Slot engine allocation on the audio thread** — message-thread build + pointer-swap + fade (§3.3).
5. **Zipper on gain/pan/polarity** — every control glides (§6.1); polarity crossfades, never flips.
6. **Haas Skew combing while dragged** — crossfaded taps, not a swept tap (§6.7).
7. **Match winds up in silence then blasts the next note** — the −70 dB freeze gate (§5.4);
   NOTHING FREE-RUNS.
8. **Width 400 % mono-collapse** — correlation meter + `Mono Below` + chain-preset discipline (§4.4 #5).
9. **Stacked boosts → limiter IMD mud** — unity gates + stacking budget + Fence (§4.1–4.2).
10. **Two riders fighting** — chain cert warns on dual awake Match (§9).
11. **Slot param stale on device swap** — re-seat on claim/wake (fb345 law, §3.3/§3.8).
12. **NaN poisoning an 8-device chain** — per-slot finite fence + mute/re-seat (§6.5).
13. **WebView bind silently no-ops for `SYN_FXS*`** — the bind-chain harness pass (§3.5).
14. **Latency ledger drift after reorders** — ledger re-derived with the order, crossfade masks (§3.7).
15. **DC offsets accumulating across asymmetric devices** — Utility's always-on DC blocker +
    per-device DC laws (DST §4.1).
16. **Preset short-list choice trap** on slot TYPE (`choice(32)` hosting shorter lists) — clamp per
    device template (fb342 law, §3.2).
17. **Denormals in idle detectors/meters** — FTZ + bias (§6.5); sleeping slots skip detectors entirely.
18. **Growing a choice list after ship** ("we'll append device 6 later") — cardinality is fixed at
    birth exactly like the param count; every list is over-sized with *Reserved* entries on day one
    (LAW C, §3.2).
19. **Any lookahead or latency-reporting device** — the sample-aligned exclusion sum makes the dry
    leak back smeared; ZERO lookahead, ZERO reported latency, latency never a function of Type or
    Quality (LAW A, §3.7/§6.6). The killed `Lead-In` knob (§7) is the canonical near-miss.
20. **A front hero knob with no backing param in some Type** — a dead knob by construction; the
    alias table in §7 must be complete for every Type before a Type ships.

---

## 13. Hard-rule compliance checklist (Laws 1–10, walked)

1. **Bus reality (−26 dBFS):** every Target/Ceiling/threshold stated rel −26 (§5.4, §5.6, §6.7);
   stacking budget derived from the measured **−0.92 dB limiter-threshold** distance, not the
   −0.30 dB ceiling (§4.2). No literature range copied raw.
2. **Chassis (fb275):** 2 dropdowns (`Type`, `Meter`) + 4×2 back knobs + front hero-4 + 2 pills
   (§7); **11 automatable knob/dropdown params** (10 back + `Mix`, the one non-aliasing front
   knob — arithmetic spelled out in §7); pragmatic Title-case names throughout, and every front
   hero has a backing param in every Type (§7 alias table).
3. **Time params 4 bars→1/256:** vacuously satisfied — no musical-division param exists; Speed is
   a physical constant, documented (§6.7).
4. **Mix 100 % = fully wet; no switch cuts audio:** Mix is the standard equal-power insert; Type
   swaps use the fade-swap-recover grammar; device claiming/removal fades through silence (§3.3, §3.5).
5. **Params evolve 0→100 / types night-and-day:** per-type discriminators (§5), no dead knobs via
   per-type relabels (§7), Speed/Depth/Width extremes explicitly destructive (Iron Grip, §10).
6. **Nothing free-runs / loop gain:** Match freeze gate, Pump rides Env 1, Drift env-gated, no
   feedback path in device or chain topology (§5.4, §5.5, §9).
7. **No clicks:** glide table (§6.7), polarity crossfade, reorder crossfade (§3.4), sleep/wake
   re-seed (§3.8).
8. **CPU-friendly:** §11; oversampling only Fence-High (and it must stay **zero-latency in every
   tier**, §6.6); sleep contract chain-wide; control-rate detectors.
9. **Audible ⇔ visible + dramatic:** Rider Rail/Orbit/Tilt Lens all param-reflecting with idle-dim
   deltas (§8.2–8.3); rack rail meters make the whole chain visible (§8.4).
10. **Recycle first:** §15 — `BellEQ::setShelf` + the already-shipped `Type::TILT` case, `MoogDelay`
    `TiltShelf`/tap grammar, `hallSm_` glide class, bloom atomics, DEVS/24-vector ABI, the
    `DistortionEngine` sleep detector, `IndyFxChain` precedent, PERMS tables. Two entries are
    **new wiring, not recycling**, and are marked as such: `juce::dsp::LinkwitzRileyFilter` (never
    instantiated in Terrain) and the rack-rail meters (§8.4).

---

## 14. Open questions for Max

1. **Slot count:** K = 5 (8 devices total) is the CPU-derived proposal — enough? (Serum 2 is
   unbounded but its modules are lighter than our reverbs.)
2. **Duplicates of Convolution reverb:** each claimed Reverb slot with Type Convolution costs IR
   memory + the 512-latency outlier — allow, or grey Convolution in slots until its latency fix lands?
3. **Order automation:** the rank-property design deliberately makes order non-automatable
   (click-free by construction). Acceptable, or do you want snapshot-morphable order someday?
4. **Second FX bus** (Serum's Bus 1/Bus 2 + per-osc bus balance knobs): parked — is one chain +
   splitter lanes enough for v1 of the epic?
5. **Utility as slot-only** (recommended, §7) vs also a 4th fixed flagship card?
6. **Match default Target:** 0 (= sit at nominal) proposed; or slightly hot (+3) for perceived-
   loudness safety in A/B against Serum?
7. **Chain templates in the preset browser:** own category ("Racks"), or appended to device presets?
8. **The Noise route pill:** slots get 6 route pills incl. Noise (matching fb338); confirm Noise
   stays route-eligible for slot devices.

---

## 15. Recycle inventory (verified by reading, with addresses)

| Need | Reuse | Address |
|---|---|---|
| Serial insert grammar + env fades | applyRvb/applyDst/applyDly lambdas + `hallSm_` (`~15 ms`, set at `:3812`) | `PluginProcessor.cpp:7137/7309/7345` |
| Exclusion-sum refactor seed | the three identical `rtd` expressions | `:7159/7161, :7326/7328, :7358/7360` |
| Send-bus plumbing | reverb/delay/dst send buffers | `PluginProcessor.h:1534/1559/1572` |
| Gain constants | `kVoiceToFxPad`, `kInstrumentMakeup` | `PluginProcessor.cpp:6300, :46` |
| Sleep/wake | `asleep_` detector + closed-form wake | `DistortionEngine.h:513/566/572–584` |
| Private-instance precedent | `IndyFxChain` (shared values, own state) | `IndyFxChain.h:1–40` |
| Crossover (Mono Below) | LR4 TPT — ⚠️ **JUCE module class, NOT yet used anywhere in Terrain `Source/`** (first use; SPL bible §3.1) | `_tools/JUCE/modules/juce_dsp/processors/juce_LinkwitzRileyFilter.h` |
| Shelves (Tilt) | `BellEQ::setShelf` (RBJ, TDF2) — **not** `ParametricEQ.h`, which has no shelf | `TerrainFilters.h:1314` |
| Tilt law itself (±9 dB mirrored pair) | `TerrainFilters` `Type::TILT` case — already shipped, lift verbatim | `TerrainFilters.h:1827–1833` |
| Shelf slope `S` (for `Focus`) | `TiltShelf::update` general α term (BellEQ hardcodes S = 1) | `MoogDelay.h:108–140` |
| Haas delay line | `MoogDelay.h` fractional tap grammar | `MoogDelay.h` |
| Viz publish | bloom atomics + rAF poll | `PluginProcessor.cpp:7393–7400`, `index.html` bloom poll |
| Rack UI + restore ABI | DEVS objects + 24-vector + `fxr-add` | `index.html:7479 / 7947 / 7705` (styles `:7256–7259`) |
| Order migration | PERMS tables (C++/JS twins) | `PluginProcessor.cpp:7383`, `index.html:7976` (restore) + `:8320` (drag) |
| State versioning | V2 extras block + migration | `PluginProcessor.cpp:8810 / 8863 / 9008 / 9145` |
| Auto pill law | measured-LUFS output match, default OFF | DISTORTION bible §4.2 |
| Latency composition | fixed-N/report-0 + dry-side delay identity | DISTORTION bible §4.4 |
| Lane ownership | claim/own/merge grammar | SPLITTER bible §6.3 |

---

## 16. Sources

* Serum 2 — *What's New in Serum 2* (official PDF; FX rack p. 11, modules/Utility p. 12, splitters
  p. 13, LFO 7–10 p. 14, Mixer p. 16): https://static.xferrecords.com/Serum%202%20What's%20New.pdf
  ✔ **Re-fetched and text-extracted 2026-08-14 (20 pp.).** Verbatim in the text layer: *"Choose from
  13 effects and 3 splitter modules"* (p. 3/11) · *"Add multiple instances of a single effect"*
  (p. 11) · *"Dual FX Busses — Two separate FX busses"* (p. 11) · *"LFO 7 to LFO 10 appear after you
  assign LFO 6"* (p. 14) · the three splitter names (p. 13) · *"Utility — New utility effect"*
  (p. 12). ⚠️ **The document contains NO dB values anywhere in its text layer** — every dB number
  attributed to it in earlier drafts is a figure-read and is flagged inline as unverified.
* Serum 2 User Guide (official, v1.0.3): https://www.xferrecords.com/manual/serum-2/docs
* Serum 2 effects guide (bus routing examples): https://monosounds.studio/serum-2-effects-guide/
* Serum 2 feature breakdown ("unlimited FX duplication … two FX busses"):
  https://sonic-weaponry.com/blogs/free-production-tutorials-and-resources/serum-2-released
* Serum 1 duplicate-FX demand (the cautionary tale):
  https://www.reddit.com/r/serum/comments/1b4uqgf/serum_fx_tab_is_there_a_way_to_add_additional/
* Kilohearts Snap Heap docs (7 lanes, lane Gain/Pan/Mix, CPU advice): https://kilohearts.com/docs/snap_heap
  ✔ **Verified 2026-08-14** — *"Snap Heap features seven lanes"*; footer controls Gain/Pan/Mix;
  *"It is advisable that you turn off all lanes that you are not using, to spare some CPU cycles."*
* Kilohearts Snap Heap product page: https://kilohearts.com/products/snap_heap
* Kilohearts Multipass (5 bands): https://kilohearts.com/products/multipass
* JUCE forum — dynamic parameter counts unsupported (quoted §3.2). ✔ **Verified 2026-08-14**:
  *"Remove and create audio parameters after processor creation"*, reply by **t0m, 8 Jan 2020** —
  https://forum.juce.com/t/remove-and-create-audio-parameters-after-processor-creation/36941
  Corroborating threads on the same site: *"Dynamic Number of Parameters"* (/t/…/8928),
  *"Dynamically alter number of plugin parameters?"* (/t/…/4745),
  *"AudioProcessor removeParameter?"* (/t/…/15608).
  ⚠️ The URL a prior draft cited — `forum.juce.com/t/adding-modify-audio-parameters-at-runtime/` —
  **404s; that thread does not exist.** Do not re-introduce it.
* Ableton Live 12 manual, Audio Effect Reference (Utility):
  https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/
  ⚠️ **NOT verified 2026-08-14** — the page truncates at Filter Delay before the Utility section,
  and the walkthrough below carries no numbers either. Every Live Utility RANGE in §2 is therefore
  unverified; re-check against the device before quoting.
* Ableton Utility walkthrough: https://www.musicguymixing.com/ableton-live-utility/
* iZotope — Signal Chain: Order of Operations:
  https://www.izotope.com/community/blog/signal-chain-order-of-operations
* Waves — What is Auto Makeup Gain: https://www.waves.com/what-is-auto-makeup-gain-in-compressor-plugins
* SoundGym — Loudness Bias: https://www.soundgym.co/blog/item?id=loudness-bias-why-plugins-sound-better
* Soneam — Why Louder Sounds Better / fair A/B: https://www.soneam.com/guides/why-louder-sounds-better/
* Level-matching case study: https://whylogicprorules.com/level-matching-plugins/
* Ordering guides (corroboration): https://peasydesign.com/guides/audio-effects-chain-order-guide/ ·
  https://tonestakr.com/guides/pedalboard-signal-chain-order/ ·
  https://motionbeach.com/blog/audio/audio-effects-in-mixing-is-there-a-correct-order/
* In-tree contracts: `Design/DISTORTION-BUILD-BIBLE.md` §2.1/§4.2/§4.4/§4.5/§6.5 ·
  `Design/SPLITTER-BUILD-BIBLE.md` §3.1/§6 · `Design/COMPRESSOR-BUILD-BIBLE.md` §2.1/§4.1
