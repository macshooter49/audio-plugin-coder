# DYNAMICS — what was measured, what was cut, what could NOT be proved

Devices: **COMPRESS** (chain kind 11) and **OTT** (chain kind 12), over one shared `DynamicsCore.h`.
Harness: `dynamics_cert.cpp` — at **fb423, 165 gates, 0 fail, exit 0** (fb422: 139 pass / 3 fail;
first draft: 114 / 0). Full run saved as `dynamics_cert.log`. Mutation: `python3 mutate.py` —
**21 mutants, 20 gates fired, 1 survivor** (`MUTATION.md`).

```
clang++ -O2 -std=c++17 -DDYN_DIR='"<TI>/Design/fx4/dynamics"' \
  -I <TI>/Tests/shim -I <TI>/Source -I <TI>/Design/fx4/dynamics \
  <TI>/Design/fx4/dynamics/dynamics_cert.cpp -o /tmp/dynamics_cert && /tmp/dynamics_cert
```
`DYN_DIR` is where cert §1 goes looking for `ROSTER.md` and the two worklets so it can prove they
still say what the headers say. It defaults to `"."` and FAILS — never skips — if they are absent.
Runtime ~4 min (the 60-second stability sweeps and 128 Character feature extractions dominate).

---

## 1. The headline numbers

**COMPRESS**

| Claim | Measured |
|---|---|
| Ratio reaches **INFINITY:1** exactly at knob 1.0 | output slope above threshold **0.003 dB/dB** |
| Attack reaches the **microsecond** decade | **21 us** (FET 76 at Attack 0); 0.12 ms at mid-window vs Exact's 5.2 ms |
| At Push/Ratio 100, a **48 dB staircase** survives as | **0.0188** of its span — 48 dB in, **0.9 dB** out (bypassed control 1.0000) |
| At Attack 0 / Release 0 / Push 100 it becomes a waveshaper | **13.3 % THD** on an 80 Hz sine (bypassed control **0.126 %**) |
| `OverEasy - Anti` goes **past inf:1 into slope -1** | **+6 dB in -> -6.02 dB out** |
| `Ride` lifts a -40 dBp probe | **+10.50 dB** (every other Type: 0.00) |
| `Limit` overshoot above its own ceiling, **zero lookahead** | **+0.71 dB** (Exact at the same knobs: +4.92 dB) |
| Dry residual at Mix 1.0 | crossfade linearity **-144.2 dB**; Mix 0 is the input to **-260 dB** |
| Closest Type pair | **5.86x JND** (Opto / Vari-Mu) |
| Weakest Character | **2.09x JND** (Opto - Fresh Cell) |
| CPU, worst Type | **9.93 us** / 128-sample block @ 48 k = **0.37 %** of one core |

**OTT**

| Claim | Measured |
|---|---|
| The Mix law: wet path nulls against its phase-matched dry at Amount 0 | **-132.7 dB** |
| `LP4 + HP4 = AP2(fc)` exactly (the identity the Mix law rests on) | **-134.8 dB** below the allpass |
| Worst comb notch at Mix 50 % | **0.24 dB** at 59 Hz (bar 1.0) |
| Engage on the reference chord at defaults | low **+11.7**, mid **+13.7**, high **+10.8 dB** — 3 of 3 bands |
| Unity through, all 8 Types at their defaults | worst **-1.02 dB** (Stagger) at fb423 (was -1.24, Sheen) |
| At Amount 100, a **46 dB staircase** survives as | **0.0742** of its span (Amount 0 control 0.9988) |
| A **-65 dBFS bed** lifted into a wall | **+33.1 dB** (Amount 0 control +0.9 dB) |
| ... and 3 s after the note at Amount/Top Lift/Raise 100 | **-280 dBFS**. Two OTTs in series: also -280 |
| `Sheen`'s air on a genuinely dark pad | ⚠️ **this row was WRONG** — the FFT was unnormalised and aimed 114 dB under the programme. Honest: **+18.70 dB** vs Over Top's **+15.53**, and at fb423 air is no longer Sheen's discriminator at all (§R3.1) |
| `Two Band`'s cross-band ducking | a loud 500 Hz costs a quiet 5 kHz **17.75 dB** (Over Top: 0.67) |
| Closest Type pair | **6.03x JND** (Over Top / Bass Safe) |
| Weakest Character | **2.06x JND** (Bass Safe - Free Low) |
| CPU, worst Type | **28.67 us** / 128-sample block @ 48 k = **1.08 %** of one core |

---

## 2. THE BIGGEST FINDING: the OTT bible's threshold table ships the device 10.7 dB quiet

OTT bible S4.5 gives a threshold table it presents as already translated to this bus:
`T_dn = -48 / -45 / -56 dBFS`, `T_up = -55 / -56 / -66`, makeup `+16 / +12 / +10`, derived as
"an exact -20 dB shift of Vital's defaults". It warns loudly against the opposite error (the dead
port). Ported exactly, on the reference chord:

```
Over Top   out-in -10.70 dB | gr +18.85 +25.72 +29.31 | band -27.1 -15.0 -26.7 dBFS
Heavy      out-in -14.96 dB | gr +26.79 +35.44 +34.51
Two Band   out-in -13.62 dB | gr +24.96 +31.48
```

Two things are wrong at once and they are the same thing: **19-29 dB of gain reduction per band is
far past the 8-18 dB window the same bible gates for**, and the makeup column cannot possibly
compensate it. The bible's own audit note argues the makeups "cannot be down-scaled" because the
bands "see the same gain reduction Vital sees" — that assumes the *band envelope* on this bus sits
where Vital's does relative to its thresholds. It does not.

