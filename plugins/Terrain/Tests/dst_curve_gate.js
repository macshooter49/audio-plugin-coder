// ══ fb614 — THE DISTORTION CURVE DRAWS, AND IT IS ON THE RIGHT AXIS ═══════════════════════════
//   node Tests/dst_curve_gate.js [index.html]
//   DSTC_MUT=nospan | staticunity | inquiet     (each must go RED)
//
// Max, on a screenshot of an idle Distortion (Linear Fold, Drive 41): "there's no curve present,
// please make sure everything is scaled properly."
//
// TWO SEPARATE FAULTS, and the first one only shows on an IDLE plugin:
//  1. NO DATA AT REST. The painter ticks fine — `__tiFrame` is pushed OUTSIDE the `if (! uiQuiet)`
//     block (PluginEditor.cpp:7016 vs the block closing at 6432) — but `window.__dstVizPush` is
//     INSIDE it, so opening the editor on a silent, untouched session left the variable undefined,
//     the JS fell to its native-poll fallback, and that poll has three documented silent death
//     modes. `<path class="dst-curve" d="">` stayed empty while the axes and the dashed line —
//     static markup — kept drawing. Exactly the screenshot.
//  2. WRONG AXIS. sampleCurve sweeps ±occSpan, which is 4.5 on FOLD, 3.0 default, 1.6 DIGITAL.
//     The reference diagonal was drawn corner to corner, i.e. assuming ±1, and mapped ±1 to 28
//     units where the curve maps it to 27. It could not be right on any family.
//
// This drives the REAL painter through the REAL push variable — no reimplementation of either.
const puppeteer = require('puppeteer-core');
const PAGE = process.argv[2] || require('path').join(__dirname, '..') + '/Source/ui/public/index.html';
const MUT  = process.env.DSTC_MUT || '';
const fs = require('fs'), path = require('path');

let PASS = 0, FAIL = 0;
const gate = (ok, name, detail) => {
  if (ok) { PASS++; console.log('  ✓ ' + name + (detail ? '   ' + detail : '')); }
  else    { FAIL++; console.log('  ✗ ' + name + (detail ? '   ' + detail : '')); }
};

