# FX4 — THE FIX ROUND

Author: Claude Code (integration owner), 2026-08-18. Successor to `CONTRACT.md`, which still binds
in full. This file is the **delta**: what the adversarial pass found, and the one new law.

Your device was **refuted**. Four certs reported **459 gates green and 0 failures**; eight skeptics
then found **16 BLOCKER / 20 MAJOR / 20 MINOR** across all four devices. The family audit's verdict
was *"do not wire yet."* Nothing has been wired. Your engine is committed at `fb421` as a baseline
to diff against.

This is not a rebuke — the DSP underneath is largely real and worth fixing, not restarting. The
EQ's decramping (0.828 dB error where RBJ gives 5.645) is genuine measured work. What failed is
**the proving**, and it failed the same way on all four devices independently.

---

## 0. 🔴 THE NEW LAW — MUTATION TESTING IS MANDATORY

**Every law-1 (night-and-day), law-4 (no clicks) and R11 (ceiling) gate must be proven to FAIL
against a deliberately broken engine, and you must paste the failing output.**

`CONTRACT.md` §3.1 already said "run every new gate against the OLD code and require it to FAIL."
It was not enforceable, so it did not happen. It is now a deliverable.

Add a section to your cert — model it on the shipped `flanger_cert.cpp` §K
("Self-check — can these gates actually fail?"). For each protected mechanism:

1. Compile a copy of your engine with that mechanism **deleted** (smoothers → tau 0, the fade-swap
   removed, the widening machine replaced by a fixed delay, the pole cap removed, …).
2. Run the cert against it.
3. **Assert the relevant gate goes RED.** If it stays green, the gate is worthless — rewrite the
   gate, not the mutation.

Deliver `MUTATION.md`: one row per protected mechanism — what was deleted, which gate should fire,
and the real before/after numbers. **A gate that survives its own mutation is a BLOCKER, not a
footnote.**

### Why this is the whole ballgame — what the skeptics actually did

| device | mutation | cert still said |
|---|---|---|
| EQ | removed all 11 smoothers, the coefficient glide, the Mix smoother, the entire fade-swap dip | **104/0** — while the gutted build BLASTS +18.62 dB where the real one dips −3.71 dB |
| Compress | every smoother tau → 0 | **66/0**, nine of ten click lines **bit-identical** |
| Widen | replaced the ENTIRE widening machine with a fixed 12 ms Haas delay | all six R11 gates green **with better numbers**, identical to 3 decimals on all six Types |
| OTT | — | the click bar sits **1.9× above the full scale of its own probe** |

---

## 1. Your blockers

### EQUALIZER
1. 🚨 **`Slant` (was `Tilt`) — a FRONT HERO — is NON-MONOTONIC.** `designOnePole(kind 5)` calls
   `designShelf1(f0,-g,+g)`, a one-pole whose 0 dB crossover sits at `f0/10^(gDb/20)`, so the
   seesaw's pivot **slides from 700 Hz to 2.8 Hz** as the knob opens. The header comment
   ("one pivot, gain −t below and +t above") is true only at t = 0.
   **Independently re-measured by the integration owner** with a sine-transfer probe at Amount's
   default: 120 Hz falls to −4.75 dB at 65 %, then RISES to **+8.55 dB** at 100 % — **13.31 dB of
   wrong-way travel**, 37.3 dB at Amount 200 %. Turn it toward the treble and the bass comes back.
   The cert's gate measured a *difference* (8 kHz minus 80 Hz), which stays monotonic while both
   ends reverse in common mode. Fix the shelf so the pivot is fixed, and gate **each end
   separately**, never their difference.
