// fb498 — PROVE THE LFO VALUE FEED IS SMOOTHED, using the SHIPPED code.
//
// This does not re-implement __modViz / __mvLfoValAt. It EXTRACTS them verbatim from
// index.html and runs them against a controllable clock, so what is measured is what ships.
//
// The model: an LFO whose true value is a sine at LFO_HZ. C++ pushes the sampled value at
// PUSH_HZ (60 Hz ceiling, self-clocked far lower on Windows per fb482). The UI renders at
// 60 fps. Before the fix every consumer latched, so between pushes the rendered value was a
// flat plateau and each push was a step. After the fix it should be piecewise linear.
const fs = require('fs');

const file = process.argv[2];
const src = fs.readFileSync(file, 'utf8');

function extract(startsWith, endsWith) {
  const i = src.indexOf(startsWith);
  if (i < 0) throw new Error('could not find: ' + startsWith.slice(0, 60));
  const j = src.indexOf(endsWith, i);
  if (j < 0) throw new Error('could not find end for: ' + startsWith.slice(0, 60));
  return src.slice(i, j + endsWith.length);
}

const modVizSrc = extract('window.__modViz=function(e,l,p){', '} };');
const valAtSrc  = extract('window.__mvLfoValAt=function(i){', 'return isFinite(out)?out:cur[i]; };');

let NOW = 1000000;
const window_ = {};
const sandbox = { window: window_, Date: { now: () => NOW }, isFinite, mvE: null, mvL: null };
const vm = require('vm');
vm.createContext(sandbox);
vm.runInContext('var mvE=null, mvL=null;\n' + modVizSrc + '\n' + valAtSrc, sandbox);

const LFO_HZ = 1.0;
const PUSH_HZ = Number(process.argv[3] || 20);   // Windows self-clocked rate
const FPS = 60;
const SECONDS = 3;

const truth = (tMs) => Math.sin(2 * Math.PI * LFO_HZ * (tMs / 1000));

const frameDt = 1000 / FPS;
const pushDt = 1000 / PUSH_HZ;
let nextPush = 0;
const raw = [], smooth = [], real = [];

