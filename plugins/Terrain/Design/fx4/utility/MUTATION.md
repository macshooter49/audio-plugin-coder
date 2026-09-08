# utl_cert — MUTATION LOG (fb445, the UTILITY strip, chain kind 14)

fb421: **a gate that has never failed has never been tested.** Every gate below was
either (a) demonstrably red on a real defect during development, or (b) deliberately
broken afterwards to confirm it notices. Anything that could not be made to fail is
recorded as such, honestly, rather than counted as coverage.

Final state: **`utl_cert` 84 pass / 0 FAIL** · **33 mutations run, 32 turned at least one
gate red**, the one that did not is in the last table.

```
cd plugins/TerrainInstrument
clang++ -O2 -std=c++17 -I Tests/shim -I Source Tests/utl_cert.cpp -o /tmp/utl_cert && /tmp/utl_cert
```

Mutations are applied to a COPY of the header under `/tmp/utlmut/inc/` and compiled with
`-I /tmp/utlmut/inc` ahead of `-I Source`, so the shipped file is never edited to run them.

---

## 1. Gates that went RED on REAL defects in this engine, then green on the fix

| # | Gate(s) that caught it | The defect | Evidence |
|---|---|---|---|
| D1 | §D ×3, §J ×2, §H `Mono Below` + `Slope` — **6 gates** | **`x - LP3(x)` IS NOT A THREE-POLE HIGH-PASS.** `x - LP(x)` is complementary only for a *single* pole; cascade three and the low-passed copy carries ~33° of phase lag in the stopband, so the subtraction stops cancelling and the "stopband" bottoms out near −5 dB. Six gates, three symptoms, one line. | `corr 40-120 Hz = +0.5173` (contract +1) · corner sweep **non-monotonic** `-0.26 -0.36 -0.45 -0.23 +0.33` · `Slope` 0.095 dB across its whole travel · 44.1/96 kHz fold `-5.6 dB`. After the per-stage complementary cascade: `+0.9999`, monotonic, `Slope` 1.892 dB, fold `-31.9 dB`. |
| D2 | §B ×2, §F, §G — **4 gates** | **`cos(pi/2)` is 3.3e-8 in float, not 0.** A naive equal-power mix therefore leaks the dry forever at −150 dBFS, and four gates that used the word "EXACTLY" were quietly false. | `unity detent = 3.725e-09` · `Mix 0 worst |out-in| = 2.328e-10` · `Strain 0 bypass = 3.725e-09`. After branching both endpoints: **all three exactly 0.000e+00**. |
| D3 | §A ×2 — **and a longer seed could not fix it** | **A FLOAT ONE-POLE STALLS SHORT OF ITS TARGET.** `v += a*(t-v)` stops moving once `a*(t-v)` falls below half an ULP of `v`; the polarity matrix parked at **−0.999964** and never arrived. Seeding for 1 s changed nothing — the value was not converging slowly, it was *parked*. | `'Flip L' + 'Flip R' worst |out+in| = 1.788e-06`, identical at 0.35 s and at 1.0 s of seed. Fixed with `glide()`, a snap ABOVE the stall point (`ulp(t)/2a` ≈ 3.6e-5 at 25 ms) → **0.000e+00**. |
| D4 | §G `Rail` vs `Cushion` | **The engine's own comment was BACKWARDS.** It claimed the stereo-linked guard *collapses* the image under load. A common gain cannot change a ratio: the linked one **preserves** the image and the *independent* ones collapse it — which is why every desk bus compressor has a stereo-link switch. | Measured `Rail +0.004 vs Cushion -0.003` on the (also wrong) correlation probe; on the corrected side/mid probe, `Rail +0.00 dB` vs `Cushion -6.31 dB`. Comment and gate both rewritten. |
| D5 | §H `Rumble` | **2…160 Hz was timid.** Four of six knob steps sat below 40 Hz, where a first-order corner move is inaudible *by construction*. | `Rumble smallest step = 0.001 dB` across the entire travel. Range → 15…320 Hz: **1.626 dB**. |
| D6 | §G `Strain at 100 % is DESTRUCTIVE, not ABSENT` — **a gate that did not exist** | **The top of `Strain` was a MUTE.** With an exact `1/drive` makeup (which is what keeps `sat'(0) == 1`) a hard-clipped output sits at `rail/drive`, so a *fixed* rail put full travel 43 dB down. Every existing guard gate stayed green through it — "bounded" gets *more* true as the output vanishes, and "unit slope at zero" only ever looks at small signals. | Found by mutation M20, which turned **0 gates red**. Per fb421 the finding was about the gate. New gate + `rail *= sqrt(drive)`: output now `-21.1 dB` with a 3rd harmonic at `-9.6 dB` of the fundamental (a real square wave). |
| D7 | §I `Strain comes in and out of circuit without a click` — **a gate that did not exist** | **The guard's DRIVE jumped while the guard CROSSFADED.** Fading the wet/dry is only half a transition: when `Strain` leaves zero the drive changes in the same block, so the curve being faded in is a different curve from one sample to the next. | Found immediately by the new gate at **10.98x a 220 Hz tone's own slope**. Fixed by gliding the drive and deriving the makeup *from the glided value* (two independent smoothers would drift off unity mid-glide and swell ~2.5 dB). Now **1.00x**. |

