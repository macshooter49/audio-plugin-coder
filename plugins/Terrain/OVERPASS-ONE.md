# OVERPASS ONE — the master list
*Max, 2026-08-28. This is the canonical list. It outlives any chat compaction. Do not lose it.*

Order is Max's, not mine. **The broken things go LAST** — his words: *"those are gonna be the
final creases."* Big wins first.

---

## 0 · THE "ALL" FOLDER — do this first, it is an easy win
Every cascade menu needs an **`All`** entry at the **top of the folder list**, containing every
item from every subfolder — **including user content**. Wavetables, filters, warp modes, presets,
the lot. Max: *"every single folder, every single subfolder, at the top is the top of the folder
name 'All'."*

## 1 · THE WAVETABLES — the biggest remaining win
**25 of 30 factory generators stop at a hard-coded harmonic count** (`Wavetable.h`: `h <= 96`,
`<= 64`, `<= 48`, `< 32`). The largest `numHarmonics` in the whole bank is **28**; "Prophet Saw"
has **24**; the reference's init table has **183**. Proof it is content and not band-limiting:
Prophet Saw reads N60 = 24 at C1 *through* C5 identically, with **h24 at −29.31 dBc and h25 at
−134.22 dBc — a 104.9 dB cliff across one harmonic with Nyquist 22 kHz away.**

Everything built in the overpass is being fed thin material. Fixing this makes every shaper,
every FM setting and every raised ceiling sound better retroactively.

Plan is written and the prototype is measured: `scratchpad/WAVETABLE-PLAN.md`. TERRA-01 is
**15 numbers and a seed** — no stored data — and hits **99.5%** of available bandwidth against
Prophet Saw's 38%, through the SHIPPED bake and playback path with zero code changes.
Build order: superset mip ladder + per-level frame length (bit-identical below C6) → `kMaxHarmonics`
512→1024 → the TERRA kernel + a grep gate that fails the build on a new hard-coded ceiling → new
tables at index 30+ **across all ten sites in ONE commit** → Catmull-Rom re-measured → **do NOT
re-tune the legacy 30**, ship TERRA additively so Max can A/B.
🚨 **THE TEN-SITE INDEX TRAP**: `index.html:16172` normalises by DOM option count, not parameter
cardinality. Grow the C++ list while any one `<select>` still lists 30 and index 29 writes 1.0 →
selects table 45. Sites: `WavetableBank.h:44-82` + `:145-178`, `PluginProcessor.cpp` 2532/2728/
2888/3048, `index.html` 6239/6513/6806/7080, plus copies at `:33298`, `:33355`, clamp at `:34845`.

## 2 · UNISON GETS ITS OWN BUTTON SET — on EVERY engine   ✅ SHIPPED fb531
**Built.** Six shapers on the page — **Stack · Range · Detune · Warp · Blend · Width** — with Voices
staying on the front `.uni-pill`. Stack is a CHOICE so it is a pill FIRST in the row, the `.fm-algo`
idiom verbatim (Max: *"put the pill in the front like FM's algo pill"*). Chevron cycles and wraps;
no page dots. **NO DSP WORK WAS NEEDED** — `URANGE`/`USTACK`/`UWARP` were already registered, pulled
(`PluginProcessor.cpp:9277-9287`), pushed to the voice (`:9330`), consumed in the render loop
(`SynthVoice.h:3519/3520/3620`) **and modulatable**; they had simply never had a control. The
`(inert)` note on UWARP was a stale comment, now corrected.
🚨 **TWO CSS TRAPS, BOTH INVISIBLE TO EVERY OTHER CHECK** (compile, auval and pluginval all pass
with the page unreachable): the wrap must live in `.front-only`, NOT after the geode wrap — that one
is inside `.sample-view`, which is `display:none` on five engines of seven; and the row must
out-specify FM's and Harm's `:not(.X-knobs)` hides (1 id + 7 classes) or it renders 0x0 on exactly
those two. Found by a per-engine DOM ancestor walk. Verified 7/7 engines at 287x38, and the arrow
cycle measured WT page1->UNISON->page1, FM page1->page2->UNISON->page1.

*(original spec below)*
Max: *"Unison is connected to all the engines."* Every engine gets an arrow, and unison lives
behind it as its own knob page:
- engine has no second page → **unison is page 2**
- engine already has a second page (FM, Harmonics, Modal, Geode, Sample) → **unison is page 3**
- Sample's blend mode adds a page when active → unison becomes **page 3 when blend is on, page 2
  when it is off**
Pattern to copy exactly, do not invent: the **`gran-knob-wrap` / `gk-arrow`** two-page idiom
(`index.html:4812-4826`, replicated 5×, all toggling `.pg2`), **6 knobs per page**.
Params already shipped and live: `URANGE` (5..4800 cents), `USTACK` (9 intervals), `UWARP`,
`UDETUNE`, `UBLEND`, `UWIDTH` (bipolar).

## 3 · PHASING TAKES UNISON'S OLD PLACE ON THE BACK PANEL   ✅ SHIPPED fb532 · REPAIRED fb544

> **fb544 — it did not actually work, and here is the measurement.** Two defects, both found
> by driving the shipped AU rather than reading the code:
> 1. **The pills were wired to nothing.** fb538 built them on `Juce.getSliderState()`, but
>    `SYN_OSC_x_PHASE` / `_PHASE_AMT` have **no `WebSliderRelay`** — only `_PHASE_MODE` does.
>    In the page `setNormalisedValue(0.5)` reported success and read back 0.5 while the host
>    still read Phase = 0. Rand displaying **`0`** for a parameter whose value is **1.0** was the
>    visible tell. Rewired onto `__setSynParam` / `getSynParam`, the same path the FX rack uses
>    for exactly this reason. Now: drag +75 px → reads `180°`, host reads `0.5`.
> 2. **Phase was note-on only.** `resolvePhase` ran in `startNote` and nowhere else, so turning
>    the knob under a held note did nothing. MEASURED against Serum 2 holding one note and moving
>    its `A Phase`: **Serum +4.7 dB, ours −79.8 dB (bit-identical)**. Serum's phase is a
>    CONTINUOUS read offset; note-on seeds only the random part on top of it. Ours now is too:
>    **−79.8 → +6.0 dB**, with the note-on phase unchanged (R = 1.0000, +89.69° per quarter
>    turn vs Serum's +89.56°) and an untouched patch **bit-identical** (FNV1a `4dd4e4e947978375`
>    on 96,000 samples, both builds).
>
> Readout is now **degrees** (`0°`–`360°`) and Rand **0–100**, which is what Serum shows;
> the PARAMETERS stay 0..1, the scale Serum automates on and the one fb538 ruled.
> Reaches **WT and FM** — the two engines that use the phase accumulator. SPEC/HARM/MODAL
> render their own oscillator and are unaffected; SAMP/GRAN are silent with no sample loaded,
> so they are unmeasured rather than "no effect".
**Built.** The back-panel pill that was UNISON is now PHASE, in the same slot at the same size
(row measured 302x28, 4 pills, unchanged). Right-click gives the four modes directly —
**Manual · Random · Free · Spread** — and also picks which value the drag edits
(**Phase · Rand · Voices · Mode**); choosing Manual or Random jumps the value to the knob you
want next. Detune/Blend/Width LEFT the pill (fb531 put them on the unison page — they were
duplicated); **Voices stayed, because this pill is its only control in the entire UI** and
dropping it would have stranded the parameter.
**NO DSP WAS WRITTEN.** `resolvePhase` already computed Serum's exact model,
`wrap(Phase + Rand x random)`. Measured through both AUs with `Tests/phase_probe.cpp`:
at Rand 0 both track **+90.00 deg per quarter turn with R = 1.0000**, the two curves 1.69 deg
apart (our sine vs their saw). Our Rand goes DEEPER at full (R 0.4048 vs their 0.6939) —
lifeguard law, keep it.
🚨 **WHY IT DIDN'T PHASE**: the shipped default mode was FREE, whose branch returns the carried
accumulator and ignores BOTH knobs. Proof: the measured phases marched continuously across a
Phase sweep (270→269→321→322→14→14→67→67, +52.5 deg/note) with no jump at any parameter change.
Default is now **Random**, matching Serum's own init (`A Rand Phase` default 1.0). A fresh
instance measures **R = 0.2771** — it phases out of the box. 🔒 The library is untouched:
`migrateBlobToVersion3` is version-gated and pins every older blob to Free.

