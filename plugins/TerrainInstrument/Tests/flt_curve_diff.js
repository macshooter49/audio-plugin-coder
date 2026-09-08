#!/usr/bin/env node
// ══════════════════════════════════════════════════════════════════════════════════════════════
//  flt_curve_diff.js — fb604 · THE DRAWN CURVE vs THE FILTER THAT ACTUALLY RUNS.
//
//      node Tests/flt_curve_diff.js                       # from plugins/TerrainInstrument
//      node Tests/flt_curve_diff.js --csv <curves.csv>    # against another measurement
//      TI_CURVE_MUT=<name> node Tests/flt_curve_diff.js   # mutation control (see below)
//
//  WHY THIS FILE EXISTS.  index.html draws the filter response from a hand-written model —
//  mag(type,f,fc,res,drv,t) in the analyser block, one enum index per roster entry mapped onto
//  model keys by CAT[]. fb384's own comment says "One source, N shapes, no drift", and the law is
//  every filter curve mirrors the DSP and moves with the knobs, no flat placeholder lines ever.
//  NOBODY HAS EVER CHECKED IT AGAINST THE DSP. This is that check.
//
//  GROUND TRUTH is Tests/flt_curves.csv, written by Tests/fltmeas.cpp: EVERY roster type
//  (the count is read from the csv, never typed here) x 8 knob conditions x 120 log-spaced
//  points, measured out of the real FilterSlot through the real SynthVoice 2x converter.
//  Regenerate it with `Tests/fltmeas --csv Tests/flt_curves.csv`.
//
//  HOW THE COMPARISON IS MADE HONEST
//   · mag() returns POWER, so the drawn dB is 10*log10(mag). Compared like for like.
//   · mag() carries NO makeup-gain model, and the DSP's per-type makeup is real, so the primary
//     metric is SHAPE: the minimax constant offset is removed first and reported separately. A
//     large offset is itself a finding (the curve sits in the wrong part of the display window),
//     but it is not the same defect as a wrong shape.
//   · BAND 30 Hz .. 16 kHz — WIDENED at fb604, and the widening is the point. The old ceiling was
//     10 kHz for exactly one reason: above it the measured curve of the 26 oversampled types
//     carried the voice's 2x BOX-DECIMATOR droop (-2.11 dB @ 12 k, -3.70 dB @ 16 k, -4.98 dB
//     @ 20 k) which the display model neither has nor should have, so a 3 dB rule up there would
//     have reported the WRAPPER as a curve bug. fb603 replaced that converter with a half-band
//     and fb604 taught the harness about it: the identity path now measures +0.00 dB to 20 kHz,
//     so the reason is gone and the top 0.7 of an octave stops being a blind spot. (20 kHz still
//     is not judged: that is the model's own band edge, not the DSP's.) The full-band error is
//     printed too, and TI_CURVE_MUT=band_full still reports the 20 Hz..20 kHz sensitivity.
//   · ANIMATED models (MAG_ANIM: comb/phaser/grain/...) are time-varying by design. Their model
//     is averaged in POWER over one full animation period, which is what a Welch PSD of the real
//     filter measures. Their animation swing is printed so an "error" that is really motion is
//     visible as motion.
//
//  WHAT IT CANNOT JUDGE — printed as SKIPPED, never silently passed:
//   · the 4 frequency-shifting / ring types: their output is not a transfer function of the input
//     at all (energy moves between frequencies), so a magnitude ratio is not their response.
//   · the formant / reverb / grain families: CUT is a vowel shift or a grain rate there, not a
//     corner, so "the curve at fc = 1 kHz" is not a claim either side is making.
//   · Diffusor: an allpass chain has unity magnitude by construction. A magnitude metric cannot
//     see what its RES does, in the DSP or in the model. (Its model draws ripple anyway — that is
//     reported as a KNOWN-INVISIBLE, not as a pass.)
//
//  MUTATION CONTROL.  TI_CURVE_MUT=<name> perturbs one input of the comparison and the offender
//  count MUST RISE. A diff that cannot grow its own offender list is not measuring anything.
//      model_flat   every model becomes a flat line  -> every judgeable type must be an offender
//      model_shift  every model's fc drops TWO octaves -> every shaped type must move
//      csv_shift    the MEASURED curve slides two octaves -> same fault, from the truth side
//      band_full    NOT a control — a sensitivity check on the band choice, reported separately
//      TI_CURVE_MUT=all runs the matrix.
// ══════════════════════════════════════════════════════════════════════════════════════════════
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.dirname(__dirname);
const HTML = path.join(ROOT, 'Source/ui/public/index.html');
const MUT  = process.env.TI_CURVE_MUT || '';