(async () => {
  const b = await puppeteer.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage(); await p.setViewport({ width: 1280, height: 900 });
  const errs = []; p.on('pageerror', e => errs.push(String(e).slice(0, 140)));
  await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 });
  await new Promise(r => setTimeout(r, 1500));

  const R = await p.evaluate(async (MUT) => {
    const out = {};
    const sp = document.getElementById('syn-panel'); if (sp) sp.style.display = 'block';
    window.dispatchEvent(new Event('resize'));
    await new Promise(r => setTimeout(r, 300));
    if (typeof window.__fxAdd !== 'function') return { ERROR: '__fxAdd hook missing' };
    window.__fxAdd('saturate');                                   // fb446's headless hook
    await new Promise(r => setTimeout(r, 400));
    const core = document.querySelector('.fxr-core[data-core="saturate"]');
    if (!core) return { ERROR: 'no saturate core after __fxAdd' };
    const curve = core.querySelector('.dst-curve'), uni = core.querySelector('.dst-unity');
    if (!curve) return { ERROR: 'no .dst-curve' };
    out.shippedEmpty = curve.getAttribute('d') === '';            // it ships blank, by design

    if (MUT === 'staticunity' && uni) {                            // never re-aim it (pre-fb614)
      const set = uni.setAttribute.bind(uni);
      uni.setAttribute = function (k, v) { if (/^[xy][12]$/.test(k)) return; return set(k, v); };
    }

    /* a REAL saturator shape: identity inside ±1, hard-clipped beyond. Its peak is 1, so the JS
       display-normaliser leaves it alone — which is what makes the coincidence test below exact. */
    const N = 128;
    const mk = (span, withX) => {
      const c = [];
      for (let i = 0; i < N; i++) {
        const x = (i / (N - 1) * 2 - 1) * span;
        c.push(Math.max(-1, Math.min(1, x)));
      }
      const o = { m: 13, b: 0, c: c, o: new Array(48).fill(0) };
      if (withX) o.x = span;
      return o;
    };
    const frame = (payload) => {
      window.__dstVizPush = MUT === 'nospan'
        ? (delete payload.x, payload) : payload;
      window.__tiFrame && window.__tiFrame();
    };
    const readUni = () => uni ? ['x1','y1','x2','y2'].map(k => +uni.getAttribute(k)) : null;

    frame(mk(4.5, true));
    await new Promise(r => setTimeout(r, 60));
    out.d45   = curve.getAttribute('d') || '';
    out.uni45 = readUni();

    frame(mk(1.0, true));
    await new Promise(r => setTimeout(r, 60));
    out.uni10 = readUni();

    const legacy = mk(3.0, false);                                 // a pre-fb614 payload: no "x"
    frame(legacy);
    await new Promise(r => setTimeout(r, 60));
    out.dLegacy = (curve.getAttribute('d') || '').length > 0;
    out.uniLegacy = readUni();
    return out;
  }, MUT);

  console.log('══ fb614 DISTORTION TRANSFER CURVE ══   mutation: ' + (MUT || '(none)'));
  if (R.ERROR) { console.log('  ✗ ' + R.ERROR); await b.close(); process.exit(1); }

  const pts = R.d45.trim().split(/[ML]/).filter(Boolean)
                  .map(t => t.trim().split(/\s+/).map(Number)).filter(a => a.length === 2 && a.every(isFinite));
  gate(R.shippedEmpty && pts.length === 128,
       '[1] ONE PUSHED FRAME DRAWS THE CURVE — the idle case Max photographed',
       'ships d="" then paints ' + pts.length + ' points');

  // the plot is 6.5..219.5; on span 4.5 the unity segment must span exactly 2/4.5 of that width
  const W45 = R.uni45 ? R.uni45[2] - R.uni45[0] : 0, FULL = 219.5 - 6.5;
  const want45 = FULL * (1 / 4.5);
  gate(Math.abs(W45 - want45) < 1.0,
       '[2] THE REFERENCE IS AIMED AT THE AXIS THE CURVE IS ON (span 4.5)',
       'unity spans ' + W45.toFixed(1) + ' of ' + FULL + ' — expected ' + want45.toFixed(1)
       + (Math.abs(W45 - want45) < 1.0 ? '' : '   ← still assuming an x-axis of ±1'));

  // THE COINCIDENCE TEST: inside ±1 the saturator IS unity, so the drawn curve must lie ON the line
  let worst = 0, checked = 0;
  if (R.uni45 && pts.length) {
    const [ux1, uy1, ux2, uy2] = R.uni45, m = (uy2 - uy1) / (ux2 - ux1);
    for (const [x, y] of pts) {
      if (x < Math.min(ux1, ux2) + 1 || x > Math.max(ux1, ux2) - 1) continue;
      worst = Math.max(worst, Math.abs(y - (uy1 + m * (x - ux1)))); checked++;
    }
  }
  gate(checked > 10 && worst < 0.6,
       '[3] A UNITY-IN-THE-LINEAR-REGION CURVE LIES **ON** THE REFERENCE — "scaled properly", measured',
       checked + ' points inside ±1, worst deviation ' + worst.toFixed(2) + 'px'
       + (worst < 0.6 ? '  (the curve and the line share one px()/py())' : '   ← they disagree'));

  const W10 = R.uni10 ? R.uni10[2] - R.uni10[0] : 0;
  gate(Math.abs(W10 - FULL) < 1.0,
       '[4] AND ON A ±1 FAMILY IT IS STILL THE FULL DIAGONAL',
       'unity spans ' + W10.toFixed(1) + ' of ' + FULL);

  gate(R.dLegacy === true,
       '[5] A PRE-fb614 PAYLOAD WITH NO "x" STILL DRAWS (falls back to ±1, never throws)',
       R.dLegacy ? 'drew' : 'blank');

  // ── the C++ half: the quiet lane must be OUTSIDE every uiQuiet gate, and change-gated ──────
  /* ⚠️ THIS BAR LIED ON ITS FIRST DRAFT, AND THE LESSON IS THE POINT. It brace-counted the raw
     C++ to find the `if (! uiQuiet)` blocks — but PluginEditor.cpp is a file that EMITS
     JAVASCRIPT, so it is full of `{` and `}` inside string literals, and it also matched the
     phrase `if (! uiQuiet)` where it appears in a COMMENT (this change's own comment, in
     backticks). It reported "outside" for the wrong reason and would have reported "inside" for
     the wrong reason just as easily. Comments and string literals are stripped first now, and
     EVERY block is checked rather than the first one found. */
  const src = fs.readFileSync(path.join(__dirname, '..', 'Source', 'PluginEditor.cpp'), 'utf8');
  const strip = (t) => {
    let o = '', i = 0;
    while (i < t.length) {
      const c = t[i], d = t[i + 1];
      if (c === '/' && d === '/') { while (i < t.length && t[i] !== '\n') { o += ' '; i++; } continue; }
      if (c === '/' && d === '*') { const e = t.indexOf('*/', i + 2);
        const end = e < 0 ? t.length : e + 2;
        for (let k = i; k < end; k++) o += (t[k] === '\n' ? '\n' : ' ');
        i = end; continue; }
      if (c === '"') { o += ' '; i++;
        while (i < t.length && t[i] !== '"') { if (t[i] === '\\') { o += ' '; i++; } o += (t[i] === '\n' ? '\n' : ' '); i++; }
        o += ' '; i++; continue; }
      o += c; i++;
    }
    return o;
  };
  const CL = strip(src).split('\n');
  const RL = src.split('\n');
  const blocks = [];
  CL.forEach((l, i) => {
    if (!/if\s*\(\s*!\s*uiQuiet\s*\)/.test(l)) return;
    let d = 0, op = -1;
    for (let j = i; j < CL.length; j++) {
      d += (CL[j].match(/\{/g) || []).length - (CL[j].match(/\}/g) || []).length;
      if (op < 0 && CL[j].includes('{')) op = j;
      if (op >= 0 && d === 0 && j > op) { blocks.push([i + 1, j + 1]); return; }
    }
  });
  let lane = CL.findIndex(l => /dstVizQuietCtr_/.test(l) && /uiQuiet/.test(l)) + 1;
  if (MUT === 'inquiet' && blocks.length) lane = blocks[0][0] + 1;   // pretend it moved inside a gate
  const inside = blocks.filter(([a, z]) => lane >= a && lane <= z);
  const changeGated = /lastDstVizQuiet_/.test(strip(src));
  gate(lane > 0 && inside.length === 0 && changeGated,
       '[6] THE QUIET-LANE PUSH IS OUTSIDE EVERY uiQuiet GATE, AND CHANGE-GATED',
       blocks.length + ' gate(s) ' + JSON.stringify(blocks) + ', lane at ' + lane
       + (inside.length === 0 ? ' — outside all of them' : ' *** INSIDE ' + JSON.stringify(inside)
          + ': `uiQuiet &&` inside `if (! uiQuiet)` can NEVER fire ***')
       + (changeGated ? ' · idle sends one push then nothing' : ' *** not change-gated ***'));

  gate(errs.length === 0, '[7] NO PAGE ERRORS', errs.length ? errs.slice(0, 2).join(' | ') : 'clean');
  console.log('\n  ' + PASS + ' pass, ' + FAIL + ' fail' + (MUT ? '   (mutation: ' + MUT + ')' : ''));
  await b.close();
  process.exit(FAIL ? 1 : 0);
})();