*(original spec below)*
Unison vacates the back panel; **phasing moves in** — random phase + current phase position,
modelled on the reference's measured set: `A Phase` (0–360°, 361 steps) and `A Rand Phase`
(0–100 amount, default 100 = fully random). Terrain's `SYN_OSC_x_PHASE` / `_PHASE_AMT` /
`_PHASE_MODE` already exist and are LIVE as of fb526 — this is the UI half.

## 4 · FILTER AS A WARP MODE, AND AS A MODULATION SOURCE   ✅ **BOTH HALVES — fb543 + fb556**
**Half A — filter AS a warp mode: DONE.** Modes **35 `LP Filter` / 36 `HP Filter`**, a TPT
state-variable filter. Corner in **harmonic number** so it rides the note; **amount 0 = wide
open = transparent** (our fb462 law — note this is the OPPOSITE direction to Serum, measured:
raising *their* warp ADDS harmonics, so their 0 is the closed end); **VAR = resonance** (Q 0.5–10).
Runs on the SUMMED osc, not per sine: a filter is LINEAR so `Σf(xᵢ) = f(Σxᵢ)` exactly, giving the
identical result for 1/16 the cost — 12 KB of state across all 96 voices instead of ~530 KB.
(Diverges only when UWARP ≠ 0, which fans the amount per sine; documented in the code.)
Measured: corner tracks the note at ratio **1.00–1.01** across two octaves; LP 0.85 leaves
**1.98 harmonics** against a predicted 2.07. Gate: `Tests/warp_filter_probe.cpp`.
### ✅ Half B — the filter IS a modulation source, fb556
Two new sources, **`Follow Filter 1` / `Follow Filter 2`** (wire 215/216), joining fb552's family.
Drag the word **Filter** on the filter device onto any knob; **one grip, resolved by the ACTIVE
slot** at drop time — the same rule the cutoff *destination* already follows, so "the filter" means
the filter you are looking at. The tap is taken inside `filterBuses` and is guarded by an identity
test (`&f1 == &filterSlot_`) because that lambda also serves the SEND filters and every pooled
duplicate: without it the last one to run each sample would win.

**MEASURED.** Osc B runs through Filter 1; **Osc A is dry and never touches it**; `Follow Filter 1
→ Osc A Level` at full depth. Vary how hard the filter is driven, and Osc A follows:

| Osc B level | Osc A, no route | Osc A via Follow F1 | ratio |
|---|---:|---:|---:|
| 0.00 | 0.1250 | **0.0000** | — (nothing in the filter, nothing to follow) |
| 0.15 | 0.1250 | 0.0184 | 0.1227 |
| 0.35 | 0.1250 | 0.0430 | 0.1229 |
| 0.50 | 0.1251 | 0.0614 | 0.1228 |
| 0.80 | 0.1251 | 0.0983 | 0.1229 |

Exactly proportional — the ratio holds to 0.2 % across the sweep — while the unrouted control is
flat. Osc A's loudness *is* the filter's output level.

**🚨 IT IS A FOLLOWER AND NOT AN AUDIO TAP, AND THAT IS STRUCTURAL, NOT LAZY.** The reference offers
`FM (Filter 1)`, i.e. the filter's own SAMPLE into the blend slot. `renderNextBlock` is **two**
per-sample loops: oscillators and the blend stage first (the modulator taps land at the end of that
one, line ~5489), then the filters (~5800). A filter value read by the blend stage is therefore one
whole **BLOCK** old, not one sample — a modulator whose delay is the host's buffer size is not a
sound you can ship, and it would break under fb-"the host may subdivide the buffer". Merging the two
loops is the real fix and it is a real cost: 94 filter types, oversampling, send and pooled
duplicates, series and parallel routing, all on the hottest path in the voice. An **envelope** of
the filter has no such problem — a block of delay on a contour is nothing — so that is what ships,
and the audio-rate version stays open with its price written down instead of half-built.

⚠️ One thing I could not demonstrate in the harness: **cutoff-dependence**. The follower tracks the
filter's output level exactly (above), but I could not get a cutoff sweep to move anything, and I
could not tell whether that is filter type 27's own response or my two carriers sitting 45 Hz apart
(too close for the demodulator to separate). Flagging it rather than claiming it.

*(original spec below)*
The reference has LPF and HPF as per-oscillator warp modes, and can cross-modulate **from a
filter's output** (`FM (Filter 1)`, `RM (Filter 2)`). We have neither. 78 of our 94 filter types
are ≤232 bytes and allocation-free, so the reuse is cheap — but **exclude the 16 delay-line types**
(CombCore is 96 B + 32 KB heap/channel ⇒ 8.2 MB).

> **Open: Var IS the resonance and has no knob.** fb538 deleted the Warp Var / W2 Var knobs, so
> Q (0.5–10, measured: it lifts the centroid 3.45 → 5.14) is reachable only from the mod matrix.
> **Owner is now item 6A** — the filter extension card is the right home for it. Deliberately NOT
> giving it a temporary knob, which 6A would only delete.

## 5 · REAL HARD SYNC   ✅ SHIPPED fb545 — *and PolyBLEP was not what was wrong*
**The premise of this item was a measurement artifact, and the fix is one constant.**