// ── MUTATION MATRIX. Re-runs this same file once per mutation in a child process.
//    The bar is NOT "the offender count went up" — with 69 of 71 already wrong that would be
//    nearly free. The bar is that EVERY TYPE THAT PASSED CLEAN BEFORE is caught after: inject
//    the fault, confirm the previously-passing case is the one that goes red.
if ((process.env.TI_CURVE_MUT || '') === 'all') {
  const cp = require('child_process');
  const run = (m) => {
    const e = Object.assign({}, process.env); e.TI_CURVE_MUT = m || '';
    const r = cp.spawnSync(process.execPath, [__filename].concat(process.argv.slice(2)),
                           { env: e, encoding: 'utf8' });
    const o = (r.stdout || '') + (r.stderr || '');
    const off = (o.match(/OFFENDERS=(\d+)/) || [])[1];
    const cl = ((o.match(/CLEAN=(.*)/) || [])[1] || '').trim();
    return { rc: r.status, off: +off, clean: cl ? cl.split(',') : [] };
  };
  const base = run('');
  // 75 = Diffusor. Its DSP magnitude is unity BY CONSTRUCTION and its model's ripples average to
  // unity, so no fault injected on either side can move it. It is excluded from the control's
  // target set for the same reason it is excluded from the diff: not measurable, and SAID so.
  const INVISIBLE = ['75'];
  const target = base.clean.filter(i => INVISIBLE.indexOf(i) < 0);
  console.log('══ fb604 · CURVE DIFF — MUTATION MATRIX ══');
  console.log('   baseline: ' + base.off + ' offenders, clean types = [' + base.clean.join(',') + ']');
  console.log('   structurally invisible, excluded from the control: [' + INVISIBLE.join(',') +
              ']  (Diffusor — allpass magnitude is unity on BOTH sides)');
  console.log('   a mutation is OK only if every remaining clean type [' + target.join(',') + '] goes red.\n');
  let bad = 0;
  for (const m of ['model_flat', 'model_shift', 'csv_shift']) {
    const r = run(m);
    const missed = target.filter(i => r.clean.indexOf(i) >= 0);
    const ok = r.off > base.off && missed.length === 0;
    if (!ok) bad = 1;
    console.log('   ' + m.padEnd(13) + ' offenders ' + String(base.off).padStart(3) + ' -> ' +
                String(r.off).padStart(3) + '   ' + (ok ? 'OK   every clean type went red'
                : 'BROKEN CONTROL — still clean after the mutation: [' + missed.join(',') + ']'));
  }
  const bf = run('band_full');
  console.log('\n   band sensitivity (NOT a control): judging the full 20 Hz..20 kHz band instead of');
  console.log('     30 Hz..' + (Number(process.env.TI_CURVE_BAND_HI || 16000) / 1000) + ' kHz gives ' +
              bf.off + ' offenders vs ' + base.off + '. None of these findings rests on');
  console.log('     the band edge; fb604 widened the ceiling 10 k -> 16 k (the 2x converter that');
  console.log('     forced the old ceiling is gone) and that alone moved the count by one.');
  console.log('\n   ' + (bad ? 'A MUTATION DID NOT CATCH A CLEAN TYPE — the diff is not a gate'
                              : 'every mutation caught every clean type — the diff is live'));
  process.exit(bad);
}

