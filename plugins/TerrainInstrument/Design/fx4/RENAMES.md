# FX4 — THE AUTHORITATIVE RENAME TABLE

**This table is a decision, not a suggestion. Apply it verbatim. Do not substitute your own names.**

Author: Claude Code (integration owner), 2026-08-18, after the family audit.
Every NEW name below was grepped against `Source/`, `Design/fx3/` and `Design/fx4/` and returned
**zero hits**. Six of my own first picks collided (`Resonant`, `Thicken`, `Snap`, `Smear`, `Hold`,
`Rebound`) and were replaced — which is exactly why one head owns this table instead of three.

## Why the collisions happened, so they don't recur

Three agents each grepped only what they could see. The dynamics agent built a real gate
(`shipped_labels.inc`, 1762 strings) and moved six names on its own; the EQ and Widen agents built
none. Result: **9 blocking collisions inside fx4, 14 against shipped, 7 doubles inside a single
card.** `Tilt` and `Sculpt` — an EQ front hero and an EQ header pill — are shipped **Tape front
knobs**.

## The precedence rule used to decide every row

1. **Shipped always beats new.**
2. Inside fx4: **header pill (Type) > knob label > dropdown option > Character.**
3. Max's mandate words win (`Air` stays the EQ hero even though a shipped filter type shares it).
4. Same word for a *different law* is worse than a plain duplicate — those are renamed even when
   precedence would allow the reuse (`Width` = Q here vs wet-side-gain everywhere else).

---

## EQUALIZER

| Slot | OLD | **NEW** | Why |
|---|---|---|---|
| front hero 1 | `Tilt` | **`Slant`** | shipped Tape front knob + Filter engine type |
| Type 6 (pill) | `Sculpt` | **`Chisel`** | shipped Tape front knob |
| P8 slot name | `Shape` | **`Trait`** | shipped Flanger + Granular back knobs |
| P8 @ Surgical | `Width` | **`Pinch`** | 7 shipped devices use `Width` for wet side gain — different law |
| P8 @ British | `Bump` | **`Slope`** | shipped Tape back knob |
| P8 @ American | `Grip` | **`Taper`** | collides with OTT back knob P5; OTT's is permanent, EQ's is 1 of 7 relabels |
| P8 @ Chisel | `Ring` | **`Sting`** | shipped FM algo `Ring` = ring modulation — same word, different physics |
| P8 @ Dynamic | `Sense` | **`Pivot`** | shipped Filter back knob, same semantic field, different meaning |
| Surgical char 0 | `Clean` | **`Plain`** | shipped 4× (Chorus/Reverb/Delay/Granular) |
| Surgical char 1 | `Tight` | *(keep)* | Widen yields this one |
| Surgical char 4 | `Carve` | **`Scoop`** | shipped Harmonic engine character |
| British char 0 | `Console` | **`Desk`** | shipped Harmonic engine main type |
| British char 3 | `Fixed Top` | **`Iron Top`** | name drift: header says `Fixed Top`, spec+roster+worklet say `Iron Top`. `charNames()` is the single source of truth |
| American char 2 | `Gentle` | **`Mellow`** | collides with OTT Type 1 (pill wins) |
| Passive char 7 | `Modern` | **`Revival`** | shipped Reverb + Delay character |
| Passive chars 3,4 | `Close Dip`,`Far Dip` | **`Close Cut`,`Far Cut`** | within-card double with the `Dip` knob |
| Open char 0 | `Silky` | **`Gloss`** | within-card double with the `Silk` knob |
| Dynamic chars 1,2 | `Fast`,`Slow` | **`Quick`,`Lazy`** | shipped Chorus `Vintage` characters |
| Dynamic char 4 | `Inverted` | **`Upward`** | shipped Phaser `Stone` character — and `Upward` is more accurate |
| Chisel char 3 | `Gain Ring` | **`Gain Peak`** | within-card double with the P8 knob |

`Low` · `Body` · `Bite` · `Air` · `Reach` · `Amount` · `Mix` · `Focus` — **keep**. Band grammar and
sanctioned shared vocabulary.

## WIDEN