The item said our sync "loses harmonics" and needed PolyBLEP for real bandwidth. Measured against
Serum 2 before writing any code, that is not what is happening.

**What is actually true.** Hard sync's harmonics come from the DISCONTINUITY at the master wrap —
the slave jumps from phase `frac(R)` back to 0. When **R is an exact integer** the slave completes
a whole number of cycles per master period, the jump height is `table(0) − table(0) = 0`, there is
no sync at all, and what is left is a transposed table band-limited by a mip picked for the faster
rate — so the harmonic count **divides by R**. That is physics, not a bug.

The bug was **where those zeros landed**. `2^(4a)` puts R = 1, 2, 4, 8, 16 at a = 0, .25, .50, .75,
1.0 — every detent, both endpoints, and the double-click default. Serum swept 0→1 never drops below
200 peaks until a = 1.0 exactly. **This is also the true source of the "nharm 183 → 45 → 8" that got
fb522's raise reverted in fb523**: that cert sampled .25/.50/.75, which are precisely the three dead
points. Reproduced exactly (Terra Stack, 203 peaks dry): 102 / 51 / 27 at those settings, and 212 /
215 at the non-integer settings either side.

**Fix — the ratio mapping, one named constant each** (`kSyncExp2` / `kFormantExp2` / `kFractalMul`,
defined once and read by BOTH `applyPhaseWarp` and `warpRateMul`, which structurally kills the
fb523 two-places trap):

| Terra Stack, npeaks | a=0.25 | 0.50 | 0.75 | 1.00 | |
|---|---|---|---|---|---|
| **Sync** `2^(4a)` → `2^(4.6a)` | 102 → **200** | 51 → **215** | 27 → **212** | 13 → **210** | ceiling 16× → 24.25× |
| **Fractalize** `1+7a` → `1+7.5a` | 215 → 215 | 216 → 213 | 204 → **215** | 27 → **208** | max was the one dead point |
| **Formant** — **deliberately NOT raised** | 205 → 204 | 206 → 205 | 216 → 215 | 191 → 191 | see below |

**Formant shares Sync's line but not Sync's defect.** Its `sin(pi*p)` window is zero at both ends
of the master period, so it removes the very discontinuity Sync lives on — its harmonics come from
the window, integer ratios cost it nothing, and it never had dead detents. Raising it to 4.6 was
tried and reverted in the same session: 202 / 213 / 196 / **111**, a third of the density gone at
the top for brightness. That is exactly the trade fb523 reverted and it is still a bad one.

**PolyBLEP was not needed and was not added.** The alias floor was measured at the harmonic
MIDPOINTS `(k+0.5)·f0`, where a periodic signal has nothing and Hann leakage is below −100 dB:

    Serum 2 Sync   −122 → −99 dBc   (worsens as it brightens)
    Terrain  after −107 → −119 dBc  (flat, at the dry floor)

We alias *less* than the reference because the mip is already chosen for the slave rate, so the
edge is formed from band-limited material. Adding BLEP would be risk with no measurable gain.
`uSyncPhase*_` stays an unused stub. **At full travel we now hold 210 peaks where Serum drops to
13** (its own max is an integer ratio).

**Also fixed: Formant is transparent at amount 0.** Its window used to apply at full depth
regardless of the knob — peak 0.078049 warp-off vs 0.018869 with Formant at 0, a different render
entirely. Same "not transparent at amount 0" defect this doc files against Sine Shaper. All four
modes (off / Sync / Formant / Fractalize) at amount 0 now render **bit-identically**,
FNV1a `420f4c70e44f4039` over 96,000 samples — and that gate is live, because Formant failed it.

⚠️ Patches using Sync or Fractalize **above 0 will move** — that is the point of the fix. Amount 0
is bit-identical, so anything with warp off or parked at zero is untouched.

## 6 · WARP EXTENSION CARDS   ✅ SHIPPED — chassis + 6A/6B fb546 · **6C fb550**
*"the warp modes are gonna have extension cards based on what particular mode that's at… I told
you I wanted that same distortion card that we have for the effect rack distortion to be an
extension. We right-click, we have an option to extend at the top of the menu… We can also do
that for the filter — it'll pop out the filter, we already have a filter extension where we can
click the resonance so we can actually sculpt and draw the resonance… that's what I want, the
extension cards, to be able to make our own shapes — our own custom warp modes, our own shapes
that warp the wavetable."*

**This absorbs the old item 6** (*"something creative, something Serum 2 doesn't have… plus
custom/drawable warp shapes — he expects that to be the best one"*). Same feature: the drawable
warp shape IS an extension card. The card system is the chassis that ask was always waiting for,
and it retro-fits every warp mode we have already shipped.

**The affordance already exists.** `openTwoPaneBrowser` takes `cfg.importLabel` + `cfg.onImport`
and pins an action button at the top of the picker (`index.html:34676`) — that is literally
"an option to extend at the top of the menu". The warp picker already routes through that same
browser (`warpPickerOpen`, `:19396`). So this is a new panel, not new menu plumbing.

**SHIPPED fb546 — one card, not three.** A warp mode is exactly one of three things and each is
already a pure static in the DSP, so the card asks C++ for its curve (`getWarpCurve`) instead of
reimplementing thirty-odd modes in JS — the same fb458 law that keeps the waterfall honest:

| kind | modes | the curve | measured |
|---|---|---|---|
| **filter** | 35–36 | \|H\| vs **harmonic number** — an impulse through the shipped `warpFiltTick` | LP at amt .5 → CORNER 11, resonant peak |
| **amp** | 9–34 | the transfer curve, `applyAmpWarp` | Hard Clip rails at ±1, identity diagonal behind it |
| **phase** | 1–8 | where in the cycle it reads, `applyPhaseWarp` | Sync at .35 → **3 ramps + a 0.05 partial** = R 3.05 ✓ |

So **6A and 6B both landed in the one card**, and 6B needed no new warp slot after all — modes 11–34
*are* the distortion family (Tube, Tape Sat., Diode, Overdrive, the folds…), and they now all open.
Phase modes came free, which means Extend works on every live mode rather than two.

**The body is the instrument**, the same ruling as the main filter's `.filt-ext` (Serum manual
p.142): drag it. X and Y are always the slot's two dimensions. MEASURED end-to-end through the
shipped AU — a click at 25 % / 25 % of the body:
    amp    → Warp Amount **0.248**, Warp Var **0.753**
    filter → Warp Amount **0.752** (x is the CORNER, so dragging right OPENS it), Var **0.753**
🎯 **This closes fb543's open resonance question.** Var IS the Q and it now has a home better than
the knob would have been.