const argv = process.argv.slice(2);
const argCsv = argv.indexOf('--csv');
const CSV = argCsv >= 0 ? argv[argCsv + 1]
                        : path.join(__dirname, 'flt_curves.csv');

// ── 1. LIFT THE WHOLE DISPLAY-MODEL BLOCK OUT OF THE SHIPPING index.html ─────────────────────
//    Sliced, never re-typed: a re-typed copy drifts and then certifies itself.
//    ⚠️ It takes the CONTIGUOUS REGION from `var CAT=[` to the end of mag(), not a fixed list of
//    named functions. mag() grows helpers (fb603 added ordA() and the vowel bank VF/VB/VG); a
//    lifter that named Q/ntch/pk and nothing else silently stopped compiling the moment one
//    arrived, and a diff that cannot parse the model is not a diff.
const html = require('fs').readFileSync(HTML, 'utf8');
const magStart = html.indexOf('var CAT=[');
const magEnd   = html.indexOf('\n    return A*A; }', magStart);
if (magStart < 0 || magEnd < 0) {
  console.error('  !! could not find the display-model block (var CAT=[ ... return A*A; }) in index.html');
  console.error('     the diff cannot run against a model it cannot parse — fix the anchors here,');
  console.error('     do NOT re-type the model.');
  process.exit(2);
}
const block = html.slice(magStart, magEnd + '\n    return A*A; }'.length);
let mag, CAT, MAG_ANIM;
{
  // A stub window/document so the block's `try{ window.__fltRoster.list(...) }catch(e){}`
  // registration lines and any Juce lookups are no-ops instead of throws.
  const stub = 'var window={}, document={}, console={log:function(){},error:function(){}};\n';
  // eslint-disable-next-line no-new-func
  const box = new Function(stub + block + '\nreturn {mag:mag, CAT:CAT, MAG_ANIM:(typeof MAG_ANIM!=="undefined")?MAG_ANIM:{}};')();
  mag = box.mag; CAT = box.CAT; MAG_ANIM = box.MAG_ANIM;
  if (typeof mag !== 'function' || !Array.isArray(CAT)) {
    console.error('  !! the lifted block did not yield mag() and CAT[]'); process.exit(2);
  }
}
const magSrcLine = html.slice(0, html.indexOf('function mag(type,f,fc,res,drv,t)')).split('\n').length;
const catSrcLine = html.slice(0, magStart).split('\n').length;

// ── 2. THE MEASURED GROUND TRUTH ─────────────────────────────────────────────────────────────
if (!fs.existsSync(CSV)) {
  console.error('  !! no measured curves at %s', CSV);
  console.error('     build and run the harness first:');
  console.error('       c++ -std=c++17 -O2 -I Tests/shim -I Source Tests/fltmeas.cpp -framework Accelerate -o /tmp/fltmeas');
  console.error('       /tmp/fltmeas --csv Tests/flt_curves.csv > /tmp/fltmeas.txt');
  process.exit(2);
}
const rows = fs.readFileSync(CSV, 'utf8').trim().split('\n');
const head = rows[0].split(',');
const GRID = head.slice(3).map(Number);
const meas = new Map();     // "idx|cond" -> number[]
const NAME = [];
for (let i = 1; i < rows.length; i++) {
  const m = rows[i].match(/^(\d+),"([^"]*)",([a-z0-9_]+),(.*)$/);
  if (!m) continue;
  const idx = +m[1];
  NAME[idx] = m[2];
  meas.set(idx + '|' + m[3], m[4].split(',').map(Number));
}
const NT = NAME.length;