**The measurement that settles it.** The engine's own per-band followers read the reference chord's
band envelopes as **-27.1 / -15.0 / -26.7 dBFS**. Not RMS — the mean-square follower with a 0.7 ms
high-band attack tracks the saw's discontinuity, which is why the high band reads ~12 dB hotter
than a spectral RMS estimate suggests (the bible's S2.4 figure of "-45...-50" is the RMS one, and
it is not what the detector sees). With those numbers, `GR = slope x (band - T)` reproduces the
measured 18.85 / 25.72 / 29.31 to two decimals.

Re-derived so each band lands **12 / 14 / 13 dB** of reduction, with makeup to match:

| Band | bible T_dn | ours | bible makeup | ours |
|---|---|---|---|---|
| Low | -48 | **-40** | +16 | **+12** |
| Mid | -45 | **-31** | +12 | **+14** |
| High | -56 | **-40** | +10 | **+13** |

Result: **-0.83 dB** through at defaults, three bands engaged, depth still inside the bible's own
gate. Every other Type's table was re-derived the same way against its own measured band levels
(`Gentle`'s 25 ms RMS pre-average reads 3-8 dB lower, so its thresholds sit 6 dB deeper;
`Two Band`'s single 650 Hz split changes the band levels completely).

**The lesson for the next device: a bible that has already done the bus translation for you has
made an assumption about what the DETECTOR sees, not about what a spectrum analyser sees.** Print
the engine's own band levels before trusting any threshold table.

---

## 3. Second-biggest: `Ride`'s upward lane was polite, and R11 says that is a failure

Compressor bible S3.6 specifies the upward lane as `T_up = T - 18 dB`, a fixed 3:1 upward ratio,
and a silence gate at -45 dBp. Implemented exactly, a -40 dBp probe came out **0.53 dB louder** —
a control nobody can hear, on the one Type whose entire identity is that quiet things come back up,
and the Type that exists specifically because R2 puts single-band up+down levelling in Compress.

Rebuilt to the brief instead of the bible: `T_up` sits **6 dB** under `T_dn` (both jaws close on a
narrow window, which is what single-band levelling means), the upward slope rides **the same ratio
law** as the downward one so Ratio 100 drives both to infinity, and the gate moved to -55 dBp to
match OTT's -78 dBFS. Same probe: **+10.50 dB**. Ratio swept on a quiet probe: **7.46 dB**,
monotone.

---

## 4. A bible claim that is not reachable with the bible's own constants

OTT bible S8 gates `levelstep` at "band outputs move **<= 6 dB** per 20 dB input step". We measure
**8.92 dB** and set the gate at <= 10.

This is arithmetic, not a calibration shortfall. On a 20 dB step the mid band goes -15 -> -35 dBFS.
`T_dn = -31`, `T_up = -37`: the quiet state lands **between the two thresholds**, in the 1:1
window, so **no upward lift happens at all** and the step comes out as `20 - GR_loud ~= 6.3 dB`
before ballistics. Run the same arithmetic on Vital's published constants against Vital's own hot
bus and you get ~9 dB. The <= 6 dB figure was never reachable with a 20 dB step and a 7-11 dB
threshold window; it needs a step big enough to cross `T_up`.

So the harness runs the family tell **properly**, with a 30 dB step that does cross both
thresholds: **10.79 dB out for 30 dB in, against an Amount-0 control reading 30.01 dB.** That is
the real statement — 19 dB of a 30 dB level change removed — and the control number proves the
metric is not lying. We narrowed the windows from the bible's 7/11/10 dB to a uniform 6 dB anyway,
which is what moved the 20 dB figure from 9.87 to 8.92.

---

## 5. Six defects the harness found in code that "worked"

Every one compiled clean, ran clean, and sounded plausible.

1. **`Edge` ran 22 dB in the WRONG DIRECTION.** The transient detector was
   `clamp01((fast - slow) / busNominal)` — the DistortionEngine's shape. On a *decaying* pluck the
   slow envelope lags the decay for hundreds of ms, so the difference stays large, the transient
   score pins at 1.0, and Edge stops being a transient control and becomes a broadband gain trim.
   Fixed as a **ratio**: `clamp01((fast/slow - 1) * 0.8)` — level-independent by construction, and
   it falls to zero during a decay (where the slow envelope sits *above* the fast one). After:
   first-30 ms peak -36.02 -> -16.84 dBFS across the knob, monotone.

2. **Two Characters were literal no-ops (0.00x JND).** `Exact - Peak Ears` selected the peak
   detector, which is Exact's *native* detector; same for `Over Top - Sharp Ears` and
   `Heavy - Peak Grab`. Rewritten: `Spike Ears` is peak **+ 5 ms hold**; OTT gained a det-3
   **instant-attack** follower so the computers see the crest instead of the mean square.

3. **`Heavy - No Clip` was byte-identical to `Welded Shut`.** The `clipHead` sentinel used `999`
   for both "inherit from the Type" and "off", so the row that meant to *remove* the per-band
   clipper inherited it instead. Sentinel changed to negative-means-inherit.

4. **The Opto attack knob did nothing.** The T4 charge coefficient was pinned at the nominal 10 ms
   and never read `Attack`, so `Glass` (attack x4) measured 0.74x JND from `Cell Classic`.

5. **The Opto memory did nothing on any probe shorter than ~10 s.** `memMul` scaled only the
   `4.5 s * min(1, M/6)` term, and `M` is a 10-second integrator — so during any realistic burst it
   is ~0 and `Fresh Cell` / `Tired Cell` were the same machine. It now scales the whole slow-pool
   constant, base included.

6. **The Bus auto-release did not adapt.** Both pools of the dual-pool release *attacked* at the
   same rate, so a 50 ms tap filled the slow pool completely and short and long events recovered
   identically (1.36x, where the SSL law wants a large ratio). The slow pool now fills slowly too:
   **12.19x**.

Same class, also fixed: `Even Pools` moved the pool mix toward the **fast** pool — i.e. *less*
two-stage release, the opposite of its name; `Deep Floor` used a -6 dB `T_up` offset, which makes
the upward lane lift **less** (sign error); OTT's makeup did not scale with `Amount`, so "Amount 0"
was a **+13 dB gain stage** and the perfect-reconstruction null read +10.9 dB; and `Top Lift`
folded back over its last eighth because raising only `T_up` eventually hit the `T_up <= T_dn`
clamp, at which point the infinite-ratio **downward** computer started eating the air the upward
one had just made (25.31 -> 49.45 -> **44.35**). The whole top-band window now slides.

---

## 6. Five defects in the HARNESS, which is the more useful list

A harness kinder than reality is worse than no harness — and a harness *stricter* than physics
wastes a night. Both happened.

1. **The Viz cannot see a 20 us attack.** `viz()` is a 60 Hz sampler, so the first draft measured
   *every* Type's attack as "16.0 ms" — which is 1/60 s, the Viz's own period. Every Type reported
   the same number and it looked like a DSP failure. Replaced with `grNow()` (the engine's live GR
   state) at 2-sample hops = **41 us** resolution.

2. **Release measured against zero, not against the steady state.** With a deep Push the
   post-burst programme is still over threshold, so "fall to 37 % of peak" is unreachable and four
   Types reported the probe length (6000 ms) as their release time. Now
   `steady + 0.37*(peak - steady)`.

3. **Crest factor on a steady probe measures nothing.** A compressor only reduces crest when the
   *envelope* varies; on a sustained saw chord the gain is constant and the crest is unchanged.
   The Amount knob measured **0.12 dB of crest removed end-to-end** for exactly that reason.
   Replaced with **envelope spread** (p90 - p10 of a 20 ms RMS envelope) on a chord under a 2 Hz /
   24 dB tremolo — a probe that *has* dynamics. Amount then reads **0.00 -> 17.41 dB**, monotone.
   (Separately: crest over a whole buffer including the first 300 ms — where the followers charge
   from zero at full makeup — reported a **45 dB** output crest. True about a start-up transient,
   useless as a statement about the device.)

4. **An FFT magnitude divided by a time-domain peak.** The zipper gate normalised inter-harmonic
   FFT energy by `peakOf(signal)` — two different units — and reported **-33 dBc of hash on a pure
   sine through a bypassed engine**. Rewritten relative to the fundamental *bin* and gated on the
   *excess over the same measurement with the knob held still*: **-79.5 dBc moving vs -78.9 dBc
   held, excess -0.59 dB.**

5. **A "known answer" that wasn't.** The air-metric self-check built a "+8 dB shelf" from a
   one-pole and expected +8; the true magnitude of that shelf over 8-12 kHz is +7.5 dB falling to
   +6. The metric was right and the *test* was wrong. Replaced with two answers exact by
   construction: a broadband x2.5119 (reads **+7.9979 dB**) and an added 10 kHz tone (reads
   **+39.6 dB**, i.e. the metric is band-selective).

Plus two gates that asked the wrong question rather than being wrong: **monotonicity direction**
(a slower attack removes *less* crest; demanding "increasing" failed four well-behaved knobs), and
**`Grip`**, which is bipolar — at -18 dB the *upward* computer does all the work and at +18 the
*downward* one does, so "dynamic range removed" is a V. Its monotone axis is the output level:
`+15.1 +12.9 +9.9 +6.4 +3.0 -0.5 -4.3 -8.2 -12.1 dB`.

**A gate that has never failed has never been tested.** Every gate here failed at least once
against real code before it passed, which is the only reason to believe the green ones.

---

## 7. What was CUT

- **OTT's `Quad` Type (4 bands).** The contract locks OTT's Viz to `grDb[3]` / `xoverHz[2]`. A
  four-band device drawn on a three-lane viz has to lie about its own band count, and "everything
  audible interacts visually" is a hard rule. The OTT bible's S12 Q6 flags the Type list as a
  pre-ship-only decision (choice cardinality is frozen at birth), so this was the moment.
  `Stagger` takes the slot — 3-band, and its mechanism (a 280:1 ballistic spread so the bands
  decouple in time and the spectrum morphs *through* the note) is one nothing else in either
  roster owns. Measured: **+3.43 dB** of extra HF-vs-LF tilt swing across a decaying note, against
  Over Top's +0.96.
- **Compress's `Grab` pill** (attack and release x0.25 on one click). The Characters already own
  that axis honestly, and a pill that silently re-times two back knobs makes their readouts lie.
- **Nothing else.** All 8 Compress Types and 8 OTT Types survived measurement. Six names changed
  (ROSTER.md S12) because the no-doubles gate found them already shipped — including `Squeeze`,
  which the CONTRACT itself uses for the Compress up+down Type but which is a shipped distortion
  Character label. Flagging that explicitly for the integration owner: the Type is **`Ride`**.

---

## 8. What I could NOT prove

1. **That the plugin reaches any of this.** The fb373 law, and not a formality: a green DSP harness
   proves the ENGINE works and never that the UI, the param IDs or the choice normalisation reach
   it. `Cassette` ran `Studio`'s machine bit-for-bit through four rounds of green measurement. Both
   rosters state their cardinalities (Compress 8/8/5, OTT 8/8/3) and the harness asserts the name
   tables and their clamping, but the UI->param->DSP round trip is the integration owner's gate.
2. **That it sounds good.** Every number is phase-independent and correlates with hearing, and the
   R11 gates are all measured — but "dramatic" is Max's call on the worklets, not a number. The two
   `*-worklet.js` files exist for exactly that and have not been listened to.
3. **Per-band ballistics from a purely broadband probe.** A Character that re-times only ONE band
   is invisible on a broadband step (the mid dominates), so `tauLo` / `tauHi` are measured with
   separate 50 Hz and 8 kHz probes. Audio-domain and honest, but the resolution is the 1 ms
   measurement window, so ballistic differences under ~35 % are below this harness's floor. The JND
   used for time constants is **1.35x**, at the optimistic end of the literature.
4. **CPU at scale.** 9.93 us (Compress) and 28.67 us (OTT) per 128-sample block are
   single-instance figures on an idle machine. Six instances x 13 kinds is ~1.9 % + ~5.6 % of a
   core by arithmetic, which is fine, but `Tests/README.md` records that running certs in parallel
   moved an fx3 CPU gate by 9 % through contention alone. Measure in the plugin.
5. **The `Bite` and `Auto` pills under real playing.** Both are implemented and gated for
   click-freeness and for not breaking unity, but neither has a discriminator in the feature
   matrix — they are per-note behaviours whose value is a judgement.
6. **That OTT's Mid-Side mode is the *right* voicing.** The -6 dB side-threshold offset (which the
   OTT bible's own audit corrects from "free widener" to "width **neutraliser**") is implemented as
   the bible's corrected recommendation. It measurably gives a third distinct L-R balance
   (+12.04 / +0.77 / **+9.70** dB), so it is a real topology — but whether 6 dB is the musical
   number is a listening decision.

---

## 9. Two things that surprised me

**The high band is ~12 dB hotter than a spectrum says it is.** A saw's energy above 2.5 kHz is
~20 dB below its total by RMS, which is what the bible's S2.4 estimate uses. But the high band's
detector has a **0.7 ms** attack and a saw is a train of discontinuities — so the follower tracks
the *edges*, not the average, and reads -26.7 dBFS where an RMS estimate says -39. Every threshold
in the OTT table depends on that difference, and it is invisible unless you print what the engine's
own follower is reading. (It is also why the first draft's Viz `bandDb` read -111 dBFS on a band
that genuinely had content: it was averaging `log|x|^2` sample-by-sample, and every zero crossing
contributes -infinity. Now reported from the follower.)

**Gain-element colour gets WEAKER as the compression gets deeper, unless you fight it.** The
obvious implementation saturates the output — the signal *after* the reduction — so the harder the
device works the quieter the drive into the saturator. Backwards: in a real FET or tube gain
element the distortion comes from the control voltage pushing the device into its nonlinear region,
not from signal level. `Heat` therefore restores 70 % of the current gain reduction before the
saturator and undoes the same factor after it — the fb419 law, makeup **inside**, so the stage's
slope at zero is exactly 1 and Heat can never move the overall gain, only the curvature. Measured:
32.98 dB of added-THD span across the knob, and bit-clean at zero GR regardless of setting.

---

## 10. Files

| File | What |
|---|---|
| `DynamicsCore.h` | the shared math: dBp bus law, fast log2/exp2 (gated to **0.00001 dB** / **0.00074 dB** against libm), branching + mean-square followers, both gain computers, the floor gate, LR4/AP2 on the shipped Simper TPT SVF |
| `TerrainCompressFx.h` | Compress engine, chain kind 11 |
| `TerrainOttFx.h` | OTT engine, chain kind 12 |
| `dynamics_cert.cpp` | 114 gates, both devices + the core |
| `dynamics_cert.log` | the full run, pasted output |
| `shipped_labels.inc` | 1762 capitalised strings extracted from `Source/` — the no-doubles gate's evidence |
| `compress-worklet.js` / `ott-worklet.js` | the same algorithms as AudioWorkletProcessors, same Type/Character/knob names, for an audible Safari mockup |
| `ROSTER.md` | the locked grids, the chassis, the R11 defences, the naming resolutions |

---
---

# THE FIX ROUND (FIXES.md / RENAMES.md) — what changed, what it measured, what is still red

Everything below is a **re-measurement after the adversarial pass**. The fb421 numbers are the
committed baseline; the "now" numbers come from `dynamics_cert.log` and `MUTATION.md` in this
directory. Two probes were written for this round and are committed with the engines:
`probe_switch.cpp` (COMPRESS switch matrix) and `probe_ott_switch.cpp` (OTT).

## 0. The blockers, and the measurement each one now has

| FIXES.md | fb421 | now |
|---|---|---|
| **COMPRESS 1** — the unseeded smoother, a real audible click | worst Type transition **17.36 dB/ms**, worst Character **16.26** | **1.23** and **1.00**, bar 2.0, over **all 64 Type and all 448 Character transitions**, five switch phases each |
| **COMPRESS 2** — the phase-blind click probe | jumped at a zero crossing on a block boundary with one 220 Hz tone; tested ONE Type pair | five phases, broadband programme, every ordered pair; detector self-checked to read **8.00** on a planted +8.00 dB step and **0.0000** on no change |
| **COMPRESS 3** — the sample-rate gate compared a constant to itself | `atk 0.68 ms (48 k: 0.68)` by construction | **realised** t63 off the audio: attack 5.500 ms at 48 k, **+2.2 %** at 44.1 k, **−0.0 %** at 96 k; release 111.5 ms, +0.1 % / −0.0 % |
| **COMPRESS 4 / R6** — `detForce` silently overrode `Detect` | 2 Characters re-pointed the detector | `detForce` **deleted from the struct**; `detectId()` published; **256 Type×Character×Detect combinations** assert the dropdown owns it |
| **OTT 1** — clickRatio divided by the t=0 start-up burst | bar sat 1.9× above the probe's own full scale; a 19.8× tree swap passed | no denominator from the engine at all; tree swap **5.95 → 0.82 dB/ms**, worst Type **5.97 → 0.88** |
| **OTT 2** — the floor gate proved on digital zeros | `−280.0 dBFS`, arithmetic | a **real dithered −96 dBFS bed**: comes out at **−76.4 dBFS**; delete `floorGate` and it comes out at **−44.4** |
| **OTT 3** — "air" measured 114 dB under the programme | `bandDbOf` was an unnormalised FFT magnitude, ~40 dB high | Parseval-normalised; **gated against a −26.02 dBFS sine (reads −26.022) and a −26.02 dBFS noise bed (reads −26.032)**; the dark pad's 8–12 kHz band is now **58.1 dB** under the programme instead of 114 |

## 1. Three things the fixes found that nobody had flagged

- **The soft-clip ceiling was built from the parameter TARGETS, not the applied gain.** `Loud War`
  switches auto-makeup on, which moves the target 12 dB while the applied makeup crawls to it over
  300 ms — so the ceiling ran 12 dB ahead of the gain it exists to catch. Measured at the switch:
  **5.47 dB of level inside one millisecond**. It is computed per sample from the glided
  threshold/lift/makeup now.
- **The gain element's DRIVE followed the raw ballistic state.** Leaving a 2nd-order smoother,
  `gr_` closes a 3 dB gap in 0.5 ms and the waveshaper's depth stepped with it — a waveform change
  no gain slew limit can catch (**2.11 dB/ms**). It follows a 20 ms-smoothed GR now. Hardware heats
  up; it does not teleport.
- **`Leaky` is a shipped label.** `index.html:8680` — the Distortion Diode-1 character list, in
  single quotes, which the old extractor never saw. `RENAMES.md` did not catch it either; the
  **widened corpus** did. Compress `Limit` char 7 is **`Porous`**.

## 2. The no-doubles gate, rebuilt

`gen_shipped_labels.py` is committed. Corpus **1762 → 3310 strings**, from Source/ **plus both
sibling fx4 directories**. Quoted strings are `.strip()`ed *before* the capitalisation test, so
`" Motion"` and `" Route"` — the two fb418 labels R6 is named after, built as
`"Chorus" + sfxD + " Motion"` — are in. The cert asserts their presence, asserts the siblings'
new names (`Slant`, `Chisel`, `Steady`, `Twofold`) are visible, and asserts the sibling-yield
exemption list is **exactly two entries** (`Gentle`, `Low Split` — both RENAMES.md rows where the
sibling gives way) so it cannot quietly grow into an excuse.

## 3. Labels are published from the header

`frontNames()` · `backNames()` · `dropdownNames()` · `pillName()` · `deviceName()` on both
engines, beside the existing `typeNames()` / `charNames()` / `detectNames()` / `stereoNames()`.
**There is no list of knob names left in the harness** — §1 reads them from the header. Both
worklets carry a `LABELS` block that says in a comment that the header wins.

## 4. 🔴 STILL RED — three OTT gates, and I am not going to buy them

> ⚠️ **SUPERSEDED at fb423 — all three are now green.** Kept verbatim because the diagnosis in
> it turned out to be right and was what Max approved. See §R3.1 below for what was built and
> what it measures.

All three appeared **because** the air metric was fixed. They were green on the fb421 engine only
because the metric was ~40 dB high and measured on content 114 dB under the programme.

```
FAIL  Sheen: ≥ 6 dB more 8–12 kHz than Over Top on a dark pad  +20.77 dB vs +15.53 dB
FAIL  closest Type pair still ≥ 3× JND                        2.62× JND  (Over Top / Sheen)
FAIL  every Character ≥ 2× JND from its Type's default        weakest 0.85× JND (Surge · Low Riser), 3 below bar
      weak: Sheen · Higher Split 1.52×   Surge · Slow Riser 1.43×   Surge · Low Riser 0.85×
```

**What this means.** `Sheen` really does put **+20.77 dB** of 8–12 kHz onto a dark pad — that is a
large, audible move, and its ratio against the fb421 metric was **+47.63 dB**, which was never
audible because the band it was lifting sat at −134.5 dBFS. The problem is not that Sheen is weak;
it is that **`Over Top` already gives +15.53 dB**, so Sheen is only **5.24 dB** more than the
generic Type. On the honest metric Sheen is not sufficiently *distinct*, and `Over Top / Sheen`
falls to 2.62× JND with it.

**What it would take.** Sheen's discriminator has to come from a mechanism `Over Top` does not
have, not from more of the same lift. The obvious candidate is already in the Type table and
under-used: Sheen's high band is the only one whose crossover moves (`xhiMul` 0.72) and whose
ballistics are 0.35/8 ms. Widening that gap — pushing X-High down further and taking the high
band's upward threshold up toward the programme so the lift is *program-dependent* rather than
just larger — is a roster change with its own re-measurement, and R11 says the ceiling should be
where it stops being useful. **I did not do it tonight, and I did not move a constant to make the
gate go green.** Same for `Surge · Low Riser` at 0.85× JND: `Surge` has no downward computer at
all, so a Character that only re-times its bands has very little in the feature vector to move —
the correct answer is probably to cut that Character, not to re-tune it.

## 5. 🔴 Two mutation survivors — see MUTATION.md §"The two survivors"

> ⚠️ **ONE OF THE TWO IS NOW CLOSED at fb423** (`compress-heat-kind-fade`, by cert §4c4). The
> other is still a survivor and still labelled. See §R3.3.

`compress-transition-slew` and `compress-heat-kind-fade` are both **redundant**: some other
mechanism holds the gate green without them (1.23 → 1.94 dB/ms, still under the 2.0 bar). Neither
is a hole in the device; both are mechanisms no gate depends on. Kept, labelled, and the forty
lines it would take to gate them properly are described rather than claimed.

## 6. CPU, after all of it

`Opto` 9.93 → **12.56 µs**/128 at 48 k (0.47 % of a core); `Heavy` 29.11 → **31.04 µs** (1.16 %).
The extra is the discrete-rewiring fades and, on OTT, the second dry allpass that now runs in both
trees so a band-count change cannot step the dry path's phase at Mix < 1.

---

# ROUND 3 (fb423) — SHEEN GETS A MECHANISM · THE CEILINGS GET MUTATED · THE LABELS GET GATED

Everything below is measured with `dynamics_cert.cpp` at fb423 and `python3 mutate.py`. Both
outputs are pasted in full — the cert log in `dynamics_cert.log`, the mutation table in
`MUTATION.md`.

## R3.1 🔴 `Sheen` — the three red gates are green, and NOT by moving a constant to a bar

§4 above diagnosed this correctly and Max approved the fix: *Sheen's discriminator has to come
from a mechanism `Over Top` does not have, not from more of the same lift.* What shipped:

| | fb421/fb422 `Sheen` | fb423 `Sheen` | `Over Top` |
|---|---|---|---|
| X-High multiplier | ×0.72 (≈ 1.69 kHz) | **×0.55 (≈ 1.29 kHz at defaults)** | ×1.0 (≈ 2.34 kHz) |
| high-band T_dn | −40 dBFS | **−26 dBFS** | −40 dBFS |
| high-band T_up | −42 dBFS | **−28 dBFS** | −46 dBFS |
| high-band makeup | +15 dB | **+2 dB** | +13 dB |

The high band's measured level on the reference chord is **−26 dBFS**. Moving both thresholds up
to sit *on* that level means the high band is now **below** its own upward threshold at programme
level, so the lift is what the band is doing right now rather than a constant added on top. The
makeup drops 13 dB because there is no longer 10 dB of downward GR to make up — that is calibration
forced by the mechanism, not a knob turned toward a bar, and unity moved −1.24 → **−0.98 dB**.

**The gate changed with the claim, and it is a harder gate, not a softer one.** The old gate was a
MAGNITUDE (`≥ 6 dB more air than Over Top`) and magnitudes can be bought with makeup. The two that
replaced it cannot be:

```
·       the honest air numbers (they are NOT the discriminator any more) Sheen +18.70 dB vs Over Top +15.53 dB on a dark pad
ok    Sheen: 6 dB into a decay its high band LIFTS while Over Top's still CLAMPS signed high-band GR: Sheen -4.00 dB (lift), Over Top +6.68 dB (clamp)
ok    Sheen: its high-band UPWARD knee sits at the PROGRAMME, Over Top's far below it net lift begins at -3 dB of input for Sheen, -18 dB for Over Top (15 dB apart, bar 9)
```

Makeup shifts the whole curve; it cannot invert the sign of the gain and it cannot move the knee.
Mutant `ott-sheen-upward-lane` puts Over Top's high-band thresholds back and turns both red.

⚠️ **Two honest caveats, because they are the weak points of this section.**
1. `Sheen`'s air advantage over `Over Top` on a dark pad is now **+3.17 dB**, *less* than the
   +5.24 dB it had before. The device does not put more top on quiet material than the generic
   Type — it puts top on at a *different place in the programme's own dynamic range*. If Max wants
   "more air" as well as "different air", that is a second change and I have not made it.
2. The sign gate is measured **6 dB below** the reference chord, not at it. At the reference level
   itself Sheen sits ON its own threshold and reads ≈ 0 by construction — that knife edge IS the
   design, and gating it would be gating a coin toss. The bar (±3 dB) is the same "a band is doing
   something" unit the ENGAGE gate uses. The margin on Sheen's side is **1.0 dB**. The robust
   number in this section is the 15 dB knee separation, not the 4.00.

**Cross-type distinctness: 2.62× → 6.03× JND.** Closest pair is now `Over Top / Bass Safe`;
`Over Top / Sheen` is 8.06×.

## R3.2 ✂️ THREE CHARACTERS CUT (CONTRACT §3.2), and what replaced two of them

| Type · Character | was | JND | disposition |
|---|---|---|---|
| `Surge · Slow Riser` | release ×3 | 1.43× | **CUT.** `Surge` has NO downward computer, so a Character that only re-times its bands moves almost nothing in the feature vector. Re-timing an upward-only device changes *when* the lift arrives, and every ballistic feature the matrix has (`stepTau`, `tauLo`, `tauHi`) is measured on a level STEP, which an upward computer settles on almost instantly regardless. Replaced by **`Tied Rise`** — `bandLink = 1`, ONE detector driving all three bands, so a loud low note stops the top from rising. That is cross-band coupling, a topology, not a time constant. **16.67× JND.** |
| `Surge · Low Riser` | spread 2.6 + mono low + memory-slowed release | 0.85× | **CUT**, same reason plus one more: its `lowUp`/`lowMono` parts act on the LOW band, whose GR on the reference chord is `−0.00` for every `Surge` Character (the chord is loud, the upward lane is off), so the feature vector could not see them either. Replaced by **`Mean Ears`** — `det = 2`, a 60 ms RMS pre-average: the riser stops hearing the waveform and rides the mean. **11.85× JND**, and it moves `air` 3.91 → 13.03, `tauLo` 8 → 280 ms and `tilt` 1.76 → 11.03. |
| `Sheen · Higher Split` | X-High ×1.6 | 1.52× | **NOT cut — it could be made distinct by a real mechanism**, which §3.2 asks you to try first. At ×1.6 it was a crossover nudge; at **×2.6** the sheen band shrinks to the top two octaves and the Type's whole upward lane applies to a genuinely different slice of spectrum. **4.54× JND.** |

I am stating the uncomfortable part plainly: **the roster is locked at 8×8, so a cut Character
leaves a hole that has to be filled.** "Cut" here means the *concept* is cut and the slot is
re-cast on a mechanism the engine actually has. That is not the same as deleting a row, and it
would be dishonest to describe it as if it were. `Mean Ears` re-uses the same physics axis as
`Over Top · Long Ears` (a 60 ms RMS pre-average) on a different Type; on an upward-only device it
does something quite different, but it is a re-used axis and not a new one.

**Character distinctness: weakest 0.85× → 2.06× JND** (`Bass Safe · Free Low`). 0 below the bar.

## R3.3 🧪 MUTATION — the ceilings and the dead knobs, which nothing had ever tried to break

FIXES.md §0 named this as the last outstanding piece anywhere in the family. Seven mutants added:

- **`compress-no-ceiling`** — the slope cap becomes a polite **2:1**. §4b's staircase gate must fire.
- **`ott-no-ceiling`** — Amount's top half stops closing the two thresholds on each other. §6b(a).
- **`compress-dead-knob (Burn, P8)`** and **`ott-dead-knob (Treble, P8)`** — one knob per device
  stubbed to a no-op, and the law-1 0→100 sweep must go red. Without these, "every knob is night
  and day" rested on the sweep having been pointed at the right member, which nothing checked.
- **`ott-sheen-upward-lane`** — R3.1's mechanism, deleted.
- **`names-downstream-drift`** — one string in `ott-worklet.js` drifted off the header. The
  downstream-equality gate is new and had therefore never failed; a gate that has never failed has
  never been tested.
- **`compress-heat-kind-fade (alone)`** — the survivor, now firing on its own (see below).

## R3.4 ✅ ONE OF THE TWO SURVIVORS IS CLOSED — cert §4c4

`compress-heat-kind-fade` survived because **every gate it faced was a LEVEL gate**. §4d measures
dB of gain per millisecond; a waveshaper whose curve changes shape in one sample at constant gain
is invisible to every level metric there is. §4c4 measures the gain element's **CURVATURE**:

    S ≡ H3_dB − 3·H1_dB          (for y = x + c·x³:  H1 ≈ A, H3 ≈ (c/4)A³  ⇒  S = 20·log₁₀(c/4))

which is independent of drive to cubic order, sampled in the 2 ms either side of the switch on a
2 kHz tone with `Burn` at 100. Real engine **2.02 dB** (Types) / **4.36 dB** (Characters) against a
6 dB bar; mutant **23.89 dB**. Control (same config both sides) **0.00 dB**.

🔬 **Two earlier drafts of this gate were level gates in disguise and are recorded in the source so
nobody rebuilds them.** Absolute 3rd-harmonic level goes as A³ and read **12.08 dB** on
`Ride: Only Up → Slow Iron` — a pair whose GR legitimately travels 0 → 15.6 dB over 60 ms at
0.35 dB/ms, well inside §4d's bar. H3/H1 goes as A² and read **10.69 dB** on the same pair for the
same reason. Section K now plants a pure +6.02 dB gain step and a ×4 curvature change and shows
the invariant reads **0.204 dB** for the first and **11.84 dB** for the second (theory: 12.04).
It also prints the honest limit of that blindness: at the drive the gain element actually runs
(H3 ≈ −20 dBc), a pure gain step still leaks **2.12 dB** into S. That leak is why the bar is 6 dB
and not 1, and it is printed rather than hidden.

**`compress-transition-slew` is still a survivor and is still labelled.** It is a LEVEL mechanism,
§4d is the level gate, and removing it moves the worst transition 1.23 → 1.94 dB/ms — under the
2.0 bar, so no gate fires. §4c4 cannot close it *by construction*: the shape invariant cancels gain
on purpose. Closing it would require a gate whose bar sits between 1.23 and 1.94 dB/ms, i.e. a gate
only this mechanism can pass, which is the thing MUTATION.md already refuses to do. **1 survivor,
down from 2.**

## R3.5 🏷️ THE LABEL GATE NOW COVERS EVERY LABEL, AND DOWNSTREAM IS GATED EQUAL

Two changes, in the order fb423 §Gate asks for — **delete first, gate second.**

**Deleted.** The harness carried its own copy of the knob names. Section 4 printed `Latch (P6)`
and `Heat (P8)` and section 6 printed `Speed (front 2)` while the headers said `Cling`, `Burn` and
`Chase` — *three stale strings in the harness whose job is to police stale strings.* Every label
§1/§4/§6 prints is now read from `frontNames()` / `backNames()` / `dropdownNames()` /
`detectNames()` / `stereoNames()` / `pillName()` at the moment of printing. There is no label table
in `dynamics_cert.cpp` any more. A table that cannot exist cannot drift.

**Gated.** A worklet is a separate runtime and a roster is a design document; neither can include
a C++ header, so both are read off disk by the cert and compared string for string:

```
ok    the downstream files are ON DISK where this gate looked found in .../Design/fx4/dynamics (compress-worklet.js, ott-worklet.js, ROSTER.md)
ok    compress-worklet TYPES/CHARS/DETECT/FRONT/BACK/DROPS == the header   8 / 64 / 5 / 4 / 8 / 2 strings, exact
ok    ott-worklet TYPES/CHARS/STEREO/FRONT/BACK/DROPS == the header        8 / 64 / 3 / 4 / 8 / 2 strings, exact
ok    every published label appears VERBATIM in ROSTER.md                  all 184 of them
ok    no RETIRED label survives downstream (ROSTER.md + both worklets)     13 retired strings x 3 files, 0 hits
```

**It found real rot the moment it ran.** Nine gates went red on the first build: seven stale
`Heat`/`Latch`/`Bite`/`Twin` rows in `ROSTER.md`, the Detect option row still reading
`Auto · Peak · Average · Long · Spike`, and the retired-name history note carrying `RMS Ears` and
`Spike Ears` in a file that is not allowed to author a name. All fixed; the history moved into
`TerrainCompressFx.h`, which is the only place a name — including a retired one — is authored.

🔬 **And the gate's own first draft was wrong in the fb392 way.** Its JS parser matched
`const FRONT = [`, but the worklets column-align their declarations (`const FRONT  = [`), so it
read **zero strings** for four of the six tables and reported a mismatch. It went red for the
right reason by luck. The parser is whitespace-tolerant now, and `the downstream files are ON DISK
where this gate looked` is its own gate — a downstream gate that quietly finds nothing to check is
the fb392 stub wearing a different hat.

**Compress's `Auto` pill left `kShared`.** It is now a one-entry `kRuled[]` citing RENAMES.md
fb423 §SANCTIONED (same law as the shipped Distortion `Auto` pill), asserted to be exactly one
entry, next to the sibling-yield list asserted to be exactly two. A gate that can exempt itself is
not a gate. Single-quoted literals stay in the corpus permanently — that is how `Leaky` hid.

## R3.6 Where this leaves the two devices

```
════════════════════════════════════════════════════════════════════════
  PASS 165   FAIL 0
════════════════════════════════════════════════════════════════════════
CERT_EXIT=0
```

| | first draft | fb422 (the fix round) | **fb423** |
|---|---|---|---|
| cert | 114 pass / 0 fail | 139 / **3** | **165 / 0**, exit 0 |
| mutants | — | 15, 13 fired, **2 survivors** | **21, 20 fired, 1 survivor** |
| closest Type pair (OTT) | — | **2.62× JND** | **6.03× JND** |
| weakest Character (OTT) | — | **0.85× JND**, 3 below bar | **2.06× JND**, 0 below bar |
| labels gated | 177, vs `Source/` only | 184, vs `Source/` + siblings | **184, vs 3319 strings, + downstream EQUALITY** |
| CPU, worst Type | — | Opto 12.56 / Heavy 31.04 µs per 128 @ 48 k | **Opto 11.89 (0.45 %) / Heavy 31.33 µs (1.18 %)** |

**What is still not proved, honestly.**

1. **`compress-transition-slew` is still a mutation survivor.** It is redundant against §4d by
   0.71 dB and there is no honest gate that only it can pass. Labelled, not claimed.
2. **`Sheen`'s sign gate has a 1.0 dB margin** (−4.00 dB against a −3.0 bar). The 15 dB knee
   separation is the robust number in that section; the sign is the vivid one.
3. **`Sheen` now puts LESS extra air on a dark pad than it used to** (+3.17 dB over `Over Top`,
   was +5.24). Its claim changed from "more air" to "air that tracks the programme". If Max wants
   both, that is a further change and I have not made it.
4. **`Mean Ears` re-uses a physics axis** (`det = 2`, the 60 ms RMS pre-average) that `Over Top ·
   Long Ears` and `Gentle · Long Window` also use. On an upward-only Type it behaves very
   differently — 11.85× JND — but it is a re-used axis, not a new one, and calling it new would be
   a claim I cannot measure.
5. **Everything here is still ENGINE-side.** A green harness proves the engine works; it never
   proves the plugin reaches it (fb373). The UI → param → DSP round trip is the integration
   owner's, and nothing in this directory can gate it.
