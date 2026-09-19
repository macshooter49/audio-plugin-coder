// ══════════════════════════════════════════════════════════════════════════════════════════════
//  tp57 — EVERY CARD WITH A CURVE DRAWS IT, HOWEVER MANY OF THEM YOU SPAWN.
//
//  Max: "every time I just loaded up a second distortion the shaper is gone ... the distortion
//  curve HAS to be there ... and make sure that doesn't happen for anything else that has a shape
//  or curve on it."
//
//  🚨 THE BAR THAT MATTERS IS [2]. The live feed only ever described instance 1 (fb355 wrote that
//  down and nobody acted on it) and the page did `rack.querySelector('.fxr-core[data-core=
//  "saturate"]')` — FIRST MATCH. So it is not enough to prove card 2 has SOME ink: a naive fix
//  that painted card 1's curve onto card 2 would pass a does-it-draw test and be a worse bug.
//  [2] pushes a DIFFERENT curve for each instance and requires each card to show its own.
//
//    node Tests/_tp57_curve_gate.js [index.html]
// ══════════════════════════════════════════════════════════════════════════════════════════════
const puppeteer = require('puppeteer-core');
const PAGE = process.argv[2] || require('path').join(__dirname, '..') + '/Source/ui/public/index.html';
let pass = 0, fail = 0;
const ok = (c, l, d) => { if (c) { pass++; console.log('  PASS  ' + l + (d ? '\n        ' + d : '')); }
                          else { fail++; console.log('  FAIL  ' + l + (d ? '\n        ' + d : '')); } };
const sleep = ms => new Promise(r => setTimeout(r, ms));