// ── 3. THE KNOB CONDITIONS, AS THE UI WOULD DRAW THEM ────────────────────────────────────────
const COND = [
  { key: 'cut100_res0',  fc:   100, res: 0.0, drv: 0.0 },
  { key: 'cut1k_res0',   fc:  1000, res: 0.0, drv: 0.0 },
  { key: 'cut8k_res0',   fc:  8000, res: 0.0, drv: 0.0 },
  { key: 'cut20k_res0',  fc: 20000, res: 0.0, drv: 0.0 },
  { key: 'cut1k_res05',  fc:  1000, res: 0.5, drv: 0.0 },
  { key: 'cut1k_res09',  fc:  1000, res: 0.9, drv: 0.0 },
  { key: 'cut1k_res10',  fc:  1000, res: 1.0, drv: 0.0 },
  { key: 'cut1k_drv1',   fc:  1000, res: 0.0, drv: 1.0 },
];

// ── 4. WHAT A MAGNITUDE DIFF CANNOT JUDGE — named, with the reason, never silently passed ────
//    ⚠️ THIS LIST IS DELIBERATELY SHORT. An earlier draft skipped the formant, reverb and
//    karplus families too, on the grounds that "CUT is a vowel shift / a tank rate, not a
//    corner". That argument is true about the -3 dB CORNER metric and FALSE about this one: all
//    of those engines are LINEAR (a resonator bank, an allpass/comb tank, a plucked string), so
//    they have a real magnitude transfer function and the drawn curve is making a claim about
//    it. Skipping them would have hidden the two largest errors in the whole roster. A skip has
//    to be a statement about the METRIC, not about how hard the type is.
const SKIP = new Map();
const skip = (list, why) => list.forEach(i => SKIP.set(i, why));
skip([21, 22, 76, 90],
     'frequency-shifting / ring — the output is NOT a transfer function of the input: energy is ' +
     'moved between frequencies, so an output/input magnitude ratio is not this engine\'s response ' +
     'at all. Structural, permanent.');
skip([25],
     'grain — the magnitude is a randomised window envelope that both the DSP and the model ' +
     're-roll; there is no stable curve for either side to be right about. Structural.');
skip([27], 'None — a bypass; both sides are unity by definition.');

// A caveat is not a skip. These ARE judged; the note says how to read the number.
const CAVEAT = new Map();
const caveat = (list, why) => list.forEach(i => CAVEAT.set(i, why));
caveat([14, 15, 16, 17, 68, 69, 70, 71],
       'formant — CUT is a vocal-tract SHIFT (0.5x..2x) or the a-e-i-o-u morph, not a corner. ' +
       'The model has to implement that shift; "the peaks track CUT one-for-one" is the wrong ' +
       'model, not an unmeasurable one.');
caveat([13, 18, 26, 66, 67, 92, 93],
       'resonator tank (reverb / karplus) — linear, so the magnitude IS its response, but the ' +
       'measured curve is a MODAL spectrum; judged at the 1/6-octave smoothing the probe uses, ' +
       'not mode by mode.');

// fb604 — THE APPENDED 24. Every one of them is LINEAR and time-invariant except the phasers'
// and combs' own animation (which MAG_ANIM already averages), so every one of them is JUDGED.
// None is skipped: a new engine whose curve nobody checked is the exact debt this file exists
// to stop accruing, and "it is new" is not a statement about the metric.
caveat([94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104],
       'phaser (fb604) — the N variants invert the output mix, so for a unity-magnitude allpass ' +
       'chain |1+A|^2 + |1-A|^2 = 4: the model must put its notches where the P variant has ' +
       'PEAKS. A model that draws the P shape for an N index is a 3 dB-plus error at every notch, ' +
       'and it is the failure this caveat exists to make legible rather than excuse.');
caveat([105, 106, 107, 108, 109, 110, 111, 112],
       'comb matrix / flange (fb604) — the damping variant is IN THE LOOP, so it changes the ' +
       'peak heights and the peak SPACING decay, not just a tilt over the top. Judged against ' +
       'the measured comb, animated-averaged like the existing combs.');
caveat([113, 114],
       'first-order shelf (fb604) — a 1st-order shelf reaches its plateau over a much wider ' +
       'transition than the 2nd-order (S=1) shelves at 78/79. If the model draws the same curve ' +
       'for both orders that is a real duplicate in the DISPLAY even where the DSP differs.');