| Slot | OLD | **NEW** | Why |
|---|---|---|---|
| Type 2 (pill) | `Shift` | **`Steady`** | shipped Reverb/Shimmer dropdown label; `Steady` also states the mechanism (static offset, no LFO) |
| Type 3 (pill) | `Double` | **`Twofold`** | shipped Chorus character |
| back knob P4 | `Wander` | **`Roam`** | shipped Chorus character |
| Amount @ Blur | `Scatter` | **`Wash`** | shipped Granular FX **Type** |
| Amount @ Twofold | `Drift` | **`Sway`** | shipped Chorus back knob + Granular char + wavetable option |
| Stack char 3 | `Tight` | **`Tight Fan`** | EQ keeps `Tight`; Widen already has a `Wide Fan` sibling to pair with |
| Twin char 0 | `Duo` | **`Two Line`** | shipped Phaser **Type**. The engine's own comment at `TerrainWidenFx.h:856` already calls it "the literal two-line SDD-320" |
| Twin char 1 | `Quad` | **`Four Line`** | shipped Tape `Heads` option; pairs with `Two Line` |
| Steady char 0 | `Silk` | **`Satin`** | EQ knob label outranks a Character |
| Steady char 1 | `Punch` | **`Jab`** | shipped Distortion back knob + Filter pill |
| Steady char 4 | `Down Double` | **`Octave Down`** | within-card double with Type `Twofold` |
| Twofold char 5 | `Static Twins` | **`Static Pair`** | within-card double with Type `Twin` |
| Blur char 7 | `Counter` | **`Opposed`** | shipped Phaser `Duo` character |
| Blur char (air) | `Air Only` | **`Top Only`** | near-miss on Max's mandate word `Air` |
| Bands char 6 | `Low Split` | **`Deep Grid`** | OTT's `Low Split`/`High Split` are a matched pair; breaking one breaks both |

`Twin` (Type 1) — **keep**, OTT yields. `Low Keep` — **keep**, sanctioned shared vocabulary (same
concept, same behaviour as the Chorus knob).

## COMPRESS

| Slot | OLD | **NEW** | Why |
|---|---|---|---|
| back knob P6 | `Latch` | **`Cling`** | shipped Arp toggle |
| back knob P8 | `Heat` | **`Burn`** | shipped `SUB_HEAT` label + sub pill menu |
| Detect opt 0 | `Auto` | **`Native`** | within-card double with the `Auto` pill — the `Air`-class violation |
| Detect opt 3 | `Long` | **`Patient`** | shipped Reverb Shape option |
| Exact char 6 | `Silky` | **`Poise`** | EQ Type default wins; `Poise` keeps the ζ-word pair coherent (critical damping) |
| Exact char 7 | `Wobble` | **`Judder`** | `Wobble` is idiomatic for a BBD (Widen); on a compressor it is one of several words |
| Opto char 5 | `Glass` | **`Crystal`** | shipped Distortion Shaper character |

**Plus one structural fix, not a rename — R6's spirit.** Characters `RMS Ears` and `Spike Ears`
set `detForce`, which **silently overrides** the `Detect` dropdown: pick `RMS Ears`, then `Detect →
Peak`, and the card still reads `RMS Ears` while a peak detector runs. That is two controls on one
axis and a visible label disagreeing with the DSP — the same defect fb418 fixed on the flanger, and
the fb373 failure mode. **Remove `detForce` from all Characters; `Detect` owns detection outright**,
and rename those two Characters to their real remaining mechanism. `Grip` is taken by OTT — pick
from: `Soft Eyes`, `Loose Grip`, `Steady Eyes`, `Blunt`, `Keen` (all verified free).

## OTT

| Slot | OLD | **NEW** | Why |
|---|---|---|---|
| front pill | `Bite` | **`Crest`** | EQ's `Bite`/`Bite Hz` is its band grammar and appears twice; this pill is the least load-bearing control in the release by its own roster |
| Over Top char 7 | `Full Bite` | **`Full Crest`** | follows the pill |
| Stereo opt 1 | `Twin` | **`Free Pair`** | Widen's `Twin` is a header pill — the most visible label on a card |
| Two Band chars | `Slow Twin`,`Fast Twin` | **`Slow Pair`,`Fast Pair`** | follow the option |

`Gentle` (Type 1) and `Grip` (P5) — **keep**, EQ yields both.

---

## The gate that has to exist afterwards

`shipped_labels.inc` **misses the fb418 labels** — `Motion` and `Route`, the two strings R6 is named
after. `PluginProcessor.cpp:4022` builds them as `"Chorus" + sfxD + " Motion"`, so the literal has a
**leading space** and the extractor's "capitalised quoted string" pattern skips it
(`grep -c '" ' shipped_labels.inc` → 0). Re-extract with a leading-space-tolerant pattern, add the
two sibling fx4 directories as sources, and re-run. A gate that cannot see yesterday's labels cannot
protect tomorrow's.

## And the thing that made this possible to get wrong

**Only the EQ publishes its knob labels from the engine** (`backNames()` / `frontNames()`).
Widen's per-Type `Amount` relabel and every Compress/OTT knob label exist **only in markdown** —
which is precisely the geometry that let `Cassette` play `Studio` for four rounds of green
measurement. **Every device must expose its labels from the header** so the card cannot print a
word the DSP disagrees with.

---

# fb423 — THE SECOND RULING (the 23 the first table did not reach)