for (let f = 0; f * frameDt < SECONDS * 1000; f++) {
  const t = f * frameDt;
  NOW = 1000000 + t;
  while (nextPush <= t) {
    NOW = 1000000 + nextPush;
    const v = truth(nextPush);
    window_.__modViz([0], [v, 0, 0, 0, 0, 0, 0, 0, 0, 0], [0.5, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    nextPush += pushDt;
  }
  NOW = 1000000 + t;
  const r = window_.__mvLfoVal ? window_.__mvLfoVal[0] : 0;   // what a LATCHING consumer saw
  const s = window_.__mvLfoValAt(0);                          // what a smoothed consumer sees
  raw.push(r); smooth.push(s == null ? r : s); real.push(truth(t));
}

function stats(series, name, lagFrames) {
  let plateaus = 0, maxJump = 0, backward = 0;
  for (let i = 1; i < series.length; i++) {
    const d = series[i] - series[i - 1];
    if (Math.abs(d) < 1e-12) plateaus++;
    if (Math.abs(d) > maxJump) maxJump = Math.abs(d);
  }
  // Direction reversals the TRUE signal did not make. Interpolation is deliberately DELAYED by
  // one push interval, so the comparison must be against truth shifted by that same lag —
  // otherwise every turning point of the sine is miscounted as a reversal (at 8 Hz that is
  // ~7.5 frames x 6 turns = 45 phantom hits, which is exactly what the unshifted metric showed).
  const L = lagFrames | 0;
  for (let i = 2 + L; i < series.length; i++) {
    const dS = Math.sign(series[i] - series[i - 1]);
    const dR = Math.sign(real[i - L] - real[i - 1 - L]);
    if (dS !== 0 && dR !== 0 && dS !== dR) backward++;
  }
  console.log(`  ${name.padEnd(10)} frames=${series.length}  flat-frames=${String(plateaus).padStart(4)} ` +
              `(${(100 * plateaus / (series.length - 1)).toFixed(1)}%)  maxStep=${maxJump.toFixed(4)}  ` +
              `against-truth-direction=${backward}`);
  return { plateaus, maxJump, backward };
}

// RMS error against truth at the series' own best alignment — how faithfully it reproduces the
// real LFO motion once its inherent delay is accounted for.
function rmsErr(series) {
  let best = Infinity;
  for (let shift = 0; shift < 40; shift++) {
    let err = 0, n = 0;
    for (let i = shift; i < series.length; i++) { const d = series[i] - real[i - shift]; err += d * d; n++; }
    if (n && err / n < best) best = err / n;
  }
  return Math.sqrt(best);
}

// lag: cross-correlate smooth against truth to find best shift
function bestLagMs(series) {
  let best = 0, bestErr = Infinity;
  for (let shift = 0; shift < 40; shift++) {
    let err = 0, n = 0;
    for (let i = shift; i < series.length; i++) { const d = series[i] - real[i - shift]; err += d * d; n++; }
    err /= n;
    if (err < bestErr) { bestErr = err; best = shift; }
  }
  return best * frameDt;
}

console.log(`\n  LFO VALUE FEED — ${LFO_HZ} Hz LFO, pushes at ${PUSH_HZ} Hz, rendered at ${FPS} fps\n`);
const R = stats(raw, "RAW", Math.round(bestLagMs(raw)/frameDt));
const S = stats(smooth, "SMOOTHED", Math.round(bestLagMs(smooth)/frameDt));
console.log(`\n  tracking RMS error vs truth: raw ${rmsErr(raw).toFixed(4)}   smoothed ${rmsErr(smooth).toFixed(4)}`);
console.log(`  measured lag: raw ${bestLagMs(raw).toFixed(1)} ms   smoothed ${bestLagMs(smooth).toFixed(1)} ms` +
            `   (one push interval = ${pushDt.toFixed(1)} ms)`);

let fail = 0;
// THE CONTRACT. Smoothing engages only while the feed is FRESH: __mvLfoValAt requires the last
// push to be within 250 ms (the pre-existing gate, index.html:27841) AND the measured gap to be
// inside fb493's 1 ms..300 ms stall window. Below ~4 Hz both of those fail by design, and the
// correct behaviour is to be BIT-IDENTICAL to today's raw latch — a slower feed than that is a
// stalled feed, and interpolating across a stall invents motion that never happened.
if (PUSH_HZ >= 4) {
  if (!(S.plateaus < R.plateaus * 0.34)) { console.log('  FAIL: smoothing did not remove the plateaus'); fail++; }
  if (!(S.maxJump < R.maxJump * 0.6))    { console.log('  FAIL: smoothing did not reduce the step size'); fail++; }
  // TRACKING ERROR, not direction. A latched staircase is FLAT on most frames, so its direction
  // is undefined and it scores a perfect zero on any reversal count simply by standing still —
  // that metric rewards exactly the defect being fixed. RMS error against best-aligned truth is
  // the honest comparison, and it cannot be gamed by holding still.
  if (!(rmsErr(smooth) < rmsErr(raw))) {
    console.log(`  FAIL: smoothed tracks truth worse than the raw latch ` +
                `(${rmsErr(smooth).toFixed(4)} vs ${rmsErr(raw).toFixed(4)})`); fail++;
  }
  if (bestLagMs(smooth) > pushDt * 1.6)  { console.log('  FAIL: lag exceeds one push interval'); fail++; }
} else {
  const same = smooth.every((v, i) => v === raw[i]);
  console.log(`  below the freshness gate: smoothed === raw for every frame? ${same}`);
  if (!same) { console.log('  FAIL: a stalled feed must fall back to the raw latch unchanged'); fail++; }
}

// stale behaviour: stop pushing, advance past the 250 ms freshness window
NOW += 400;
const stale = window_.__mvLfoValAt(0);
console.log(`\n  after a 400 ms feed stall __mvLfoValAt returns ${stale} (must be null so callers keep their RAW latch, never 0)`);
if (stale !== null) { console.log('  FAIL: stale feed did not return null'); fail++; }

console.log(fail ? `\n  ${fail} CHECK(S) FAILED\n` : '\n  ALL CHECKS PASSED\n');
process.exit(fail ? 1 : 0);