caveat([115, 116, 117],
       'formant register (fb604) — soprano / tenor / alto are the SAME DSP as the bass register ' +
       'at 14-17 with different published formant tables, so CUT is a vocal-tract shift here too. ' +
       'The model needs the register\'s own F1..F3, not the bass table with a gain on it.');

const KNOWN_INVISIBLE = new Map([
  [75, 'Diffusor — an allpass chain has unity magnitude BY CONSTRUCTION. The DSP measures flat ' +
       '(0.0 dB res-sensitivity, structural). A magnitude metric cannot say whether its RES does ' +
       'anything, in the DSP or in the model; it can only say the two agree on being flat.'],
]);

// ── 5. THE COMPARISON ────────────────────────────────────────────────────────────────────────
// fb604 — 10 k -> 16 k, see the banner. TI_CURVE_BAND_HI overrides the ceiling for the
// sensitivity check that justified the widening ("how many offenders come from the top octave
// alone?"), so the answer is reproducible instead of a sed of this file.
const BAND_HI = Number(process.env.TI_CURVE_BAND_HI || 16000);
const BAND = (MUT === 'band_full') ? [20, 20000] : [30, BAND_HI];
const inBand = GRID.map(f => f >= BAND[0] && f <= BAND[1]);
const NBAND = inBand.filter(Boolean).length;

function modelDb(catKey, fc, res, drv, animated) {
  // Animated models are averaged in POWER over one animation period — that is what a Welch PSD
  // of the running filter measures. Static models are evaluated once (t is unused there).
  const ts = animated ? [0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5] : [0];
  const out = new Array(GRID.length);
  const swing = new Array(GRID.length);
  for (let i = 0; i < GRID.length; i++) {
    let s = 0, lo = Infinity, hi = -Infinity;
    for (const t of ts) {
      let p = mag(catKey, GRID[i], fc, res, drv, t);
      if (!isFinite(p) || p < 0) p = 0;
      s += p; if (p < lo) lo = p; if (p > hi) hi = p;
    }
    const mean = s / ts.length;
    out[i] = 10 * Math.log10(Math.max(mean, 1e-12));
    swing[i] = 10 * Math.log10(Math.max(hi, 1e-12)) - 10 * Math.log10(Math.max(lo, 1e-12));
  }
  return { db: out, swing };
}

// minimax constant offset: the offset a viewer's eye removes for free.
function shapeErr(a, b) {
  let lo = Infinity, hi = -Infinity;
  for (let i = 0; i < a.length; i++) {
    if (!inBand[i] || !isFinite(a[i]) || !isFinite(b[i])) continue;
    const d = a[i] - b[i];
    if (d < lo) lo = d;
    if (d > hi) hi = d;
  }
  if (!isFinite(lo)) return { max: 0, off: 0, rms: 0, at: 0 };
  const off = 0.5 * (lo + hi);
  let mx = 0, at = 0, ss = 0, n = 0;
  for (let i = 0; i < a.length; i++) {
    if (!inBand[i] || !isFinite(a[i]) || !isFinite(b[i])) continue;
    const d = Math.abs(a[i] - b[i] - off);
    if (d > mx) { mx = d; at = GRID[i]; }
    ss += d * d; n++;
  }
  return { max: mx, off: -off, rms: Math.sqrt(ss / Math.max(1, n)), at };
}

