# Terrain Instrument — FX Chain Bible
### Multi-instance architecture + the Utility (gain-staging) device
*The foundation document for the MULTI-DEVICE CHAIN epic — "the biggest task yet."*
*Research locked 2026-08-14. Written against tree fb345 (70de2d9). Every file:line below was read, not assumed.*

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
   among them), **"Add multiple instances of a single effect"** verbatim, drag-reorder, per-module
   bypass + Mix + output meter, across **Main / Bus 1 / Bus 2** (What's New in Serum 2, pp. 3, 11–12, 16).
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
  `applyRvb` (`PluginProcessor.cpp:7137`), `applyDst` (`:7310`), `applyDly` (`:7347`),
  dispatched by `switch (fxPerm_)` over the **6-way permutation** (`:7383–7392`).
* `fxPerm_` is read once per block at `:5860` from `SYN_FX_ORDER` — an `AudioParameterChoice(6)`
  built at `PluginProcessor.cpp:3488`. ⚠️ The comment at `ParameterIDs.hpp:435` still says
  *"bool: false = Reverb→Delay"* — stale since fb341; fix in passing.
* Each device is an **env-gated insert**: reverb/delay use `leftChannel[i] += wet·w − duck·sg`
  (equal-power sin/cos mix, `:7112–7114`); the distortion uses the fb318 **replace grammar**
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

### 1.3 Gain constants (the −26 dBFS reality's plumbing)

* `kVoiceToFxPad = 0.5f` (−6.02 dB) — the pre-FX pad on every send and main-send read
  (`PluginProcessor.cpp:6300`).
* `kInstrumentMakeup = 2.0f` (+6.02 dB) — the fb299 measured-Serum-match output makeup
  (`PluginProcessor.cpp:46`; single note −20 → −14 dBFS = Serum's −14.01).
* The fb264 master limiter soft-knees at **−0.3 dBFS** after makeup — the only clipper in the path.
* Net: **FX-bus program sits ≈ −26 dBFS** (measured, DISTORTION bible §2.1). Every threshold,
  drive, target and ceiling in this file is stated **relative to −26 dBFS program** (LAW 1).

### 1.4 The UI rack

* `DEVS` array (`index.html:7479`) — one JS object per device: `core:` key
  (`'reverb'|'delay'|'saturate'`), `pwrP/tp/rp/pp` param IDs, 4 front `knobs`, `back:{d1,d2,knobs[8]}`.
* **The `+ Add effect` button already exists**: rendered at `:7705`
  (`<div class="fxr-add" data-act="add">`), styled `:7256–7259`, drag-insert respects it `:8311`.
  Today it spawns nothing — it is the door §3 walks through.
* Drag order → `SYN_FX_ORDER` at `:8317–8324`: builds the core-key sequence, matches it against the
  6-row `PERMS` table, writes `pi/5` normalized. Restore: `fxrRestoreOrder` (`:7974–7988`).
* Per-device restore reads a **24-slot vector** (see `fxrRestoreDistortion`, `:7955–7972`):
  `[type, power, front×4, d1, d2, route×6, pills×2, back×8]`. This vector IS the de-facto device
  state ABI — §3.3 reuses it verbatim.

### 1.5 The precedents the architecture stands on

* **Sleep:** `DistortionEngine.h` — `asleep_` gate (`:513`), 2048-sample double-sided silence
  detector (`:566`, in/out both < 1e-12), closed-form wake re-seed (`:572–584`). The proven
  per-device CPU kill-switch. Reverb/delay lack it — §3.8 spreads it.
* **Multi-instance DSP:** `IndyFxChain.h:1–40` — *"One private FX-chain instance … own copies of
  all 7 global FX modules … shares APVTS parameter VALUES … but has INDEPENDENT STATE."* Terrain
  has already built a second instance of a whole chain once. What it has never done is give an
  instance **independent parameters** — that is the actual new ground (§3.2).
* **State:** `getStateInformation` (`PluginProcessor.cpp:8810`, `apvts.copyState()` + versioned
  extras), `setStateInformation` (`:9008`, `replaceState` at `:9145`, V1→V2 migration precedent).
* **Latency:** DISTORTION bible §4.4 — fixed-8/report-0, dry-side delay composition
  `delay8(left − duck·sg) = delay8(left) − duck·delay8(sg)`; `ConvolutionReverb.h:166` still runs
  512 late and silent. The multi-instance ledger (§3.7) generalizes this.
* Param registry: **972** `constexpr char` IDs in `ParameterIDs.hpp` (counted); FX today ≈ 76
  (53 RVB+DLY, ~23 DST).

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
* **M/S width.** Mid/Side matrixing (Blumlein, 1934) → every modern "utility": width is a single
  multiplier on S. Ableton Live's Utility (Gain ±35 dB, Width 0–400 %, Bass Mono with adjustable
  crossover, per-channel phase invert — Live 12 manual) made "the boring device" the most-used
  device in the DAW. Serum 2's `Utility` module transcribes it into the synth rack: **Polarity Inv
  L/R · LPF · HPF · Mono Bass + Freq · Width · Pan · Mix** (What's New p. 12) — that exact param set
  is our competitive floor.
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
| **Serum 2** | `+ FX` button, rack list per bus (Main/Bus 1/Bus 2) | **"Add multiple instances of a single effect"** (What's New p. 11) | drag-and-drop rows | per-module Mix + output meter on every row; mixer faders: osc channels 0…−48, FX bus channels **+12…−36**, Main **+3…−48** dB (p. 16) |
| **Kilohearts Snap Heap** | click empty lane slot to add snapin | unlimited per lane | drag within/between **7 lanes**, serial L→R, adjacent lanes pairable parallel | per-lane footer **Gain / Pan / Mix**; docs: *"turn off all lanes that you are not using, to spare some CPU cycles"* |
| **Kilohearts Multipass** | same snapin grammar | unlimited | per-band chains | **up to 5 bands**, per-band chains (the splitter-lane cousin) |
| **Ableton racks** | unlimited devices/chains | unlimited | drag | Utility everywhere; chain Volume per rack chain (Live 12 manual) |
| **Serum 1 (the cautionary tale)** | fixed 10-row rack | **no** — users asked for it by name (r/serum, "Serum FX Tab: is there a way to add additional instances…") | reorder only | — |

Two structural lessons: (a) *every* modern rack treats "device row + per-row mix/meter + drag" as
the atomic unit — exactly Terrain's existing DEVS grammar; (b) Serum 2's LFO 7–10 "appear after you
assign LFO 6" (What's New p. 14) — **grow-on-demand UI over a pre-allocated pool** is how a
fixed-parameter plugin fakes a dynamic list. That is the entire trick of §3.2.

### 3.2 🔑 THE HOST CONSTRAINT — parameters cannot be born at runtime

JUCE forum, verbatim: *"JUCE doesn't support a dynamic number of plug-in parameters. You can change
the names of existing parameters and groups … but changing the number of them won't work."* VST3/AU
hosts cache the parameter list at instantiation; add/remove breaks automation, preset diffing and
some hosts outright. **Therefore: the + button can never create parameters. It can only claim a
pre-allocated SLOT.**

**THE SLOT POOL.** Ship `K = 5` generic slots (3 flagships + 5 slots = 8 devices max, §11 justifies
the cap) with an instance-indexed namespace, mirroring the 24-vector ABI (§1.4) plus two:

```
SYN_FXS{k}_DEVICE    choice(N_DEVICES+1)  0 = Empty · 1 = Reverb · 2 = Delay · 3 = Distortion
                                          · 4 = Utility · 5.. = future devices (append-only!)
SYN_FXS{k}_POWER     bool                  default OFF
SYN_FXS{k}_TYPE      choice(23)            the device's Type list (max cardinality of any device —
                                          the DST 23 sets the size; smaller lists clamp, the
                                          fb342 short-list choice trap law)
SYN_FXS{k}_K1..K4    float 0..1            front knobs (K4 = Mix, 100 % = fully wet)
SYN_FXS{k}_D1        choice(8)             back dropdown 1 (Character-class)
SYN_FXS{k}_D2        choice(20)            back dropdown 2 (largest existing d2 = delay's 20-row sync list)
SYN_FXS{k}_P1..P8    float 0..1            back-8, relabelled per device+type (SPL/DST precedent)
SYN_FXS{k}_PILL1/2   bool                  front pills
SYN_FXS{k}_SRC_A..SRC_NOISE  bool ×6       route pills → the slot's send bus
```

26 params × 5 slots = **130 new params** on top of 972 — well inside APVTS practice. Host
automation lanes read "FX Slot 2 P5" — generic, but Serum 2's own rack automation reads the same
way; per-slot display names can be refreshed via `AudioProcessorListener::audioProcessorChanged`
when DEVICE changes (names may update; the *count* never does).

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
  saved/restored inside the existing `getStateInformation` extras block (`PluginProcessor.cpp:8861`
  version grammar). Not an APVTS param → not automatable → no host PDC/click trap.
* **Legacy:** `SYN_FX_ORDER` stays registered (removing a param is as illegal as adding one). On
  restore: if `fxChainOrder` is absent, derive it from the choice(6) — the 6-row `PERMS` table
  already in both C++ (`:7383`) and JS (`:7978`) is the migration table. Going forward the property
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
  param-ID stems `SYN_FXS{k}_`). The 24-vector restore loop (`:7955`) is already generic — point it
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

### 3.7 The latency ledger

Per-device fixed latency constants (DST §4.4 law: fixed per device class, host told 0):
`Reverb 0` (convolution's internal 512 stays its own documented outlier until fixed), `Delay 0`,
`Distortion 8`, `Utility 0`, future devices declare theirs in their bibles. With N devices the
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
  covers every demo in the Serum 2 splitter material (p. 13 shows lanes hosting 1–2 modules each).

---

## 4. HALF B — inter-device leveling (the anti-mud doctrine)

### 4.1 🔑 THE UNITY-THROUGH LAW

**Every device, powered ON at its default settings, passes program within +1.0 dB RMS (dry-path
devices: ±0.5 dB) of its powered-OFF level, measured on the −26 dBFS certified chord.**

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
  problem; it is a *perceptual and limiter* problem. The only hard boundary is fb264's −0.3 dBFS
  soft-knee after `kInstrumentMakeup` (+6.02 dB). Program at −26 dBFS has ≈ 19.7 dB of post-makeup
  headroom before the knee.
* **The stacking budget:** each device may boost; boosts SUM. Three devices at +7 dB each puts the
  chord into the limiter, and *"clipping a sum of tones = broadband IMD"* (fb264 law) — that
  grind is mud mechanism #0. Policy: any factory preset of any device lands its output ≤ −6 dB
  under the knee on the chord test (i.e. ≤ +13 dB over nominal), and *chain* factory presets
  (§10.5) are certified as a chain.
* Serum 2's own numbers agree: FX-bus faders reach **+12 dB** max, Main **+3 dB** (What's New
  p. 16) — the host grants each stage a bounded boost and keeps the sum survivable.
* Between-device nominal stays −26 dBFS: a device expecting hotter input (compressor thresholds!)
  states its ranges relative to it (COMPRESSOR bible §2.1 — thresholds −46…−6 dBFS ≡ −20…+20 rel).

### 4.3 Ordering wisdom (defaults, and the creative inversions)

The classic serial doctrine — **dynamics → gain/dirt → EQ/tone → modulation → time** (iZotope
"Signal Chain: Order of Operations"; the pedalboard guides agree) — and *why* each link holds:

* Compress before dirt: stable drive into the nonlinearity → consistent harmonic bite.
* EQ before time: *"keep unwanted resonances from swelling"* (iZotope) — cut mud before it echoes.
* Time last: reverbs/delays are statistically dense; distorting or compressing them turns space
  into hash (…which is precisely the shoegaze/dub inversion below).

Terrain's grammar: with Utility in the pool, the recommended default order token list is
`utl? · dst · dly · rvb` (dirt → echo → space), matching today's perm-2 family. Chain presets
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
| 1 | Low-mid masking buildup | reverb + delay + chorus all *add* 200–500 Hz energy; masking hides transients first | wet-path Low Cut defaults already exist (`SYN_RVB_LOWCUT`, `SYN_DLY_LOWCUT` def 22); law: every wet device ships a low-cut param and a non-zero factory default in chain presets |
| 2 | Cumulative resonance | multiple resonant stages within ~⅓ oct multiply: two +6 dB peaks aligned = +12 dB ring | spread factory centers; the EQ bible's dynamic-band note; audit chord spectra in the chain cert |
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
(−45°…+45°); `Mono Below` = LR4 split (in-tree TPT crossover, SPL §3.1) with the low band summed
to mono; Haas `Skew` micro-delays one channel 0–12 ms (integer+frac delay line, MoogDelay grammar).
**Discriminator:** side/mid energy ratio sweeps −∞…+12 dB; correlation meter travels +1 → −1 —
nothing else in the rack moves correlation at constant spectrum.

### 5.3 `Tilt` — the one-knob spectrum lever
Quad 34 lineage.
**DSP:** complementary low-shelf/high-shelf pair, ±9 dB each, pivot knob 100 Hz–6.3 kHz (log,
default 632 Hz), RBJ shelves from `ParametricEQ.h`. `Focus` narrows shelf S. At max tilt: +9/−9 —
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
RBJ high/low shelf pair, mirrored gains, shared pivot; recompute coefficients at block rate with
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
control-rate gain — oversampling anything else is pure waste, LAW 8). Latency contribution: 0
(Fence's 2× uses the polyphase IIR at 4-sample internal cost *inside* the fixed budget only if
Quality ≥ High; Standard runs 1× tanh — knee softness hides the negligible alias at gain-staging
levels ≥ −26 dBFS program).

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

**Front card:** hero knobs `Level` (=Gain) · `Width` · `Shape` (type's signature: Tilt/Target/
Depth/Ceiling relabel) · `Mix` + the live visualizer (§8) + 2 pills.
**Pills:** `Auto` (loudness-matched output trim, default OFF — DST §4.2 grammar) · type's 2nd:
Trim `Sum` (mono audition) / Stereo `Swap` / Tilt `Flat` / Match `Hold` / Pump `Invert` / Fence `Hard`.
**Back:** d1 `Type` (choice 6, §5) · d2 `Meter` (choice 5: VU · RMS · Loud · Peak · Corr — feeds
the card meter AND Match's detector) · the 4×2:

| | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| **Trim** | Gain | Pan | Low Cut | High Cut |
| | Balance | Phase L | Phase R | Drift* |
| **Stereo** | Width | Rotate | Mono Below | Skew |
| | Pan | Side Tone | Gain | Center Keep |
| **Tilt** | Tilt | Pivot | Focus | Gain |
| | Low Cut | High Cut | Pan | Width |
| **Match** | Target | Speed | Range | Gain |
| | Attack Bias | Freeze Below | Pan | Width |
| **Pump** | Depth | Curve | Floor | Gain |
| | Attack Trim | Release Trim | Pan | Width |
| **Fence** | Ceiling | Grab | Lead-In | Gain |
| | Low Cut | High Cut | Pan | Width |

*Drift = ±0.5 dB slow stereo level wander, env-gated (character, honest, dies with the note).
All names pragmatic (say what it DOES), Title-case, acronym-free. 11 params total per the spec:
2 dropdowns + 8 knobs; front knobs alias back params (the DEVS `p:` grammar, like Delay's Time).

Param IDs: singleton `SYN_UTL_*` if Utility also ships as a 4th flagship, else purely slot-hosted
(`SYN_FXS{k}_*`) — **recommendation: slot-only.** Utility is the first *born-multi-instance* device
(you want 2–3 of them by doctrine §4.6), and making it slot-only forces the §3 machinery to be real
on day one instead of retrofitted.

---

## 8. Visualizers

### 8.1 How the greats draw gain staging (mechanisms, precisely)
* **Serum 2 rack rail:** every FX row ends in a vertical stereo output meter beside its Mix knob;
  the mixer page draws faders against printed dB scales (+12…−36 bus, +3…−48 main) with slim
  peak bars (What's New pp. 11–12, 16). Mechanism: per-module post meters, always-on, tiny.
* **Serum 2 Compressor row:** numeric THRESH readout (−18.1 dB) + X-LOW/BELOW/X-HIGH band meters
  inline in the row (p. 11) — the *device teaches its own level math*.
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
16. **Preset short-list choice trap** on slot TYPE (choice(23) hosting shorter lists) — clamp per
    device template (fb342 law, §3.2).
17. **Denormals in idle detectors/meters** — FTZ + bias (§6.5); sleeping slots skip detectors entirely.

---

## 13. Hard-rule compliance checklist (Laws 1–10, walked)

1. **Bus reality (−26 dBFS):** every Target/Ceiling/threshold stated rel −26 (§5.4, §5.6, §6.7);
   stacking budget derived from the measured −0.3 dB knee distance (§4.2). No literature range copied raw.
2. **Chassis (fb275):** 2 dropdowns (`Type`, `Meter`) + 4×2 back knobs + front hero-4 + 2 pills
   (§7); 11 params; pragmatic Title-case names throughout.
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
8. **CPU-friendly:** §11; oversampling only Fence-High; sleep contract chain-wide; control-rate
   detectors.
9. **Audible ⇔ visible + dramatic:** Rider Rail/Orbit/Tilt Lens all param-reflecting with idle-dim
   deltas (§8.2–8.3); rack rail meters make the whole chain visible (§8.4).
10. **Recycle first:** §15 — TPT crossover, ParametricEQ shelves, hallSm_ glide class, bloom
    atomics, DEVS/24-vector ABI, sleep detector, IndyFxChain precedent, PERMS tables.

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
| Serial insert grammar + env fades | applyRvb/applyDst/applyDly lambdas + `hallSm_` | `PluginProcessor.cpp:7137/7310/7347` |
| Exclusion-sum refactor seed | the three identical `rtd` expressions | `:7159/7161, :7326/7328, :7358/7360` |
| Send-bus plumbing | reverb/delay/dst send buffers | `PluginProcessor.h:1534/1559/1572` |
| Gain constants | `kVoiceToFxPad`, `kInstrumentMakeup` | `PluginProcessor.cpp:6300, :46` |
| Sleep/wake | `asleep_` detector + closed-form wake | `DistortionEngine.h:513/566/572–584` |
| Private-instance precedent | `IndyFxChain` (shared values, own state) | `IndyFxChain.h:1–40` |
| Crossover (Mono Below) | TPT LR4 (SPL bible §3.1, in-tree) | filter system |
| Shelves (Tilt) | RBJ shelf coefficients | `ParametricEQ.h` |
| Haas delay line | `MoogDelay.h` fractional tap grammar | `MoogDelay.h` |
| Viz publish | bloom atomics + rAF poll | `PluginProcessor.cpp:7395+`, `index.html` bloom poll |
| Rack UI + restore ABI | DEVS objects + 24-vector + `fxr-add` | `index.html:7479/7955/7705` |
| Order migration | PERMS tables (C++/JS twins) | `PluginProcessor.cpp:7383`, `index.html:7978` |
| State versioning | V2 extras block + migration | `PluginProcessor.cpp:8810/8861/9008` |
| Auto pill law | measured-LUFS output match, default OFF | DISTORTION bible §4.2 |
| Latency composition | fixed-N/report-0 + dry-side delay identity | DISTORTION bible §4.4 |
| Lane ownership | claim/own/merge grammar | SPLITTER bible §6.3 |

---

## 16. Sources

* Serum 2 — *What's New in Serum 2* (official PDF; FX rack p. 11, modules/Utility p. 12, splitters
  p. 13, LFO 7–10 p. 14, Mixer p. 16): https://static.xferrecords.com/Serum%202%20What's%20New.pdf
* Serum 2 User Guide (official, v1.0.3): https://www.xferrecords.com/manual/serum-2/docs
* Serum 2 effects guide (bus routing examples): https://monosounds.studio/serum-2-effects-guide/
* Serum 2 feature breakdown ("unlimited FX duplication … two FX busses"):
  https://sonic-weaponry.com/blogs/free-production-tutorials-and-resources/serum-2-released
* Serum 1 duplicate-FX demand (the cautionary tale):
  https://www.reddit.com/r/serum/comments/1b4uqgf/serum_fx_tab_is_there_a_way_to_add_additional/
* Kilohearts Snap Heap docs (7 lanes, lane Gain/Pan/Mix, CPU advice): https://kilohearts.com/docs/snap_heap
* Kilohearts Snap Heap product page: https://kilohearts.com/products/snap_heap
* Kilohearts Multipass (5 bands): https://kilohearts.com/products/multipass
* JUCE forum — dynamic parameter counts unsupported (quoted §3.2):
  https://forum.juce.com/t/adding-modify-audio-parameters-at-runtime/ (and the sibling
  "Remove and create audio parameters after processor creation" thread)
* Ableton Live 12 manual, Audio Effect Reference (Utility):
  https://www.ableton.com/en/live-manual/12/live-audio-effect-reference/
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
