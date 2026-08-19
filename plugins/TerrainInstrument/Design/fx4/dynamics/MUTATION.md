# MUTATION.md — COMPRESS · OTT · DynamicsCore

**FIXES.md §0.** Every law-1, law-4 and R11 gate on these two devices has been run against a
deliberately broken copy of the engine, and the failing output is pasted below.

Produced by `python3 mutate.py`. It copies `DynamicsCore.h`, `TerrainCompressFx.h`,
`TerrainOttFx.h`, `dynamics_cert.cpp` and `shipped_labels.inc` into a scratch directory, deletes
**one mechanism** with an exact string replacement — and **aborts if the text it expects is not
there**, so a mutation can never silently fail to apply — rebuilds the cert against the mutant,
runs only the sections that matter, and requires the named gate to turn `FAIL`.

> A mutation that does not apply produces a false green, which is the disease this file treats.
> Every replacement is asserted to match exactly once.

---

## The table

| mutant | mechanism deleted | gate that fired | REAL engine | MUTANT |
|---|---|---|---|---|
| `compress-smoother-seed` | `seedShape()` — the new smoother is not handed the live `gr_` | §4c3 GR continuity, 64 Type changes | **0.86 dB** | **17.44 dB** |
| `compress-smoother-seed+noslew` | ↑ *and* the transition slew limiter | §4d all 64 Type transitions | **1.23 dB/ms** | **10.34 dB/ms** |
| `compress-transition-slew` (alone) | the 1.5 dB/ms transition slew limit | — | 1.23 / 1.00 dB/ms | 1.94 / 1.25 dB/ms — **SURVIVED** |
| `compress-heat-kind-fade+noslew` | the waveshaper-kind crossfade snaps instead | — | 1.23 dB/ms | 1.94 dB/ms — **SURVIVED** |
| `compress-colour-drive-smoothing` | the gain element's drive follows the RAW ballistic state | §4d all 64 Type transitions | **1.23 dB/ms** | **2.11 dB/ms** |
| `compress-clip-ceiling-tracking` | the soft-clip ceiling comes off the TARGETS again (fb421) | §4d all 448 Character transitions | **1.00 dB/ms** | **5.47 dB/ms** |
| `compress-discrete-fades+noslew` | all ten discrete-rewiring fades set to tau 0 | §4d all 448 Character transitions | **1.00 dB/ms** | **2.82 dB/ms** |
| `compress-sample-rate` | `coefTau` ignores `fs` (hard-codes 48 kHz) | §4g realised t63 at 96 kHz | **−0.0 %** | **−45.5 % attack, −50.1 % release** |
| `compress-detect-ownership` | a Character re-points the detector (`detForce` restored) | §4c2 no Character changes the rectifier | 0 overrides | **24 overrides** |
| `ott-tree-swap-fade` | back to fb421's instant `dip_ = 0.0f` | §6c the TREE SWAP | **0.82 dB/ms** | **5.94 dB/ms** |
| `ott-transition-slew` | OTT's 1.5 dB/ms transition slew limit | §6c all 448 Character transitions | **1.39 dB/ms** | **3.02 dB/ms** |
| `ott-mid-side-fade` | the M/S basis rotates in one sample | §6c Stereo Linked → Mid-Side | **0.25 dB/ms** | **2.66 dB/ms** |
| `ott-band-clip-fade` | Heavy's per-band clipper inserts in one sample | §6c all 64 Type transitions | **0.88 dB/ms** | **5.20 dB/ms** |
| `core-floor-gate` | `floorGate()` deleted — returns 1.0 always | §6b(d) a real −96 dBFS floor | **−76.4 dBFS** | **−44.4 dBFS** |
| `cert-fft-normalisation` | the Parseval normalisation dropped (what fb421 shipped) | §2 spectrum is CALIBRATED | **−26.022 dBFS** | **+38.956 dBFS** |

**15 mutants · 13 gates fired · 2 survivors.**

---

## Pasted output