The second family audit built an independent inventory of **366 published labels** straight from the
engine headers and found **23 unruled collisions** the first table missed. The reason it missed them
is worth stating: my grep covered `Source/` C++ and the fx3/fx4 design dirs, but most of these live
in **`index.html` option arrays** and **`Source/*_test.cpp` name tables** — places a C++-literal
grep does not reach. The agents' rebuilt extractors (3064 and 3310 strings) do reach them.

**That is the same failure one level down: I checked what I could see.** Same as the agents. The
durable fix is not a better ruling, it is that every device now carries a gate over **all** its
published labels — see §Gate below.

Four of my own candidates collided again — `Stock`, `Swarm`, `Choir`, `Chorale` — and were replaced.
Every name below greps to **zero hits** across `Source/`, `Design/fx3/` and `Design/fx4/`.

## EQUALIZER — 8 renames (its own gate found these and failed loudly at 121/1; correct behaviour)

| Slot | OLD | **NEW** | Collides with |
|---|---|---|---|
| British char 2 | `Forward` | **`Ahead`** | shipped |
| American char 7 | `Runaway` | **`Bolt`** | shipped |
| Passive char 0 | `Program` | **`Baseline`** | shipped |
| Open char 3 | `Stacked` | **`Twin Shelf`** | shipped |
| Dynamic char 7 | `Peak Hold` | **`Peak Keep`** | shipped |
| Chisel char 1 | `Razor` | **`Scalpel`** | shipped |
| Chisel char 5 | `Telephone` | **`Handset`** | shipped |
| Chisel char 7 | `Metal` | **`Tin`** | shipped **Vintage-reverb Character** (`index.html:8614`) — worse than the agent thought, same slot class as `Modern`→`Revival` |

## WIDEN — 8 renames (Widen detected none of these; it built no gate)

| Slot | OLD | **NEW** | Collides with |
|---|---|---|---|
| **Type 0 (header pill)** | `Stack` | **`Throng`** | shipped FM algo + spectral mode. `Throng` is also the better word — this device's stated identity is *a crowd* |
| **front hero relabel @ Bands** | `Split` | **`Cleave`** | shipped FM algo — the same three-entry list RENAMES already ruled `Ring` off |
| Twofold char 0 | `Vocal` | **`Lilt`** | shipped **Plate-reverb Character** — identical class to `Modern`→`Revival` |
| char | `Warble` | **`Quaver`** | shipped Downsample-distortion Character |
| char | `Velvet` | **`Plush`** | shipped Shaper-Asym preset |
| Field option | `Direct` | **`Straight`** | shipped Distortion Character |
| Field option | `Collapse` | **`Gather`** | shipped Distortion Character |
| Twin char 6 | `Wobble` | **`Tremble`** | the first table KEPT this; the wider corpus shows it is also a shipped **Tape preset**. Reversing my own row |

## SANCTIONED — no rename, ruled explicitly so nobody re-opens them

- **`Stereo` · `Mid` · `Side` · `Left` · `Right`** — M/S routing vocabulary, sanctioned **as a group**
  wherever it appears (EQ `Focus`, OTT `Stereo`, any future device). There is no synonym for these
  and inventing one would be worse than the duplication. This also settles the one cross-fx4 hit.
- **`Tone` · `Retrig`** — shared vocabulary, CONTRACT §4 class. Same concept, same behaviour.
- **`Blur` · `Coarse`** — mockup-only / synth-side strings, not live rack labels. No action.
- **Compress's `Auto` pill** — sanctioned. It is the *same law* as the shipped Distortion `Auto`
  pill (auto gain compensation), which is exactly what CONTRACT §4 sanctions. Recording it as a
  **ruling** rather than leaving it as the dynamics gate's self-granted `kShared` entry, because a
  gate that can exempt itself is not a gate.

## §Gate — the durable fix, and it is not another table

Two of three audits found collisions the previous pass could not see, because each pass grepped a
different corpus. **Every device must gate ALL of its published labels — not the ones it changed.**

- Widen has **no `shipped_labels.inc` and no `§names` section**; its `widen_cert.cpp` runs
  A,B,C,D,E,F,R,G,H,I,J,K,L,M,N,O,Z and nothing checks names. Its `FINDINGS §9` grep covered only
  the 15 new RENAMES strings — **its other 80 published labels have never been checked against
  anything.** That is the first audit's failure repeating verbatim: *checking what you changed
  instead of the whole card.* Build the gate. Model it on `dynamics_cert.cpp:645-725`, which is well
  built: exactly 2 sibling-yield exemptions, asserted to be exactly 2 so it cannot quietly grow.
- Every cert additionally gates that **ROSTER.md and the worklet name tables EQUAL the header
  arrays.** 22 stale strings currently survive downstream — `eq-worklet.js` still carries 12 old
  Character names as a second `CSPEC[].nm` table, which is *literally the two-table geometry the EQ
  engine deleted*. The card is built from those files. This is fb373's geometry, still standing.