**Extend rides the browser's top action row** (`importLabel`/`onImport`, index.html:34676) — Max:
*"an option to extend at the top of the menu"*. Offered only when a mode is selected: measured 1
row at mode 35, **0 rows at None**, so that gate can fail. `osc` and `slot` are DERIVED from the
mode param id both call sites already had, so nothing new is plumbed through two unrelated pills.

**THE SIGNATURE IS THE AXIS.** On a filter mode x is **harmonic number** — 1 2 4 8 16 32 64 128 —
because this filter's corner rides the note, so those gridlines stay put at every pitch. That is
the one thing separating it from the main filter and nothing else in the plugin has that axis.
On amp and phase the faint diagonal is the identity: distance from it is what the mode does.

⚠️ **Two things deliberately not done yet**, both honest gaps rather than oversights:
- **No live spectrum under the curve.** The rack's filter card has one; this does not, so it is
  not yet "dramatically audio-reactive" by the house rule. It reacts to the knobs, not the audio.
- **6C · DRAW YOUR OWN WARP SHAPE** — the prize, and the reason the chassis exists. A drawn curve
  becomes the phase/amp map of a custom mode in a RESERVED slot (37–47).


**ROOM: slots 37–47 are free — eleven more warp modes.** Answers Max's *"unless there's a way to
add more… maybe we can do this with all types of filters"*: yes. We ship **94 filter engines**
already, so band-pass / notch / comb / formant can each be a warp mode, each opening the same card.

**⚠️ WHAT THE FILTER WARP ACTUALLY IS — Max asked, and he is right about the effect but the
mechanism is worth stating exactly.** It does NOT reshape the wavetable data or the phase read.
The other 34 warp modes are phase-domain remaps (they bend *where* in the cycle you read) or
amp-domain shapers (they bend the *value* you got). Modes 35/36 are neither: a real stateful TPT
filter running on that oscillator's **summed** output (`SynthVoice.h:3765`), after the unison sum,
before the mix, before the main filter. So "it's already a filter built inside of it" is correct.
It sounds different from the main filter for three reasons, all deliberate: the corner is in
**harmonic number** so it tracks the note (the main filter is in Hz and dulls as you play up);
it is **per-oscillator and pre-mix** so it can carve osc A against osc B; and **both warp slots
chain**, so LP in slot 1 + HP in slot 2 is a per-osc band-pass.

## 7 · FME / FML — missing capabilities, not depth   ✅ SHIPPED fb551
The reference ships **three** FM curves and we shipped one. Blend modes **9 `FM Exp`** and
**10 `FM Clamp`** close it — the blend slot is our equivalent of its FM-family warp, because ours
already picks its own source (Osc A-D · Sub · Noise · Self · LFO 1-10).

- **FM** (mode 1, unchanged) — thru-zero. A deviation in **Hz** added to the read rate; drive it
  negative and the carrier reverses and keeps oscillating.