```
mutant                             gate that must fire                    result
------------------------------------------------------------------------------------------------------------
compress-smoother-seed             GR is continuous across all 64 Type changes RED (good)
        FAIL  GR is continuous across all 64 Type changes (≤ 2 dB in the first 0.17 ms) worst 17.44 dB  (Limit → Opto)
compress-smoother-seed+noslew      Type transitions                       RED (good)
        FAIL  all 64 Type transitions ≤ 2.0 dB of gain moved in 1 ms worst 10.34 dB/ms  (Exact → Opto)   [fb421 engine: 17.36]
compress-transition-slew (alone)   transitions                            *** SURVIVED ***
compress-heat-kind-fade+noslew     transitions                            *** SURVIVED ***
compress-colour-drive-smoothing    transitions                            RED (good)
        FAIL  all 64 Type transitions ≤ 2.0 dB of gain moved in 1 ms worst 2.11 dB/ms  (Vari-Mu → Limit)   [fb421 engine: 17.36]
compress-clip-ceiling-tracking     transitions                            RED (good)
        FAIL  all 448 Character transitions ≤ 2.0 dB of gain moved in 1 ms worst 5.47 dB/ms  (Limit: Loud War → Pump Limit)   [fb421 engine: 16.26]
compress-discrete-fades+noslew     transitions                            RED (good)
        FAIL  all 448 Character transitions ≤ 2.0 dB of gain moved in 1 ms worst 2.82 dB/ms  (Ride: Only Up → Fast Clamp)   [fb421 engine: 16.26]
compress-sample-rate               REALISED t63                           RED (good)
        FAIL  96.0 kHz: REALISED t63 within 12 % of 48 kHz         attack 3.000 ms (-45.5 %), release 55.6 ms (-50.1 %), settled GR 21.78 dB vs 21.74
compress-detect-ownership          no Character changes the rectifier     RED (good)
        FAIL  no Character changes the rectifier at any Detect setting 24 overrides, first: Exact · Loose Grip overrides Detect=Peak
ott-tree-swap-fade                 TREE SWAP                              RED (good)
        FAIL    the TREE SWAP Over Top → Two Band (3 bands → 2) 5.94 dB/ms   [fb421 engine: 5.95 — an instant dip_ = 0.0f]
ott-transition-slew                Character transitions                  RED (good)
        FAIL  all 448 Character transitions ≤ 2.0 dB of gain moved in 1 ms worst 3.02 dB/ms  (Heavy: Fast Grind → Wall Ears)   [fb421 engine: 3.02]
ott-mid-side-fade                  Mid-Side                               RED (good)
        FAIL    Stereo Linked → Mid-Side                         2.66 dB/ms
ott-band-clip-fade                 Type transitions                       RED (good)
        FAIL  all 64 Type transitions ≤ 2.0 dB of gain moved in 1 ms worst 5.20 dB/ms  (Surge → Heavy)   [fb421 engine: 5.97]
core-floor-gate                    -96 dBFS floor comes OUT at            RED (good)
        FAIL  (d) a REAL dithered -96 dBFS floor comes OUT at      -44.4 dBFS (+51.64 dB of lift)  ← must stay inaudible
cert-fft-normalisation             CALIBRATED                             RED (good)
        FAIL  spectrum is CALIBRATED: a −26.02 dBFS sine reads its own level reads 38.956 dBFS (true -26.021)
------------------------------------------------------------------------------------------------------------
15 mutants, 13 gates fired, 2 SURVIVORS  → compress-transition-slew (alone), compress-heat-kind-fade+noslew
```

---

## The two survivors — stated plainly, not buried

Neither is a hole in the *device*. Both are mechanisms that turned out to be **redundant**: some
other mechanism already holds the gate green without them, so deleting them changes nothing a
gate can see. That is still a survivor by §0's definition and it is reported as one.

**1. `compress-transition-slew` — the 1.5 dB/ms transition slew limit, alone.**
Removing it takes the worst Type transition from **1.23 → 1.94 dB/ms** and the worst Character
transition from **1.00 → 1.25**. Both stay under the 2.0 bar, so no gate fires. It *was*
load-bearing when it was written — at that point the worst cell was 3.97 dB/ms — and two later
fixes (the gain element's drive following a smoothed GR, and the soft-clip ceiling tracking the
applied gain rather than the target) removed the need for it. It is now 0.71 dB of margin, not a
mechanism the gates depend on.
**Disposition: KEPT, and honestly labelled.** Deleting it would leave the worst cell 0.06 dB
under the bar, which is not a margin I would ship. The right fix if this is unacceptable is to
delete it and re-voice, not to invent a gate that only it can pass.

**2. `compress-heat-kind-fade` — the waveshaper-kind crossfade, with the slew limit also off.**
Same shape: **1.23 → 1.94 dB/ms**, no gate fires. When it was written it took the worst Type
transition from 5.53 to 1.66; the colour-drive smoothing added afterwards covers the same fault
by a different route (the depth `k` no longer jumps, so the two curves are evaluated at nearly
the same operating point and the difference between them is small).
**Disposition: KEPT.** A waveshaper whose curve changes shape in one sample is wrong on its own
terms even where the current probe cannot see it — but I will not claim a gate proves it, because
none does.

**What it would take to close both properly:** a gate on the *gain element's output continuity*
specifically — the same shape as §4c3's GR-continuity gate but on the post-`colour` sample, with
the ballistics held. That is about forty lines and one more measurement pass; it is not written
tonight and I am not going to describe it as done.

---

## What the mutation round found that the cert did not

Running the mutants **changed two gates**, both of which had been written wrong:

1. **The floor gate defeated itself.** Version 2 isolated the upward computer by subtracting the
   lift of a −110 dBFS bed ("makeup only"). Delete `floorGate` and the −110 dBFS reference *also*
   gets the full upward lift — the two numbers move together and the difference reads **−2.11 dB**,
   green, on an engine with no floor gate at all. *A reference computed through the mechanism
   under test is not a reference.* The bar is absolute now: a −96 dBFS bed must come **out** below
   −70 dBFS. Real engine −76.4; mutant −44.4.
2. **The smoother-seed gate was too weak to be evidence.** With the transition slew limiter also
   present, deleting the seed left the click matrix at 2.04 dB/ms — over the bar, but by 0.04 dB
   and on one cell out of 512. Defence in depth is good engineering and terrible evidence. §4c3
   now measures the seed and nothing else — `gr_` is the same physical quantity in all five
   smoother shapes, so it must be continuous across a shape change — and reads **0.86 dB** on the
   real engine against **17.44 dB** on the mutant.