const THRESH = 3.0;
const results = [];
for (let t = 0; t < NT; t++) {
  if (NAME[t] === undefined) continue;
  const catKey = CAT[t];
  const animated = !!MAG_ANIM[catKey];
  const r = { idx: t, name: NAME[t], cat: catKey, animated, conds: [], worst: 0, worstAt: '', worstOff: 0,
              skipped: SKIP.get(t) || null, invisible: KNOWN_INVISIBLE.get(t) || null,
              maxSwing: 0, resModel: 0, resMeas: 0, errShape: 0, errRes: 0, errDrv: 0,
              deadConds: [], caveat: CAVEAT.get(t) || null };
  let deadConds = 0, liveConds = 0;
  for (const c of COND) {
    let mv = meas.get(t + '|' + c.key);
    if (!mv) continue;
    // A condition in which the DSP puts out nothing has no curve to be right or wrong about.
    // Detected, not hardcoded: when a silent engine is fixed this stops skipping on its own.
    const alive = mv.some((v, i) => inBand[i] && v > -80);
    if (!alive && MUT !== 'csv_flat') { deadConds++; r.deadConds.push(c.key); continue; }
    liveConds++;
    // csv_shift rotates the MEASURED curve TWO octaves up. (An earlier control flattened it
    // instead — that made the offender count FALL 69 -> 64, because several near-flat models
    // then matched a flat truth. A mutation that can make a gate greener is not a control.)
    //
    // fb604 — WHY TWO OCTAVES AND NOT ONE. Both shift controls were one octave, and both went
    // BROKEN the moment the roster gained a FIRST-ORDER shelf: at 6 dB/oct the transition is so
    // wide that an octave of fc error is only a 2.7 dB shape error on Low EQ 6(113) and 1.8 dB on
    // High EQ 6(114) once the minimax offset comes off — UNDER the 3.0 dB bar, so the control
    // could not prove the diff sees those two types at all. That is a fact about the gentlest
    // curve in the roster, not about the detector, and the cure is an injected fault big enough
    // to be a fault for EVERY judgeable type rather than for most of them. Two octaves takes
    // Low EQ 6 to 5.2 dB (model_shift) / 3.9 dB (csv_shift) and High EQ 6 to 3.3 dB on both.
    // \u26a0\ufe0f 3.3 against a 3.0 bar is a 0.3 dB margin: the first-order shelves are the roster's
    // SENSITIVITY FLOOR for any fc-shift control, and a third order of shelf, or a gentler one,
    // would need a bigger shift again. Written down here so the next person who sees a shift
    // control go BROKEN checks the slope of the new type before suspecting the diff.
    const SHIFT = 0.25;                       // two octaves down, the model side
    if (MUT === 'csv_shift') { const g = mv.slice(); mv = GRID.map((f, i) => {
      let j = 0; while (j < GRID.length - 1 && GRID[j] < f * SHIFT) j++; return g[j]; }); }
    let mo = modelDb(catKey, MUT === 'model_shift' ? c.fc * SHIFT : c.fc, c.res, c.drv, animated);
    if (MUT === 'model_flat') mo = { db: GRID.map(() => 0), swing: GRID.map(() => 0) };
    const e = shapeErr(mo.db, mv);
    r.maxSwing = Math.max(r.maxSwing, Math.max(...mo.swing.filter((_, i) => inBand[i])));
    r.conds.push({ key: c.key, max: e.max, rms: e.rms, off: e.off, at: e.at });
    if (e.max > r.worst) { r.worst = e.max; r.worstAt = c.key; r.worstOff = e.off; }
    // Split the error by WHICH KNOB is being asked about — a shape that is right at res 0 and
    // wrong at res 1.0 is a resonance-scaling bug in the model, not a wrong filter shape, and
    // the two are fixed in different places.
    if (c.res === 0 && c.drv === 0) r.errShape = Math.max(r.errShape, e.max);
    else if (c.drv === 0)           r.errRes   = Math.max(r.errRes,   e.max);
    else                            r.errDrv   = Math.max(r.errDrv,   e.max);
  }
  if (liveConds === 0)
    r.skipped = 'the DSP puts out NOTHING at any measured knob position — there is no curve to ' +
                'compare. This skip is detected from the measurement, not hardcoded: fix the ' +
                'silence and the type is judged again automatically.';
  // KNOB RESPONSE: does the drawn curve move with RES the way the filter does?
  const m0 = meas.get(t + '|cut1k_res0'), m9 = meas.get(t + '|cut1k_res09');
  if (m0 && m9) {
    const M0 = modelDb(catKey, 1000, 0.0, 0, animated).db, M9 = modelDb(catKey, 1000, 0.9, 0, animated).db;
    let a = 0, b = 0;
    for (let i = 0; i < GRID.length; i++) {
      if (!inBand[i]) continue;
      a = Math.max(a, Math.abs(M9[i] - M0[i]));
      b = Math.max(b, Math.abs(m9[i] - m0[i]));
    }
    r.resModel = a; r.resMeas = b;
  }
  results.push(r);
}