- **FM Exp** (mode 9) — the deviation is a **ratio**: `f = f_c·2^(10·d·mod)`. ⚠️ The only blend mode
  that is pitch-proportional, and deliberately so: exponential FM is a 1 V/oct law. The carrier's
  rate comes back under its own name (`blendCarrInc_`), read by modes 9/10 and nothing else —
  fb523's deletion of `repInc[]` from mode 1's line stands.
  **Octaves SUM across slots** (2^a·2^b), so multiple slots compose correctly and cost one exp per
  carrier per sample rather than one per slot.
  🔑 **It takes a LINEAR taper, not the 361:1 one**, and the measurement is why: its depth is
  already an exponent, so the FM taper on top made the knob doubly exponential and crushed the mode
  into its last 15 % (centroid 113 Hz at half travel against mode 1's 3,076). Linear in octaves
  lands on mode 1's own curve — two exponentials in `d` with similar bases.
  🔑 **No pitch drift**, and not by luck: exponential FM classically runs sharp, and `kFmLeak`'s
  2.29 Hz corner turns a constant frequency offset into a constant PHASE offset for free.
- **FM Clamp** (mode 10) — linear FM that is **not** thru-zero: the total instantaneous rate is
  clamped at zero, so the carrier stalls but never reverses. Same ceiling, same taper as mode 1;
  the clamp is the only difference and it is the whole mode.

**MEASURED — stationary tone, sine carrier at 110 Hz, modulator = Osc B, every point taken on a
settled state (the row before it discarded).** `alias` = peak at the `(k+0.5)·f0` midpoints, where a
periodic signal has nothing.

| knob | FM centroid / alias | FM Exp | FM Clamp |
|---|---|---|---|
| 0.00 | 110 Hz · −118 dBc | 110 Hz · −119 | 110 Hz · −119 |
| 0.10 | 237 · −115 | 126 · −118 | 213 · −111 |
| 0.30 | 897 · −114 | 232 · −116 | 498 · −111 |
| 0.50 | 3,121 · −110 | 900 · −110 | 1,803 · −110 |
| 0.70 | 10,367 · −103 | 3,347 · −109 | 5,785 · −109 |
| 1.00 | 10,559 · **−58** | 11,580 · −96 | 11,809 · −94 |

- **The floor is exact.** At depth 0 all three read the dry spectrum (1 peak, 110 Hz, ooh −47.8).
- **The ceilings all arrive**, and the two new modes get there *cleaner* than mode 1 does (−96/−94
  against −58 dBc), because mode 1's top is deliberately reference-matched destructive territory.
- **FM Clamp is darker through the musical range** (0.56× FM's centroid at half travel), which is
  the reference's own direction for FML (its FML/FM centroid ratio is 0.33).

**🚨 NEITHER NEW MODE FEEDS THE MIP PICKER, and that is a decision, not an oversight.** Mode 1 does
not either: its aliasing above d ≈ 0.59 is measured and reference-matched. The exposure is the same
order in all three — at 110 Hz mode 1's full-depth read rate is 735× the carrier and mode 9's is
2^10 = 1024× — so dulling 9/10 while 1 stays bright would make the clamp mode darker than its own
sibling for a reason that has nothing to do with the clamp.

**Two things were built, A/B'd, and thrown away.** A C¹ knee on the clamp (to kill the corner's
infinite bandwidth) measured **identically** to the hard corner on a stationary tone, so the corner
stays — it is literally what the mode is called. The reading that argued for the knee (−71.4 dBc)
was a **harness artefact**: the first spectrum after a big parameter change carries the previous
setting's tail. See [[feedback-the-first-spectrum-after-a-change-is-the-previous-one]].

**Gates.** `Tests/blend_mode_gate.py` (new, mutation-tested both ways) reds the build if the C++
`modes` StringArray and the JS `MODES` array disagree, if any mode lacks a pill token, or if a live
mode is missing from `famMode()` — the last one is fb470's trap, and it is real: dropping modes 9/10
from ONE of the six mode-range tests in SynthVoice.h made both modes **completely silent** while FM
kept working, which is exactly what the mutation run showed.

## 8 · BLEND SLOTS 7 `Dist` / 8 `Filter`   ✅ **CLOSED BY fb553 — as one mode, not two**
Slots 7/8 stay frozen and inert **on purpose**. Blend mode **6 `Warp`** (fb553, item 9B) delivers
both and more: it drives the oscillator's warp amount at audio rate, and "warp amount" is one knob
with 37 meanings — modes 9-34 are shapers (`Dist`), 35/36 are the per-osc filter (`Filter`), 37 is
the curve you drew. One mode, no new parameters, and the last two empty boxes never need filling.

## 9 · AUDIO AS A MODULATION SOURCE — the oscillators and the noise   ◐ **9A SHIPPED fb552**
Max saw it on a Serum 2 reel: *"he was able to literally take the noise and make it a modulation
source on Osc A's volume… the noise was like a little plucky thing so the audio was plucking the
volume."* He wants our four oscillators and the noise engine (not the sub) to be draggable mod
sources — from the osc header, or straight off the wavetable visualiser.

**WHAT WE HAVE, measured.** `ModSource` is 10 LFOs · EnvAmp/EnvFilter/EnvMod1/EnvMod2/EnvPitch ·
Velocity · Note · 8 DRIFT lanes · 27 dynamic envelopes. **Every one is control-rate. There is no
audio source of any kind**, and modulation is staged **per block** (`ownM`/`modWinP`).

**TWO DESIGNS, AND THEY ARE DIFFERENT PRODUCTS. Do not conflate them.**
- **9A · FOLLOWER (block-rate).** Take the osc/noise **amplitude envelope**, smooth it, hand it to
  the matrix as an ordinary source. This is what "the audio is plucking the volume" actually
  sounds like. It fits the existing architecture exactly — one new ModSource family plus a
  follower — and it reaches **every destination we already have** (1,152 in the FX rack alone) on
  day one. Low risk, high reach. **Ship this first.**
- **9B · TRUE AUDIO-RATE.** The modulator's own sample, per sample. Volume → ring mod / AM;
  pitch → FM. Needs a per-sample path the block-rate matrix does not have, and only a handful of
  destinations are even meaningful. ⚠️ Note we ALREADY ship audio-rate modulation under other
  names: the FM engine, and RM/AM as blend modes. 9B is narrower than it sounds.

UI: we already have a mod-drag system (`getModDrag`/`setModDrag`) — this is a new drag SOURCE
affordance on the osc header and the noise card, not new plumbing.

### ✅ 9A SHIPPED — fb552
Five new `ModSource`s (`FollowA..D`, `FollowN`), wire codes 210-214, **appended at the end of the
enum** so nothing shifted. Drag the word **Osc** on any oscillator — the same label that is its
on/off — or the **noise's own name**, onto any knob. No Sub, by Max's exclusion.

**THE DETECTOR IS A PEAK DETECTOR, NOT A MEAN ONE, and ripple is why.** `|sin|` carries a ripple at
2·f0; a symmetric one-pole fast enough to catch a pluck (τ ≈ 3 ms, corner 53 Hz) passes that
wholesale at any bass note and the "envelope" wobbles at twice the pitch. Instant attack + a 25 ms
release holds the peak across the cycle: flat above ~40 Hz, transient still exact, **and the scale
comes out right for free** — a full-scale oscillator reads exactly 1.0, no fudge factor to go stale
when the tables change. `kFollowReleaseMs` is the one number that matters (fastest decay it can
express / slowest pitch it can hold).

**IT FOLLOWS THE SOUND, NOT THE FADER.** The taps are `modPrev_[]`/`noiseModTap_` — the same
pre-gain taps the blend slots read — so a routed follower sets `modSrcForce_`/`noiseForce_` and
keeps following a source whose Level is 0 **or that is switched off entirely**. Verified: with Osc B
disabled, a follower on it still drove Osc A's Level to full.

**It owns Level exactly like an envelope** (fb183): at depth 100 % the destination IS the source's
shape. That one line is the reel.

**MEASURED — one render, two carriers, so there is no alignment to argue about.** Osc A low, Osc B
high and tremolo'd by an AM blend; complex-demodulate each carrier and correlate their envelopes:

| | Osc A env sd | r(env A, env B) |
|---|---:|---:|
| **Follow B → Level A** | **46.0 %** | **+0.910** |
| no route | 8.2 % | +0.113 |

**THE DOOR NEARLY ATE THE WHOLE FEATURE.** `setSynthModMatrix` drops an unrecognised source with a
bare `continue`. Five sources were wired through six files and every route would have been thrown
away there — drag working, underline drawing, patch saving, nothing modulating. Proved load-bearing
without a rebuild, by sending codes either side of it: **s=211 → r +0.910; s=209 and s=215 → +0.12,
identical to no route at all.** `Tests/mod_source_gate.py` (new, all four doors mutation-tested)
now reds the build on it, on a JS/C++ encoder mismatch, on a decoder with no upper bound (wire 210
used to decode as "Env 111" — fb261's bug one family later), and on the popped card's `wire-1`.

**Floor gated**: with followers compiled in and none routed, dry/FM/FM-Exp render 1/55/39 peaks at
110/3119/943 Hz against the pre-change build's 1/55/39 at 110/3124/972 — inside the harness spread.

**Still open:** the follower's attack/release are fixed constants rather than knobs.

### ✅ 9B SHIPPED — fb553, as blend mode 6 `Warp`
9A gave the matrix a source's ENVELOPE; 9B is the source's own SAMPLE, per sample. The honest
reading of that is **not** a new kind of route: the block-staged matrix cannot deliver a per-sample
value to 487 destinations, and on the 480 that are block-rate an audio-rate value is inaudible or
noise — a route that silently does nothing is worse than one that does not exist. So 9B went where
per-sample modulation already lives: **the blend slot**, which is exactly a {source, depth} pair
evaluated every sample. FM/PD spend it on phase, AM/RM on amplitude, **mode 6 on the warp amount**.
Both warp slots, so it works wherever the shaper happens to sit. **WT and FM engines only** —
SAMP/GRAN/SPEC/HARM/MODAL never evaluate a per-sample warp amount at all.

**MEASURED on a full-bandwidth table (Terra Stack, loaded through the real editor — every earlier
warp alias number in this document was taken on preset 0, a SINE, where table aliasing is
impossible):**

| | npeaks | centroid | out-of-harm | alias pk/rms |
|---|---:|---:|---:|---:|
| table dry | 203 | 391 Hz | −45.7 | −113.7 / −135.7 |
| Bitcrush static | 216 | 459 | −27.7 | −112.2 / −120.4 |
| **+ Warp d = 0** | 216 | 459 | −27.7 | −111.2 / −120.3 |
| + Warp d = 0.60 | 216 | 582 | −25.6 | −110.8 / −117.2 |
| Sync (knob 0) static | 203 | 391 | −45.7 | −113.3 / −135.6 |
| + Warp d = 1.00 | 216 | **2,983** | −16.8 | −110.5 / −121.4 |
| LP Filter static | 47 | 189 | −45.9 | −118.6 / −140.1 |
| + Warp d = 0.70 | **107** | 228 | −45.7 | −114.9 / −136.4 |

**🚨 THE FILTER MODES DID NOTHING AT FIRST, and that is the whole reason this item was worth doing
carefully.** `warpFiltCoef()` runs once per block — right for a knob, useless for a cutoff swept at
audio rate. Mode 6 on warp mode 35 measured **npeaks 1, centroid 110 Hz, identical to the static
filter**: live, saved, drawn, silent. Fixed with a per-sample coefficient recompute that costs no
transcendental (`fastExp2` for the corner, a Padé [3/2] for `tan`), armed only by a mode-6 slot, so
an unmodulated filter is bit-identical to what shipped.

**🚨 AND THE MIP HAD TO WIDEN — measured, at last, on a table that can show it.** The mip is chosen
once per block from the warp amount; sweeping it at audio rate makes the read rate exceed what that
mip is band-limited for. Sync at knob 0 with full modulation, mip blind vs mip widened:

| | alias peak | alias rms | out-of-harm |
|---|---:|---:|---:|
| mip blind to the swing | −95.0 dBc | −101.2 | −12.2 |
| **mip widened (shipped)** | **−110.5** | **−121.4** | −16.8 |

**15.5 dB of peak alias and 20.2 dB of RMS**, and it costs nothing when no mode-6 slot is armed.
The same test on preset 0 showed *no difference at all* — a sine has no upper harmonics to alias, so
it cannot fail a mip test. That is worth remembering before trusting any alias number from this
harness. See [[feedback-a-sine-carrier-cannot-fail-a-mip-test]].

## 10 · CURVE EVERYWHERE — and the one that matters is the MOD CONNECTION   ⬅ Max, 2026-08-31
Max: *"when he put that noise on Osc A's volume he right-clicked the volume and pressed curve
edit… I wanna know how deep the Serum curve edits go."*

**WHAT SERUM EXPOSES, measured off its own 2,623 AU parameters.** Only two curve families are
parameters at all: **`Env 1-4 Atk/Dec/Rel Curve`** (12 knobs) and **`Porta Curve`**. Its mod matrix
is **64 slots** exposing only `Mod N Amount` and `Mod N Out`.
⚠️ **Everything drawn — LFO shapes, and the per-connection curve Max watched — is internal state,
NOT an AU parameter, so I could not enumerate it from here and have not.** The reel is the spec for
that behaviour; treat "Serum's curve list" as unmeasured until we read it off the UI.

**WHAT WE HAVE.** LFO drawing · the distortion curve card (fb328, persisted as a JSON blob — the
precedent for a drawn editor that survives a save) · FLOW arp/chop/gli `*_CURVE` knobs (single-value
shapes) · fb546's warp cards, which VIEW a curve today and become editable at 6C.
**WHAT WE DO NOT HAVE:** a mod assignment is `{ source, dest, depth, enabled }` — **no curve, no
polarity, no offset. A connection is a straight linear scale.** And our envelopes have no segment
curves at all, where Serum ships 12.

**RANKED, most expressive first:**
1. **10A · THE MOD-CONNECTION CURVE.**   ✅ **SHIPPED fb554**
   Right-click any modulated knob → **Curve · <source>**. 129 points, the same resolution and the
   same drawing idiom as fb550's warp curve, so a drawn curve means one thing everywhere here.
   Double-click inside → straight line, and a straightened route carries **no curve at all** rather
   than an identity one: transparent by construction, not by arithmetic.

   **The curve rides the ROUTE**, in the JSON that already round-trips through `getSynthMod` and
   the patch state — no new native function, no new persistence, and no way for a curve to get
   separated from the connection it belongs to. `applyModCurve()` is **one function** called from
   both the per-voice evaluator and the processor's global pass: sources do not share a convention
   (envelopes and followers arrive as level−1, velocity as 0..1, LFO/Drift as ±1), so each is
   mapped into 0..1, curved, and mapped back by its own inverse. Two copies of that would have
   drifted silently — the same knob curving differently depending on whether its destination
   happens to be per-voice or global.

   **MEASURED** — the fb552 rig (one render, two carriers, envelopes demodulated and correlated),
   same route, four curves:

   | curve | r(env A, env B) | Osc A env sd | |
   |---|---:|---:|---|
   | none | +0.908 | 46.1 % | the shipped straight multiply |
   | **y = x** | **+0.909** | **46.2 %** | identity — indistinguishable from none ✅ |
   | **y = 1−x** | **−0.864** | 97.8 % | **the sign flips** — loud source now means quiet knob |
   | step at 0.5 | +0.840 | 60.2 % | a hard gate |

   **The UI, driven end to end**: drag → route → right-click → `Curve · Follow Osc B` → the editor
   opens (168×124) → 129 points drawn with real mouse events → the curve is in the saved patch.
   ⚠️ The probe could not reach it for six runs, for two reasons that are both about the harness,
   not the plugin: the hover underline needs rAF, which an offscreen window suspends, and the
   300 ms post-drag click suppressor eats a synthetic click that lands microseconds after a drop.
   A human hits neither. `window.__tiCurveEdit` / `__tiCurveOf` are the headless gate hooks that
   settled it, in the same idiom as `__tiRoutes`.
2. **10B · ENVELOPE SEGMENT CURVES**   ✅ **ALREADY SHIPPED — this entry was wrong**
   "Serum has 12; we have none" was **stale and false**, and it was written without looking. We have
   **96**: the five legacy envelopes each carry Attack/Decay/Release curve *parameters*
   (`Synth Amp/Filter/Pitch/Mod 1/Mod 2 · {Attack,Decay,Release} Curve`, 15 in APVTS), and all 27
   dynamic envelopes carry `ca`/`cd`/`cr` per segment in their own blob (81 more). Against Serum's 12.

   **VERIFIED, not just counted** — Amp Attack held long, the rising contour sampled at ¼/½/¾ of its
   climb:

   | Attack Curve | ¼ | ½ | ¾ | |
   |---|---:|---:|---:|---|
   | 0.00 | 0.061 | 0.170 | 0.475 | concave — slow, then fast |
   | 0.50 | 0.259 | 0.493 | 0.698 | linear |
   | 1.00 | 0.687 | 0.860 | 0.890 | convex — fast, then slow |

   Nothing to build. **The lesson is the entry itself**: a roadmap line claimed a gap that a
   two-minute parameter dump would have refuted, and it survived three sessions.

3. **10C · velocity / key-tracking curves.**   ✅ **SHIPPED fb555**
   - **Velocity** already had a curve knob (`Synth Velocity Curve`, fb262) and, since fb554, a
     *drawn* per-connection curve as well. Nothing to build.
   - **Key tracking did not exist as a mod source at all.** `ModSource::Note` had been in the enum
     since Batch 2 and was **evaluated nowhere**: no wire code, no evaluator, offered by no UI. Now
     it is a real source — wire code **201**, drag the **Key** row out of the voice panel.

   It joins the **shape family** (unipolar, `level−1`), so it OWNS a Linear01 destination the way an
   envelope does: *the knob is what the top of the keyboard does, and low notes pull down from it*.
   Any other key-tracking shape — including a classic bipolar pivot at middle C — is a curve you
   draw on the connection (fb554). **That is what 10C asked for, and 10A had already built it.**
   Its value is the plugin's own `ktRamp_` (0 at C1, 1 at C6, latched at note-on), deliberately the
   same ramp the existing KEYTRACK feature uses — "Note" and "keytrack" have to mean one thing.

   **MEASURED** — `Key → Osc A Level` at full depth, against a flat control, level predicted by
   `(note−36)/60`:

   | note | no route | Key → Level A | predicted |
   |---|---:|---:|---:|
   | 24 | 0.2500 | **0.0000** | 0.000 (below C1, clamped) |
   | 48 | 0.2500 | **0.1000** | 0.100 |
   | 60 | 0.2500 | **0.2000** | 0.200 |
   | 72 | 0.2500 | **0.3000** | 0.300 |
   | 96 | 0.2500 | **0.5000** | 0.500 |

   Exact to five decimals at every note; the control is flat, so without the route there is no key
   dependence at all. The **Key row's number is the live key position**, so it moves while you play.
   (Drawable WARP shapes are already item **6C** and are not duplicated here.)

**🔑 THE SEQUENCING THAT MATTERS: 10A wants the SAME drawn-curve editor 6C builds.** Do 6C first
and 10A is mostly wiring. And note the reel Max watched is exactly **9A + 10A** — the follower plus
the connection curve. That pair, in that order, is the shortest path to the thing he saw.

---

---

## THE BROKEN THINGS — LAST, by Max's instruction ("the final creases")
| | state, verified 2026-08-28 |
|---|---|
| **Sine Shaper** | still `1.0f + amount * 4.0f`. Centroid climbs to 11,440 at 0.8 then **falls to 8,855** at 1.0 — the last fifth goes backwards. Also **not transparent at amount 0**. |
| **P-Quantize** | still `2^(5.0 − 4.0a)`. Moves **4.6% across its entire sweep**, and is **+63% brighter than the bare carrier at a = 0** when it should be inert. Cause never found. |
| **AM's top quarter** | eaten by the master limiter (`kLimiterThresh = 0.90`). Law is exact to depth 0.75, then 0.946 → 0.348 by 1.00: only **34.8%** of algorithmic peak survives. A LIFEGUARD-LAW violation. |
| **FM on rich carriers** | 2% out-of-harmonic at knob **0.372**, not the predicted 0.588. `fmRateMul` covers only `Engine::FM`, so blend-FM's excursion never reaches the mip pick. Top should break; the shallow end must stay clean. |
| **Diode 2** | drive knob runs **backwards** (THD 9.5 → 8.2). Fix compiled in but **never certified through the plugin**. |

## 🚨 THE PARAMETER AUDIT IS NO LONGER OPTIONAL — and one third of it is now automated
**fb547.** Max: *"it doesn't seem like range or stack is doing anything."* Both were wired to
nothing. `SYN_OSC_x_URANGE` and `SYN_OSC_x_USTACK` had **no `WebSliderRelay`**, so
`getSliderState` handed the panel a live-looking state that read 0 forever (Range's real default
is 150 cents) and swallowed every write. MEASURED before the fix, with a relayed knob as control:

    Warp Amount (relay)  set(0.8) -> host 0.8          ✓
    Unison Range         set(0.8) -> host 0.495301     ✗ unchanged
    Unison Stack         set(0.8) -> host 0            ✗ unchanged

That is the THIRD time this exact failure has shipped (fb544 phase pills, fb546 note, fb547), and
**twice a human found it before we did.** `Tests/relay_gate.py` now fails the build if any
`data-syn` / `getSliderState` control is bound to a parameter with no relay — mutation-tested by
deleting one relay, which reds it. The remaining two thirds of the audit (does each knob MOVE, and
what is its exposure ratio) are still open below.

## HOUSEKEEPING
- **Sweep every `Tests/*` cert for the preset-0 trap.** Terrain's WT Preset renders as preset 0
  headless without a message-thread pump — every cert that selects a table has been measuring the
  default. A pile of past green checkmarks are worth less than they look.
- **The parameter audit.** Every knob: does it move, and what is its EXPOSURE RATIO (what it
  delivers at max ÷ what the engine can still deliver). Today caught four dead controls by
  accident; that is unlikely to be the last.
- Re-tune the 20 factory presets after the unison `d^2.5` taper change (no migration row, by design).
- FX-card CPU baseline has **never** been measured; every oversampling cost estimate rests on it.

## UNTOUCHED FROM THE ORIGINAL MANDATE
Multi-sample engine (soundfonts → pianos, bells) · more filter types (they ship 108) · preset menu
→ terrain patcher · the voice-panel randomisers (Osc Detune Rnd, Osc Pan Rand, Env Rand, Cutoff
Rand) · **more/better wavetables beyond TERRA** (Max will not build presets on another maker's
tables — in-house content is required, not preferred).

---
## LAWS THAT BIND ALL OF IT
🎯 **GATE THE RESULT, NOT THE CAUSE** (HARD RULE, Max 2026-08-28) — a grep gate proves a CAUSE is
absent, never that the RESULT is good. The wavetable form of it:
> **A table may not end its content while its last harmonic is above −60 dBc.**
> No amplitude law lands on a loud value and then jumps to exactly zero. Only truncation does.

`harmonic_ceiling_gate.py` greps for hard-coded loop bounds and is not enough: **`Serum HD` loops to
`h <= 96` and DELIVERS 17** — a *multiplicative* Gaussian buried it, no ceiling required; `Formant`
declares 96 and delivers 7. Both pass any grep. **Two defects, never conflate them: THE CLIFF** (loop
stops at a hard integer — absent content) and **THE EARLY TAPER** (declared count is fiction because a
multiplicative envelope buried it; the fix is ADDITIVE resonance, `1 + Σ g·exp(…)`, which the TERRA
kernel already obeys and the legacy generators do not). Measured, twelve legacy tables delete LOUD
content: Spectral Drift h32 @ **0.0 dBc** (flat spectrum, truncated — the proof case) · Static Evolve
h64 @ −1.4 · Even h15 @ −23.5 · Prophet Saw h26 @ −25.9 · Dustbowl h30 @ −27.4 · Jupiter PWM h96 @
−27.8 · Juno Str h30 @ −29.5 · OB-X Saw h22 @ −29.7 · CS-80 Brass h40 @ −30.5 · Moog Sqr h32 @ −31.0 ·
PPG Wave h64 @ −34.8 · Drift h64 @ −36.1. Control group that must keep passing: DX7 EP −82, Choir
−67, Whisper −63, Vowel Morph −72, Serum HD −176, Formant −823. Harness: `Tests/wt_profile.py`.
🔢 **COUNT IS NOT RICHNESS — REPORT Neff, NEVER N60.** Prophet Saw scores **5.7** and Terra Stack
**4.4**: Terra Stack is correctly saw-like — more bandwidth, same character. The genuinely dense ones
are Terra Cloud 120.7 · Dust 59.8 · Bar 29.2 · Glass 25.3. N60 is gamed by piling partials at −80 dB.
⚠️ **`Wavetable_test.cpp` HAS NEVER RUN.** It is compiled into the plugin (`CMakeLists.txt:55`) but
nothing anywhere instantiates a `juce::UnitTestRunner` — every `expect()` in it is compile-only. That
is how an assertion reading "harmonic 7 must be PRESENT" sat green while projecting onto the wrong
quadrature (a square is all-cosine; the sine projection is 0.00000 for every h). QUEUED: wire a runner.

🏊 **THE LIFEGUARD LAW** (CLAUDE.md §5) — the knob's 100% must be the ALGORITHM's 100%; unused
headroom is a defect; fold-back at the top is the product; the shallow end stays clean; never a
limiter, never a clamp, never a "safe" ceiling.
⚖️ **CLEAN ROOM** — measure the reference's OUTPUT as a target number, implement with our own DSP.
Never decompile, never extract a table or preset, never transcribe a curve.
🧩 **NAME THE SEAM** — a call site in one owner's file and the callee in another's means NOBODY
writes the call. 28 params shipped inert that way and passed pluginval, auval AND bit-identity.
Every new parameter needs a MOVEMENT gate, not just a floor gate.
🔗 **A CONSTANT THAT FEEDS A SELECTION PATH LIVES IN TWO PLACES** (`warpRateMul`, the JS default
mirrors, `kFoldPre`). Change both or it compiles clean and silently misbehaves.

---

## 🆕 FOUND 2026-08-28 BY THE NOVEL-WARP RESEARCH — verified independently

### 🟠 FIVE SHIPPED WARP MODES EMIT DC WITH NO BLOCKER — needs Max's call, it is a SOUND CHANGE
`warpAmpNeedsDc()` (`SynthVoice.h:1325`) opens `if (mode < 9 || amount <= 0.001f) return false;`,
so the phase-domain modes never arm `wtRectDc*`. **A non-bijective phase remap changes the cycle
mean**, and measured worst-case |DC| as a fraction of full scale:
**Mirror 0.249 · P-Quantize 0.212 (saw) · Sync/Formant 0.172 · Fractalize 0.123.**
I confirmed the gate and grepped the chain: **there is no other DC removal anywhere downstream of
the oscillator sum.** It also survives the unison sum — every sine gets the same warp, so the
offset adds coherently. Consequences: eaten headroom, an asymmetric waveform, and early limiter
engagement (`kLimiterThresh = 0.90`).
⚠️ **Why it is not an overnight fix:** arming the blocker is a 38 Hz high-pass on five modes that
have shipped for months — it changes existing sounds and breaks bit-identity. Options are (a) arm
it and accept the change, (b) arm it only above an amount threshold, (c) leave it and document.
**Max decides.** Also note the research doc's own build-checklist got this exactly backwards — it
claimed phase modes "cannot possibly produce DC" and must be added to the `return false` list.
The measurement says the `default: return true` is correct.

### The novel-shape research: measured, and the headline candidate DIED
`scratchpad/novel-warp-modes.md` (708 lines) + `scratchpad/warpbench/` (the rig: exactly-periodic
f0 so every FFT bin is exact, tables band-limited to the note's mip limit so the baseline aliases
at **0.000%**, and an 8x-oversampled truth reference; shipped modes copied verbatim from
`SynthVoice.h`/`Shapers.h` so every comparison is against what actually ships).
- **C1 SHATTER as specced is not a scramble, it is a x3 PITCH SHIFT.** `j = (i*3) & (N-1)` is
  multiplication mod 2^m — a linear map — so the read head advances three slices per slice. On a
  sine at full amount the fundamental measures **-340.3 dB**: the note is gone. It accidentally
  reimplemented Fractalize. **The bit-reversal variant (C1b) is the real one** and it is strong:
  nh 163 at amount 0.20, zero DC, f0 intact until the very top.
- Survivors worth building, with numbers in the doc: **C1b SHATTER (bit-reversal)**, **C3 ZENO**
  (projective/Mobius quantise with a PULL — fixes P-Quantize's three documented defects at once),
  **C2 CRUMPLE** (fBm domain warp; the only candidate that generates EVEN harmonics, and 0.000%
  alias at C3), **C5 FRACTAL** (Takagi folder, genuinely odd at +160..+195 dB odd/even, zero DC,
  and the first shaper with a dB/oct SLOPE knob).
- Killed by measurement: **C4 GRAY is dead travel** (nh = 1 at amount 0.2 AND 0.5 — nothing happens
  until 1.0), **C7 PERSPECTIVE is not transparent at 0** (nh 65, f0 -13.1 dB with the knob at zero).
- ⚠️ Two build traps for whoever ships these: `warpAmpNeedsDc` defaults `true` for index >= 9, which
  is CORRECT for these (see above) — do not "fix" it; and **`warpRateMul()` takes `(mode, amt)` and
  NOT `var`**, so any mode whose read rate depends on VAR would be band-limited for the wrong rate
  — exactly the fb523 "the raise LOST harmonics" failure. Widen the lambda.
- Independent confirmation of a known defect: **P-Quantize at amount 0.00 measures nh 11 and
  centroid 1262 Hz against dry's nh 1 / 131 Hz.** Its transparency defect is real and reproducible
  outside the plugin.
- 🔑 Correction to a claim I had repeated: "a 1-periodic phase remap folds back onto the harmonic
  grid so OOHR is blind to it" only holds when `sr/f0` is an integer, which it generally is not.
  Shipped Bend at C7 amount 1.0 measures **18.7% inharmonic**. OOHR sees these modes fine.
- Highest-value idea that does NOT fit today's signature: `p -> p + a*table'(p)` — steering the read
  by the table's own SLOPE (an unsharp mask, not a displacement map). The reference does not have
  it; it needs a table pointer in `applyPhaseWarp`.