2. **The ring gate is mathematically incapable of failing.** `t60ms()` runs a 1.5 s window and
   returns `N*1000/FS` = exactly 1500.0 when the tail never crosses −60 dB; the bar is 3100. With
   `limitRing()`'s body deleted the cert still passed, printing **1500 ms — the saturation value of
   its own ruler** — while actually ringing **11.8 seconds**. Use a window longer than the bar, and
   sweep Amount to its ceiling (the gate only ever visited Amount's default).
3. Click gates: see §0.

### WIDEN
1. 🚨 **R11 measures a trigonometric identity, not your device.** Step 8 of `processStereo` is an
   equal-power M/S rotation: at Width 1.0, θ = π/2, so mid is multiplied by cos(π/2) = **0 by
   construction**. `corr = −1.000` and a −130 dB mono fold are the arithmetic consequence of
   zeroing mid on *any* stereo signal. R11 is the gate Max's entire brief hangs on.
   **Re-derive it on the IDENTITY control** — `Amount` at 100 % on every Type, and
   `Feedback`+`Voices` at 100 % together — not on the M/S rotation.
2. 🚨 **The night-and-day gates read write-only telemetry.** `liveTargetCents(v)` returns
   `cents_[v]`; the audio path uses `depS_[v]` / `baseS_[v]`. `viz().voiceCents[]` and
   `liveRateHz()` are likewise published state. Under the total-Haas gutting **every one of these
   gates stayed green** while the audio was a static delay with no detune and no crowd.
   This is fb392 and fb417 in one place. Measure the OUTPUT.
3. 🚨 **`Rate` — a FRONT HERO — is BIT-IDENTICALLY dead on `Steady` and `Blur`.** `Spread` is dead
   on Twin/Blur/Bands (`sprSm0_()` appears only inside the `viz_.voicePan[...]` assignment, never
   in the audio path). `Roam` dead on Twin/Bands, `Balance` dead on Twin. Law 1 fails. **The cert
   sweeps P1–P8 on ONE Type** — sweep every knob on every Type.
4. **The `Voices` floor of 3 does not apply to `Twin`.** `nPair_ = clampi((nV_+1)/2 + C.x1 - 1,1,4)`
   → at `nV_=3` with `Two Line`, `nPair_ = 1` = **2 lines**. ROSTER §0 claims the chorus boundary
   "is enforced in the DSP, not in prose… Cert §O asserts it." It does not — it measures `nV_`, not
   the line count. Also run the chorus-boundary gate on `Twin` and `Steady`; it currently runs on
   `Stack` and `Twofold` only, i.e. not on the Types that could be choruses.
5. R11 politeness: `Amount` at 100 % gives `Twin` **28 cents** — a mid-depth chorus, on the Type Max
   named — while your own `Seasick` Character reaches ±155. The headroom exists and the hero knob
   does not use it. `Spread` moves correlation by **0.125 across its entire travel**, on the device
   whose whole job is stereo. Re-voice upward (R11).

### COMPRESS
1. 🚨 **A real, audible click.** When `relShape_` changes (RS_EXP ↔ RS_DAMPED/RS_DUAL/RS_OPTO) the
   newly-selected smoother's state (`v2_`, `y2_`, `grF_`, `grS_`) is never seeded from the live
   `gr_`, so gain reduction collapses **10.96 → 0.00 dB inside one block** — an ~+11 dB step. **4 of
   8 Type and 2 of 8 Character transitions** do this. The cert tests `Exact → Vari-Mu`, the one
   RS_EXP→RS_EXP case, i.e. the only clean one. `Exact → Ride` reads **7.30×** at the harness's own
   alignment — had the cert picked Ride, this gate would be red today. Seed the new smoother.
2. 🚨 **The click probe is phase-blind by coincidence.** It jumps params at `i = FS*0.5 = 24000`
   with a 220 Hz tone: exactly **110 whole cycles**, a zero crossing, and also exactly a 64-sample
   block boundary. A compressor's every artifact is a gain step, and a gain step at a zero crossing
   produces no sample-to-sample jump at all. Jump at several phases, deliberately off block
   boundaries, and on program material.
3. 🚨 **The sample-rate gates compare a constant to itself.** `fabs(e.attackMs() - e48.attackMs())`
   — `attackMs()` returns `atkMs_`, computed purely from the knob and the Type table with no
   sample-rate term. Measure the **realised** time constant at 44.1 and 96 kHz.
4. **R6 spirit** — see `RENAMES.md`: remove `detForce` from Characters; `Detect` owns detection.

### OTT
1. 🚨 **`clickRatio` divides by the engine's own t=0 start-up burst** (0.14163 — 26× the steady-state
   max jump, 69× the input's own max step). With the bar at 2.5×, a click must exceed |Δy| = 0.354
   to fail: **1.9× the peak of the output signal itself.** Two real discontinuities are already
   hidden, including a **19.8× Two Band tree swap**.
2. 🚨 **The floor-gate proofs are vacuous.** Both probes append exact digital zeros after the note;
   the device is feed-forward (`y = band*g`), so zero in gives zero out for any finite gain. The
   reported −280 dBFS is arithmetic. This is the fb416 shape — the fault lives in the bulk and the
   probe has no bulk. Use a real noise floor.
3. 🚨 **The "air" mechanism is measured on content ~114 dB below the programme.** `bandDbOf` returns
   an **unnormalised** 4096-point FFT magnitude, so the printed "dBFS" is ~40 dB high; sine-injection
   calibration puts the true content at −134.5 dBFS. After maximum lift the band is still 67–80 dB
   under. The ratio moves; the ear cannot follow. fb417 exactly.

---

## 2. Names — apply `Design/fx4/RENAMES.md` VERBATIM

It is a decision, not a suggestion, and every new name in it was grepped against `Source/`,
`Design/fx3/` and `Design/fx4/` and returned zero hits. **Do not substitute your own** — six of my
own first picks collided and were replaced, which is exactly why one head owns the table.

Then **rebuild the no-doubles gate** so it can see what it missed:
- Re-extract `shipped_labels.inc` with a **leading-space-tolerant** pattern. It currently misses
  `Motion` and `Route` — the two fb418 strings R6 is named after — because they are built as
  `"Chorus" + sfxD + " Motion"`.
- Add the two **sibling fx4 directories** as sources. The gate checked `Source/` only, which is why
  five cross-sibling collisions survived.

## 3. Publish your labels from the engine

Only the EQ exposes `backNames()` / `frontNames()`. Widen's per-Type `Amount` relabel and every
Compress/OTT knob label exist **only in markdown** — the exact geometry that let `Cassette` play
`Studio`. Add a label array to your header and make it the single source of truth for the card, the
roster and the worklet. Fix any drift you find (EQ's `Fixed Top` vs `Iron Top` is one).

## 4. What NOT to do

- **Do not tune a constant until a gate goes green.** It moves the failure elsewhere. Cut and say so.
- **Do not weaken a gate to pass it.** If a gate is wrong, rewrite it so it measures the right thing
  and prove the new one fails under mutation.
- **Do not touch anything outside your directory.** `Source/`, `Tests/`, `index.html`, git, builds
  are still forbidden — integration is serial and it is mine.
- **Do not report done without pasted output** from both the cert and the mutation run.