// ── 6. REPORT ────────────────────────────────────────────────────────────────────────────────
//    node's console.log understands %s/%d only — every number here is formatted before it is
//    printed, so a "%.1f" can never reach the terminal as literal text.
const judged    = results.filter(r => !r.skipped);
const offenders = judged.filter(r => r.worst > THRESH);
const clean     = judged.filter(r => r.worst <= THRESH);
const skipped   = results.filter(r => r.skipped);
const resDead   = judged.filter(r => r.resModel < 1 && r.resMeas > 6);
const P = (s, n) => String(s).padEnd(n);
const L = (s, n) => String(s).padStart(n);
const F = (v, n, d) => L(v.toFixed(d), n);
const say = (...a) => console.log(a.join(''));

say('══ fb603 · UI CURVE vs MEASURED DSP ══');
say('   model : index.html:', magSrcLine, '  mag()   +  index.html:', catSrcLine,
    '  CAT[]   (', NT, ' indices -> ', new Set(CAT).size, ' model keys)');
say('   truth : ', CSV);
say('           ', NT, ' types x ', COND.length, ' conditions x ', GRID.length, ' log-spaced points, measured out of the real FilterSlot');
say('   band  : ', BAND[0], ' Hz .. ', BAND[1], ' Hz  (', NBAND, ' of ', GRID.length, ' grid points)',
    MUT === 'band_full' ? '   ** MUTATED **' : '');
say('   metric: max |drawn dB - measured dB| after removing the minimax constant offset');
say('   rule  : a drawn curve more than ', THRESH.toFixed(1), ' dB from the DSP is WRONG');
if (MUT) say('   MUTATION CONTROL TI_CURVE_MUT=', MUT, ' ACTIVE');
say('');

say('   DETECTOR STATUS — every line says whether the detector FIRED');
say('     types in the csv                 : ', NT);
say('     judgeable by a magnitude metric  : ', judged.length);
say('     SKIPPED (metric is meaningless)  : ', skipped.length);
say('     curve-vs-DSP  (> ', THRESH.toFixed(1), ' dB)        : ',
    offenders.length ? 'FIRED' : 'did not fire', '  (', offenders.length, ' of ', judged.length, ' judged types)');
say('     RES drawn dead (DSP moves, drawing does not) : ',
    resDead.length ? 'FIRED  (' + resDead.length + ' types)' : 'did not fire');
say('');

say('   ── SKIPPED, AND WHY (never silently passed) ─────────────────────────────────────');
const byWhy = new Map();
skipped.forEach(r => { if (!byWhy.has(r.skipped)) byWhy.set(r.skipped, []); byWhy.get(r.skipped).push(r); });
for (const [why, list] of byWhy) {
  say('     ', why);
  say('       ', list.map(r => r.name + '(' + r.idx + ')').join(', '));
}
{
  const withCav = results.filter(r => r.caveat && !r.skipped);
  const byCav = new Map();
  withCav.forEach(r => { if (!byCav.has(r.caveat)) byCav.set(r.caveat, []); byCav.get(r.caveat).push(r); });
  for (const [why, list] of byCav) {
    say('     JUDGED WITH A CAVEAT (not skipped): ', list.map(r => r.name + '(' + r.idx + ')').join(', '));
    say('       ', why);
  }
  const dead = results.filter(r => r.deadConds.length && !r.skipped);
  if (dead.length) {
    say('     CONDITIONS SKIPPED because the DSP is SILENT in them (detected, not hardcoded):');
    dead.forEach(r => say('       ', r.name, '(', r.idx, ') : ', r.deadConds.join(', ')));
  }
}
for (const [idx, why] of KNOWN_INVISIBLE) {
  const r = results.find(x => x.idx === idx);
  if (r) say('     KNOWN-INVISIBLE  ', r.name, '(', idx, ')  worst ', r.worst.toFixed(1), ' dB — ', why);
}
say('');