(async () => {
  const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox', '--allow-file-access-from-files'] });
  const p = await b.newPage(); await p.setViewport({ width: 1400, height: 950 });
  const errs = []; p.on('pageerror', e => errs.push(String(e).slice(0, 150)));
  await p.goto('file://' + PAGE, { waitUntil: 'load', timeout: 60000 }); await sleep(1600);
  await p.evaluate(() => { const sp = document.getElementById('syn-panel'); if (sp) sp.style.display = 'block';
    window.dispatchEvent(new Event('resize')); });
  await sleep(300);

  // ── spawn THREE distortions and one of every other card that draws a shape ────────────────
  const spawned = await p.evaluate(async () => {
    const add = window.__fxAdd || window.__fxrAdd;
    if (typeof add !== 'function') return { ERROR: 'no __fxAdd hook' };
    ['saturate', 'saturate', 'saturate', 'eqz', 'flt', 'cho', 'fla', 'cmp', 'granular'].forEach(k => { try { add(k); } catch (e) {} });
    await new Promise(r => setTimeout(r, 800));
    return { devs: (window.__fxrDevs ? window.__fxrDevs() : []).filter(Boolean).map(d => d.core + ':' + d.inst) };
  });
  ok(!spawned.ERROR && (spawned.devs || []).filter(d => /^saturate/.test(d)).length === 3,
     '[0] THREE DISTORTIONS AND ONE OF EVERY OTHER SHAPE-BEARING CARD ARE IN THE RACK', JSON.stringify(spawned));

  // ── [1] push a live feed carrying a DIFFERENT curve per instance ─────────────────────────
  //  A saturator shape whose peak is exactly 1 so the JS display-normaliser leaves it alone.
  const painted = await p.evaluate(async () => {
    const N = 128;
    const mk = (kind) => { const c = [];
      for (let i = 0; i < N; i++) { const x = i / (N - 1) * 2 - 1;
        c.push(kind === 0 ? Math.max(-1, Math.min(1, x * 3))        // hard clip
             : kind === 1 ? Math.tanh(x * 2.2) / Math.tanh(2.2)     // soft S
             :              Math.sign(x) * Math.pow(Math.abs(x), 0.45)); }  // a fat curve
      return c; };
    window.__dstVizPush = { m: 13, b: 0.6, x: 1, c: mk(0), o: new Array(48).fill(0.5),
      e: [ { i: 2, m: 5, b: 0.6, x: 1, c: mk(1), o: new Array(48).fill(0.5) },
           { i: 3, m: 9, b: 0.6, x: 1, c: mk(2), o: new Array(48).fill(0.5) } ] };
    window.__tiFrame && window.__tiFrame();
    await new Promise(r => setTimeout(r, 200));
    window.__tiFrame && window.__tiFrame();
    await new Promise(r => setTimeout(r, 200));
    const DEVS = window.__fxrDevs ? window.__fxrDevs() : [];
    return [...document.querySelectorAll('#fxr-rack .fxr-core[data-core="saturate"]')].map(c => {
      const dev = c.closest('.fxr-dev'), ix = dev ? +dev.getAttribute('data-dev') : -1;
      const path = c.querySelector('.dst-curve');
      const d = path ? (path.getAttribute('d') || '') : null;
      return { inst: (DEVS[ix] && DEVS[ix].inst) | 0, pts: d === null ? -1 : d.trim().split(/[ML]/).filter(Boolean).length, d: d };
    });
  });
  ok(painted.length === 3 && painted.every(c => c.pts > 100),
     '[1] 🚨 ALL THREE DISTORTION CARDS HAVE A CURVE — none is left with <path d="">',
     painted.map(c => 'inst ' + c.inst + ': ' + c.pts + ' points').join(' · '));

  // ── [2] 🚨 AND EACH ONE IS ITS OWN CURVE ─────────────────────────────────────────────────
  const uniq = new Set(painted.map(c => c.d));
  ok(uniq.size === painted.length,
     '[2] 🚨 EACH CARD DRAWS ITS OWN CURVE — a fix that copied instance 1 onto every card would pass [1] and be a worse bug',
     painted.length + ' cards, ' + uniq.size + ' distinct paths');

  // ── [3] THE SECOND CARD FOLLOWS ITS OWN PAYLOAD, NOT THE FIRST'S ─────────────────────────
  //  Swap only instance 2's curve and require only instance 2's path to move.
  const follow = await p.evaluate(async () => {
    const before = [...document.querySelectorAll('#fxr-rack .fxr-core[data-core="saturate"] .dst-curve')].map(e => e.getAttribute('d'));
    const N = 128, c = []; for (let i = 0; i < N; i++) { const x = i / (N - 1) * 2 - 1; c.push(x * 0.35); }
    window.__dstVizPush.e[0].c = c;                       // instance 2 only
    window.__tiFrame && window.__tiFrame();
    await new Promise(r => setTimeout(r, 200));
    const after = [...document.querySelectorAll('#fxr-rack .fxr-core[data-core="saturate"] .dst-curve')].map(e => e.getAttribute('d'));
    return before.map((d, i) => d !== after[i]);
  });
  ok(follow.length === 3 && follow[0] === false && follow[1] === true && follow[2] === false,
     '[3] MOVING ONLY INSTANCE 2\'s PAYLOAD MOVES ONLY INSTANCE 2\'s CARD', 'moved = ' + JSON.stringify(follow));

  // ── [4] EVERY OTHER SHAPE-BEARING CARD IN THE RACK HAS INK ───────────────────────────────
  //  A sweep, not a list: whatever a card calls its line, if it carries the house's .dst-curve
  //  treatment it is a picture the user is meant to see, and an empty one is the same bug.
  const sweep = await p.evaluate(() => {
    const out = [];
    document.querySelectorAll('#fxr-rack .fxr-dev').forEach(dev => {
      const core = dev.querySelector('.fxr-core'); if (!core) return;
      const kind = core.getAttribute('data-core');
      core.querySelectorAll('path.dst-curve, path[class*="-curve"]').forEach(pth => {
        const d = (pth.getAttribute('d') || '').trim();
        const segs = d.split(/[ML]/).filter(Boolean).length;
        out.push({ kind: kind, cls: pth.getAttribute('class'), segs: segs });
      });
    });
    return out;
  });
  const empties = sweep.filter(s => s.segs < 2);
  ok(sweep.length >= 6 && empties.length === 0,
     '[4] EVERY SHAPE-BEARING CARD IN THE RACK HAS A DRAWN LINE',
     sweep.length + ' lines checked' + (empties.length ? ' · EMPTY: ' + JSON.stringify(empties) : ' · none empty'));

  // ── [5] 🚨 AND ON THE PATCHER, WHERE THE CARDS DO NOT LIVE IN THE RACK ──────────────────
  //  The canvas MOVES a card out of #fxr-rack into its node (adoptFx → n.body.appendChild), so a
  //  rack-scoped query finds nothing there. This never showed up by eye because a card painted
  //  BEFORE it was adopted carries its `d` attribute over — only a distortion SPAWNED while the
  //  canvas is open comes up blank, which is exactly the way Max hit it. Both cards are wiped and
  //  re-pushed from inside the canvas, so a stale attribute cannot carry the bar.
  const onCanvas = await p.evaluate(async () => {
    if (!window.setActivePanel) return { err: 'no panel switch' };
    window.setActivePanel('tp');
    await new Promise(r => setTimeout(r, 2600));
    document.querySelectorAll('.fxr-core[data-core="saturate"] .dst-curve').forEach(e => e.setAttribute('d', ''));
    const N = 128, mk = (k) => { const c = [];
      for (let i = 0; i < N; i++) { const x = i / (N - 1) * 2 - 1;
        c.push(k === 0 ? Math.max(-1, Math.min(1, x * 3)) : k === 1 ? Math.tanh(x * 2.2) / Math.tanh(2.2)
             : Math.sign(x) * Math.pow(Math.abs(x), 0.45)); } return c; };
    window.__dstVizPush = { m: 13, b: 0.6, x: 1, c: mk(0), o: new Array(48).fill(0.5),
      e: [ { i: 2, m: 5, b: 0.6, x: 1, c: mk(1), o: new Array(48).fill(0.5) },
           { i: 3, m: 9, b: 0.6, x: 1, c: mk(2), o: new Array(48).fill(0.5) } ] };
    window.__tiFrame && window.__tiFrame();
    await new Promise(r => setTimeout(r, 300));
    window.__tiFrame && window.__tiFrame();
    await new Promise(r => setTimeout(r, 300));
    const cards = [...document.querySelectorAll('.fxr-core[data-core="saturate"]')];
    return { inRack: cards.filter(c => c.closest('#fxr-rack')).length,
             onCanvas: cards.filter(c => c.closest('#tp-page')).length,
             pts: cards.map(c => { const d = (c.querySelector('.dst-curve') || {}).getAttribute
               ? c.querySelector('.dst-curve').getAttribute('d') || '' : ''; return d.trim().split(/[ML]/).filter(Boolean).length; }) };
  });
  ok(onCanvas.onCanvas === 3 && onCanvas.pts.length === 3 && onCanvas.pts.every(n => n > 100),
     '[5] 🚨 A DISTORTION ADOPTED ONTO THE PATCHER STILL DRAWS ITS CURVE — the cards are out of #fxr-rack there, and a rack-scoped query finds nothing',
     JSON.stringify(onCanvas));

  ok(errs.length === 0, '[6] THE PAGE THREW NOTHING', errs.join(' | '));
  console.log('\n  ' + pass + ' passed, ' + fail + ' failed\n');
  await b.close(); process.exit(fail ? 1 : 0);
})();