## 2. Gate defects — found by mutation or by a false RED, and FIXED as gates (fb421)

> *"If a mutation turns zero gates red, that is a finding about your GATE."* Six of these
> were exactly that. Four more were the opposite — a gate failing a correct engine, which
> is the same disease.

| # | Symptom | What was actually wrong with the GATE |
|---|---|---|
| G1 | `Rumble` 0.001 dB, `Mono Below` 0.072 dB, `Slope` 0.095 dB — three "dead" knobs that were not dead | §H averaged \|Δ\| flat over 20 Hz–16 kHz, so a control acting on ONE octave was divided by the nine it does not touch. **An ear does not average across the spectrum — it notices the octave that changed.** Metric → mean per octave, then the MAX over octaves and over L/R/M/S. Same numbers become 1.626 / 4.672 / 1.892 dB. |
| G2 | `Steer` 32.19x, `Twist` 9.47x "clicks" that were not clicks | The click metric divided the worst step of **either** channel by the RMS of the **left** one. `Steer` at the end of its travel mutes the left channel on purpose; the survivor's perfectly ordinary slope divided by a channel that had been switched off reads as a click. → normalise on both channels. |
| G3 | `Gain` 28.48x "click" | The metric compared the worst step to a FIXED reference. It was not a discontinuity, it was the tone being 30 dB louder at the end of a +90 dB sweep. **An absolute step bar cannot tell a click from a crescendo.** → normalise inside 256-sample blocks. |
| G4 | **M11 (delete ALL matrix smoothing) → 0 RED** | The pill/wiring probes toggled at `i = 0.4 * 48000 = 19200`, which at 220 Hz is **exactly 88 whole cycles** — the tone was sitting on a zero crossing, so inverting the polarity produced no step at all. A gate that toggles at one arbitrary instant tests one arbitrary phase. → sweep the toggle across a whole cycle, take the worst; bar 40x → 6x. M11 now: **7 RED, up to 69.41x**. |
| G5 | **M23 (make `Bleed` full-band) → 0 RED** | §H only asked whether the knob DID something, and a full-band crosstalk still does. But HF-only is the entire claim — `Bleed` is the inverse of `Mono Below`. → new gate on the BAND: corr 3–12 kHz `+0.9554` while corr 40–150 Hz stays `+0.1020`. |
| G6 | **M29 (`kImageMax` 300 % → 130 %) → 0 RED** | Every §C gate asks that the mid SURVIVES, and a timid maximum makes that *easier*. R11 cuts both ways: the bound needs a floor AND the travel has to reach somewhere worth going. → new gate reading the engine's own `meterImageW()` **and** the measured side/mid (3.00x / 5.24x). |
| G7 | **M25 (delete all four section crossfades) → 0 RED** | §I swept CONTINUOUS knobs and toggled PILLS — and the sections that come in and out when a knob *leaves zero* were covered by neither. **A knob crossing zero is a switch wearing a knob's clothes.** → new §I subsection toggling `Strain` / `Mono Below` / `Bleed` 0↔on across 8 phases. M25 now **3 RED** (14.75x / 17.37x / 16.44x) — and the new gate immediately found defect D7. |
| G8 | **M27 (delete `railFold`'s BIBO period wrap) → 0 RED** | Subtle: without the wrap the fold degenerates to `\|x\| - 2c`, which the `1/drive` makeup scales back to roughly the input — still perfectly BOUNDED, still measurably "different", it has just stopped being a wavefolder. → new gate on HARMONICS 10–30 vs the fundamental: fold `0.48` vs clip `0.11`. |
| G9 | §G unit-slope: false RED at 9.619 dB on `Fuse` | **1e-4 is not a small signal here.** `Fuse` drives 48 dB and then another 4x into a HALVED rail, so 1e-4 arrives at 4x the rail. The probe has to be small relative to the WORST-CASE drive (~1000x), not to full scale. → 1e-6, now 0.000 dB. |
| G10 | §H `Mix` 0.301 dB | On a transparent strip **the wet IS the dry**, so `Mix` was blending a signal with itself and the only thing moving was the equal-power law's own +3 dB bulge. Not a dead knob — a knob with nothing to do. → swept over `Wiring = Difference` + strain, the bod_cert precedent (which sweeps in-loop controls with the loop closed). Now 1.547 dB. |

## 3. Mutations run deliberately, and what each one caught

| # | Mutation | Gates RED | Caught by the intended gate |
|---|---|---|---|
| M01 | delete both polarity flips | 2 | YES — §A `Flip L + Flip R gives EXACTLY -in`, `Flip L alone nulls a mono source` (`-29.0 dBFS`, want silence) |
| M02 | delete the `Trade` pill | 1 | YES — §A Trade |
| M03 | flip `Difference`'s sign into a SUM | 1 | YES — §A `Difference NULLS a correlated pair` |
| M04 | remove the mid FLOOR (adopt Widen R11: Image kills the mid) | 4 | YES — mid-gain sweep, and `Strip` fold-down at **−240.00 dB** where Widen's cert *demands* ≤ −25 |
| M05 | DC block passes through | 3 | YES — §E `DC ON removes it`, the `Tuck` pair, §H `Rumble` |
| M06 | delete `Mono Below` | 6 | YES — §D ×3, §H ×2, §J ×2 |
| M07 | revert the crossover to `x - LP3(x)` (**the original D1 defect**) | 6 | YES — the same six that found it the first time |
| M08 | move the guard makeup OUTSIDE (`invDrive -> 1`) | 4 | YES — unit slope at zero, bounded output, both sample rates |
| M09 | bypass the whole guard | 7 | YES — every §G gate plus §H `Strain`/`Clamp` |
| M10 | un-link `Rail` (per-channel detector) | 2 | YES — the corrected stereo-link gate, plus Character distinctness |
| M11 | delete the matrix smoothing (switch, don't glide) | 7 | YES **after G4** — polarity **69.41x**, every Wiring change 13–28x |
| M12 | remove the fader's true zero (t=0 becomes −60 dB) | 2 | YES — `Gain 0 is a true -inf`, and Mix-100 residual jumps to −86 dBFS |
| M13 | remove the unity-detent snap | 2 | YES — `unity is exactly 0.000 dB` (1.118e-08) |
| M14 | remove the `glide()` snap (**the D3 float stall returns**) | 2 | YES — `Sum` identity, `Flip` exactness |
| M15 | compute the equal-power endpoints instead of branching (**D2 returns**) | 3 | YES — all three "EXACTLY" gates |
| M16 | fb373: clamp `Type` on the SLOT count, not the live count | 1 | YES — `reserved slots resolve to entry 0` |
| M17 | collapse `Ripple` into `Cushion` (delete the mirror) | 1 | YES — Character distinctness |
| M18 | make `Coil` full-band (delete the band split) | 1 | YES — Character distinctness |
| M19 | `Rumble` back to the timid 2…160 Hz (**D5 returns**) | 1 | YES — §H `Rumble` (0.169 dB) |
| M20 | rail stops tracking `sqrt(drive)` (**D6 returns**) | 2 | YES **after D6's new gate** — `Strain at 100 % is DESTRUCTIVE, not ABSENT` |
| M21 | remove `Slope`'s order compensation (the corner drags with the slope) | 2 | YES — the `Slope` skirt gate and the `Bleed` band gate |
| M22 | make `Canopy` full-band (identical to `Strip`) | 1 | YES — Type distinctness, at **0.000 dB** |
| M23 | make `Bleed` full-band | 1 | YES **after G5** |
| M24 | `Dim` is −6 dB instead of −20 | 1 | YES — §B Dim |
| M25 | delete all four section crossfades | 3 | YES **after G7** — 14.75x / 17.37x / 16.44x |
| M26 | delete the denormal flush | **0** | **NO — see the last table** |
| M27 | delete `railFold`'s BIBO period wrap | 1 | YES **after G8** |
| M28 | `kStrainMaxDb` 48 → 6 (a timid maximum) | 3 | YES — Character distinctness and §H `Strain`/`Clamp` both collapse |
| M29 | `kImageMax` 3.0 → 1.3 (a timid maximum) | 1 | YES **after G6** |
| M30 | `kGainMaxDb` +30 → +6 (a timid maximum) | 1 | YES — §B fader top |
| M31 | replace `Turn`'s rotation with `Strip`'s scaling | 2 | YES — `Turn widens a MONO source`, Type distinctness at 0.000 dB |
| M32 | make `Outer` scale the mid too | 2 | YES — fold-down invariance, Type distinctness at 0.000 dB |
| M33 | stop gliding the guard drive (**D7 returns**) | 1 | YES **after D7's new gate** — 10.98x |

## 4. NOT load-bearing — recorded rather than claimed

| Mechanism | Mutation result | The honest reading |
|---|---|---|
| the denormal flush in `LP1::process` and `DCBlock::process` (`if (fabs(z) < 1e-25f) z = 0`) | **M26 → 0 RED**, and no reformulation of an *audio* gate can change that | It is a **CPU** protection, not an audio property: by construction it only ever zeroes values already 100+ dB below anything audible, so no spectral, level, correlation or click metric can ever see it. Gating it honestly would mean a timing gate, which is machine-dependent and flaky, and the harness deliberately has none. It is kept because `ScopedNoDenormals` is explicitly NOT assumed (CONTRACT §2) and a one-pole decaying toward zero is exactly where subnormals live — but it is **not covered by a gate, and this table says so instead of counting it**. |
| `meterGuardDb()`, `meterMid()`, `meterSide()` | never read by any gate | The other six meters ARE load-bearing and appear in gate conditions: `meterGainDb` (§B mute), `meterMidGain` (§C floor sweep), `meterImageW` (§C 300 %), `meterCorr` (§C mono), `meterPolarity` (§A ×2), `meterType` + `meterWiring` (§A reserved slots). These three exist for the UI's 60 Hz push and have no cert consumer. Listed, not claimed. |

## 5. CPU (48 kHz, 128-sample blocks, measured)

| Setting | µs/block | % of one core |
|---|---|---|
| defaults — the transparent strip | 3.43 | 0.128 % |
| everything engaged, worst Character (`Coil`) | 10.49 | 0.393 % |

Every optional section (guard, DC, Bleed, Mono Below, the whole M/S block) sits behind a
crossfade amount that reaches **exactly** zero, so the transparent path really does skip
them — which is both the CPU story and the reason `Mix 0` and `Strain 0` are bit-exact.