say('   ── EVERY TYPE WHOSE DRAWN CURVE IS MORE THAN ', THRESH.toFixed(1), ' dB FROM THE DSP ──────────');
say('        SHAPE = the 4 res-0 cutoffs (is the drawn SHAPE this filter at all)');
say('        RES   = res 0.5/0.9/1.0 at 1 kHz (does the drawn resonance SCALE like the DSP)');
say('        DRV   = drive 1 at 1 kHz');
say('   IDX  NAME                MODEL      SHAPE    RES    DRV   WORST  AT CONDITION   OFFSET  RES-move model/DSP');
offenders.sort((a, b) => b.worst - a.worst).forEach(r => {
  say('   ', L(r.idx, 3), '  ', P(r.name, 19), ' ', P(r.cat, 10), ' ',
      F(r.errShape, 5, 1), ' ', F(r.errRes, 6, 1), ' ', F(r.errDrv, 6, 1), ' ', F(r.worst, 6, 1), '  ',
      P(r.worstAt, 13), ' ', L((r.worstOff >= 0 ? '+' : '') + r.worstOff.toFixed(1), 6), '   ',
      F(r.resModel, 5, 1), ' / ', F(r.resMeas, 5, 1), ' dB',
      (r.resModel < 1 && r.resMeas > 6) ? '   RES DRAWN DEAD' : '');
});
{
  const shapeBad = judged.filter(r => r.errShape > THRESH);
  const resBad   = judged.filter(r => r.errShape <= THRESH && r.errRes > THRESH);
  say('');
  say('   ── WHERE THE ERROR LIVES ────────────────────────────────────────────────────');
  say('     drawn SHAPE already wrong at res 0, before resonance : ', shapeBad.length, ' of ', judged.length);
  say('     shape right at res 0, resonance SCALING wrong        : ', resBad.length,
      resBad.length ? '  (' + resBad.map(r => r.name + '(' + r.idx + ')').join(', ') + ')' : '');
  say('     drawn curve honest at every knob                     : ', clean.length,
      clean.length ? '  (' + clean.map(r => r.name + '(' + r.idx + ')').join(', ') + ')' : '');
}
if (!offenders.length) say('   (none)');
say('');
say('   ── WITHIN ', THRESH.toFixed(1), ' dB — the drawing is honest ─────────────────────────────────');
say('   ', clean.length ? clean.slice().sort((a, b) => a.worst - b.worst)
  .map(r => r.name + '(' + r.idx + ') ' + r.worst.toFixed(1)).join('   ') : '(none)');
say('');

if (resDead.length) {
  say('   ── THE DRAWN CURVE DOES NOT MOVE WITH RES, BUT THE FILTER DOES ─────────────────');
  resDead.forEach(r => say('   ', L(r.idx, 3), '  ', P(r.name, 19), ' model moves ',
                           r.resModel.toFixed(1), ' dB, DSP moves ', r.resMeas.toFixed(1), ' dB'));
  say('');
}

say('   ', offenders.length, ' of ', judged.length, ' judgeable types draw a curve more than ',
    THRESH.toFixed(1), ' dB from the filter that runs.');
say('   worst offender: ', offenders.length
  ? offenders[0].name + '(' + offenders[0].idx + ')  ' + offenders[0].worst.toFixed(1) + ' dB at ' + offenders[0].worstAt
  : '(none)');
say('');
// machine-readable, for the runner's mutation check
say('OFFENDERS=', offenders.length, ' JUDGED=', judged.length, ' SKIPPED=', skipped.length);
say('CLEAN=', clean.map(r => r.idx).join(','));
process.exit(offenders.length ? 1 : 0);
